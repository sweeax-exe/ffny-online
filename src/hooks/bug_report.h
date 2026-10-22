/* Bug reports: the game's log and a picture of the screen, sent to the developers through the
 * account service (netplay/relay/accounts/reports.go). F9 in the game takes the picture
 * (d3d11_translator.c) and sends both. Windows only, like the account service. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Send a report in the background. kind is "picture" (F9) or "crash"; log_path NULL means this
 * session's own log, read a moment later so the draws F9 asked for are in it; picture_path is a
 * 24-bit BMP or NULL. */
void bug_report_send(const char *kind, const char *log_path, const char *picture_path);

/* A line for the screen while a report is on its way and for a few seconds after; 0 otherwise. */
int bug_report_toast(char *out, int size);
int bug_report_busy(void);
/* F9 pressed: 1 to take the picture and send it now, 0 while the last one is under a minute old
 * or still on its way (a toast says so). */
int bug_report_f9(void);

/* After a crash: the previous run's log, when it ended in a crash report and the launcher has not
 * offered it yet (1, path in out). bug_report_crash_seen marks it offered, sent or not. */
int bug_report_last_crash(char *out, int size);
void bug_report_crash_seen(const char *log_path);

#ifdef __cplusplus
}
#endif
