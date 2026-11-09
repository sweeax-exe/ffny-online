/* The vendored audio decoders (thirdparty/audio-decoders, unmodified upstream files), compiled
 * once for src/hooks/song_convert.c. */
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#include "../../thirdparty/audio-decoders/dr_mp3.h"
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#include "../../thirdparty/audio-decoders/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "../../thirdparty/audio-decoders/dr_wav.h"
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "../../thirdparty/audio-decoders/stb_vorbis.c"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
