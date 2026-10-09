/* One shared command policy for FAPP/1 builder, loader and execution. */
#include "falcon.h"

bool market_line_allowed(const char *line, i32 size) {
    static const char *allowed[] = {
        "echo", "date", "uname", "uptime", "whoami", "pwd", "ls",
        "help", "cal", "hwinfo", "free", "df", "clear", NULL
    };
    if (size <= 0 || size > 180) return false;
    for (i32 i = 0; i < size; i++) {
        char c = line[i];
        if (c < 32 || c > 126 || c == ';' || c == '|' || c == '>' ||
            c == '<' || c == '$' || c == '\\' || c == 96 ||
            c == '&') return false;
    }
    for (i32 j = 0; allowed[j]; j++) {
        i32 n = k_strlen(allowed[j]);
        if (size >= n && k_strncmp(line, allowed[j], n) == 0 &&
            (size == n || line[n] == ' ')) return true;
    }
    return false;
}
