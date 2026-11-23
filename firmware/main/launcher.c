#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "okou_agent.h"

/* Identifies the managed native application in the USB installation package. */
static const char quota_watch_profile[] __attribute__((used, section(".rodata_custom_desc"), aligned(4))) =
    "quota-watch/native/stopwatch";

static const char *TAG = "launcher_app";

void launcher_start(void)
{
    /* board_init has configured the active-low Select input. Count the hold
     * from boot, with at least half a second of observed input. */
    if (gpio_get_level(GPIO_NUM_1) == 0) {
        int64_t deadline = esp_timer_get_time() + 500000;
        if (deadline < 3000000) deadline = 3000000;
        while (gpio_get_level(GPIO_NUM_1) == 0) {
            if (esp_timer_get_time() >= deadline) {
                esp_err_t err = okou_enter_bootloader();
                ESP_LOGE(TAG, "Entering bootloader: %s", esp_err_to_name(err));
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    ESP_LOGI(TAG, "Starting %s", quota_watch_profile);
    okou_config_t cfg = OKOU_CONFIG_DEFAULT();
    cfg.manage_network = true; /* the SDK's Wi-Fi manager is shared with the launcher (okl/wifis) */
    esp_err_t e = okou_agent_start(&cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "okou_agent_start: %s (no network)", esp_err_to_name(e));
    }
}
