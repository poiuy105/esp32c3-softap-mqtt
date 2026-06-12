#include "ota_handler.h"
#include "http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "auth_middleware.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "OTA";

static bool ota_updating = false;

bool ota_is_updating(void)
{
    return ota_updating;
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

    /* Check authentication via unified middleware */
    ret = auth_check_request(req);
    if (ret != ESP_OK) {
        return ESP_OK; // Response already sent by auth middleware
    }

    /* Check if already updating */
    if (ota_updating) {
        ESP_LOGW(TAG, "OTA already in progress, rejecting request");
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"OTA already in progress\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    ota_updating = true;

    /* Get content length */
    int content_len = req->content_len;
    ESP_LOGI(TAG, "OTA content length: %d bytes", content_len);

    if (content_len <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_len);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Invalid content length\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
    }

    /* Check partition size */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"No OTA partition found\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
    }

    if (content_len > update_partition->size) {
        ESP_LOGE(TAG, "Firmware too large: %d > %lu bytes", content_len, (unsigned long)update_partition->size);
        httpd_resp_set_status(req, "413 Payload Too Large");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Firmware too large\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
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
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"OTA begin failed\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
    }

    /* Receive firmware data */
    #define OTA_BUF_SIZE 4096
    char *ota_buf = malloc(OTA_BUF_SIZE);
    if (ota_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        esp_ota_end(ota_handle);
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Memory allocation failed\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
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
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"Receive timeout\"}", HTTPD_RESP_USE_STRLEN);
            ota_updating = false;
            return ESP_OK;
        }

        /* Verify firmware header on first chunk */
        if (!header_verified) {
            if (!verify_firmware_header((uint8_t *)ota_buf, recv_len)) {
                ESP_LOGE(TAG, "Firmware header verification failed");
                free(ota_buf);
                esp_ota_end(ota_handle);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, "{\"error\":\"Invalid firmware header\"}", HTTPD_RESP_USE_STRLEN);
                ota_updating = false;
                return ESP_OK;
            }
            header_verified = true;
        }

        ret = esp_ota_write(ota_handle, (const void *)ota_buf, recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
            free(ota_buf);
            esp_ota_end(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_set_type(req, "application/json");
            httpd_resp_send(req, "{\"error\":\"OTA write failed\"}", HTTPD_RESP_USE_STRLEN);
            ota_updating = false;
            return ESP_OK;
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
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"OTA end failed\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
    }

    /* Set boot partition */
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"Set boot partition failed\"}", HTTPD_RESP_USE_STRLEN);
        ota_updating = false;
        return ESP_OK;
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
    /* Check authentication via unified middleware */
    esp_err_t ret = auth_check_request(req);
    if (ret != ESP_OK) {
        return ESP_OK;
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
