#include "subscription_usage.h"

#include "cJSON.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *field(const cJSON *obj, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(obj, name);
}

static uint8_t remaining(const cJSON *item) {
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) return 255;
    return (uint8_t)lround(100 - fmin(100, fmax(0, item->valuedouble)));
}

static bool uint_value(const cJSON *item, uint32_t *out) {
    if (!cJSON_IsNumber(item)) return false;
    double n = item->valuedouble;
    if (!isfinite(n) || n < 0 || n > UINT32_MAX || n != floor(n)) return false;
    *out = (uint32_t)n;
    return true;
}

/* Provider timestamps are RFC 3339, independent of the watch's timezone. */
static bool digits(const char **cursor, int length, int *out) {
    int value = 0;
    for (int i = 0; i < length; i++) {
        char c = **cursor;
        if (c < '0' || c > '9') return false;
        value = value * 10 + c - '0';
        (*cursor)++;
    }
    *out = value;
    return true;
}

static bool leap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static uint32_t reset_time(const cJSON *value) {
    if (!cJSON_IsString(value)) return 0;
    const char *s = value->valuestring;
    int year, month, day, hour, minute, second;
    if (!digits(&s, 4, &year) || *s++ != '-' || !digits(&s, 2, &month) ||
        *s++ != '-' || !digits(&s, 2, &day) || *s++ != 'T' ||
        !digits(&s, 2, &hour) || *s++ != ':' || !digits(&s, 2, &minute) ||
        *s++ != ':' || !digits(&s, 2, &second)) return 0;
    static const int days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (year < 1970 || year > 2106 || month < 1 || month > 12 || day < 1 ||
        day > days_in_month[month - 1] + (month == 2 && leap(year)) ||
        hour > 23 || minute > 59 || second > 59) return 0;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return 0;
        while (*s >= '0' && *s <= '9') s++;
    }
    int offset = 0;
    if (*s == 'Z') s++;
    else {
        char sign = *s++;
        int h, m;
        if ((sign != '+' && sign != '-') || !digits(&s, 2, &h) ||
            *s++ != ':' || !digits(&s, 2, &m) || h > 23 || m > 59) return 0;
        offset = (sign == '+' ? 1 : -1) * (h * 3600 + m * 60);
    }
    int days = day - 1;
    for (int y = 1970; y < year; y++) days += 365 + leap(y);
    for (int m = 1; m < month; m++) days += days_in_month[m - 1] + (m == 2 && leap(year));
    int64_t epoch = (int64_t)days * 86400 + hour * 3600 + minute * 60 + second - offset;
    return !*s && epoch > 0 && epoch <= UINT32_MAX ? (uint32_t)epoch : 0;
}

static int32_t credit_cents(const cJSON *credits) {
    const cJSON *balance = field(credits, "balance");
    if (!cJSON_IsFalse(field(credits, "unlimited")) || !cJSON_IsString(balance)) return -1;
    const char *s = balance->valuestring;
    if (*s < '0' || *s > '9') return -1;
    int64_t whole = 0;
    while (*s >= '0' && *s <= '9') {
        whole = whole * 10 + *s++ - '0';
        if (whole > INT32_MAX / 100) return -1;
    }
    int64_t cents = whole * 100;
    if (*s == '.') {
        s++;
        if (*s < '0' || *s > '9') return -1;
        cents += (*s++ - '0') * 10;
        if (*s >= '0' && *s <= '9') cents += *s++ - '0';
        if (*s >= '5' && *s <= '9') cents++;
        while (*s >= '0' && *s <= '9') s++;
    }
    return !*s && cents <= INT32_MAX ? (int32_t)cents : -1;
}

static bool codex_usage(quota_card *c, const cJSON *root) {
    const cJSON *limits = field(root, "rate_limit");
    const char *names[] = {"primary_window", "secondary_window"};
    for (int i = 0; i < 2; i++) {
        const cJSON *window = field(limits, names[i]);
        uint32_t seconds, reset = 0, after;
        if (!uint_value(field(window, "limit_window_seconds"), &seconds)) continue;
        if (!uint_value(field(window, "reset_at"), &reset) &&
            uint_value(field(window, "reset_after_seconds"), &after) &&
            after <= UINT32_MAX - c->captured) reset = c->captured + after;
        uint8_t pct = remaining(field(window, "used_percent"));
        if (seconds == 18000) { c->short_pct = pct; c->short_reset = reset; }
        if (seconds == 604800) { c->week_pct = pct; c->week_reset = reset; }
    }
    uint32_t count;
    if (uint_value(field(field(root, "rate_limit_reset_credits"), "available_count"), &count) &&
        count <= INT32_MAX) c->resets = (int32_t)count;
    c->credit_cents = credit_cents(field(root, "credits"));
    const cJSON *plan = field(root, "plan_type");
    if (cJSON_IsString(plan) && *plan->valuestring && strlen(plan->valuestring) < sizeof c->tier &&
        strcspn(plan->valuestring, "\r\n\t") == strlen(plan->valuestring))
        snprintf(c->tier, sizeof c->tier, "%s", plan->valuestring);
    return c->short_pct != 255 || c->short_reset || c->week_pct != 255 || c->week_reset ||
           c->resets >= 0 || c->credit_cents >= 0;
}

static void claude_window(const cJSON *root, const char *name, uint8_t *pct, uint32_t *reset) {
    const cJSON *window = field(field(root, "rate_limits"), name);
    if (!cJSON_IsObject(window)) window = field(root, name);
    const char *used[] = {"utilization", "used_percentage", "used_percent", "usedPercentage", "usedPercent"};
    const char *dates[] = {"resets_at", "resetsAt", "reset_at", "resetAt"};
    for (size_t i = 0; i < sizeof used / sizeof *used && *pct == 255; i++)
        *pct = remaining(field(window, used[i]));
    for (size_t i = 0; i < sizeof dates / sizeof *dates && !*reset; i++)
        *reset = reset_time(field(window, dates[i]));
}

bool subscription_apply_usage(quota_card *card, const char *json, uint32_t observed) {
    if (!card || !json || observed < 1600000000 || observed < card->captured || card->provider > 1)
        return false;
    cJSON *root = cJSON_ParseWithOpts(json, NULL, true);
    quota_card next = *card;
    next.captured = observed;
    next.short_pct = next.week_pct = 255;
    next.short_reset = next.week_reset = 0;
    next.credit_cents = next.resets = -1;
    bool ok = false;
    if (cJSON_IsObject(root) && !field(root, "error")) {
        if (card->provider == 0) ok = codex_usage(&next, root);
        else {
            claude_window(root, "five_hour", &next.short_pct, &next.short_reset);
            claude_window(root, "seven_day", &next.week_pct, &next.week_reset);
            ok = next.short_pct != 255 || next.short_reset || next.week_pct != 255 || next.week_reset;
        }
    }
    cJSON_Delete(root);
    if (ok) *card = next;
    return ok;
}
