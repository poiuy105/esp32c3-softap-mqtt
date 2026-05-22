#include <string.h>
#include "http_server.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "api_handlers.h"
#include "web_page.h"

static const char *TAG = "http_server";
static httpd_handle_t server = NULL;

esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive redirect: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t captive_portal_detect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Captive portal detect: %s", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_server_start(void)
{
    ESP_LOGI(TAG, "Starting HTTP server");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 16;  // Increase from default 8 to accommodate all handlers
    
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);
        
        api_handlers_register(server);

        // Captive portal detection paths (Android / iOS / Windows / macOS)
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

        httpd_uri_t catch_all = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &catch_all);
        
        ESP_LOGI(TAG, "HTTP server server started successfully");
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
