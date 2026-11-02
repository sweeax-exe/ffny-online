/* The fight camera and what it frames, as one log line (read-only; nothing here writes guest memory).
 *
 * Why: a Team picture report (9a2c08, build 39) showed the HUD of four fighters and a camera on an
 * empty, dark part of the venue. The lockstep checksums cover the fighters, the generators and the
 * time scale, not the camera, so a camera that parts on one PC does not show as a desync, and the
 * report's log had no camera or fighter position to tell "the fighters were apart" from "this PC's
 * camera was elsewhere". This line is written with every picture report (F9) and, for tests, every
 * RECOMP_CAMERA_WATCH=<steps> fight steps (src/hooks/test_telemetry.c).
 *
 * What the game's camera does (src/recomp/gen, verified by reading and by these lines):
 *   sub_001A3D50, called by every fight step, rebuilds the per-step snapshot at 0x340D58 from the
 *   fighter records; its four slots at 0x340D70 (stride 0xC8) hold "fighter present" (+0), the
 *   fighter's position (+0x34 / +0x40) and flags (+0xC0; bit 3 = the record's +0x932 bit 0x200,
 *   a knocked-out fighter). sub_000AC590 (this = CLogic+0x2DB44 = 0x3772A4), the step's last call,
 *   copies the venue's camera tuning (0x39C168.., read from the venue XML's <camera> block at load
 *   by sub_000AB160) and runs sub_000AB550: the box around every present, not-out slot, its centre
 *   followed in X and Z, its half 3-D diagonal clamped to 20..80 for the distance. The view is the
 *   director's eye (+4) and target (+0x10). Everything it reads is simulation state, so every PC
 *   computes the same camera unless that state or the tuning differs. */
#include "host.h"
#include <stdio.h>
#include <stdlib.h>
#include "recomp/gen/recomp_types.h"

#define CAM_DIRECTOR  0x003772A4u   /* CLogic (0x349760) + 0x2DB44 */
#define CAM_SLOTS     0x00340D70u
#define CAM_STRIDE    0xC8u
#define ACTORS        0x003B92E0u
#define STRIDE        0x12E8u

static void camera_line(const char *why, long long step)
{
    char line[1400], at[32] = "";
    int n = 0, i;
    if (step >= 0) snprintf(at, sizeof at, " %lld", step);
    n += snprintf(line + n, sizeof line - (size_t)n,
                  "[CAMWATCH] %s%s: logic state %u, mode %02X, flags %08X, tutorial %08X; dir %08X sel %d dist %.1f "
                  "pitch %.1f eye %.1f,%.1f,%.1f at %.1f,%.1f,%.1f; tune fov %.0f/%.0f angle %.0f/%.0f offset %.0f/%.0f "
                  "change %.0f up %.1f |",
                  why, at, MEM32(0x00349760u + 0x2D820u), MEM8(0x003B907Eu), MEM32(0x003B9054u), MEM32(0x003B8598u),
                  MEM32(CAM_DIRECTOR), (int)MEM32(CAM_DIRECTOR + 0x84u), (double)MEMF(CAM_DIRECTOR + 0x34u),
                  (double)MEMF(CAM_DIRECTOR + 0x38u),
                  (double)MEMF(CAM_DIRECTOR + 4u), (double)MEMF(CAM_DIRECTOR + 8u), (double)MEMF(CAM_DIRECTOR + 0xCu),
                  (double)MEMF(CAM_DIRECTOR + 0x10u), (double)MEMF(CAM_DIRECTOR + 0x14u), (double)MEMF(CAM_DIRECTOR + 0x18u),
                  (double)MEMF(0x0039C168u), (double)MEMF(0x0039C18Cu), (double)MEMF(0x0039C17Cu),
                  (double)MEMF(0x0039C180u), (double)MEMF(0x0039C188u), (double)MEMF(0x0039C184u),
                  (double)MEMF(0x0039C178u), (double)MEMF(0x0039C16Cu));
    for (i = 0; i < 4 && n > 0 && n < (int)sizeof line; i++) {
        uint32_t s = CAM_SLOTS + (uint32_t)i * CAM_STRIDE, f = ACTORS + (uint32_t)i * STRIDE;
        if (MEM32(f + 0x28u) == 0xFFFFFFFFu) continue;      /* no fighter in this slot */
        n += snprintf(line + n, sizeof line - (size_t)n, " %d: %s%s at %.1f,%.1f,%.1f (framed %.1f,%.1f,%.1f) flags %04X;",
                      i, MEM32(s) ? "framed" : "not framed", (MEM8(s + 0xC0u) & 8u) ? " (out)" : "",
                      (double)MEMF(f + 0x44u), (double)MEMF(f + 0x48u), (double)MEMF(f + 0x4Cu),
                      (double)MEMF(s + 0x40u), (double)MEMF(s + 0x44u), (double)MEMF(s + 0x48u), MEM16(f + 0x932u));
    }
    fprintf(stderr, "%s\n", line);
}

/* Tests: every RECOMP_CAMERA_WATCH=<steps> fight steps, and at the first. */
void defjam_camera_watch(long long step)
{
    static int period = -1;
    if (period < 0) period = getenv("RECOMP_CAMERA_WATCH") ? atoi(getenv("RECOMP_CAMERA_WATCH")) : 0;
    if (period <= 0 || step <= 0 || (step != 1 && step % period)) return;
    camera_line("step", step);
}

/* A picture report (F9): the camera as the picture was taken. Outside a fight the values are the
 * last fight's, which the logic state says. */
void defjam_camera_report(void)
{
    camera_line("picture report", -1);
}
