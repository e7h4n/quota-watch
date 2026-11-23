#include "trending.h"

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool count_text(const char *start, const char *end, uint32_t *out) {
    while (start < end && isspace((unsigned char)*start)) ++start;
    while (end > start && isspace((unsigned char)end[-1])) --end;
    if (start == end || !isdigit((unsigned char)*start)) return false;
    uint32_t value = 0;
    for (; start < end; ++start) {
        if (*start == ',') continue;
        if (!isdigit((unsigned char)*start)) return false;
        unsigned digit = (unsigned)(*start - '0');
        if (value > (INT32_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *out = value;
    return true;
}

static bool repo_part(char *dest, size_t cap, const char *s, size_t n) {
    if (!n || n >= cap) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = s[i];
        if (!isalnum(c) && c != '-' && c != '_' && c != '.') return false;
    }
    memcpy(dest, s, n);
    dest[n] = 0;
    return true;
}

static bool parse_article(char *row, trend_project *out) {
    const char *suffix = strstr(row, "/stargazers\"");
    if (!suffix) return false;
    const char *path = suffix;
    while (path > row && path[-1] != '"') --path;
    if (path < row + 6 || memcmp(path - 6, "href=\"", 6) || *path != '/') return false;
    ++path;
    if (path >= suffix) return false;
    const char *slash = memchr(path, '/', suffix - path);
    if (!slash || !repo_part(out->owner, sizeof out->owner, path, slash - path) ||
        !repo_part(out->repo, sizeof out->repo, slash + 1, suffix - slash - 1)) return false;

    const char *close = strstr(suffix, "</a>");
    const char *icon = strstr(suffix, "</svg>");
    if (!close || !icon || icon >= close || !count_text(icon + 6, close, &out->stars)) return false;

    out->daily = -1;
    const char *label = strstr(close, " stars today");
    if (!label) label = strstr(close, " star today");
    if (label) {
        const char *start = label;
        while (start > close && (isdigit((unsigned char)start[-1]) || start[-1] == ',')) --start;
        uint32_t value;
        if (count_text(start, label, &value)) out->daily = (int32_t)value;
    }
    return true;
}

unsigned trend_parse(char *html, trend_project *projects, unsigned capacity) {
    unsigned count = 0;
    char *row = html;
    while (count < capacity && (row = strstr(row, "<article"))) {
        char *end = strstr(row, "</article>");
        if (!end) break;
        *end = 0;
        if (parse_article(row, &projects[count])) ++count;
        *end = '<';
        row = end + strlen("</article>");
    }
    return count;
}
