idf_component_register(SRCS "board.cpp" "main.cpp" "trending.c" "trend_parse.c" "accounts.c" "bluetooth.c" "oauth_settings.c" "launcher.c" "subscription_usage.c" "subscription_refresh.c"
    INCLUDE_DIRS "." REQUIRES esp_netif esp_http_client mbedtls json bt driver esp_timer app_update nvs_flash M5GFX M5PM1 M5IOE1 okou_agent)
