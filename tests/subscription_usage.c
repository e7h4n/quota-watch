#include "subscription_usage.h"
#include <assert.h>
#include <string.h>

int main(void) {
    const uint32_t now = 1800000000;
    quota_card card = {.id="profile", .email="owner@example.com", .tier="Unknown"};
    const char *codex = "{\"rate_limit\":{"
        "\"primary_window\":{\"limit_window_seconds\":604800,\"used_percent\":100,\"reset_at\":1800000100},"
        "\"secondary_window\":{\"limit_window_seconds\":18000,\"used_percent\":25,\"reset_after_seconds\":42}},"
        "\"credits\":{\"unlimited\":false,\"balance\":\"5.105\"},"
        "\"rate_limit_reset_credits\":{\"available_count\":0},\"plan_type\":\"pro\"}";
    assert(subscription_apply_usage(&card, codex, now));
    assert(card.short_pct == 75 && card.week_pct == 0);
    assert(card.short_reset == now + 42 && card.week_reset == now + 100);
    assert(card.credit_cents == 511 && card.resets == 0 && !strcmp(card.tier,"pro"));
    assert(!strcmp(card.id, "profile") && !strcmp(card.email, "owner@example.com"));
    quota_card saved = card;
    const char *bad[] = {"{}", "[]", "{", "{\"error\":\"invalid_token\"}", "{}trailing",
                         "{\"credits\":{\"unlimited\":false,\"balance\":\"1e9\"}}"};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        assert(!subscription_apply_usage(&card, bad[i], now + 1));
        assert(!memcmp(&card, &saved, sizeof card));
    }
    assert(!subscription_apply_usage(&card, codex, now - 1));
    assert(!memcmp(&card, &saved, sizeof card));
    assert(subscription_apply_usage(&card,
        "{\"rate_limit_reset_credits\":{\"available_count\":2}}", now + 5));
    assert(card.short_pct == 255 && card.short_reset == 0 && card.week_pct == 255);
    assert(card.credit_cents == -1 && card.resets == 2);
    card.provider = 1;
    assert(subscription_apply_usage(&card, "{\"five_hour\":{\"utilization\":0},"
        "\"rate_limits\":{\"seven_day\":{\"used_percentage\":12.5,"
        "\"resets_at\":\"2024-02-29T08:00:00.000+08:00\"}}}", now + 6));
    assert(card.short_pct == 100 && card.week_pct == 88 && card.week_reset == 1709164800);
    assert(card.credit_cents == -1 && card.resets == -1);
    saved = card;
    assert(!subscription_apply_usage(&card,
        "{\"seven_day\":{\"resets_at\":\"2025-02-29T00:00:00Z\"}}", now + 7));
    assert(!memcmp(&card, &saved, sizeof card));
    return 0;
}
