#include <string.h>
#include "api_handlers.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs_config.h"
#include "state_machine.h"
#include "wifi_manager.h"
#include "auth_middleware.h"

static const char *TAG = "api_handlers";

static esp_err_t login_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/login");
    
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid request\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid JSON\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    const cJSON *username = cJSON_GetObjectItemCaseSensitive(root, "username");
    const cJSON *password = cJSON_GetObjectItemCaseSensitive(root, "password");
    
    if (!cJSON_IsString(username) || !cJSON_IsString(password)) {
        cJSON_Delete(root);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Missing username or password\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    char token[AUTH_TOKEN_LEN + 1] = {0};
    esp_err_t err = auth_login(username->valuestring, password->valuestring, token, sizeof(token));
    
    cJSON_Delete(root);
    
    if (err == ESP_ERR_TIMEOUT) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Too many failed attempts, please try again later\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid username or password\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    // Set auth cookie
    char cookie_hdr[128];
    snprintf(cookie_hdr, sizeof(cookie_hdr), "%s=%s; Path=/; HttpOnly; SameSite=Strict", AUTH_COOKIE_NAME, token);
    httpd_resp_set_hdr(req, "Set-Cookie", cookie_hdr);
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "token", token);
    cJSON_AddBoolToObject(resp, "success", true);
    char *resp_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    
    return ESP_OK;
}

static esp_err_t logout_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/logout");
    
    auth_logout(req);
    
    // Clear cookie
    httpd_resp_set_hdr(req, "Set-Cookie", AUTH_COOKIE_NAME "=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
    
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", true);
    char *resp_str = cJSON_Print(resp);
    cJSON_Delete(resp);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    free(resp_str);
    
    return ESP_OK;
}

static esp_err_t get_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET /api/config");
    
    // Check authentication
    esp_err_t auth_ret = auth_check_request(req);
    if (auth_ret != ESP_OK) {
        return ESP_OK; // Response already sent by auth middleware
    }
    
    app_config_t config;
    nvs_load_all_config(&config);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    // Do NOT return WiFi password - security risk
    cJSON_AddStringToObject(root, "wifi_password", "********");
    cJSON_AddStringToObject(root, "mqtt_uri", config.mqtt_uri);
    cJSON_AddNumberToObject(root, "mqtt_port", config.mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_username", config.mqtt_username);
    // Do NOT return MQTT password - security risk
    cJSON_AddStringToObject(root, "mqtt_password", "********");
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

static esp_err_t post_config_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/config");
    
    // Check authentication
    esp_err_t auth_ret = auth_check_request(req);
    if (auth_ret != ESP_OK) {
        return ESP_OK;
    }
    
    // Check Content-Length to prevent buffer overflow
    int content_len = req->content_len;
    if (content_len <= 0 || content_len > 512) {
        ESP_LOGW(TAG, "Invalid content length: %d", content_len);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Request too large\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    char *buf = malloc(content_len + 1);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_OK;
    }
    
    int ret = httpd_req_recv(req, buf, content_len);
    if (ret <= 0) {
        free(buf);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Failed to read request body\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error: %s", error_ptr ? error_ptr : "unknown");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid JSON\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    
    const cJSON *wifi_ssid = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
    const cJSON *wifi_password = cJSON_GetObjectItemCaseSensitive(root, "wifi_password");
    const cJSON *mqtt_uri = cJSON_GetObjectItemCaseSensitive(root, "mqtt_uri");
    const cJSON *mqtt_port = cJSON_GetObjectItemCaseSensitive(root, "mqtt_port");
    const cJSON *mqtt_username = cJSON_GetObjectItemCaseSensitive(root, "mqtt_username");
    const cJSON *mqtt_password = cJSON_GetObjectItemCaseSensitive(root, "mqtt_password");
    
    if (cJSON_IsString(wifi_ssid) && cJSON_IsString(mqtt_uri)) {
        // Save WiFi config and check result
        const char *wifi_pass = cJSON_IsString(wifi_password) ? wifi_password->valuestring : "";
        esp_err_t err = nvs_save_wifi_config(wifi_ssid->valuestring, wifi_pass);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save WiFi config: %s", esp_err_to_name(err));
            cJSON_Delete(root);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Failed to save WiFi config\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        
        // Save MQTT config and check result
        err = nvs_save_mqtt_config(mqtt_uri->valuestring,
                           cJSON_IsNumber(mqtt_port) ? (uint16_t)mqtt_port->valueint : 1883,
                           cJSON_IsString(mqtt_username) ? mqtt_username->valuestring : "",
                           cJSON_IsString(mqtt_password) ? mqtt_password->valuestring : "");
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save MQTT config: %s", esp_err_to_name(err));
            cJSON_Delete(root);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Failed to save MQTT config\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        
        // Mark as configured (not first boot anymore)
        err = nvs_set_first_boot(false);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set first_boot flag: %s", esp_err_to_name(err));
            // Continue anyway, this is not critical
        }
        
        // Verify config was saved correctly by reloading
        app_config_t verify_config;
        err = nvs_load_all_config(&verify_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to verify saved config: %s", esp_err_to_name(err));
            cJSON_Delete(root);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Failed to verify config\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        
        if (strlen(verify_config.wifi_ssid) == 0) {
            ESP_LOGE(TAG, "WiFi SSID is empty after save!");
            cJSON_Delete(root);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"WiFi SSID save verification failed\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_OK;
        }
        
        ESP_LOGI(TAG, "Config verified: SSID=%s", verify_config.wifi_ssid);
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddTrueToObject(response, "success");
        char *resp_str = cJSON_Print(response);
        cJSON_Delete(response);
        
        if (resp_str == NULL) {
            ESP_LOGE(TAG, "Failed to print JSON response");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
            cJSON_Delete(root);
            return ESP_OK;
        }
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        free(resp_str);
        cJSON_Delete(root);
        
        ESP_LOGI(TAG, "Config saved and verified, triggering CONFIG_RECEIVED event");
        state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
        
        return ESP_OK;
    }
    
    cJSON_Delete(root);
    httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Missing required fields\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t get_status_handler(httpd_req_t *req)
{
    // Public endpoint - no auth required
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_machine_get_state_name(state_machine_get_current_state()));
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

static esp_err_t get_scan_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/scan");
    
    // Check authentication
    esp_err_t auth_ret = auth_check_request(req);
    if (auth_ret != ESP_OK) {
        return ESP_OK;
    }
    
    wifi_scan_results_t results = {0};
    
    esp_err_t err = wifi_manager_scan_wifi(&results);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_OK;
    }
    
    cJSON *root = cJSON_CreateArray();
    for (uint16_t i = 0; i < results.count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", results.aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", results.aps[i].rssi);
        cJSON_AddNumberToObject(ap, "channel", results.aps[i].channel);
        
        const char *security = (results.aps[i].authmode != WIFI_AUTH_OPEN) ? "SECURED" : "OPEN";
        cJSON_AddStringToObject(ap, "security", security);
        
        cJSON_AddItemToArray(root, ap);
    }
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to print JSON");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory error");
        return ESP_OK;
    }
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    ESP_LOGI(TAG, "Scan result: %d APs found", results.count);
    return ESP_OK;
}

esp_err_t api_handlers_register(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering API handlers");
    
    httpd_uri_t login_post = {
        .uri = "/api/login",
        .method = HTTP_POST,
        .handler = login_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &login_post);
    
    httpd_uri_t logout_post = {
        .uri = "/api/logout",
        .method = HTTP_POST,
        .handler = logout_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &logout_post);
    
    httpd_uri_t config_get = {
        .uri = "/api/config",
        .method = HTTP_GET,
        .handler = get_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &config_get);
    
    httpd_uri_t config_post = {
        .uri = "/api/config",
        .method = HTTP_POST,
        .handler = post_config_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &config_post);
    
    httpd_uri_t status_get = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = get_status_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &status_get);
    
    httpd_uri_t scan_get = {
        .uri = "/api/scan",
        .method = HTTP_GET,
        .handler = get_scan_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &scan_get);
    
    ESP_LOGI(TAG, "API handlers registered");
    return ESP_OK;
}
