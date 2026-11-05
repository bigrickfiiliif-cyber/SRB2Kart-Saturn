// SRB2Kart Saturn - Nintendo 3DS port
// Sound + music: a small software mixer feeding one NDSP stereo channel.
//
//  * SFX: Doom/DMX lumps, RIFF WAV and Ogg Vorbis, kept as mono s16 at their
//    native rate and resampled while mixing.
//  * Music: Ogg Vorbis through Tremor (integer decoder, fast on ARM11) with
//    SRB2's LOOPPOINT / LOOPMS / LENGTHMS tags, and MOD/XM/S3M/IT through
//    libxmp. MIDI and GME formats are not supported on this platform.
//
// Mixing runs on its own thread (New 3DS core 2 when available) inside
// N3DS_AudioLock(); everything the game thread touches that the mixer also
// reads is changed under the same lock.

#include <stdlib.h>
#include <string.h>
#include <tremor/ivorbisfile.h>
#include <xmp.h>

#include "../doomdef.h"
#include "../doomstat.h"
#include "../i_sound.h"
#include "../i_system.h"
#include "../s_sound.h"
#include "../sounds.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../byteptr.h"
#include "../m_argv.h"

#include "n3ds_hw.h"

#define OUTRATE   N3DS_AUDIO_RATE
#define NUMVOICES 32

UINT8 sound_started = false;

// ============================================================ SFX decode ====

typedef struct
{
	INT16  *data;   // mono
	UINT32  len;    // samples
	UINT32  step;   // 16.16 source samples per output sample
} n3ds_sfx_t;

static n3ds_sfx_t *new_sfx(UINT32 samples, UINT32 rate)
{
	n3ds_sfx_t *s = Z_Malloc(sizeof *s + samples * sizeof(INT16), PU_SOUND, NULL);
	s->data = (INT16 *)(s + 1);
	s->len = samples;
	s->step = (UINT32)(((UINT64)rate << 16) / OUTRATE);
	return s;
}

static n3ds_sfx_t *decode_dmx(const UINT8 *lump, size_t len)
{
	const UINT8 *p = lump;
	UINT16 ver, rate;
	UINT32 samples, i;
	n3ds_sfx_t *s;

	if (len < 8)
		return NULL;

	ver = READUINT16(p);
	if (ver != 3)
		return NULL;
	rate = READUINT16(p);
	samples = READUINT32(p);
	if (!rate || samples > len - 8)
		return NULL;

	s = new_sfx(samples, rate);
	for (i = 0; i < samples; i++)
		s->data[i] = (INT16)(((INT16)p[i] - 128) << 8);
	return s;
}

static n3ds_sfx_t *decode_wav(const UINT8 *lump, size_t len)
{
	const UINT8 *p = lump + 12, *end = lump + len;
	UINT16 fmt = 0, chans = 0, bits = 0;
	UINT32 rate = 0;
	const UINT8 *data = NULL;
	UINT32 datalen = 0, frames, i;
	n3ds_sfx_t *s;

	if (len < 44 || memcmp(lump, "RIFF", 4) || memcmp(lump + 8, "WAVE", 4))
		return NULL;

	while (p + 8 <= end)
	{
		UINT32 ck = p[4] | (p[5] << 8) | (p[6] << 16) | ((UINT32)p[7] << 24);
		const UINT8 *body = p + 8;
		if (body + ck > end)
			ck = (UINT32)(end - body);

		if (!memcmp(p, "fmt ", 4) && ck >= 16)
		{
			fmt   = body[0] | (body[1] << 8);
			chans = body[2] | (body[3] << 8);
			rate  = body[4] | (body[5] << 8) | (body[6] << 16) | ((UINT32)body[7] << 24);
			bits  = body[14] | (body[15] << 8);
		}
		else if (!memcmp(p, "data", 4))
		{
			data = body;
			datalen = ck;
		}
		p = body + ck + (ck & 1);
	}

	if (fmt != 1 || !data || !rate || !chans || (bits != 8 && bits != 16))
		return NULL;

	frames = datalen / (chans * (bits / 8));
	s = new_sfx(frames, rate);

	for (i = 0; i < frames; i++)
	{
		INT32 acc = 0;
		UINT16 c;
		for (c = 0; c < chans; c++)
		{
			if (bits == 8)
				acc += ((INT32)data[(i * chans + c)] - 128) << 8;
			else
			{
				const UINT8 *q = data + (i * chans + c) * 2;
				acc += (INT16)(q[0] | (q[1] << 8));
			}
		}
		s->data[i] = (INT16)(acc / chans);
	}
	return s;
}

// --- memory-backed Ogg reader (shared by SFX and music) ---

typedef struct
{
	const UINT8 *data;
	size_t size, pos;
} memfile_t;

static size_t mem_read(void *ptr, size_t size, size_t nmemb, void *src)
{
	memfile_t *m = (memfile_t *)src;
	size_t want = size * nmemb, left = m->size - m->pos;
	if (want > left)
		want = left;
	memcpy(ptr, m->data + m->pos, want);
	m->pos += want;
	return size ? want / size : 0;
}

static int mem_seek(void *src, ogg_int64_t off, int whence)
{
	memfile_t *m = (memfile_t *)src;
	ogg_int64_t np;
	switch (whence)
	{
		case SEEK_SET: np = off; break;
		case SEEK_CUR: np = (ogg_int64_t)m->pos + off; break;
		case SEEK_END: np = (ogg_int64_t)m->size + off; break;
		default: return -1;
	}
	if (np < 0 || np > (ogg_int64_t)m->size)
		return -1;
	m->pos = (size_t)np;
	return 0;
}

static int mem_close(void *src)
{
	(void)src;
	return 0;
}

static long mem_tell(void *src)
{
	return (long)((memfile_t *)src)->pos;
}

static const ov_callbacks mem_callbacks = { mem_read, mem_seek, mem_close, mem_tell };

static n3ds_sfx_t *decode_ogg(const UINT8 *lump, size_t len)
{
	memfile_t mf = { lump, len, 0 };
	OggVorbis_File vf;
	vorbis_info *vi;
	ogg_int64_t total;
	n3ds_sfx_t *s;
	INT16 buf[2048];
	UINT32 out = 0;
	int bs, chans;

	if (len < 4 || memcmp(lump, "OggS", 4))
		return NULL;
	if (ov_open_callbacks(&mf, &vf, NULL, 0, mem_callbacks) < 0)
		return NULL;

	vi = ov_info(&vf, -1);
	total = ov_pcm_total(&vf, -1);
	if (!vi || total <= 0 || total > 30 * 48000)
	{
		ov_clear(&vf);
		return NULL;
	}

	chans = vi->channels;
	s = new_sfx((UINT32)total, (UINT32)vi->rate);

	for (;;)
	{
		long got = ov_read(&vf, (char *)buf, sizeof buf, &bs);
		long frames, i;
		if (got <= 0)
			break;
		frames = got / (2 * chans);
		for (i = 0; i < frames && out < s->len; i++)
		{
			INT32 acc = buf[i * chans];
			if (chans > 1)
				acc = (acc + buf[i * chans + 1]) / 2;
			s->data[out++] = (INT16)acc;
		}
	}
	s->len = out;
	ov_clear(&vf);
	return s;
}

// ================================================================ voices ====

typedef struct
{
	n3ds_sfx_t *sfx;
	UINT32 pos;    // integer sample position
	UINT32 frac;   // 16-bit fraction
	INT32  lvol, rvol; // 0..256
} voice_t;

static voice_t voices[NUMVOICES];
static UINT8 sfx_volume = 31;

static void calc_gains(voice_t *v, UINT8 vol, UINT8 sep)
{
	// vol 0..255, sep 0..255 (128 = centre), sfx_volume 0..31
	INT32 base = ((INT32)vol + 1) * sfx_volume / 31; // 0..256
	INT32 l = (255 - sep) * 2, r = sep * 2;
	if (l > 255) l = 255;
	if (r > 255) r = 255;
	v->lvol = base * l / 255;
	v->rvol = base * r / 255;
}

// ================================================================= music ====

typedef enum { MUSIC_NONE, MUSIC_OGG, MUSIC_XMP } musicbackend_t;

static musicbackend_t music_backend = MUSIC_NONE;
static memfile_t      music_mem;
static OggVorbis_File music_vf;
static xmp_context    music_xmp = NULL;

static boolean music_playing = false;
static boolean music_paused  = false;
static boolean music_looping = false;
static UINT32  music_rate = 44100;
static int     music_chans = 2;
static ogg_int64_t music_loop_sample = 0; // in source samples
static UINT32  music_length_ms = 0;

// resampler state (OGG path)
static INT16  music_buf[4096 * 2];
static int    music_buf_frames = 0;
static int    music_buf_pos = 0;
static UINT32 music_frac = 0;
static UINT32 music_step = 0;
static INT16  music_prev[2] = {0, 0};

static UINT8   music_volume = 18;     // 0..31
static UINT8   internal_volume = 100; // 0..100, used for fades
static INT32   music_gain = 0;        // 0..256, computed

// fades (driven from I_UpdateSound on the game thread)
static boolean   is_fading = false;
static UINT8     fading_source = 0, fading_target = 0;
static precise_t fading_start = 0;
static UINT32    fading_ms = 0;
static void    (*fading_callback)(void) = NULL;

static void update_music_gain(void)
{
	music_gain = (INT32)music_volume * (INT32)internal_volume * 256 / (31 * 100);
}

// Refills music_buf from the decoder. Returns frames available.
static int refill_ogg(void)
{
	int bs, tries = 0;

	music_buf_pos = 0;
	music_buf_frames = 0;

	while (tries++ < 4)
	{
		long got = ov_read(&music_vf, (char *)music_buf, sizeof music_buf, &bs);
		if (got > 0)
		{
			music_buf_frames = (int)(got / (2 * music_chans));
			return music_buf_frames;
		}
		if (got == OV_HOLE)
			continue;

		// end of stream (or error): loop or stop
		if (music_looping && ov_pcm_seek(&music_vf, music_loop_sample) == 0)
			continue;

		music_playing = false;
		return 0;
	}
	return 0;
}

static void mix_music_ogg(INT32 *acc, int frames)
{
	int i;
	const INT32 gain = music_gain;

	for (i = 0; i < frames; i++)
	{
		INT32 l0, r0, l1, r1, f;
		INT16 *cur;

		while (music_frac >= 65536)
		{
			music_frac -= 65536;
			if (music_buf_pos >= music_buf_frames && !refill_ogg())
				return;
			cur = &music_buf[music_buf_pos * music_chans];
			music_prev[0] = cur[0];
			music_prev[1] = (music_chans > 1) ? cur[1] : cur[0];
			music_buf_pos++;
		}

		if (music_buf_pos >= music_buf_frames && !refill_ogg())
			return;
		cur = &music_buf[music_buf_pos * music_chans];

		// linear interpolation between previous and current frame
		f = (INT32)(music_frac >> 1); // 0..32767
		l0 = music_prev[0]; r0 = music_prev[1];
		l1 = cur[0];        r1 = (music_chans > 1) ? cur[1] : cur[0];

		acc[i*2]   += (((l0 + (((l1 - l0) * f) >> 15))) * gain) >> 8;
		acc[i*2+1] += (((r0 + (((r1 - r0) * f) >> 15))) * gain) >> 8;

		music_frac += music_step;
	}
}

static void mix_music_xmp(INT32 *acc, int frames)
{
	INT16 tmp[1024 * 2];
	const INT32 gain = music_gain;

	while (frames > 0)
	{
		int n = frames > 1024 ? 1024 : frames, i;
		if (xmp_play_buffer(music_xmp, tmp, n * 4, music_looping ? 0 : 1) != 0)
		{
			music_playing = false;
			return;
		}
		for (i = 0; i < n; i++)
		{
			acc[i*2]   += (tmp[i*2]   * gain) >> 8;
			acc[i*2+1] += (tmp[i*2+1] * gain) >> 8;
		}
		acc += n * 2;
		frames -= n;
	}
}

// ================================================================= mixer ====

static void mixer_callback(int16_t *out, int frames, void *user)
{
	static INT32 acc[2048 * 2];
	int i, v;
	(void)user;

	if (frames > 2048)
		frames = 2048;
	memset(acc, 0, frames * 2 * sizeof(INT32));

	if (music_playing && !music_paused && music_gain > 0)
	{
		if (music_backend == MUSIC_OGG)
			mix_music_ogg(acc, frames);
		else if (music_backend == MUSIC_XMP)
			mix_music_xmp(acc, frames);
	}

	for (v = 0; v < NUMVOICES; v++)
	{
		voice_t *vc = &voices[v];
		n3ds_sfx_t *s = vc->sfx;
		UINT32 pos, frac, step, len;
		INT32 lv, rv;
		const INT16 *d;

		if (!s)
			continue;

		pos = vc->pos; frac = vc->frac; step = s->step; len = s->len;
		lv = vc->lvol; rv = vc->rvol; d = s->data;

		for (i = 0; i < frames; i++)
		{
			INT32 smp;
			if (pos >= len)
			{
				vc->sfx = NULL;
				break;
			}
			smp = d[pos];
			acc[i*2]   += (smp * lv) >> 8;
			acc[i*2+1] += (smp * rv) >> 8;
			frac += step;
			pos += frac >> 16;
			frac &= 0xFFFF;
		}
		vc->pos = pos;
		vc->frac = frac;
	}

	for (i = 0; i < frames * 2; i++)
	{
		INT32 s = acc[i];
		if (s > 32767) s = 32767;
		else if (s < -32768) s = -32768;
		out[i] = (int16_t)s;
	}
}

// ============================================================ SFX API ======

void I_StartupSound(void)
{
	if (sound_started)
		return;

	if (M_CheckParm("-nosound"))
		return;

	memset(voices, 0, sizeof voices);
	update_music_gain();

	if (N3DS_AudioInit(mixer_callback, NULL) != 0)
	{
		CONS_Alert(CONS_WARNING,
			"Audio unavailable. Make sure sdmc:/3ds/dspfirm.cdc exists\n"
			"(dump it with the DSP1 homebrew).\n");
		return;
	}

	sound_started = true;
	CONS_Printf("I_StartupSound(): NDSP %d Hz, %d voices\n", OUTRATE, NUMVOICES);
}

void I_ShutdownSound(void)
{
	if (!sound_started)
		return;
	I_UnloadSong();
	N3DS_AudioExit();
	sound_started = false;
}

void I_UpdateSound(void)
{
	if (is_fading)
	{
		UINT32 elapsed = (UINT32)((I_GetPreciseTime() - fading_start) * 1000 / I_GetPrecisePrecision());
		UINT8 vol;

		if (music_paused)
		{
			// don't advance the fade while paused
			fading_start = I_GetPreciseTime() - (precise_t)((UINT64)elapsed * I_GetPrecisePrecision() / 1000);
		}
		else if (elapsed >= fading_ms)
		{
			void (*cb)(void) = fading_callback;
			I_SetInternalMusicVolume(fading_target);
			is_fading = false;
			fading_callback = NULL;
			if (cb)
				cb();
		}
		else
		{
			INT32 delta = (INT32)fading_target - (INT32)fading_source;
			vol = (UINT8)((INT32)fading_source + delta * (INT32)elapsed / (INT32)fading_ms);
			I_SetInternalMusicVolume(vol);
		}
	}
}

void *I_GetSfx(sfxinfo_t *sfx)
{
	UINT8 *lump;
	n3ds_sfx_t *s;
	size_t len;

	if (sfx->lumpnum == LUMPERROR)
		sfx->lumpnum = S_GetSfxLumpNum(sfx);

	sfx->length = W_LumpLength(sfx->lumpnum);
	len = (size_t)sfx->length;
	lump = W_CacheLumpNum(sfx->lumpnum, PU_SOUND);

	s = decode_dmx(lump, len);
	if (!s) s = decode_wav(lump, len);
	if (!s) s = decode_ogg(lump, len);

	Z_Free(lump);
	return s;
}

void I_FreeSfx(sfxinfo_t *sfx)
{
	int v;

	if (sfx->data)
	{
		N3DS_AudioLock();
		for (v = 0; v < NUMVOICES; v++)
			if (voices[v].sfx == sfx->data)
				voices[v].sfx = NULL;
		N3DS_AudioUnlock();
		Z_Free(sfx->data);
	}
	sfx->data = NULL;
	sfx->lumpnum = LUMPERROR;
}

INT32 I_StartSound(sfxenum_t id, UINT8 vol, UINT8 sep, INT32 channel)
{
	voice_t *vc;
	n3ds_sfx_t *s = (n3ds_sfx_t *)S_sfx[id].data;

	if (!sound_started || !s)
		return -1;

	if (channel < 0 || channel >= NUMVOICES)
	{
		for (channel = 0; channel < NUMVOICES; channel++)
			if (!voices[channel].sfx)
				break;
		if (channel == NUMVOICES)
			return -1;
	}

	N3DS_AudioLock();
	vc = &voices[channel];
	vc->sfx = s;
	vc->pos = 0;
	vc->frac = 0;
	calc_gains(vc, vol, sep);
	N3DS_AudioUnlock();
	return channel;
}

void I_StopSound(INT32 handle)
{
	if (handle < 0 || handle >= NUMVOICES)
		return;
	N3DS_AudioLock();
	voices[handle].sfx = NULL;
	N3DS_AudioUnlock();
}

boolean I_SoundIsPlaying(INT32 handle)
{
	if (handle < 0 || handle >= NUMVOICES)
		return false;
	return voices[handle].sfx != NULL;
}

void I_UpdateSoundParams(INT32 handle, UINT8 vol, UINT8 sep)
{
	if (handle < 0 || handle >= NUMVOICES)
		return;
	N3DS_AudioLock();
	calc_gains(&voices[handle], vol, sep);
	N3DS_AudioUnlock();
}

void I_SetSfxVolume(UINT8 volume)
{
	sfx_volume = volume;
}

// =========================================================== music API =====

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
	I_UnloadSong();
}

musictype_t I_SongType(void)
{
	switch (music_backend)
	{
		case MUSIC_OGG: return MU_OGG;
		case MUSIC_XMP: return MU_MOD;
		default:        return MU_NONE;
	}
}

boolean I_SongPlaying(void)
{
	return music_backend != MUSIC_NONE;
}

boolean I_SongPaused(void)
{
	return music_paused;
}

boolean I_SetSongSpeed(float speed)
{
	(void)speed;
	return false;
}

UINT32 I_GetSongLength(void)
{
	if (music_backend == MUSIC_XMP)
	{
		struct xmp_frame_info fi;
		N3DS_AudioLock();
		xmp_get_frame_info(music_xmp, &fi);
		N3DS_AudioUnlock();
		return (UINT32)fi.total_time;
	}
	return music_length_ms;
}

boolean I_SetSongLoopPoint(UINT32 looppoint)
{
	UINT32 length;

	if (music_backend != MUSIC_OGG || !music_looping)
		return false;

	length = I_GetSongLength();
	if (length > 0)
		looppoint %= length;

	N3DS_AudioLock();
	music_loop_sample = (ogg_int64_t)looppoint * music_rate / 1000;
	N3DS_AudioUnlock();
	return true;
}

UINT32 I_GetSongLoopPoint(void)
{
	if (music_backend != MUSIC_OGG)
		return 0;
	return (UINT32)(music_loop_sample * 1000 / music_rate);
}

boolean I_SetSongPosition(UINT32 position)
{
	UINT32 length;

	if (music_backend == MUSIC_XMP)
	{
		N3DS_AudioLock();
		xmp_seek_time(music_xmp, (int)position);
		N3DS_AudioUnlock();
		return true;
	}
	if (music_backend != MUSIC_OGG)
		return false;

	length = I_GetSongLength();
	if (length && position >= length)
	{
		UINT32 loop = I_GetSongLoopPoint();
		position = (length > loop) ? loop + (position - length) % (length - loop) : 0;
	}

	N3DS_AudioLock();
	ov_pcm_seek(&music_vf, (ogg_int64_t)position * music_rate / 1000);
	music_buf_frames = music_buf_pos = 0;
	music_frac = 0;
	music_playing = true;
	N3DS_AudioUnlock();
	return true;
}

UINT32 I_GetSongPosition(void)
{
	ogg_int64_t pcm;

	if (music_backend == MUSIC_XMP)
	{
		struct xmp_frame_info fi;
		N3DS_AudioLock();
		xmp_get_frame_info(music_xmp, &fi);
		N3DS_AudioUnlock();
		return (UINT32)fi.time;
	}
	if (music_backend != MUSIC_OGG)
		return 0;

	N3DS_AudioLock();
	// decoder position minus what is still sitting in our buffer
	pcm = ov_pcm_tell(&music_vf) - (music_buf_frames - music_buf_pos);
	N3DS_AudioUnlock();
	if (pcm < 0)
		pcm = 0;
	return (UINT32)(pcm * 1000 / music_rate);
}

void I_UpdateSongLagThreshold(void)
{
}

// Parses LOOPPOINT= / LOOPMS= / LENGTHMS= comments.
static void read_ogg_tags(void)
{
	vorbis_comment *vc = ov_comment(&music_vf, -1);
	int i;

	music_loop_sample = 0;
	music_length_ms = 0;

	if (!vc)
		return;

	for (i = 0; i < vc->comments; i++)
	{
		const char *c = vc->user_comments[i];
		if (!strncasecmp(c, "LOOPPOINT=", 10))
			music_loop_sample = atoll(c + 10);
		else if (!strncasecmp(c, "LOOPMS=", 7))
			music_loop_sample = (ogg_int64_t)(atof(c + 7) * music_rate / 1000.0);
		else if (!strncasecmp(c, "LENGTHMS=", 9))
			music_length_ms = (UINT32)atoi(c + 9);
	}

	if (!music_length_ms)
	{
		ogg_int64_t total = ov_pcm_total(&music_vf, -1);
		if (total > 0)
			music_length_ms = (UINT32)(total * 1000 / music_rate);
	}
}

boolean I_LoadSong(char *data, size_t len)
{
	I_UnloadSong();

	if (!sound_started || !data || len < 4)
		return false;

	if (!memcmp(data, "OggS", 4))
	{
		vorbis_info *vi;

		music_mem.data = (const UINT8 *)data;
		music_mem.size = len;
		music_mem.pos = 0;

		if (ov_open_callbacks(&music_mem, &music_vf, NULL, 0, mem_callbacks) < 0)
		{
			CONS_Alert(CONS_ERROR, "I_LoadSong: not a valid Ogg Vorbis stream\n");
			return false;
		}

		vi = ov_info(&music_vf, -1);
		music_rate = vi ? (UINT32)vi->rate : 44100;
		music_chans = vi ? vi->channels : 2;
		if (music_chans < 1 || music_chans > 2)
		{
			ov_clear(&music_vf);
			CONS_Alert(CONS_ERROR, "I_LoadSong: %d-channel Ogg not supported\n", music_chans);
			return false;
		}
		music_step = (UINT32)(((UINT64)music_rate << 16) / OUTRATE);
		read_ogg_tags();

		music_backend = MUSIC_OGG;
		return true;
	}

	// Tracker modules
	music_xmp = xmp_create_context();
	if (music_xmp && xmp_load_module_from_memory(music_xmp, data, (long)len) == 0)
	{
		music_backend = MUSIC_XMP;
		return true;
	}
	if (music_xmp)
	{
		xmp_free_context(music_xmp);
		music_xmp = NULL;
	}

	if (!memcmp(data, "MThd", 4))
		CONS_Alert(CONS_WARNING, "MIDI music is not supported on 3DS\n");
	else
		CONS_Alert(CONS_WARNING, "I_LoadSong: unsupported music format\n");
	return false;
}

void I_UnloadSong(void)
{
	I_StopSong();

	N3DS_AudioLock();
	if (music_backend == MUSIC_OGG)
		ov_clear(&music_vf);
	else if (music_backend == MUSIC_XMP)
	{
		xmp_release_module(music_xmp);
		xmp_free_context(music_xmp);
		music_xmp = NULL;
	}
	music_backend = MUSIC_NONE;
	N3DS_AudioUnlock();
}

boolean I_PlaySong(boolean looping)
{
	if (music_backend == MUSIC_NONE)
		return false;

	N3DS_AudioLock();
	music_looping = looping;
	music_paused = false;

	if (music_backend == MUSIC_OGG)
	{
		ov_pcm_seek(&music_vf, 0);
		music_buf_frames = music_buf_pos = 0;
		music_frac = 0;
		music_prev[0] = music_prev[1] = 0;
	}
	else
	{
		if (xmp_start_player(music_xmp, OUTRATE, 0) != 0)
		{
			N3DS_AudioUnlock();
			return false;
		}
		xmp_set_player(music_xmp, XMP_PLAYER_INTERP, XMP_INTERP_LINEAR);
		xmp_set_player(music_xmp, XMP_PLAYER_MIX, 70); // stereo separation %
	}

	music_playing = true;
	N3DS_AudioUnlock();

	I_SetInternalMusicVolume(100);
	return true;
}

void I_StopSong(void)
{
	I_StopFadingSong();

	N3DS_AudioLock();
	if (music_playing && music_backend == MUSIC_XMP)
		xmp_end_player(music_xmp);
	music_playing = false;
	music_paused = false;
	N3DS_AudioUnlock();
}

void I_PauseSong(void)
{
	music_paused = true;
}

void I_ResumeSong(void)
{
	music_paused = false;
}

void I_SetMusicVolume(UINT8 volume)
{
	music_volume = volume;
	update_music_gain();
}

boolean I_SetSongTrack(INT32 track)
{
	(void)track;
	return false;
}

void I_SetInternalMusicVolume(UINT8 volume)
{
	internal_volume = volume > 100 ? 100 : volume;
	update_music_gain();
}

void I_StopFadingSong(void)
{
	is_fading = false;
	fading_callback = NULL;
	fading_ms = 0;
}

boolean I_FadeSongFromVolume(UINT8 target_volume, UINT8 source_volume, UINT32 ms, void (*callback)(void))
{
	if (source_volume > 100)
		source_volume = 100;

	I_StopFadingSong();

	if (!ms || target_volume == source_volume)
	{
		I_SetInternalMusicVolume(target_volume);
		if (callback)
			callback();
		return true;
	}

	fading_source = source_volume;
	fading_target = target_volume;
	fading_ms = ms;
	fading_start = I_GetPreciseTime();
	fading_callback = callback;
	is_fading = true;
	I_SetInternalMusicVolume(source_volume);
	return true;
}

boolean I_FadeSong(UINT8 target_volume, UINT32 ms, void (*callback)(void))
{
	return I_FadeSongFromVolume(target_volume, internal_volume, ms, callback);
}

boolean I_FadeOutStopSong(UINT32 ms)
{
	return I_FadeSongFromVolume(0, internal_volume, ms, &I_StopSong);
}

boolean I_FadeInPlaySong(UINT32 ms, boolean looping)
{
	if (I_PlaySong(looping))
		return I_FadeSongFromVolume(100, 0, ms, NULL);
	return false;
}
