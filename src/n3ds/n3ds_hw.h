// SRB2Kart Saturn - Nintendo 3DS port
// Thin hardware layer over libctru.
//
// The engine headers and <3ds.h> do not mix well (both define a lot of short
// global names), so everything that needs libctru lives in n3ds_hw.c and the
// engine-facing glue (i_system.c, i_video.c, i_sound.c, ...) only sees this
// header.

#ifndef __N3DS_HW_H__
#define __N3DS_HW_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where the game looks for srb2.srb, *.kart, config and addons.
#define N3DS_HOME_PARENT "sdmc:/3ds"
#define N3DS_GAMEDIR     "sdmc:/3ds/srb2kart"

// ---------------------------------------------------------------- system ----
void     N3DS_SysInit(void);
void     N3DS_SysExit(void);
int      N3DS_IsNew3DS(void);
int      N3DS_CpuMHz(void);                // measured clock (268 or ~804)
extern int32_t n3ds_speedup_init_rc, n3ds_speedup_cfg_rc;
int      N3DS_AptMainLoop(void);           // 0 when the system asks us to quit
uint64_t N3DS_Ticks(void);                 // ARM11 system ticks
uint64_t N3DS_TicksPerSecond(void);
void     N3DS_SleepNs(int64_t ns);
size_t   N3DS_FreeMem(size_t *total);
// Linear heap used as overflow for the zone allocator (see z_zone.cpp).
void    *N3DS_LinearAlloc(size_t size);
void     N3DS_LinearFree(void *p);
int      N3DS_IsLinear(const void *p);
int      N3DS_NetInit(void);               // returns 0 on success
void     N3DS_NetExit(void);
void     N3DS_ErrorScreen(const char *title, const char *msg); // blocks until START

// ------------------------------------------------------------------ input ---
enum
{
	N3DS_BTN_A      = 1u << 0,
	N3DS_BTN_B      = 1u << 1,
	N3DS_BTN_SELECT = 1u << 2,
	N3DS_BTN_START  = 1u << 3,
	N3DS_BTN_DRIGHT = 1u << 4,
	N3DS_BTN_DLEFT  = 1u << 5,
	N3DS_BTN_DUP    = 1u << 6,
	N3DS_BTN_DDOWN  = 1u << 7,
	N3DS_BTN_R      = 1u << 8,
	N3DS_BTN_L      = 1u << 9,
	N3DS_BTN_X      = 1u << 10,
	N3DS_BTN_Y      = 1u << 11,
	N3DS_BTN_ZL     = 1u << 14,
	N3DS_BTN_ZR     = 1u << 15,
	N3DS_BTN_TOUCH  = 1u << 20,
};

typedef struct
{
	uint32_t held;       // N3DS_BTN_* bitmask
	int      circle_x;   // circle pad, roughly -156..156, +y = up
	int      circle_y;
	int      cstick_x;   // New 3DS C-stick, same range
	int      cstick_y;
	int      touch_x;    // 0..319 when N3DS_BTN_TOUCH is held
	int      touch_y;    // 0..239
} n3ds_input_t;

void N3DS_ReadInput(n3ds_input_t *in);

// Opens the system software keyboard. Returns 1 if the user confirmed text.
int  N3DS_Keyboard(const char *hint, const char *initial, char *out, size_t outlen);

// ------------------------------------------------------------------ video ---
#define N3DS_TOP_W 400
#define N3DS_TOP_H 240

void N3DS_VideoInit(void);
void N3DS_VideoExit(void);
// Converts an 8-bit indexed image of size w*h into the top screen, scaling to
// 400x240 if needed (nearest neighbour). pal is RGB565.
void N3DS_VideoBlit8(const uint8_t *src, int w, int h, int pitch, const uint16_t *pal);
void N3DS_VideoPresent(int vsync);
// Blit + present in one go. On New 3DS the conversion runs on CPU core 2 in
// parallel with the next frame (the source is copied, so it can be reused).
void N3DS_VideoSubmit(const uint8_t *src, int w, int h, int pitch, const uint16_t *pal, int vsync);
int  N3DS_VideoAsync(void);
void N3DS_SetBottomScreen(int on);         // backlight / console on bottom screen
void N3DS_ConsolePrint(const char *txt);   // bottom screen log

// ------------------------------------------------------------------ audio ---
#define N3DS_AUDIO_RATE 32728              // native DSP rate, no resampling

// Called from the audio thread: fill `frames` stereo s16 frames into out.
typedef void (*n3ds_audio_cb)(int16_t *out, int frames, void *user);

int  N3DS_AudioInit(n3ds_audio_cb cb, void *user); // 0 on success
void N3DS_AudioExit(void);
void N3DS_AudioLock(void);
void N3DS_AudioUnlock(void);
void N3DS_AudioPause(int pause);

// ---------------------------------------------------------------- threads ---
typedef void (*n3ds_thread_fn)(void *);
void *N3DS_ThreadCreate(n3ds_thread_fn fn, void *arg, size_t stack, int prio_offset, int core);
void  N3DS_ThreadJoin(void *thread);

void *N3DS_MutexCreate(void);
void  N3DS_MutexLock(void *m);
void  N3DS_MutexUnlock(void *m);
void *N3DS_CondCreate(void);
void  N3DS_CondWait(void *c, void *m);
void  N3DS_CondSignal(void *c);
void  N3DS_CondBroadcast(void *c);

#ifdef __cplusplus
}
#endif

#endif
