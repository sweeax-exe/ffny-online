/*
 * Bug reports. Players found it hard to find and attach the right run-....log.err, and a log
 * alone cannot show a wrong picture, so F9 in the game sends both: this session's log and the
 * picture F9 just took, as multipart/form-data to POST /api/report on the account service.
 * The answer is a short id the toast shows, so a player can name the report in a ticket.
 *
 * What leaves the PC: the log, cut to its first 512 KB and last 2.5 MB if it is longer, with the
 * Windows user folder blanked; the picture as a PNG; the build number. The privacy page
 * (accounts/server.go) says the same.
 */
#include "bug_report.h"

#if defined(_WIN32)
#include "account.h"
#include "updater.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../../tools/xboxrecomp/src/nv2a/stb/stb_image_write.h"

#define LOG_HEAD (512 << 10)
#define LOG_TAIL (2560 << 10)
#define TOAST_MS 8000
#define COOLDOWN_MS 60000

typedef struct { char *p; size_t n, cap; } Buf;

static int put(Buf *b, const void *data, size_t n)
{
    if (b->n + n > b->cap) {
        size_t cap = b->cap ? b->cap : 1 << 16;
        char *p;
        while (cap < b->n + n) cap *= 2;
        if (!(p = (char *)realloc(b->p, cap))) return 0;
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
    return 1;
}

static int puts_(Buf *b, const char *s) { return put(b, s, strlen(s)); }

static void png_out(void *ctx, void *data, int n) { put((Buf *)ctx, data, (size_t)n); }

static SRWLOCK s_lock = SRWLOCK_INIT;
static char s_toast[160];
static ULONGLONG s_toast_until;    /* 0: shown while sending */
static volatile LONG s_busy;

static void set_toast(const char *text, int lasting)
{
    AcquireSRWLockExclusive(&s_lock);
    snprintf(s_toast, sizeof(s_toast), "%s", text);
    s_toast_until = lasting ? GetTickCount64() + TOAST_MS : 0;
    ReleaseSRWLockExclusive(&s_lock);
}

int bug_report_toast(char *out, int size)
{
    int on;
    AcquireSRWLockShared(&s_lock);
    on = s_toast[0] && (!s_toast_until || GetTickCount64() < s_toast_until);
    if (on) snprintf(out, size, "%s", s_toast);
    ReleaseSRWLockShared(&s_lock);
    return on;
}

/* Replace every case-insensitive occurrence of what with by, in place (by is not longer). */
static size_t blank(char *s, size_t n, const char *what, const char *by)
{
    size_t wl = strlen(what), bl = strlen(by), i = 0, o = 0;
    if (wl < 4 || bl > wl) return n;
    while (i < n) {
        if (i + wl <= n && _strnicmp(s + i, what, wl) == 0) {
            memcpy(s + o, by, bl);
            o += bl;
            i += wl;
        } else {
            s[o++] = s[i++];
        }
    }
    return o;
}

/* The log, its head and tail if long, with the user folder blanked. */
/* Lines a long session repeats by the hundred thousand and that tell nothing about a bug: the
 * graphics queue's positions, the per-frame counters, kernel call counts, USB polls. About nine in
 * ten lines of a long log; left out, a report's head and tail reach much further into the session.
 * The frame's [BATCH] dump and every other line stay. */
static const char *const k_noise[] = {
    "  [NV2A] DMA_PUT", "  [NV2A] PCRTC_START", "[TRANS] frames ", "  [GPU] draw #", "  [READ] from=",
    "  [KERNEL]   ordinal", "  [KTRAIL]", "  [OHCI0] periodic", "  [DPC] queued", "  [PGRAPH] software method",
    "  [NV2A] interrupt",
};

static int noise(const char *line, size_t n)
{
    size_t i, k;
    for (i = 0; i < sizeof(k_noise) / sizeof(k_noise[0]); i++) {
        k = strlen(k_noise[i]);
        if (n >= k && !memcmp(line, k_noise[i], k)) return 1;
    }
    return 0;
}

/* The log without the noise: its first LOG_HEAD bytes and its last LOG_TAIL, with the user folder
 * blanked. Read through once in pieces, so a log of hundreds of MB costs no more memory. */
static int read_log(const char *path, Buf *out)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);
    Buf head = { 0 }, tail = { 0 };
    char *chunk, *line = NULL;
    size_t line_n = 0, line_cap = 0, cut = 0, dropped = 0;
    int head_full = 0;
    DWORD got;
    char profile[MAX_PATH], user[128], seg[140], note[96];
    if (h == INVALID_HANDLE_VALUE) return 0;
    chunk = (char *)malloc(1 << 20);
    if (!chunk) { CloseHandle(h); return 0; }
    for (;;) {
        DWORD i, from = 0;
        int last = !ReadFile(h, chunk, 1 << 20, &got, NULL) || got == 0;
        for (i = 0; i <= got; i++) {
            int end = last ? i == got : i < got && chunk[i] == '\n';
            if (!end && i < got) continue;
            if (i == got && !last) {            /* a line that goes on in the next piece */
                size_t k = i - from;
                if (line_n + k > line_cap) {
                    char *p2 = (char *)realloc(line, line_cap = (line_n + k) * 2 + 256);
                    if (!p2) break;
                    line = p2;
                }
                memcpy(line + line_n, chunk + from, k);
                line_n += k;
                break;
            }
            {
                size_t k = i - from + (i < got ? 1 : 0);
                const char *text = chunk + from;
                if (line_n) {                   /* finish the line carried over */
                    if (line_n + k > line_cap) {
                        char *p2 = (char *)realloc(line, line_cap = (line_n + k) * 2 + 256);
                        if (!p2) break;
                        line = p2;
                    }
                    memcpy(line + line_n, text, k);
                    text = line;
                    k += line_n;
                    line_n = 0;
                }
                if (k && noise(text, k)) dropped++;
                else if (k && !head_full && head.n + k <= LOG_HEAD) put(&head, text, k);
                else if (k) {
                    head_full = 1;          /* in order: once full, everything goes on in the tail */
                    put(&tail, text, k);
                    if (tail.n > 2 * LOG_TAIL) {    /* keep only the newest LOG_TAIL bytes, from a line start */
                        size_t keep = LOG_TAIL, at = tail.n - keep;
                        while (at < tail.n && tail.p[at - 1] != '\n') at++;
                        cut += at;
                        memmove(tail.p, tail.p + at, tail.n - at);
                        tail.n -= at;
                    }
                }
            }
            from = i + 1;
        }
        if (last) break;
    }
    CloseHandle(h);
    free(chunk);
    free(line);
    if (tail.n > LOG_TAIL) {
        size_t at = tail.n - LOG_TAIL;
        while (at < tail.n && tail.p[at - 1] != '\n') at++;
        cut += at;
        memmove(tail.p, tail.p + at, tail.n - at);
        tail.n -= at;
    }
    if (cut || dropped) {
        snprintf(note, sizeof(note), "\n[REPORT] ... %zu bytes left out here; %zu noise lines left out ...\n", cut, dropped);
        put(&head, note, strlen(note));
    }
    if (tail.n) put(&head, tail.p, tail.n);
    free(tail.p);
    if (!head.p) return 0;
    if (GetEnvironmentVariableA("USERPROFILE", profile, sizeof(profile)))
        head.n = blank(head.p, head.n, profile, "%USERPROFILE%");
    if (GetEnvironmentVariableA("USERNAME", user, sizeof(user))) {     /* in other folders: D:\<name>\Games */
        snprintf(seg, sizeof(seg), "\\%s\\", user);
        head.n = blank(head.p, head.n, seg, "\\%USER%\\");
    }
    put(out, head.p, head.n);
    free(head.p);
    return 1;
}

/* The F9 picture (our own 24-bit bottom-up BMP) as a PNG. */
static int read_picture(const char *path, Buf *out)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[54], *rgb;
    int w, h, row, y, ok = 0;
    if (!f) return 0;
    if (fread(hdr, 1, 54, f) == 54 && hdr[0] == 'B' && hdr[1] == 'M' && hdr[28] == 24) {
        w = hdr[18] | hdr[19] << 8;
        h = hdr[22] | hdr[23] << 8;
        row = (w * 3 + 3) & ~3;
        rgb = (w > 0 && h > 0 && w <= 8192 && h <= 8192) ? (unsigned char *)malloc((size_t)w * h * 3) : NULL;
        if (rgb) {
            unsigned char *line = (unsigned char *)malloc(row);
            ok = line != NULL;
            fseek(f, hdr[10] | hdr[11] << 8, SEEK_SET);
            for (y = 0; ok && y < h; y++) {
                unsigned char *dst = rgb + (size_t)(h - 1 - y) * w * 3;
                int x;
                if (fread(line, 1, row, f) != (size_t)row) { ok = 0; break; }
                for (x = 0; x < w; x++) {
                    dst[x * 3] = line[x * 3 + 2];
                    dst[x * 3 + 1] = line[x * 3 + 1];
                    dst[x * 3 + 2] = line[x * 3];
                }
            }
            free(line);
            ok = ok && stbi_write_png_to_func(png_out, out, w, h, 3, rgb, w * 3);
            free(rgb);
        }
    }
    fclose(f);
    return ok;
}

/* This session's log: wherever stderr goes, if it is a file. */
static int own_log(char *out, int size)
{
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(stderr));
    char path[MAX_PATH + 8];
    DWORD n;
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK) return 0;
    n = GetFinalPathNameByHandleA(h, path, sizeof(path), FILE_NAME_NORMALIZED);
    if (!n || n >= sizeof(path)) return 0;
    snprintf(out, size, "%s", strncmp(path, "\\\\?\\", 4) == 0 ? path + 4 : path);
    return 1;
}

typedef struct { char kind[16], log[MAX_PATH + 8], picture[MAX_PATH + 8]; } Job;

static void field(Buf *b, const char *boundary, const char *name, const char *file, const char *ctype)
{
    puts_(b, "--");
    puts_(b, boundary);
    puts_(b, "\r\nContent-Disposition: form-data; name=\"");
    puts_(b, name);
    if (file) {
        puts_(b, "\"; filename=\"");
        puts_(b, file);
    }
    puts_(b, "\"\r\n");
    if (ctype) {
        puts_(b, "Content-Type: ");
        puts_(b, ctype);
        puts_(b, "\r\n");
    }
    puts_(b, "\r\n");
}

static DWORD WINAPI send_thread(LPVOID param)
{
    Job *job = (Job *)param;
    Buf log = { 0 }, pic = { 0 }, body = { 0 };
    char boundary[48], ctype[96], build[16], answer[128], text[160];
    int status = 0, have_pic;

    if (!job->log[0]) {
        Sleep(2500);                /* the frame after F9 and its draws (nv2a_pb_dump_next_frame) */
        fflush(stderr);
        if (!own_log(job->log, sizeof(job->log))) job->log[0] = 0;
    }
    if (!job->log[0] || !read_log(job->log, &log)) {
        set_toast("Bug report not sent: this session has no log file.", 1);
        goto done;
    }
    have_pic = job->picture[0] && read_picture(job->picture, &pic);

    snprintf(boundary, sizeof(boundary), "ffny%08lx%08lx", (unsigned long)GetTickCount(), (unsigned long)GetCurrentProcessId());
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", boundary);
    snprintf(build, sizeof(build), "%d", updater_local_build());
    field(&body, boundary, "kind", NULL, NULL);
    puts_(&body, job->kind);
    puts_(&body, "\r\n");
    field(&body, boundary, "build", NULL, NULL);
    puts_(&body, build);
    puts_(&body, "\r\n");
    field(&body, boundary, "log", "run.log.err", "text/plain");
    put(&body, log.p, log.n);
    puts_(&body, "\r\n");
    if (have_pic) {
        field(&body, boundary, "picture", "picture.png", "image/png");
        put(&body, pic.p, pic.n);
        puts_(&body, "\r\n");
    }
    puts_(&body, "--");
    puts_(&body, boundary);
    if (!puts_(&body, "--\r\n")) {
        set_toast("Bug report not sent: out of memory.", 1);
        goto done;
    }
    status = account_send_bug_report(ctype, body.p, (int)body.n, answer, sizeof(answer));
    fprintf(stderr, "[REPORT] bug report (%s, log %zu bytes, picture %zu bytes) sent: HTTP %d %s\n", job->kind, log.n,
            pic.n, status, status == 201 ? answer : "");
    if (status == 201)
        snprintf(text, sizeof(text), "Bug report sent - code %.6s. Thanks!", answer);
    else if (status == 429)
        snprintf(text, sizeof(text), "Bug report not sent: too many reports, try again in an hour.");
    else
        snprintf(text, sizeof(text), "Bug report not sent (%s). The picture is beside settings.ini.",
                 status ? "the server said no" : "no connection");
    set_toast(text, 1);
done:
    free(log.p);
    free(pic.p);
    free(body.p);
    free(job);
    InterlockedExchange(&s_busy, 0);
    return 0;
}

void bug_report_send(const char *kind, const char *log_path, const char *picture_path)
{
    Job *job;
    HANDLE t;
    if (InterlockedExchange(&s_busy, 1)) return;        /* one at a time */
    job = (Job *)calloc(1, sizeof(*job));
    if (!job) { InterlockedExchange(&s_busy, 0); return; }
    snprintf(job->kind, sizeof(job->kind), "%s", kind);
    if (log_path) snprintf(job->log, sizeof(job->log), "%s", log_path);
    if (picture_path) snprintf(job->picture, sizeof(job->picture), "%s", picture_path);
    set_toast("Sending the bug report...", 0);
    t = CreateThread(NULL, 0, send_thread, job, 0, NULL);
    if (t) CloseHandle(t);
    else { free(job); InterlockedExchange(&s_busy, 0); set_toast("", 1); }
}

int bug_report_busy(void) { return s_busy != 0; }

/* F9 pressed: whether to take the picture and send it now. One report a minute from a game; the
 * service has its own limit per address (accounts/reports.go). Says on screen either way. */
int bug_report_f9(void)
{
    static ULONGLONG next_allowed;
    ULONGLONG now = GetTickCount64();
    char text[96];
    if (s_busy) return 0;               /* the toast already says it is on its way */
    if (now < next_allowed) {
        snprintf(text, sizeof(text), "Please wait %u s before the next bug report.",
                 (unsigned)((next_allowed - now + 999) / 1000));
        set_toast(text, 1);
        return 0;
    }
    next_allowed = now + COOLDOWN_MS;
    set_toast("Bug report: taking a picture...", 0);
    return 1;
}

/* The run before this one, if it ended in a crash report (main.c) not offered yet. Logs are
 * named by their start time (pc_settings.c), so the newest other one is the previous run. */
int bug_report_last_crash(char *out, int size)
{
    WIN32_FIND_DATAA fd;
    HANDLE find = FindFirstFileA("logs\\run-*.log.err", &fd);
    char own[MAX_PATH + 8], best[MAX_PATH] = "", path[MAX_PATH + 16], *tail;
    const char *own_name = NULL;
    HANDLE h;
    LARGE_INTEGER size_, at;
    DWORD got = 0;
    int crashed = 0;
    if (find == INVALID_HANDLE_VALUE) return 0;
    if (own_log(own, sizeof(own))) own_name = strrchr(own, '\\') ? strrchr(own, '\\') + 1 : own;
    do {
        if (own_name && _stricmp(fd.cFileName, own_name) == 0) continue;
        if (strcmp(fd.cFileName, best) > 0) snprintf(best, sizeof(best), "%s", fd.cFileName);
    } while (FindNextFileA(find, &fd));
    FindClose(find);
    if (!best[0]) return 0;
    snprintf(path, sizeof(path), "logs\\%s.seen", best);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return 0;
    snprintf(path, sizeof(path), "logs\\%s", best);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    tail = (char *)malloc((1 << 20) + 1);
    if (tail && GetFileSizeEx(h, &size_)) {
        at.QuadPart = size_.QuadPart > (1 << 20) ? size_.QuadPart - (1 << 20) : 0;
        if (SetFilePointerEx(h, at, NULL, FILE_BEGIN) && ReadFile(h, tail, 1 << 20, &got, NULL)) {
            DWORD i;
            for (i = 0; i + 7 <= got && !crashed; i++)
                crashed = memcmp(tail + i, "[CRASH]", 7) == 0;
        }
    }
    free(tail);
    CloseHandle(h);
    if (crashed) snprintf(out, size, "%s", path);
    return crashed;
}

void bug_report_crash_seen(const char *log_path)
{
    char path[MAX_PATH + 16];
    HANDLE h;
    snprintf(path, sizeof(path), "%s.seen", log_path);
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

#else

void bug_report_send(const char *kind, const char *log_path, const char *picture_path)
{ (void)kind; (void)log_path; (void)picture_path; }
int bug_report_toast(char *out, int size) { (void)out; (void)size; return 0; }
int bug_report_busy(void) { return 0; }
int bug_report_f9(void) { return 0; }
int bug_report_last_crash(char *out, int size) { (void)out; (void)size; return 0; }
void bug_report_crash_seen(const char *log_path) { (void)log_path; }

#endif
