#include "auth_middleware.h"
#include "nvs_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "auth_middleware";

// Default credentials for first boot
#define DEFAULT_ADMIN_USERNAME "admin"
#define DEFAULT_ADMIN_PASSWORD "synaflow2024"

// Simple token storage (single token, industrial devices typically have one admin)
static char current_token[AUTH_TOKEN_LEN + 1] = {0};
static bool token_valid = false;
static int failed_attempts = 0;
static TickType_t lockout_until = 0;

// Public URIs that don't require authentication
static const char *public_uris[] = {
    "/",
    "/index.html",
    "/login.html",
    "/api/login",
    "/api/status",
    "/api/scan",
    "/generate_204",
    "/gen_204",
    "/hotspot-detect.html",
    "/connecttest.txt",
    "/redirect",
    "/fwlink",
    NULL
};

static void generate_random_token(char *token, size_t len)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < len; i++) {
        uint32_t rand_val = esp_random();
        token[i] = charset[rand_val % (sizeof(charset) - 1)];
    }
    token[len] = '\0';
}

esp_err_t auth_middleware_init(void)
{
    ESP_LOGI(TAG, "Initializing auth middleware");
    
    // If no admin credentials exist, create default ones
    if (!nvs_admin_credentials_exist()) {
        ESP_LOGW(TAG, "No admin credentials found, creating defaults");
        ESP_LOGW(TAG, "Default username: %s", DEFAULT_ADMIN_USERNAME);
        ESP_LOGW(TAG, "Default password: %s", DEFAULT_ADMIN_PASSWORD);
        
        esp_err_t err = nvs_save_admin_credentials(DEFAULT_ADMIN_USERNAME, DEFAULT_ADMIN_PASSWORD);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save default admin credentials: %s", esp_err_to_name(err));
            return err;
        }
    }
    
    return ESP_OK;
}

bool auth_is_public_uri(const char *uri)
{
    if (uri == NULL) return false;
    
    for (int i = 0; public_uris[i] != NULL; i++) {
        if (strcmp(uri, public_uris[i]) == 0) {
            return true;
        }
    }
    
    // Also allow static assets (CSS, JS, images) if they are public
    size_t len = strlen(uri);
    if (len > 4) {
        const char *ext = uri + len - 4;
        if (strcmp(ext, ".css") == 0 || strcmp(ext, ".js") == 0 ||
            strcmp(ext, ".png") == 0 || strcmp(ext, ".ico") == 0) {
            return true;
        }
    }
    
    return false;
}

esp_err_t auth_check_request(httpd_req_t *req)
{
    if (req == NULL) return ESP_FAIL;
    
    // Check if URI is public
    if (auth_is_public_uri(req->uri)) {
        return ESP_OK;
    }
    
    // Check lockout
    if (failed_attempts >= 5) {
        if (xTaskGetTickCount() < lockout_until) {
            ESP_LOGW(TAG, "Auth locked out, too many failed attempts");
            httpd_resp_set_status(req, "429 Too Many Requests");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Too many failed attempts, please try again later\"}", HTTPD_RESP_USE_STRLEN);
            return ESP_FAIL;
        } else {
            failed_attempts = 0;
        }
    }
    
    // Try Bearer token from header
    char auth_hdr[128] = {0};
    size_t auth_len = sizeof(auth_hdr);
    
    if (httpd_req_get_hdr_value_len(req, "Authorization") > 0) {
        if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_len) == ESP_OK) {
            if (strncmp(auth_hdr, AUTH_HEADER_PREFIX, strlen(AUTH_HEADER_PREFIX)) == 0) {
                const char *token = auth_hdr + strlen(AUTH_HEADER_PREFIX);
                if (token_valid && strcmp(token, current_token) == 0) {
                    return ESP_OK;
                }
            }
        }
    }
    
    // Try Cookie
    char cookie_hdr[256] = {0};
    size_t cookie_len = sizeof(cookie_hdr);
    if (httpd_req_get_hdr_value_len(req, "Cookie") > 0) {
        if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_hdr, cookie_len) == ESP_OK) {
            char *token_pos = strstr(cookie_hdr, AUTH_COOKIE_NAME "=");
            if (token_pos != NULL) {
                token_pos += strlen(AUTH_COOKIE_NAME "=");
                char token[AUTH_TOKEN_LEN + 1] = {0};
                size_t i = 0;
                while (*token_pos && *token_pos != ';' && i < AUTH_TOKEN_LEN) {
                    token[i++] = *token_pos++;
                }
                token[i] = '\0';
                if (token_valid && strcmp(token, current_token) == 0) {
                    return ESP_OK;
                }
            }
        }
    }
    
    // Auth failed
    failed_attempts++;
    if (failed_attempts >= 5) {
        lockout_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000); // 30s lockout
        ESP_LOGW(TAG, "Auth lockout activated for 30s");
    }
    
    ESP_LOGW(TAG, "Auth failed for URI: %s (attempt %d)", req->uri, failed_attempts);
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

esp_err_t auth_login(const char *username, const char *password, char *token_out, size_t token_size)
{
    if (username == NULL || password == NULL || token_out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Check lockout
    if (failed_attempts >= 5 && xTaskGetTickCount() < lockout_until) {
        return ESP_ERR_TIMEOUT;
    }
    
    char stored_user[32] = {0};
    char stored_pass[64] = {0};
    
    esp_err_t err = nvs_read_admin_credentials(stored_user, sizeof(stored_user), stored_pass, sizeof(stored_pass));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read admin credentials from NVS");
        return ESP_FAIL;
    }
    
    if (strcmp(username, stored_user) != 0 || strcmp(password, stored_pass) != 0) {
        failed_attempts++;
        if (failed_attempts >= 5) {
            lockout_until = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
            ESP_LOGW(TAG, "Login lockout activated for 30s");
        }
        ESP_LOGW(TAG, "Login failed for user: %s (attempt %d)", username, failed_attempts);
        return ESP_FAIL;
    }
    
    // Success - generate new token
    generate_random_token(current_token, AUTH_TOKEN_LEN);
    token_valid = true;
    failed_attempts = 0;
    
    if (token_size > AUTH_TOKEN_LEN) {
        strncpy(token_out, current_token, token_size);
    }
    
    ESP_LOGI(TAG, "Login successful for user: %s", username);
    return ESP_OK;
}

esp_err_t auth_logout(httpd_req_t *req)
{
    token_valid = false;
    memset(current_token, 0, sizeof(current_token));
    ESP_LOGI(TAG, "Logout performed");
    return ESP_OK;
}

const char* auth_get_default_username(void)
{
    return DEFAULT_ADMIN_USERNAME;
}

const char* auth_get_default_password(void)
{
    return DEFAULT_ADMIN_PASSWORD;
}
