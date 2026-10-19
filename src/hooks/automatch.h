/* Starting a match without the menus: the front end's own FuncCall strings, replayed on the
 * game thread from the main menu (docs/research/online-match-setup.md). Online rooms use it so
 * both PCs start the same match with the settings the room agreed on; RECOMP_AUTOMATCH starts
 * one for tests. */
#ifndef DEFJAM_AUTOMATCH_H
#define DEFJAM_AUTOMATCH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AutoMatch {
    int type;           /* front-end match-type index: 0 One on One */
    int rules;          /* 0 as the front end sends */
    int venue;          /* venue.xml feID: 1 The Foundation */
    int count;          /* fighters, 2..4 */
    int fighter[4];     /* fighter.xml ID */
    int pad[4];         /* controller port, -1 for the CPU */
} AutoMatch;

/* Ask for a match; it starts at the next quiet moment on the main menu. */
void automatch_request(const AutoMatch *m);

/* 1 while a request waits or the sequence runs. */
int automatch_pending(void);

/* From the FuncCall hook (src/recomp_manual.c), after the front end's own call returned:
 * `text` is that call. Runs the sequence when it is due. */
void automatch_on_funccall(const char *text);

/* Replays one front-end command on the game thread; returns the guest address of its result
 * string or 0. Defined in src/recomp_manual.c; call only from automatch_on_funccall. */
unsigned int defjam_fe_call(const char *command);

#ifdef __cplusplus
}
#endif

#endif
