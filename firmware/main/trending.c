#include "trending.h"

#include <stdlib.h>
#include <string.h>
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define TREND_URL "https://github.com/trending?since=daily"
enum { HTML_LIMIT = 1536 * 1024 };
static SemaphoreHandle_t mutex;
static trend_project *list;
static unsigned count, generation;
static time_t fetched;
static bool failed;

static unsigned download(trend_project *result) {
    esp_http_client_config_t config = {
        .url = TREND_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "Quota-Watch",
        .timeout_ms = 12000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return 0;
    char *html = NULL;
    unsigned parsed = 0;
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    int64_t length = esp_http_client_fetch_headers(client);
    if (esp_http_client_get_status_code(client) != 200 || length > HTML_LIMIT) goto done;
    size_t capacity = length > 0 ? (size_t)length : HTML_LIMIT;
    html = heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!html) goto done;
    size_t used = 0;
    while (used < capacity + 1) {
        size_t room = capacity + 1 - used;
        int n = esp_http_client_read(client, html + used, room > 4096 ? 4096 : room);
        if (n < 0) goto done;
        if (n == 0) break;
        used += n;
    }
    if (used <= capacity && esp_http_client_is_complete_data_received(client)) {
        html[used] = 0;
        parsed = trend_parse(html, result, TREND_LIMIT);
    }
done:
    free(html);
    esp_http_client_cleanup(client);
    return parsed;
}

static void refresh(void *arg) {
    bool clock_started = false;
    for (;;) {
        esp_netif_ip_info_t ip;
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (!sta || esp_netif_get_ip_info(sta, &ip) != ESP_OK || !ip.ip.addr) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (time(NULL) < 1600000000) {
            if (!clock_started) {
                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
                esp_err_t err = esp_netif_sntp_init(&config);
                clock_started = err == ESP_OK || err == ESP_ERR_INVALID_STATE;
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        trend_project *next = heap_caps_calloc(TREND_LIMIT, sizeof *next, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        unsigned n = next ? download(next) : 0;
        xSemaphoreTake(mutex, portMAX_DELAY);
        failed = !n;
        if (n) {
            trend_project *old = list;
            list = next;
            next = old;
            count = n;
            fetched = time(NULL);
        }
        ++generation;
        xSemaphoreGive(mutex);
        free(next);
        if (n) ESP_LOGI("trending", "Loaded %u GitHub projects", n);
        else ESP_LOGW("trending", "Could not refresh daily projects");
        vTaskDelay(pdMS_TO_TICKS(n ? 3600000 : 60000));
    }
}

void trending_start(void) {
    mutex = xSemaphoreCreateMutex();
    if (!mutex || xTaskCreate(refresh, "github_trending", 8192, NULL, 2, NULL) != pdPASS) failed = true;
}

trend_view trending_read(unsigned index) {
    trend_view view = {.failed = true};
    if (!mutex) return view;
    xSemaphoreTake(mutex, portMAX_DELAY);
    view.count = count;
    view.generation = generation;
    view.fetched = fetched;
    view.failed = failed;
    if (count) view.project = list[index % count];
    xSemaphoreGive(mutex);
    return view;
}
