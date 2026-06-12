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

static bool ota_updating = false;

bool ota_is_updating(void)
{
    return ota_updating;
}

/* Receive firmware data via HTTP POST and write to OTA partition */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    esp_err_t ret;
    esp_ota_handle_t ota_handle = NULL;
    const esp_partition_t *update_partition = NULL;

    ESP_LOGI(TAG, "OTA POST request received");

    /* Check if already updating */
    if (ota_updating) {
        ESP_LOGW(TAG, "OTA already in progress, rejecting request");
        httpd_resp_send_err(req, 503, "OTA already in progress");
        return ESP_FAIL;
    }
    ota_updating = true;

    /* Get content length */
    int content_len = req->content_len;
    ESP_LOGI(TAG, "OTA content length: %d bytes", content_len);
    
    if (content_len <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_len);
        httpd_resp_send_err(req, 400, "Invalid content length");
        ota_updating = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update starting, firmware size: %d bytes", content_len);

    /* Get update partition */
    update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        httpd_resp_send_err(req, 500, "No OTA partition");
        ota_updating = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx",
             update_partition->subtype, (unsigned long)update_partition->address);

    /* Begin OTA write */
    ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, 500, "OTA begin failed");
        ota_updating = false;
        return ESP_FAIL;
    }

    /* Receive firmware data */
    char *ota_buf = malloc(1024);
    if (ota_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA buffer");
        esp_ota_end(ota_handle);
        httpd_resp_send_err(req, 500, "Memory error");
        ota_updating = false;
        return ESP_FAIL;
    }

    int remaining = content_len;
    int received = 0;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, ota_buf, (remaining > 1024) ? 1024 : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                ESP_LOGE(TAG, "OTA receive timeout");
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive timeout");
            } else {
                ESP_LOGE(TAG, "OTA receive error: %d", recv_len);
                httpd_resp_send_err(req, 500, "Receive error");
            }
            free(ota_buf);
            esp_ota_end(ota_handle);
            ota_updating = false;
            return ESP_FAIL;
        }

        ret = esp_ota_write(ota_handle, (const void *)ota_buf, recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
            free(ota_buf);
            esp_ota_end(ota_handle);
            httpd_resp_send_err(req, 500, "Write failed");
            ota_updating = false;
            return ESP_FAIL;
        }

        remaining -= recv_len;
        received += recv_len;

        /* Log progress every 100KB */
        if (received % (100 * 1024) < 1024) {
            ESP_LOGI(TAG, "OTA progress: %d / %d bytes (%d%%)",
                     received, content_len, (received * 100) / content_len);
        }
    }

    free(ota_buf);

    /* End OTA write */
    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, 500, "OTA validation failed");
        ota_updating = false;
        return ESP_FAIL;
    }

    /* Set boot partition */
    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, 500, "Set boot partition failed");
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
