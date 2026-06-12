#ifndef AUTH_MIDDLEWARE_H
#define AUTH_MIDDLEWARE_H

#include "esp_err.h"
#include "esp_http_server.h"

#define AUTH_TOKEN_LEN 32
#define AUTH_HEADER_PREFIX "Bearer "
#define AUTH_COOKIE_NAME "auth_token"

/**
 * @brief Initialize auth middleware (generate default credentials if none exist)
 */
esp_err_t auth_middleware_init(void);

/**
 * @brief Check if request has valid auth token (Bearer or Cookie)
 * @return ESP_OK if authenticated, ESP_FAIL otherwise (response already sent)
 */
esp_err_t auth_check_request(httpd_req_t *req);

/**
 * @brief Verify username/password and generate token
 * @param username Input username
 * @param password Input password
 * @param token_out Output buffer for token (min AUTH_TOKEN_LEN+1 bytes)
 * @param token_size Size of token_out buffer
 * @return ESP_OK if credentials valid and token generated
 */
esp_err_t auth_login(const char *username, const char *password, char *token_out, size_t token_size);

/**
 * @brief Logout (invalidate token)
 */
esp_err_t auth_logout(httpd_req_t *req);

/**
 * @brief Check if a URI is public (no auth required)
 */
bool auth_is_public_uri(const char *uri);

/**
 * @brief Get default admin username (for first-boot display)
 */
const char* auth_get_default_username(void);

/**
 * @brief Get default admin password (for first-boot display)
 */
const char* auth_get_default_password(void);

#endif
