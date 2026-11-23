#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Wire v1 uses little endian integers. All strings are NUL terminated.
// An id identifies one provider profile. 255 means absent percentage.
typedef struct {
    uint32_t captured, short_reset, week_reset;
    int32_t credit_cents, resets;  // -1 means absent
    uint8_t provider, short_pct, week_pct, reserved;
    char id[33], email[97], tier[30];
} quota_card;
enum {MAX_CARDS=8};
void accounts_init(void);
unsigned accounts_read(unsigned index, quota_card *out);
bool accounts_upsert(const quota_card *card);
bool accounts_delete(const char *id);
unsigned accounts_generation(void);
void bluetooth_start(void);
int bluetooth_code(void);
bool bluetooth_ready(void);
void launcher_start(void);
#ifdef __cplusplus
}
#endif
