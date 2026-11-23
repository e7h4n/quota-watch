#pragma once

#include "quota.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUOTA_REFRESH_OK,
    QUOTA_REFRESH_INVALID,
    QUOTA_REFRESH_NETWORK,
    QUOTA_REFRESH_AUTH_REQUIRED,
    QUOTA_REFRESH_FORBIDDEN,
    QUOTA_REFRESH_RATE_LIMITED,
    QUOTA_REFRESH_PROVIDER_ERROR
} quota_refresh_result;

/* Call only with a grant issued for this provider's account usage resource.
 * codex_account_id is the workspace selected during authorization.
 * Updates the caller's card only on a complete, valid response. */
quota_refresh_result subscription_refresh(quota_card *card, const char *access_token,
                                          const char *codex_account_id);

#ifdef __cplusplus
}
#endif
