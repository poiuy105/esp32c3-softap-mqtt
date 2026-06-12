#include "ota_handler.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA";

// OTA credentials (industrial default - should be configurable via NVS)
#define OTA_USERNAME "admin"
#define OTA_PASSWORD "synaflow2024"

static bool ota_updating = false;

bool ota_is_updating(void)
{
    return ota_updating;
}

/* Base64 decode table */
static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Simple Base64 decode for Basic Auth */
static int base64_decode(const char *input, char *output, int out_len)
{
    int i = 0, j = 0, k = 0;
    unsigned char char_array_4[4], char_array_3[3];
    int in_len = strlen(input);
    int out_pos = 0;

    while (in_len-- && (input[k] != '=') &&
           (strchr(base64_chars, input[k]) != NULL)) {
        char_array_4[i++] = input[k++];
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = strchr(base64_chars, char_array_4[i]) - base64_chars;

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + char_array_4[3];

            for (i = 0; i < 3 && out_pos < out_len; i++)
                output[out_pos++] = char_array_3[i];
            i = 0;
        }
    }
    output[out_pos] = '\0';
    return out_pos;
}

/* Verify HTTP Basic Auth */
static esp_err_t check_basic_auth(httpd_req_t *req)
{
    char auth_hdr[128] = {0};
    size_t auth_len = sizeof(auth_hdr);

    // Check if Authorization header exists
    if (httpd_req_get_hdr_value_len(req, "Authorization") == 0) {
        ESP_LOGW(TAG, "OTA request without Authorization header");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"OTA\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    // Get Authorization header
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, auth_len) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get Authorization header");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"OTA\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    // Check if Basic auth
    if (strncmp(auth_hdr, "Basic ", 6) != 0) {
        ESP_LOGW(TAG, "Invalid auth scheme: %s", auth_hdr);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    // Decode Base64
    char decoded[64] = {0};
    base64_decode(auth_hdr + 6, decoded, sizeof(decoded));

    // Check credentials format (username:password)
    char *colon = strchr(decoded, ':');
    if (colon == NULL) {
        ESP_LOGW(TAG, "Invalid auth format");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    *colon = '\0';
    char *username = decoded;
    char *password = colon + 1;

    // Verify credentials
    if (strcmp(username, OTA_USERNAME) != 0 || strcmp(password, OTA_PASSWORD) != 0) {
        ESP_LOGW(TAG, "OTA auth failed for user: %s", username);
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"OTA\"");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA auth successful for user: %s", username);
    return ESP_OK;
}

/* Verify firmware header (ESP32 magic byte) */
static bool verify_firmware_header(const uint8_t *data, size_t len)
{
    if (len < 2) {
        ESP_LOGE(TAG, "Firmware too small for header check");
        return false;
    }

    // ESP32 firmware magic byte: 0xE9
    if (data[0] != 0xE9) {
        ESP_LOGE(TAG, "Invalid firmware magic byte: 0x%02X (expected 0xE9)", data[0]);
        return false;
    }

    // Segment count should be reasonable (1-16)
    uint8_t segment_count = data[1];
    if (segment_count == 0 || segment_count > 16) {
        ESP_LOGE(TAG, "Invalid segment count: %d", segment_count);
        return false;
    }

    ESP_LOGI(TAG, "Firmware header valid: magic=0xE9, segments=%d", segment_count);
    return true;
}

/* CRC32 calculation (Ethernet CRC) */
static uint32_t calc_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/* Receive firmware data via HTTP POST and write to OTA partition */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    esp_err_t ret;
    esp_ota_handle_t ota_handle = NULL;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "OTA POST request received");

    /* Check authentication */
    ret = check_basic_auth(req);
    if (ret != ESP_OK) {
        return ret;  // Auth failed, response already sent
    }

    /* Check if already updating */
    if (ota_updating) {
        ESP_LOGW(TAG, "OTA already in progress, rejecting request");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }
    ota_updating = true;

    /* Get content length */
    int content_len = req->content_len;
    ESP_LOGI(TAG, "OTA content length: %d bytes", content_len);

    if (content_len <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_len);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    /* Check partition size */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    if (content_len > update_partition->size) {
        ESP_LOGE(TAG, "Firmware too large: %d > %lu bytes", content_len, (unsigned long)update_partition->size);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update starting, firmware size: %d bytes, partition size: %lu bytes",
             content_len, (unsigned long)update_partition->size);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition->subtype, (unsigned long)update_partition->address);

    /* Begin OTA write */
    ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    /* Receive firmware data */
    #define OTA_BUF_SIZE 4096
    char *ota_buf = malloc(OTA_BUF_SIZE);
    if (ota_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        esp_ota_end(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    int remaining = content_len;
    int received = 0;
    bool header_verified = false;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, ota_buf, (remaining > OTA_BUF_SIZE) ? OTA_BUF_SIZE : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "OTA receive timeout");
            } else {
                ESP_LOGE(TAG, "OTA receive error: %d", recv_len);
            }
            free(ota_buf);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "408 Request Timeout");
            httpd_resp_send(req, NULL, 0);
            ota_updating = false;
            return ESP_FAIL;
        }

        /* Verify firmware header on first chunk */
        if (!header_verified) {
            if (!verify_firmware_header((uint8_t *)ota_buf, recv_len)) {
                ESP_LOGE(TAG, "Firmware header verification failed");
                free(ota_buf);
                esp_ota_end(ota_handle);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_send(req, NULL, 0);
                ota_updating = false;
                return ESP_FAIL;
            }
            header_verified = true;
        }

        ret = esp_ota_write(ota_handle, (const void *)ota_buf, recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
            free(ota_buf);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_send(req, NULL, 0);
            ota_updating = false;
            return ESP_FAIL;
        }

        remaining -= recv_len;
        received += recv_len;

        /* Log progress every 100KB */
        if (received % (100 * 1024) < OTA_BUF_SIZE) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes (%d%%)",
                     received, content_len, (received * 100) / content_len);
        }
    }

    free(ota_buf);

    ESP_LOGI(TAG, "OTA receive complete: %d bytes", received);

    /* End OTA write */
    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    /* Set boot partition */
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, NULL, 0);
        ota_updating = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful! Wrote %d bytes. Rebooting...", received);

    /* Send success response */
    const char *resp = "{\"status\":\"success\",\"message\":\"OTA update successful, rebooting...\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    /* Delay to allow response to be sent */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Reboot */
    esp_restart();

    return ESP_OK;
}

/* GET /api/ota - return current OTA info */
static esp_err_t ota_info_handler(httpd_req_t *req)
{
    /* Check authentication */
    esp_err_t ret = check_basic_auth(req);
    if (ret != ESP_OK) {
        return ret;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();

    char resp[256];
    snprintf(resp, sizeof(resp),
             "{\"running_partition\":\"%s\",\"boot_partition\":\"%s\",\"updating\":%s}",
             running ? running->label : "unknown",
             boot ? boot->label : "unknown",
             ota_updating ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

esp_err_t ota_handler_register(void)
{
    ESP_LOGI(TAG, "Registering OTA handlers");

    httpd_handle_t server = httpd_get_server();
    if (server == NULL) {
        ESP_LOGE(TAG, "HTTP server not started, cannot register OTA handlers");
        return ESP_ERR_INVALID_STATE;
    }

    httpd_uri_t ota_post = {
        .uri = "/api/ota",
        .method = HTTP_POST,
        .handler = ota_post_handler,
        .user_ctx = NULL,
    };

    httpd_uri_t ota_get = {
        .uri = "/api/ota",
        .method = HTTP_GET,
        .handler = ota_info_handler,
        .user_ctx = NULL,
    };

    httpd_register_uri_handler(server, &ota_post);
    httpd_register_uri_handler(server, &ota_get);

    ESP_LOGI(TAG, "OTA handlers registered (POST /api/ota, GET /api/ota)");
    return ESP_OK;
}
