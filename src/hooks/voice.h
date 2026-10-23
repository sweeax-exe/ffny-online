/* Voice chat in online rooms: everyone in the room hears everyone, in the room and in the fight.
 * Through the relay as DATA (netplay.c forwards P_VOICE packets here), 16 kHz mono IMA ADPCM,
 * 40 ms a packet, only while a player talks. Push-to-talk (a key), open microphone with voice
 * detection, or off: the "online" settings voice, voice_key, voice_volume. Windows only for now. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P_VOICE 7           /* netplay DATA payload type: u16 sequence, i16 predictor, u8 index, 320 bytes */

/* A voice packet from the room member in relay slot `slot` (the netplay thread). */
void voice_on_packet(int slot, const uint8_t *p, int len);

/* A line for the screen: who is talking, and whether this PC sends; 0 when there is nothing to show. */
int voice_overlay(char *out, int size);

/* The microphone's level after the gains, 0..1, for the settings' meter; -1 while it is closed. */
float voice_mic_meter(void);

/* One room member's volume in percent (0 mutes them, up to 200), by relay slot; for this room only. */
int voice_player_volume(int slot);
void voice_set_player_volume(int slot, int percent);

#ifdef __cplusplus
}
#endif
