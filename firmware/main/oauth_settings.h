#pragma once
#include <stdint.h>
#include "host/ble_gatt.h"

int client_id_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg);
void client_id_cancel(void);
