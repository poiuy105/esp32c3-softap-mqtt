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
    ESP_LOGI(TAG, "Root handler: %s", req->uri);
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

// Android captive portal success response
static esp_err_t android_captive_success_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Android captive success: %s", req->uri);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "success", 7);
    return ESP_OK;
}

// iOS/macOS captive portal response
static esp_err_t apple_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Apple captive: %s", req->uri);
    // Return a simple HTML that redirects
    const char *html = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// Windows captive portal response
static esp_err_t windows_captive_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Windows captive: %s", req->uri);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, "Microsoft Connect Test", 22);
    return ESP_OK;
}

// Return 204 No Content for some captive checks
static esp_err_t no_content_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "No content: %s", req->uri);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_server_start(void)
{
    ESP_LOGI(TAG, "Starting HTTP server");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 24;  // Increased for more captive portal paths
    config.max_open_sockets = 8;   // Allow more concurrent connections
    
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

        // Android captive portal detection
        httpd_uri_t android_uris[] = {
            { .uri = "/generate_204",       .method = HTTP_GET, .handler = no_content_handler, .user_ctx = NULL },
            { .uri = "/gen_204",            .method = HTTP_GET, .handler = no_content_handler, .user_ctx = NULL },
        };
        for (int i = 0; i < sizeof(android_uris) / sizeof(android_uris[0]); i++) {
            httpd_register_uri_handler(server, &android_uris[i]);
        }

        // iOS/macOS captive portal detection
        httpd_uri_t apple_uris[] = {
            { .uri = "/hotspot-detect.html",.method = HTTP_GET, .handler = apple_captive_handler, .user_ctx = NULL },
            { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = apple_captive_handler, .user_ctx = NULL },
            { .uri = "/captiveportal.html", .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
        };
        for (int i = 0; i < sizeof(apple_uris) / sizeof(apple_uris[0]); i++) {
            httpd_register_uri_handler(server, &apple_uris[i]);
        }

        // Windows captive portal detection
        httpd_uri_t windows_uris[] = {
            { .uri = "/connecttest.txt",    .method = HTTP_GET, .handler = windows_captive_handler, .user_ctx = NULL },
            { .uri = "/ncsi.txt",           .method = HTTP_GET, .handler = windows_captive_handler, .user_ctx = NULL },
            { .uri = "/redirect",           .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/fwlink",             .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
        };
        for (int i = 0; i < sizeof(windows_uris) / sizeof(windows_uris[0]); i++) {
            httpd_register_uri_handler(server, &windows_uris[i]);
        }

        // Generic captive portal paths
        httpd_uri_t generic_uris[] = {
            { .uri = "/check_network",      .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
            { .uri = "/generate_200",       .method = HTTP_GET, .handler = captive_portal_detect_handler, .user_ctx = NULL },
        };
        for (int i = 0; i < sizeof(generic_uris) / sizeof(generic_uris[0]); i++) {
            httpd_register_uri_handler(server, &generic_uris[i]);
        }

        // Catch-all must be last
        httpd_uri_t catch_all = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &catch_all);
        
        ESP_LOGI(TAG, "HTTP server started successfully with captive portal support");
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
