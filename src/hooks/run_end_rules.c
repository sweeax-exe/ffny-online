/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 sweeax-exe (FFNY Online) */
/* run_end.h's file and message, pure C (tests/unit/test_run_end_native.py compiles this natively). */
#include "run_end.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The value after `key` in one line ("at=" ...), letters a module's name may hold only; 0 without it. */
static int value(const char *line, size_t n, const char *key, char *out, size_t size)
{
    size_t k = strlen(key), i, o = 0;
    for (i = 0; i + k <= n; i++) {
        if ((i && line[i - 1] != ' ') || strncmp(line + i, key, k)) continue;
        for (i += k; i < n && line[i] != ' ' && o + 1 < size; i++) {
            char c = line[i];
            int ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     c == '.' || c == '_' || c == '-' || c == '+';
            out[o++] = ok ? c : '_';
        }
        out[o] = 0;
        return 1;
    }
    return 0;
}

int run_end_parse(const char *text, RunEnd *out)
{
    char how[12] = "", exit_code[12] = "", fault[12] = "", v[32];
    int begun = 0, crashed = 0, exited = 0;
    const char *p = text;
    memset(out, 0, sizeof(*out));
    while (*p) {
        size_t n = strcspn(p, "\r\n"), w = strcspn(p, " \r\n");
        #define IS(word) (w == strlen(word) && !strncmp(p, word, w))
        if (IS("begin")) {
            begun = 1;
            if (value(p, n, "build=", v, sizeof v)) out->build = atoi(v);
            if (value(p, n, "cl=", v, sizeof v)) out->cl = atol(v);
            if (value(p, n, "at=", v, sizeof v)) out->at = atoll(v);
        } else if (IS("closed") || IS("quit") || IS("update")) {
            if (!how[0]) snprintf(how, sizeof how, "%.*s", (int)w, p);
        } else if (IS("hung")) {
            out->hung = 1;
        } else if (IS("moved")) {
            out->hung = 0;
        } else if (IS("crash")) {
            crashed = 1;        /* the last one: a fault before it was handled, the run went on */
            fault[0] = out->where[0] = out->from[0] = 0;
            value(p, n, "code=", fault, sizeof fault);
            value(p, n, "at=", out->where, sizeof out->where);
            value(p, n, "from=", out->from, sizeof out->from);
        } else if (IS("exit")) {
            exited = value(p, n, "code=", exit_code, sizeof exit_code);
            if (value(p, n, "at=", v, sizeof v)) out->at = atoll(v);
        }
        #undef IS
        p += n;
        p += strspn(p, "\r\n");
    }
    if (!begun) return 0;
    /* A close after a fault is a close (a fault can be handled, or the player closed the window that stood there); a
     * fault with exit code 0 was handled too: the title left by itself. */
    if (how[0]) snprintf(out->end, sizeof out->end, "%s", how);
    else if (crashed && !(exited && !strcmp(exit_code, "00000000"))) snprintf(out->end, sizeof out->end, "crash");
    else if (exited) snprintf(out->end, sizeof out->end, "%s", !strcmp(exit_code, "00000000") ? "quit" : "exit");
    else snprintf(out->end, sizeof out->end, "unknown");
    if (strcmp(out->end, "crash")) out->where[0] = out->from[0] = 0;
    snprintf(out->code, sizeof out->code, "%s", exited ? exit_code : !strcmp(out->end, "crash") ? fault : "");
    return 1;
}

int run_end_ask_due(int mode, int asked)
{
    if (mode != RUN_END_ASK) return 0;
    return asked < RUN_END_ASK_TIMES ? 1 : 2;
}

int run_end_json(const RunEnd *r, long long now, char *out, size_t size)
{
    long long ago = r->at > 0 && now > r->at ? now - r->at : 0;
    int n = snprintf(out, size, "{\"build\":%d,\"cl\":%ld,\"end\":\"%s\",\"hung\":%d,\"code\":\"%s\",\"at\":\"%s\",\"from\":\"%s\",\"ago\":%lld}",
                     r->build, r->cl, r->end, r->hung, r->code, r->where, r->from, ago);
    return n > 0 && (size_t)n < size ? n : 0;
}
