#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>

static const char *TAG = "dns_server";

#define DNS_PORT 53
#define DNS_MAX_LEN 512
#define DNS_TTL 300

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

static TaskHandle_t dns_task_handle = NULL;
static int dns_socket = -1;
static uint32_t dns_override_ip = 0;

// Parse domain name from DNS query
static int parse_domain_name(const uint8_t *buffer, int offset, char *domain, int max_len)
{
    int pos = offset;
    int domain_pos = 0;
    int first_label = 1;
    
    while (buffer[pos] != 0) {
        uint8_t len = buffer[pos];
        
        // Check for compression pointer
        if ((len & 0xC0) == 0xC0) {
            uint16_t ptr = ((len & 0x3F) << 8) | buffer[pos + 1];
            return parse_domain_name(buffer, ptr, domain + domain_pos, max_len - domain_pos);
        }
        
        pos++;
        
        if (!first_label) {
            if (domain_pos < max_len - 1) {
                domain[domain_pos++] = '.';
            }
        }
        first_label = 0;
        
        for (int i = 0; i < len && domain_pos < max_len - 1; i++) {
            domain[domain_pos++] = buffer[pos++];
        }
    }
    
    domain[domain_pos] = '\0';
    return pos + 1;  // +1 for the null terminator
}

static void dns_server_task(void *pvParameters)
{
    uint8_t rx_buffer[DNS_MAX_LEN];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int query_count = 0;

    while (dns_socket >= 0) {
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&client_addr, &addr_len);
        if (len < 0) {
            if (dns_socket >= 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ESP_LOGW(TAG, "recvfrom failed: errno=%d", errno);
            }
            continue;  // Don't break on timeout, just continue
        }

        if (len < sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *header = (dns_header_t *)rx_buffer;
        uint16_t flags = ntohs(header->flags);
        uint16_t qdcount = ntohs(header->qdcount);

        // Only process standard queries
        if ((flags & 0x8000) != 0 || qdcount == 0) {
            continue;
        }

        // Parse domain name
        char domain[256] = {0};
        int query_offset = sizeof(dns_header_t);
        parse_domain_name(rx_buffer, query_offset, domain, sizeof(domain));
        
        query_count++;
        ESP_LOGI(TAG, "DNS query #%d: %s from %s", query_count, domain, 
                 inet_ntoa(client_addr.sin_addr));

        // Build response
        uint8_t tx_buffer[DNS_MAX_LEN];
        memcpy(tx_buffer, rx_buffer, len);

        dns_header_t *resp_header = (dns_header_t *)tx_buffer;
        resp_header->flags = htons(0x8180);  // Standard response, no error
        resp_header->ancount = htons(1);
        resp_header->nscount = 0;
        resp_header->arcount = 0;

        int resp_len = len;

        // Add answer section
        tx_buffer[resp_len++] = 0xC0;
        tx_buffer[resp_len++] = 0x0C;  // Pointer to question name
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;  // Type A
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;  // Class IN
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x01;
        tx_buffer[resp_len++] = 0x2C;  // TTL = 300 seconds
        tx_buffer[resp_len++] = 0x00;
        tx_buffer[resp_len++] = 0x04;  // RDLENGTH = 4
        memcpy(&tx_buffer[resp_len], &dns_override_ip, 4);
        resp_len += 4;

        int sent = sendto(dns_socket, tx_buffer, resp_len, 0,
               (struct sockaddr *)&client_addr, addr_len);
        
        if (sent > 0) {
            ESP_LOGI(TAG, "DNS response sent: %s -> 192.168.4.1", domain);
        }
    }

    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(uint16_t port, const char *override_ip)
{
    if (dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    dns_override_ip = inet_addr(override_ip);
    if (dns_override_ip == INADDR_NONE) {
        ESP_LOGE(TAG, "Invalid IP address");
        return ESP_ERR_INVALID_ARG;
    }

    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return ESP_FAIL;
    }

    int opt = 1;
    setsockopt(dns_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Set non-blocking mode instead of timeout
    int flags = fcntl(dns_socket, F_GETFL, 0);
    fcntl(dns_socket, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket");
        close(dns_socket);
        dns_socket = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create task");
        close(dns_socket);
        dns_socket = -1;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS server started on port %d, override IP: %s", port, override_ip);
    return ESP_OK;
}

esp_err_t dns_server_stop(void)
{
    if (dns_task_handle == NULL) {
        return ESP_OK;
    }

    int sock = dns_socket;
    dns_socket = -1;

    if (sock >= 0) {
        close(sock);
    }

    while (dns_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
