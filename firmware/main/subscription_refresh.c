#include "subscription_refresh.h"
#include "subscription_usage.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "mbedtls/platform_util.h"

enum { MAX_USAGE_BYTES = 32768 };

static bool safe_header_value(const char *text, size_t max_length) {
    if (!text || !*text) return false;
    for (size_t i = 0; text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (i >= max_length || c < 33 || c > 126) return false;
    }
    return true;
}

static quota_refresh_result read_response(esp_http_client_handle_t client, quota_card *card) {
    if (esp_http_client_open(client, 0) != ESP_OK) return QUOTA_REFRESH_NETWORK;
    int64_t length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status == 401) return QUOTA_REFRESH_AUTH_REQUIRED;
    if (status == 403) return QUOTA_REFRESH_FORBIDDEN;
    if (status == 429) return QUOTA_REFRESH_RATE_LIMITED;
    if (status <= 0) return QUOTA_REFRESH_NETWORK;
    if (status != 200 || length > MAX_USAGE_BYTES) return QUOTA_REFRESH_PROVIDER_ERROR;

    // Chunked replies do not have a content length. Bound the complete response
    // before decoding it, and reject embedded NULs or truncated JSON.
    size_t capacity = length > 0 ? (size_t)length : MAX_USAGE_BYTES;
    char *body = malloc(capacity + 1);
    if (!body) return QUOTA_REFRESH_NETWORK;
    size_t total = 0;
    quota_refresh_result result = QUOTA_REFRESH_NETWORK;
    while (total < capacity + 1) {
        int n = esp_http_client_read(client, body + total, (int)(capacity + 1 - total));
        if (n < 0) break;
        if (n == 0) {
            if (!esp_http_client_is_complete_data_received(client)) break;
            result = QUOTA_REFRESH_PROVIDER_ERROR;
            if (memchr(body, 0, total)) break;
            time_t now = time(NULL);
            if (now < 1600000000 || now > UINT32_MAX) break;
            body[total] = 0;
            if (subscription_apply_usage(card, body, (uint32_t)now)) result = QUOTA_REFRESH_OK;
            break;
        }
        total += (size_t)n;
        if (total > capacity) result = QUOTA_REFRESH_PROVIDER_ERROR;
    }
    free(body);
    return result;
}

quota_refresh_result subscription_refresh(quota_card *card, const char *access_token,
                                          const char *codex_account_id) {
    if (!card || card->provider > 1 || !safe_header_value(access_token, 16384) ||
        (!card->provider && !safe_header_value(codex_account_id, 128)))
        return QUOTA_REFRESH_INVALID;

    esp_http_client_config_t config = {
        .url = card->provider ? "https://api.anthropic.com/api/oauth/usage"
                              : "https://chatgpt.com/backend-api/wham/usage",
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true,
        .timeout_ms = 10000,
        .user_agent = "Quota-Watch",
        .buffer_size_tx = (int)strlen(access_token) + 512,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return QUOTA_REFRESH_NETWORK;
    size_t size = strlen(access_token) + sizeof("Bearer ");
    char *authorization = malloc(size);
    quota_refresh_result result = QUOTA_REFRESH_NETWORK;
    if (authorization) {
        memcpy(authorization, "Bearer ", 7);
        memcpy(authorization + 7, access_token, size - 7);
        esp_err_t err = esp_http_client_set_header(client, "Authorization", authorization);
        if (err == ESP_OK && !card->provider)
            err = esp_http_client_set_header(client, "ChatGPT-Account-Id", codex_account_id);
        if (err == ESP_OK && card->provider)
            err = esp_http_client_set_header(client, "anthropic-beta", "oauth-2025-04-20");
        if (err == ESP_OK) result = read_response(client, card);
        esp_http_client_delete_header(client, "Authorization");
        mbedtls_platform_zeroize(authorization, size);
        free(authorization);
    }
    esp_http_client_cleanup(client);
    return result;
}
