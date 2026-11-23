idf_component_register(SRCS "board.cpp" "main.cpp" "accounts.c" "bluetooth.c" "launcher.c" "subscription_usage.c"
    INCLUDE_DIRS "." REQUIRES json bt driver esp_timer app_update nvs_flash M5GFX M5PM1 M5IOE1 okou_agent)
