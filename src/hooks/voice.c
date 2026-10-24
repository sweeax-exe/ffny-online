/*
 * Voice chat in online rooms (voice.h).
 *
 * The relay already forwards DATA from a room member to every other member, in any phase, so a
 * voice packet is one more DATA payload type and the server needs nothing new. 16 kHz mono,
 * 40 ms (640 samples) a packet, IMA ADPCM (4 bits a sample: 320 bytes, about 8 KB/s while a
 * player talks, nothing while they don't); no codec library to ship.
 *
 * A thread of its own polls every 10 ms. While this PC is in a room it keeps the default speakers
 * fed (waveOut, 20 ms blocks, at most 80 ms queued) with the talkers mixed from a ring each; it
 * opens the default microphone (waveIn, 40 ms blocks) at the first push-to-talk press (open
 * microphone: at once), sends the frames it should, and closes both when the room is left.
 * Packets arrive on the netplay thread and go into the slot's ring; a ring more than 240 ms
 * behind drops its oldest audio, so a late burst cannot build up delay. winmm, not SDL: the
 * game's SDL is built without audio, and winmm needs nothing more on the players' PCs.
 * RECOMP_VOICE_TEST_TONE=1 (tests) sends a tone in place of the microphone, all the time.
 */
#include "voice.h"

#if defined(_WIN32)
#include "netplay.h"
#include "recomp_settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <mmsystem.h>
#include "input_host.h"
#pragma comment(lib, "winmm.lib")

#define RATE        16000
#define FRAME       640                 /* samples: 40 ms */
#define FRAME_BYTES (FRAME * 2)
#define SLOTS       4                   /* NETPLAY_MAX_PLAYERS */
#define TALK_MS     300                 /* "talking" this long after a packet */
#define OPEN_HANG   12                  /* open microphone: frames sent after the voice stops (480 ms) */

/* ---- IMA ADPCM ------------------------------------------------------------------------------ */

static const int k_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767 };
static const int k_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { int pred, index; } Adpcm;

static int adpcm_nibble(Adpcm *s, int sample)
{
    int step = k_step[s->index], diff = sample - s->pred, code = 0, delta = step >> 3;
    if (diff < 0) { code = 8; diff = -diff; }
    if (diff >= step) { code |= 4; diff -= step; delta += step; }
    if (diff >= step >> 1) { code |= 2; diff -= step >> 1; delta += step >> 1; }
    if (diff >= step >> 2) { code |= 1; delta += step >> 2; }
    s->pred += code & 8 ? -delta : delta;
    if (s->pred > 32767) s->pred = 32767;
    if (s->pred < -32768) s->pred = -32768;
    s->index += k_index[code];
    if (s->index < 0) s->index = 0;
    if (s->index > 88) s->index = 88;
    return code;
}

static int adpcm_sample(Adpcm *s, int code)
{
    int step = k_step[s->index], delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    s->pred += code & 8 ? -delta : delta;
    if (s->pred > 32767) s->pred = 32767;
    if (s->pred < -32768) s->pred = -32768;
    s->index += k_index[code];
    if (s->index < 0) s->index = 0;
    if (s->index > 88) s->index = 88;
    return s->pred;
}

/* ---- state ---------------------------------------------------------------------------------- */

#define MIX         320                 /* output blocks of 20 ms */
#define OUT_BUFS    4                   /* at most 80 ms queued for the speakers */
#define IN_BUFS     4
#define RING        RATE                /* 1 s of each talker */
#define MAX_BEHIND  (RATE * 240 / 1000) /* more than this queued: drop the oldest */
#define KEEP        (RATE * 80 / 1000)

static SRWLOCK s_lock = SRWLOCK_INIT;           /* the talkers' rings, against the netplay thread */
static int16_t s_ring[SLOTS][RING];
static unsigned s_rd[SLOTS], s_wr[SLOTS];       /* samples read and written, counted on (mod RING) */
static HWAVEOUT s_out;
static WAVEHDR s_ohdr[OUT_BUFS];
static int16_t s_obuf[OUT_BUFS][MIX];
static HWAVEIN s_in;
static WAVEHDR s_ihdr[IN_BUFS];
static int16_t s_ibuf[IN_BUFS][FRAME];
static int s_in_next;
static volatile LONG s_open;                    /* playback is up: packets are played */
static ULONGLONG s_heard[SLOTS];                /* when a packet from the slot last came */
static uint16_t s_seq_in[SLOTS];
static int s_seq_seen[SLOTS];
static volatile LONG s_sending;                 /* this PC sends right now */
static int s_player_vol[SLOTS] = { 100, 100, 100, 100 };    /* per room slot, for this room only */
static int s_mic_failed, s_out_failed;
static INIT_ONCE s_once = INIT_ONCE_STATIC_INIT;
static const WAVEFORMATEX k_format = { WAVE_FORMAT_PCM, 1, RATE, RATE * 2, 2, 16, 0 };

/* "T", "V", "F5", "Mouse4", "Tab", "CapsLock", "LAlt" ...: the key as a Windows virtual-key code, 0 if unknown */
static int key_code(const char *name)
{
    static const struct { const char *name; int vk; } named[] = {
        { "tab", VK_TAB }, { "capslock", VK_CAPITAL }, { "space", VK_SPACE }, { "lalt", VK_LMENU },
        { "ralt", VK_RMENU }, { "alt", VK_MENU }, { "lshift", VK_LSHIFT }, { "rshift", VK_RSHIFT },
        { "lctrl", VK_LCONTROL }, { "rctrl", VK_RCONTROL }, { "mouse3", VK_MBUTTON }, { "mouse4", VK_XBUTTON1 },
        { "mouse5", VK_XBUTTON2 }, { "insert", VK_INSERT }, { "home", VK_HOME }, { "end", VK_END },
        { "pageup", VK_PRIOR }, { "pagedown", VK_NEXT }, { "delete", VK_DELETE },
    };
    size_t i;
    if (!name || !*name) return 0;
    if (!name[1] && isalnum((unsigned char)name[0])) return toupper((unsigned char)name[0]);
    if ((name[0] == 'F' || name[0] == 'f') && isdigit((unsigned char)name[1])) {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    for (i = 0; i < sizeof(named) / sizeof(named[0]); i++)
        if (!_stricmp(name, named[i].name)) return named[i].vk;
    return 0;
}

/* the key is down and the game's window has the focus */
static int key_held(int vk)
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (!vk || !fg) return 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId() && (GetAsyncKeyState(vk) & 0x8000) != 0;
}

/* the controller's push-to-talk button (online.voice_pad) is held on this PC's pads (a player's wish:
 * "map a ptt button on the controller", 2026-10-23); 0 when it is off */
static int pad_held(void)
{
    static const WORD masks[] = { 0, 0x0020 /* back */, 0x0040 /* left stick */, 0x0080 /* right stick */,
                                  0x0001 /* d-pad up */, 0x0002 /* d-pad down */ };
    int which = recomp_settings_get("online", "voice_pad", 0);
    XBOX_INPUT_STATE s;
    if (which <= 0 || which >= (int)(sizeof(masks) / sizeof(masks[0]))) return 0;
    memset(&s, 0, sizeof(s));
    return xbox_HostInputGetLocalState(&s) == ERROR_SUCCESS && (s.Gamepad.wButtons & masks[which]) != 0;
}

/* ---- the speakers: open while in a room, fed 20 ms at a time ---------------------------------- */

static void close_all(void)
{
    int i;
    InterlockedExchange(&s_open, 0);
    if (s_out) {
        waveOutReset(s_out);
        for (i = 0; i < OUT_BUFS; i++) waveOutUnprepareHeader(s_out, &s_ohdr[i], sizeof(WAVEHDR));
        waveOutClose(s_out);
        s_out = NULL;
    }
    if (s_in) {
        waveInReset(s_in);
        for (i = 0; i < IN_BUFS; i++) waveInUnprepareHeader(s_in, &s_ihdr[i], sizeof(WAVEHDR));
        waveInClose(s_in);
        s_in = NULL;
    }
    InterlockedExchange(&s_sending, 0);
    AcquireSRWLockExclusive(&s_lock);
    memset(s_rd, 0, sizeof(s_rd));
    memset(s_wr, 0, sizeof(s_wr));
    memset(s_seq_seen, 0, sizeof(s_seq_seen));
    for (i = 0; i < SLOTS; i++) s_player_vol[i] = 100;      /* a new room starts with everyone at 100 % */
    ReleaseSRWLockExclusive(&s_lock);
}

static int open_playback(void)
{
    int i;
    MMRESULT r;
    if (s_out_failed) return 0;
    r = waveOutOpen(&s_out, WAVE_MAPPER, &k_format, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        s_out = NULL;
        s_out_failed = 1;
        fprintf(stderr, "[VOICE] no playback device (waveOutOpen %u)\n", (unsigned)r);
        return 0;
    }
    for (i = 0; i < OUT_BUFS; i++) {
        memset(&s_ohdr[i], 0, sizeof(WAVEHDR));
        s_ohdr[i].lpData = (LPSTR)s_obuf[i];
        s_ohdr[i].dwBufferLength = MIX * 2;
        waveOutPrepareHeader(s_out, &s_ohdr[i], sizeof(WAVEHDR));
        s_ohdr[i].dwFlags |= WHDR_DONE;         /* free to fill */
    }
    InterlockedExchange(&s_open, 1);
    fprintf(stderr, "[VOICE] in a room: voice chat on\n");
    return 1;
}

/* every free output block: the talkers' next 20 ms, mixed (silence when nobody talks) */
static void feed_speakers(void)
{
    float gain = (float)recomp_settings_get("online", "voice_volume", 100) / 100.0f;
    int b, i, k;
    for (b = 0; b < OUT_BUFS; b++) {
        float mix[MIX];
        if (!(s_ohdr[b].dwFlags & WHDR_DONE)) continue;
        memset(mix, 0, sizeof(mix));
        AcquireSRWLockExclusive(&s_lock);
        for (k = 0; k < SLOTS; k++) {
            unsigned have = s_wr[k] - s_rd[k], n = have < MIX ? have : MIX;
            float pv = (float)s_player_vol[k] / 100.0f;
            for (i = 0; i < (int)n; i++) mix[i] += s_ring[k][(s_rd[k] + (unsigned)i) % RING] * pv;
            s_rd[k] += n;
        }
        ReleaseSRWLockExclusive(&s_lock);
        for (i = 0; i < MIX; i++) {
            float v = mix[i] * gain;
            s_obuf[b][i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
        }
        s_ohdr[b].dwFlags &= ~WHDR_DONE;
        waveOutWrite(s_out, &s_ohdr[b], sizeof(WAVEHDR));
    }
}

static int open_mic(void)
{
    int i;
    MMRESULT r;
    if (s_in) return 1;
    if (s_mic_failed) return 0;
    r = waveInOpen(&s_in, WAVE_MAPPER, &k_format, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR) {
        s_in = NULL;
        s_mic_failed = 1;       /* once a session: no microphone, or Windows denies it (privacy settings) */
        fprintf(stderr, "[VOICE] no microphone (waveInOpen %u)\n", (unsigned)r);
        return 0;
    }
    for (i = 0; i < IN_BUFS; i++) {
        memset(&s_ihdr[i], 0, sizeof(WAVEHDR));
        s_ihdr[i].lpData = (LPSTR)s_ibuf[i];
        s_ihdr[i].dwBufferLength = FRAME_BYTES;
        waveInPrepareHeader(s_in, &s_ihdr[i], sizeof(WAVEHDR));
        waveInAddBuffer(s_in, &s_ihdr[i], sizeof(WAVEHDR));
    }
    s_in_next = 0;
    waveInStart(s_in);
    fprintf(stderr, "[VOICE] microphone open\n");
    return 1;
}

/* the next recorded 40 ms, or 0 while none is ready */
static int read_mic(int16_t *pcm)
{
    WAVEHDR *h = &s_ihdr[s_in_next];
    int ok;
    if (!s_in || !(h->dwFlags & WHDR_DONE)) return 0;
    ok = h->dwBytesRecorded == FRAME_BYTES;
    if (ok) memcpy(pcm, s_ibuf[s_in_next], FRAME_BYTES);
    h->dwFlags &= ~WHDR_DONE;
    h->dwBytesRecorded = 0;
    waveInAddBuffer(s_in, h, sizeof(WAVEHDR));
    s_in_next = (s_in_next + 1) % IN_BUFS;
    return ok ? 1 : -1;
}

/* ---- receiving ------------------------------------------------------------------------------ */

void voice_on_packet(int slot, const uint8_t *p, int len)
{
    int16_t pcm[FRAME];
    Adpcm st;
    uint16_t seq;
    unsigned i;
    if (slot < 0 || slot >= SLOTS || len != 6 + FRAME / 2 || p[0] != P_VOICE || !s_open) return;
    seq = (uint16_t)(p[1] | p[2] << 8);
    if (s_seq_seen[slot] && (int16_t)(seq - s_seq_in[slot]) <= 0) return;      /* old or repeated */
    s_seq_in[slot] = seq;
    s_seq_seen[slot] = 1;
    st.pred = (int16_t)(p[3] | p[4] << 8);
    st.index = p[5] > 88 ? 88 : p[5];
    for (i = 0; i < FRAME / 2; i++) {
        pcm[2 * i] = (int16_t)adpcm_sample(&st, p[6 + i] & 15);
        pcm[2 * i + 1] = (int16_t)adpcm_sample(&st, p[6 + i] >> 4);
    }
    {   /* how much comes in, for the log (and the tests) */
        static unsigned got[SLOTS];
        if ((++got[slot] & (got[slot] - 1)) == 0 || got[slot] % 250 == 0)
            fprintf(stderr, "[VOICE] %u packet(s) from room slot %d (sequence %u)\n", got[slot], slot, seq);
    }
    s_heard[slot] = GetTickCount64();
    AcquireSRWLockExclusive(&s_lock);
    if (s_wr[slot] - s_rd[slot] + FRAME > MAX_BEHIND) s_rd[slot] = s_wr[slot] - KEEP;   /* a late burst: catch up */
    for (i = 0; i < FRAME; i++) s_ring[slot][(s_wr[slot] + i) % RING] = pcm[i];
    s_wr[slot] += FRAME;
    ReleaseSRWLockExclusive(&s_lock);
}

/* ---- sending -------------------------------------------------------------------------------- */

static void send_frame(const int16_t *pcm, uint16_t seq, Adpcm *st)
{
    uint8_t b[6 + FRAME / 2];
    int i;
    b[0] = P_VOICE;
    b[1] = (uint8_t)seq;
    b[2] = (uint8_t)(seq >> 8);
    b[3] = (uint8_t)st->pred;
    b[4] = (uint8_t)(st->pred >> 8);
    b[5] = (uint8_t)st->index;
    for (i = 0; i < FRAME / 2; i++) {
        int lo = adpcm_nibble(st, pcm[2 * i]), hi = adpcm_nibble(st, pcm[2 * i + 1]);
        b[6 + i] = (uint8_t)(lo | hi << 4);
    }
    netplay_send_voice(b, (int)sizeof(b));
}

/* Level before sending. Headset microphones record quietly ("my voice is too quiet", the first test
 * with a real microphone), so with voice_auto_level a frame is brought towards a common loudness,
 * up to 6x, the gain following slowly (quiet frames, breath and room noise, never raise it); then
 * the player's own voice_mic_volume. Every frame read sets the level meter the settings show. */
#define AGC_TARGET 4500.0f
#define AGC_MAX    6.0f
static volatile LONG s_meter;                   /* the microphone's level after the gain, 0..1000 */

static void level(int16_t *pcm, float *gain)
{
    double sum = 0, out = 0;
    float rms, want, g;
    int i;
    for (i = 0; i < FRAME; i++) sum += (double)pcm[i] * pcm[i];
    rms = (float)sqrt(sum / FRAME);
    if (!recomp_settings_get("online", "voice_auto_level", 1)) {
        *gain = 1.0f;
    } else if (rms > 250.0f) {
        want = AGC_TARGET / rms;
        if (want > AGC_MAX) want = AGC_MAX;
        if (want < 1.0f) want = 1.0f;
        *gain += (want - *gain) * (want < *gain ? 0.5f : 0.15f);   /* down fast, up slowly */
    }
    g = *gain * (float)recomp_settings_get("online", "voice_mic_volume", 100) / 100.0f;
    for (i = 0; i < FRAME; i++) {
        float v = pcm[i] * g;
        pcm[i] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
        out += (double)pcm[i] * pcm[i];
    }
    out = sqrt(out / FRAME) / 12000.0;          /* a loud voice fills most of the meter */
    InterlockedExchange(&s_meter, (LONG)(out > 1.0 ? 1000 : out * 1000));
}

float voice_mic_meter(void) { return s_in ? (float)s_meter / 1000.0f : -1.0f; }

int voice_player_volume(int slot) { return slot >= 0 && slot < SLOTS ? s_player_vol[slot] : 100; }

void voice_set_player_volume(int slot, int percent)
{
    if (slot < 0 || slot >= SLOTS) return;
    s_player_vol[slot] = percent < 0 ? 0 : percent > 200 ? 200 : percent;
}

static DWORD WINAPI voice_thread(LPVOID unused)
{
    int16_t pcm[FRAME];
    uint16_t seq = 0;
    Adpcm enc = { 0, 0 };
    int hang = 0, test = getenv("RECOMP_VOICE_TEST_TONE") != NULL;
    float agc = 1.0f;
    double phase = 0;
    ULONGLONG next_tone = 0;
    (void)unused;
    {   /* named for crash reports (main.c prints the thread's name) */
        typedef HRESULT (WINAPI *SetThreadDescriptionFn)(HANDLE, PCWSTR);
        SetThreadDescriptionFn name = (SetThreadDescriptionFn)(void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                                                                    "SetThreadDescription");
        if (name) name(GetCurrentThread(), L"voice chat");
    }
    for (;;) {
        int mode = recomp_settings_get("online", "voice", 1);     /* 0 off, 1 push to talk, 2 open */
        int want = mode != 0 && netplay_voice_room();
        char keyname[32];
        int vk, i, got;
        Sleep(10);
        if (!want) {
            if (s_open || s_in) close_all();
            continue;
        }
        if (!s_open && !open_playback()) continue;
        feed_speakers();

        if (test) {                     /* a 440 Hz tone, 40 ms at a time, as if someone talked all along */
            ULONGLONG now = GetTickCount64();
            if (now < next_tone) continue;
            next_tone = next_tone ? next_tone + 40 : now + 40;
            for (i = 0; i < FRAME; i++) { pcm[i] = (int16_t)(6000 * sin(phase)); phase += 2 * 3.14159265 * 440 / RATE; }
            InterlockedExchange(&s_sending, 1);
            send_frame(pcm, ++seq, &enc);
            continue;
        }
        recomp_settings_get_text("online", "voice_key", keyname, sizeof(keyname), "T");
        vk = key_code(keyname);
        if (mode == 1 && !s_in && (key_held(vk) || pad_held())) open_mic();   /* the microphone opens at the first press */
        if (mode == 2) open_mic();
        if (!s_in) { InterlockedExchange(&s_sending, 0); continue; }
        while ((got = read_mic(pcm)) != 0) {
            int send;
            if (got < 0) continue;
            if (mode == 1) {
                send = key_held(vk) || pad_held();
            } else {                    /* open: send while the voice is above the room noise, and a moment after */
                double sum = 0;
                for (i = 0; i < FRAME; i++) sum += (double)pcm[i] * pcm[i];
                if (sqrt(sum / FRAME) > 400.0) hang = OPEN_HANG;     /* before the level control: quiet microphones too */
                send = hang > 0;
                if (hang > 0) hang--;
            }
            level(pcm, &agc);           /* every frame: the meter shows what would go out */
            if (send) {
                if (!s_sending) enc.pred = 0, enc.index = 0;    /* a fresh start for each burst */
                send_frame(pcm, ++seq, &enc);
            }
            InterlockedExchange(&s_sending, send);
        }
    }
}

static BOOL CALLBACK start_once(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
    HANDLE t = CreateThread(NULL, 0, voice_thread, NULL, 0, NULL);
    (void)once; (void)param; (void)ctx;
    if (t) CloseHandle(t);
    return TRUE;
}

int voice_overlay(char *out, int size)
{
    ULONGLONG now = GetTickCount64();
    int i, n = 0, used = 0;
    InitOnceExecuteOnce(&s_once, start_once, NULL, NULL);
    if (!s_open || size <= 0) return 0;
    out[0] = 0;
    if (s_sending) used += snprintf(out + used, (size_t)(size - used), "MIC ON");
    for (i = 0; i < SLOTS && used < size - 1; i++) {
        char name[25];
        if (!s_heard[i] || now - s_heard[i] > TALK_MS) continue;
        if (!netplay_room_name(i, name, sizeof(name))) snprintf(name, sizeof(name), "Player %d", i + 1);
        used += snprintf(out + used, (size_t)(size - used), "%s%s", used ? (n ? ", " : "   Talking: ") : "Talking: ", name);
        n++;
    }
    return used > 0;
}

#else

void voice_on_packet(int slot, const uint8_t *p, int len) { (void)slot; (void)p; (void)len; }
int voice_overlay(char *out, int size) { (void)out; (void)size; return 0; }
float voice_mic_meter(void) { return -1.0f; }
int voice_player_volume(int slot) { (void)slot; return 100; }
void voice_set_player_volume(int slot, int percent) { (void)slot; (void)percent; }

#endif
