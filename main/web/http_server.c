#include <string.h>
#include "http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "api_handlers.h"
#include "config_page.h"
#include "ota_page.h"
#include "login_page.h"
#include "admin_page.h"
#include "ota_handler.h"
#include "auth_middleware.h"
#include "wifi_manager.h"

static const char *TAG = "http_server";
static httpd_handle_t server = NULL;

esp_err_t config_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    
    esp_err_t ret = httpd_resp_send(req, CONFIG_HTML, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send config HTML: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t admin_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    
    esp_err_t ret = httpd_resp_send(req, ADMIN_HTML, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send admin HTML: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t ota_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    
    esp_err_t ret = httpd_resp_send(req, OTA_HTML, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send OTA HTML: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t login_page_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
    
    esp_err_t ret = httpd_resp_send(req, LOGIN_HTML, HTTPD_RESP_USE_STRLEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send login HTML: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* Root handler: redirect to config page (SoftAP) or admin page (STA) */
static esp_err_t root_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Root handler, WiFi state: %s", 
             wifi_manager_is_sta_connected() ? "STA connected" : "not connected");
    
    if (wifi_manager_is_sta_connected()) {
        // Device is connected to WiFi, show admin page
        return admin_page_handler(req);
    } else {
        // Device is in SoftAP mode, show config page
        return config_page_handler(req);
    }
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive redirect: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login.html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_portal_detect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive portal detect: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login.html");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_server_start(void)
{
    ESP_LOGI(TAG, "Starting HTTP server");
    
    // Check if server is already running
    if (server != NULL) {
        ESP_LOGI(TAG, "HTTP server already running, skipping start");
        return ESP_OK;
    }
    
    // Initialize auth middleware first
    esp_err_t auth_err = auth_middleware_init();
    if (auth_err != ESP_OK) {
        ESP_LOGE(TAG, "Auth middleware init failed: %s", esp_err_to_name(auth_err));
        return auth_err;
    }
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24;  // Increased for new handlers
    config.stack_size = 8192;      // Increase stack for large HTML responses
    config.send_wait_timeout = 20; // Increase send timeout (seconds)
    config.recv_wait_timeout = 20; // Increase receive timeout (seconds)
    config.uri_match_fn = httpd_uri_match_wildcard; // REQUIRED for /* support
    
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Step 1: Register exact page handlers FIRST (before wildcard)
        httpd_uri_t login_uri = {
            .uri = "/login.html",
            .method = HTTP_GET,
            .handler = login_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &login_uri);
        
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        httpd_uri_t index_uri = {
            .uri = "/index.html",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &index_uri);
        
        httpd_uri_t config_html_uri = {
            .uri = "/config.html",
            .method = HTTP_GET,
            .handler = config_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_html_uri);
        
        httpd_uri_t ota_html_uri = {
            .uri = "/ota.html",
            .method = HTTP_GET,
            .handler = ota_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ota_html_uri);
        
        // Step 2: Register API handlers
        api_handlers_register(server);
        
        // Step 3: Register OTA handlers
        ota_handler_register();
        
        // Step 4: Captive portal detection paths (Android / iOS / Windows / macOS)
        httpd_uri_t captive_uris[] = {
            { .uri = "/generate_204",       .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/gen_204",            .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/hotspot-detect.html",.method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/connecttest.txt",    .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/redirect",           .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/fwlink",             .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
        };
        for (int i = 0; i < sizeof(captive_uris) / sizeof(captive_uris[0]); i++) {
            httpd_register_uri_handler(server, &captive_uris[i]);
        }
        
        // Step 5: /* wildcard MUST be registered LAST
        httpd_uri_t catch_all = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &catch_all);
        
        ESP_LOGI(TAG, "HTTP server started successfully");
        return ESP_OK;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

esp_err_t http_server_stop(void)
{
    ESP_LOGI(TAG, "Stopping HTTP server");
    
    if (server) {
        httpd_stop(server);
        server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    
    return ESP_OK;
}

httpd_handle_t httpd_get_server(void)
{
    return server;
}
