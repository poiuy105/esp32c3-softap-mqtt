#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"
#include "esp_http_server.h"

esp_err_t http_server_start(void);
esp_err_t http_server_stop(void);

/**
 * @brief Get the HTTP server handle
 * @return httpd_handle_t Server handle, or NULL if not started
 */
httpd_handle_t httpd_get_server(void);

#endif
