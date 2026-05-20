#include <string.h>
#include "api_handlers.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs_config.h"
#include "state_machine.h"

static const char *TAG = "api_handlers";

static esp_err_t get_config_handler(httpd_req_t *req)
{
    app_config_t config;
    nvs_load_all_config(&config);
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", config.wifi_ssid);
    cJSON_AddStringToObject(root, "mqtt_uri", config.mqtt_uri);
    cJSON_AddNumberToObject(root, "mqtt_port", config.mqtt_port);
    cJSON_AddStringToObject(root, "mqtt_username", config.mqtt_username);
    cJSON_AddTrueToObject(root, "is_configured");
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

static esp_err_t post_config_handler(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';
    
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        const char *error_ptr = cJSON_GetErrorPtr();
        ESP_LOGE(TAG, "JSON parse error: %s", error_ptr);
        return ESP_FAIL;
    }
    
    const cJSON *wifi_ssid = cJSON_GetObjectItemCaseSensitive(root, "wifi_ssid");
    const cJSON *wifi_password = cJSON_GetObjectItemCaseSensitive(root, "wifi_password");
    const cJSON *mqtt_uri = cJSON_GetObjectItemCaseSensitive(root, "mqtt_uri");
    const cJSON *mqtt_port = cJSON_GetObjectItemCaseSensitive(root, "mqtt_port");
    const cJSON *mqtt_username = cJSON_GetObjectItemCaseSensitive(root, "mqtt_username");
    const cJSON *mqtt_password = cJSON_GetObjectItemCaseSensitive(root, "mqtt_password");
    
    if (cJSON_IsString(wifi_ssid) && cJSON_IsString(mqtt_uri)) {
        nvs_save_wifi_config(wifi_ssid->valuestring, 
                           cJSON_IsString(wifi_password) ? wifi_password->valuestring : "");
        
        nvs_save_mqtt_config(mqtt_uri->valuestring,
                           cJSON_IsNumber(mqtt_port) ? (uint16_t)mqtt_port->valueint : 1883,
                           cJSON_IsString(mqtt_username) ? mqtt_username->valuestring : "",
                           cJSON_IsString(mqtt_password) ? mqtt_password->valuestring : "");
        
        cJSON *response = cJSON_CreateObject();
        cJSON_AddTrueToObject(response, "success");
        char *resp_str = cJSON_Print(response);
        cJSON_Delete(response);
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, resp_str, strlen(resp_str));
        free(resp_str);
        cJSON_Delete(root);
        
        state_machine_trigger_event(EVENT_CONFIG_RECEIVED);
        
        return ESP_OK;
    }
    
    cJSON_Delete(root);
    return ESP_FAIL;
}

static esp_err_t get_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_machine_get_state_name(state_machine_get_current_state()));
    
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    
    return ESP_OK;
}

esp_err_t api_handlers_register(httpd_handle_t server)
{
    ESP_LOGI(TAG, "Registering API handlers");
    
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
    
    return ESP_OK;
}
