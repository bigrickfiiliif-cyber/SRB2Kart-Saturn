// SRB2Kart Saturn - Nintendo 3DS port
// libctru hardware layer. See n3ds_hw.h.

#include <3ds.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n3ds_hw.h"

// ================================================================ system ====

// libctru defaults: 32 KB main-thread stack (far too small for the engine's
// renderer / Lua / I_Error buffers) and a 32 MB linear heap (we only need a
// few MB for framebuffers + audio). Give the rest to the normal heap.
u32 __stacksize__ = 1024 * 1024;
u32 __ctru_linear_heap_size = 8 * 1024 * 1024;

static int is_new3ds = 0;
static aptHookCookie apt_cookie;
static volatile int audio_paused_by_apt = 0;

static void apt_hook(APT_HookType hook, void *param)
{
	(void)param;
	switch (hook)
	{
		case APTHOOK_ONSUSPEND:
		case APTHOOK_ONSLEEP:
			audio_paused_by_apt = 1;
			N3DS_AudioPause(1);
			break;
		case APTHOOK_ONRESTORE:
		case APTHOOK_ONWAKEUP:
			audio_paused_by_apt = 0;
			N3DS_AudioPause(0);
			break;
		default:
			break;
	}
}

void N3DS_SysInit(void)
{
	bool n3ds = false;
	APT_CheckNew3DS(&n3ds);
	is_new3ds = n3ds ? 1 : 0;

	// 804 MHz + L2 cache on New 3DS. No-op on old models.
	osSetSpeedupEnable(true);

	// Let us use ~30% of the system core (core 1) for the audio thread.
	APT_SetAppCpuTimeLimit(30);

	irrstInit(); // C-stick / ZL / ZR
	romfsInit(); // CA bundle for HTTPS (master server, downloads)

	aptHook(&apt_cookie, apt_hook, NULL);
}

void N3DS_SysExit(void)
{
	aptUnhook(&apt_cookie);
	N3DS_AudioExit();
	N3DS_VideoExit();
	N3DS_NetExit();
	romfsExit();
	irrstExit();
}

int N3DS_IsNew3DS(void)
{
	return is_new3ds;
}

int N3DS_AptMainLoop(void)
{
	return aptMainLoop() ? 1 : 0;
}

uint64_t N3DS_Ticks(void)
{
	return svcGetSystemTick();
}

uint64_t N3DS_TicksPerSecond(void)
{
	return SYSCLOCK_ARM11;
}

void N3DS_SleepNs(int64_t ns)
{
	if (ns > 0)
		svcSleepThread(ns);
}

size_t N3DS_FreeMem(size_t *total)
{
	if (total)
		*total = osGetMemRegionSize(MEMREGION_APPLICATION);
	return osGetMemRegionFree(MEMREGION_APPLICATION);
}

static u32 *soc_buffer = NULL;
#define SOC_BUFFERSIZE 0x100000

int N3DS_NetInit(void)
{
	if (soc_buffer)
		return 0;

	soc_buffer = (u32 *)memalign(0x1000, SOC_BUFFERSIZE);
	if (!soc_buffer)
		return -1;

	if (R_FAILED(socInit(soc_buffer, SOC_BUFFERSIZE)))
	{
		free(soc_buffer);
		soc_buffer = NULL;
		return -1;
	}
	return 0;
}

void N3DS_NetExit(void)
{
	if (!soc_buffer)
		return;
	socExit();
	free(soc_buffer);
	soc_buffer = NULL;
}

void N3DS_ErrorScreen(const char *title, const char *msg)
{
	// Fall back to a plain text console on the top screen.
	N3DS_AudioExit();
	N3DS_VideoExit();
	gfxInitDefault();
	consoleInit(GFX_TOP, NULL);
	printf("\x1b[31;1m%s\x1b[0m\n\n%s\n\n\x1b[33mPress START to exit.\x1b[0m\n",
		title ? title : "Error", msg ? msg : "");

	while (aptMainLoop())
	{
		hidScanInput();
		if (hidKeysDown() & KEY_START)
			break;
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	gfxExit();
}

// ================================================================= input ====

void N3DS_ReadInput(n3ds_input_t *in)
{
	circlePosition cp, cs;
	touchPosition tp;
	u32 held;

	hidScanInput();
	held = hidKeysHeld();
	hidCircleRead(&cp);
	hidCstickRead(&cs);

	// libctru KEY_* bit positions match N3DS_BTN_* (see hid.h); mask out
	// the synthetic circle pad / c-stick direction bits.
	in->held = held & (N3DS_BTN_A | N3DS_BTN_B | N3DS_BTN_SELECT | N3DS_BTN_START
		| N3DS_BTN_DRIGHT | N3DS_BTN_DLEFT | N3DS_BTN_DUP | N3DS_BTN_DDOWN
		| N3DS_BTN_R | N3DS_BTN_L | N3DS_BTN_X | N3DS_BTN_Y
		| N3DS_BTN_ZL | N3DS_BTN_ZR | N3DS_BTN_TOUCH);

	in->circle_x = cp.dx;
	in->circle_y = cp.dy;
	in->cstick_x = cs.dx;
	in->cstick_y = cs.dy;

	if (held & KEY_TOUCH)
	{
		hidTouchRead(&tp);
		in->touch_x = tp.px;
		in->touch_y = tp.py;
	}
	else
		in->touch_x = in->touch_y = -1;
}

int N3DS_Keyboard(const char *hint, const char *initial, char *out, size_t outlen)
{
	SwkbdState swkbd;
	SwkbdButton button;

	swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, (int)outlen - 1);
	swkbdSetFeatures(&swkbd, SWKBD_PREDICTIVE_INPUT | SWKBD_DARKEN_TOP_SCREEN);
	if (hint)
		swkbdSetHintText(&swkbd, hint);
	if (initial)
		swkbdSetInitialText(&swkbd, initial);

	button = swkbdInputText(&swkbd, out, outlen);
	return button == SWKBD_BUTTON_CONFIRM;
}

// ================================================================= video ====

static int video_started = 0;
static int bottom_console = 0;
static PrintConsole bottom_con;

static int blit_w = 0, blit_h = 0;
static uint16_t xsrc[N3DS_TOP_W];
static uint32_t yoff[N3DS_TOP_H];

void N3DS_VideoInit(void)
{
	if (video_started)
		return;

	gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSet3D(false);

	consoleInit(GFX_BOTTOM, &bottom_con);
	consoleSelect(&bottom_con);
	bottom_console = 1;

	video_started = 1;
}

void N3DS_VideoExit(void)
{
	if (!video_started)
		return;
	gfxExit();
	video_started = 0;
	bottom_console = 0;
}

void N3DS_SetBottomScreen(int on)
{
	gspLcdInit();
	if (on)
		GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTTOM);
	else
		GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
	gspLcdExit();
}

void N3DS_ConsolePrint(const char *txt)
{
	if (bottom_console)
		fputs(txt, stdout);
}

static void setup_scale(int w, int h, int pitch)
{
	int i;
	for (i = 0; i < N3DS_TOP_W; i++)
		xsrc[i] = (uint16_t)((i * w) / N3DS_TOP_W);
	for (i = 0; i < N3DS_TOP_H; i++)
		yoff[i] = (uint32_t)(((i * h) / N3DS_TOP_H) * pitch);
	blit_w = w;
	blit_h = h | (pitch << 16);
}

// The top framebuffer is 240x400, stored column-major and rotated:
// screen pixel (x, y) lives at fb[x * 240 + (239 - y)].
void N3DS_VideoBlit8(const uint8_t *src, int w, int h, int pitch, const uint16_t *pal)
{
	u16 fbw, fbh;
	uint16_t *fb;
	int x, y;

	if (!video_started || !src)
		return;

	fb = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbw, &fbh);

	if (w == N3DS_TOP_W && h == N3DS_TOP_H)
	{
		// 1:1 fast path. Two source columns per pass, so every cache line we
		// pull in from the source is used twice before it is evicted.
		for (x = 0; x < N3DS_TOP_W; x += 2)
		{
			uint16_t *col0 = fb + x * N3DS_TOP_H + (N3DS_TOP_H - 1);
			uint16_t *col1 = col0 + N3DS_TOP_H;
			const uint8_t *s = src + x;
			for (y = 0; y < N3DS_TOP_H; y++)
			{
				*col0-- = pal[s[0]];
				*col1-- = pal[s[1]];
				s += pitch;
			}
		}
		return;
	}

	if (blit_w != w || blit_h != (h | (pitch << 16)))
		setup_scale(w, h, pitch);

	for (x = 0; x < N3DS_TOP_W; x++)
	{
		uint16_t *col = fb + x * N3DS_TOP_H + (N3DS_TOP_H - 1);
		const uint8_t *s = src + xsrc[x];
		for (y = 0; y < N3DS_TOP_H; y++)
			*col-- = pal[s[yoff[y]]];
	}
}

void N3DS_VideoPresent(int vsync)
{
	if (!video_started)
		return;
	gfxFlushBuffers();
	gfxSwapBuffers();
	if (vsync)
		gspWaitForVBlank();
}

// ================================================================= audio ====

#define AUDIO_BUFS    4
#define AUDIO_FRAMES  768   // ~23 ms per buffer at 32728 Hz

static ndspWaveBuf  wavebufs[AUDIO_BUFS];
static int16_t     *audio_mem = NULL;
static n3ds_audio_cb audio_cb = NULL;
static void        *audio_user = NULL;
static LightLock    audio_lock;
static LightEvent   audio_event;
static Thread       audio_thread = NULL;
static volatile int audio_run = 0;
static int          audio_started = 0;

static void ndsp_callback(void *data)
{
	(void)data;
	LightEvent_Signal(&audio_event);
}

static void fill_buffer(ndspWaveBuf *wb)
{
	int16_t *pcm = (int16_t *)wb->data_pcm16;

	LightLock_Lock(&audio_lock);
	audio_cb(pcm, AUDIO_FRAMES, audio_user);
	LightLock_Unlock(&audio_lock);

	DSP_FlushDataCache(pcm, AUDIO_FRAMES * 2 * sizeof(int16_t));
	ndspChnWaveBufAdd(0, wb);
}

static void audio_thread_fn(void *arg)
{
	int i;
	(void)arg;

	while (audio_run)
	{
		for (i = 0; i < AUDIO_BUFS; i++)
		{
			if (wavebufs[i].status == NDSP_WBUF_DONE)
				fill_buffer(&wavebufs[i]);
		}
		LightEvent_Wait(&audio_event);
	}
}

int N3DS_AudioInit(n3ds_audio_cb cb, void *user)
{
	int i, prio = 0x30;
	float mix[12];

	if (audio_started)
		return 0;

	// Needs sdmc:/3ds/dspfirm.cdc (dumped with the "DSP1" homebrew).
	if (R_FAILED(ndspInit()))
		return -1;

	audio_cb = cb;
	audio_user = user;
	LightLock_Init(&audio_lock);
	LightEvent_Init(&audio_event, RESET_ONESHOT);

	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspChnReset(0);
	ndspChnSetInterp(0, NDSP_INTERP_NONE);
	ndspChnSetRate(0, (float)N3DS_AUDIO_RATE);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	memset(mix, 0, sizeof(mix));
	mix[0] = mix[1] = 1.0f;
	ndspChnSetMix(0, mix);

	audio_mem = (int16_t *)linearAlloc(AUDIO_BUFS * AUDIO_FRAMES * 2 * sizeof(int16_t));
	if (!audio_mem)
	{
		ndspExit();
		return -1;
	}
	memset(audio_mem, 0, AUDIO_BUFS * AUDIO_FRAMES * 2 * sizeof(int16_t));
	memset(wavebufs, 0, sizeof(wavebufs));

	for (i = 0; i < AUDIO_BUFS; i++)
	{
		wavebufs[i].data_vaddr = audio_mem + i * AUDIO_FRAMES * 2;
		wavebufs[i].nsamples = AUDIO_FRAMES;
		wavebufs[i].status = NDSP_WBUF_DONE;
	}

	ndspSetCallback(ndsp_callback, NULL);

	audio_run = 1;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);

	// Prefer the extra New 3DS core, then the system core, then ours.
	audio_thread = threadCreate(audio_thread_fn, NULL, 64 * 1024, prio - 1, 2, false);
	if (!audio_thread)
		audio_thread = threadCreate(audio_thread_fn, NULL, 64 * 1024, prio - 1, 1, false);
	if (!audio_thread)
		audio_thread = threadCreate(audio_thread_fn, NULL, 64 * 1024, prio - 1, 0, false);
	if (!audio_thread)
	{
		audio_run = 0;
		linearFree(audio_mem);
		audio_mem = NULL;
		ndspExit();
		return -1;
	}

	audio_started = 1;
	LightEvent_Signal(&audio_event);
	return 0;
}

void N3DS_AudioExit(void)
{
	if (!audio_started)
		return;

	audio_run = 0;
	LightEvent_Signal(&audio_event);
	threadJoin(audio_thread, U64_MAX);
	threadFree(audio_thread);
	audio_thread = NULL;

	ndspChnWaveBufClear(0);
	ndspExit();
	linearFree(audio_mem);
	audio_mem = NULL;
	audio_started = 0;
}

void N3DS_AudioLock(void)
{
	if (audio_started)
		LightLock_Lock(&audio_lock);
}

void N3DS_AudioUnlock(void)
{
	if (audio_started)
		LightLock_Unlock(&audio_lock);
}

void N3DS_AudioPause(int pause)
{
	if (audio_started)
		ndspChnSetPaused(0, pause ? true : false);
}

// =============================================================== threads ====

typedef struct
{
	n3ds_thread_fn fn;
	void *arg;
} thread_start_t;

static void thread_trampoline(void *p)
{
	thread_start_t st = *(thread_start_t *)p;
	free(p);
	st.fn(st.arg);
}

void *N3DS_ThreadCreate(n3ds_thread_fn fn, void *arg, size_t stack, int prio_offset, int core)
{
	int prio = 0x30;
	Thread t;
	thread_start_t *st = (thread_start_t *)malloc(sizeof(*st));

	if (!st)
		return NULL;
	st->fn = fn;
	st->arg = arg;

	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	prio += prio_offset;
	if (prio < 0x18) prio = 0x18;
	if (prio > 0x3F) prio = 0x3F;

	t = threadCreate(thread_trampoline, st, stack ? stack : 64 * 1024, prio, core, false);
	if (!t && core != 0)
		t = threadCreate(thread_trampoline, st, stack ? stack : 64 * 1024, prio, 0, false);
	if (!t)
		free(st);
	return t;
}

void N3DS_ThreadJoin(void *thread)
{
	if (!thread)
		return;
	threadJoin((Thread)thread, U64_MAX);
	threadFree((Thread)thread);
}

// Recursive mutex: the engine was written against SDL mutexes, which are
// recursive, and e.g. the console locks its state again from inside a locked
// section. A plain LightLock would deadlock the thread on itself.
typedef struct
{
	LightLock     lock;
	void *volatile owner; // TLS pointer of the owning thread (unique per thread)
	int           count;
} n3ds_mutex_t;

void *N3DS_MutexCreate(void)
{
	n3ds_mutex_t *m = (n3ds_mutex_t *)calloc(1, sizeof(n3ds_mutex_t));
	if (m)
		LightLock_Init(&m->lock);
	return m;
}

void N3DS_MutexLock(void *p)
{
	n3ds_mutex_t *m = (n3ds_mutex_t *)p;
	void *self = getThreadLocalStorage();

	if (m->owner == self)
	{
		m->count++;
		return;
	}
	LightLock_Lock(&m->lock);
	m->owner = self;
	m->count = 1;
}

void N3DS_MutexUnlock(void *p)
{
	n3ds_mutex_t *m = (n3ds_mutex_t *)p;

	if (m->owner != getThreadLocalStorage() || m->count <= 0)
		return; // not ours; ignore like SDL would report an error
	if (--m->count == 0)
	{
		m->owner = NULL;
		LightLock_Unlock(&m->lock);
	}
}

void *N3DS_CondCreate(void)
{
	CondVar *c = (CondVar *)malloc(sizeof(CondVar));
	if (c)
		CondVar_Init(c);
	return c;
}

void N3DS_CondWait(void *c, void *p)
{
	n3ds_mutex_t *m = (n3ds_mutex_t *)p;
	int saved = m->count;

	// fully release while waiting, then restore the recursion depth
	m->owner = NULL;
	m->count = 0;
	CondVar_Wait((CondVar *)c, &m->lock);
	m->owner = getThreadLocalStorage();
	m->count = saved;
}

void N3DS_CondSignal(void *c)
{
	CondVar_Signal((CondVar *)c);
}

void N3DS_CondBroadcast(void *c)
{
	CondVar_Broadcast((CondVar *)c);
}
