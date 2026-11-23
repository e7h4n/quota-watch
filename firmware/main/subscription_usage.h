#pragma once

#include "quota.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Apply one provider usage response to an existing, identified card. On failure
 * leave the last good card (including its observation time) untouched. */
bool subscription_apply_usage(quota_card *card, const char *json, uint32_t observed);

#ifdef __cplusplus
}
#endif
