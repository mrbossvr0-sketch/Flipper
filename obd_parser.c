#include "obd_parser.h"
#include <ctype.h>
#include <string.h>

static int hexval(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Accept ELM327 text containing echo, spaces, and optional 7E8 headers.
 * Find the actual 41/01-PID sequence; do not use echo request as a response.
 */
bool obd_parse_pid_response(const char* raw, uint8_t pid, ObdLive* live) {
    if(!raw || !live) return false;
    char hex[768];
    size_t n = 0;
    /* Compact typical ELM replies while retaining only hex bytes.
     * Nonhex status words are discarded line-by-line, not mixed into data.
     */
    const char* p = raw;
    while(*p) {
        const char* end = p;
        while(*end && *end != '\r' && *end != '\n' && *end != '>') end++;
        size_t len = (size_t)(end - p);
        bool has_nonhex = false;
        for(size_t j = 0; j < len; j++) {
            char c = p[j];
            if(!isspace((unsigned char)c) && hexval(c) < 0 && c != ':') {
                has_nonhex = true;
                break;
            }
        }
        if(!has_nonhex) {
            for(size_t j = 0; j < len && n < sizeof(hex) - 1; j++) {
                if(hexval(p[j]) >= 0) hex[n++] = p[j];
            }
        }
        p = *end ? end + 1 : end;
    }
    hex[n] = '\0';
    for(size_t i = 0; i + 6 <= n; i++) {
        int s0 = hexval(hex[i]);
        int s1 = hexval(hex[i + 1]);
        int p0 = hexval(hex[i + 2]);
        int p1 = hexval(hex[i + 3]);
        if(s0 < 0 || s1 < 0 || p0 < 0 || p1 < 0) continue;
        if(s0 * 16 + s1 != 0x41 || p0 * 16 + p1 != pid) continue;
        int a = hexval(hex[i + 4]) * 16 + hexval(hex[i + 5]);
        if(a < 0) return false;
        if(pid == 0x0C) {
            if(i + 8 > n) return false;
            int b = hexval(hex[i + 6]) * 16 + hexval(hex[i + 7]);
            if(b < 0) return false;
            live->rpm = ((a * 256 + b) / 4);
            live->rpm_ok = true;
            return true;
        } else if(pid == 0x0D) {
            live->speed_kmh = a;
            live->speed_ok = true;
            return true;
        } else if(pid == 0x05) {
            live->coolant_c = a - 40;
            live->coolant_ok = true;
            return true;
        }
    }
    return false;
}
