#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { TREND_LIMIT = 30 };
typedef struct {
    char owner[40], repo[101];
    uint32_t stars;
    int32_t daily; /* -1 when GitHub omits the daily change */
} trend_project;

unsigned trend_parse(char *html, trend_project *projects, unsigned capacity);

typedef struct {
    trend_project project;
    unsigned count, generation;
    time_t fetched;
    bool failed;
} trend_view;
void trending_start(void);
trend_view trending_read(unsigned index);

#ifdef __cplusplus
}
#endif
