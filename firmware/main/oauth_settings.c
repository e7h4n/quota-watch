#include "oauth_settings.h"
#include <stdbool.h>
#include <string.h>
#include "host/ble_hs.h"
#include "nvs.h"

enum { ID_MAX = 128, BEGIN = 1, APPEND = 2, COMMIT = 3 };
static struct {
    uint16_t owner;
    uint8_t provider, expected, used;
    char value[ID_MAX + 1];
    bool active;
} transfer;

void client_id_cancel(void) { memset(&transfer, 0, sizeof transfer); }

static int stored_id(unsigned provider, char value[ID_MAX + 1], bool write) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("quota_oauth", write ? NVS_READWRITE : NVS_READONLY, &nvs);
    if (!write && err == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (err != ESP_OK) return BLE_ATT_ERR_UNLIKELY;
    const char *key = provider ? "claude_id" : "codex_id";
    if (write) {
        err = *value ? nvs_set_str(nvs, key, value) : nvs_erase_key(nvs, key);
        if (err == ESP_OK) err = nvs_commit(nvs);
    } else {
        size_t size = ID_MAX + 1;
        err = nvs_get_str(nvs, key, value, &size);
    }
    nvs_close(nvs);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND ? 0 : BLE_ATT_ERR_UNLIKELY;
}

int client_id_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg) {
    unsigned provider = (uintptr_t)arg;
    struct ble_gap_conn_desc peer;
    if (ble_gap_conn_find(conn, &peer) || !peer.sec_state.authenticated || !peer.sec_state.encrypted)
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char saved[ID_MAX + 1] = {0};
        int err = stored_id(provider, saved, false);
        if (err) return err;
        return os_mbuf_append(ctx->om, saved, strlen(saved)) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    }
    uint8_t p[20];
    uint16_t n = 0;
    if (ble_hs_mbuf_to_flat(ctx->om, p, sizeof p, &n) || n == 0) goto invalid;
    if (p[0] == BEGIN && n == 2 && p[1] <= ID_MAX) {
        client_id_cancel();
        transfer.owner = conn;
        transfer.active = true;
        transfer.provider = provider;
        transfer.expected = p[1];
        return 0;
    }
    if (!transfer.active || transfer.owner != conn || transfer.provider != provider) goto invalid;
    if (p[0] == APPEND && n > 2 && p[1] == transfer.used &&
        transfer.used + n - 2 <= transfer.expected) {
        for (int i = 2; i < n; i++) if (p[i] < 33 || p[i] > 126) goto invalid;
        memcpy(transfer.value + transfer.used, p + 2, n - 2);
        transfer.used += n - 2;
        return 0;
    }
    if (p[0] == COMMIT && n == 1 && transfer.used == transfer.expected) {
        int err = stored_id(provider, transfer.value, true);
        client_id_cancel();
        return err;
    }
invalid:
    client_id_cancel();
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
}
