// SRB2Kart Saturn - Nintendo 3DS port
// System interface: timing, input, filesystem helpers, error handling.

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../doomdef.h"
#include "../m_misc.h"
#include "../i_system.h"
#include "../i_video.h"
#include "../i_sound.h"
#include "../i_joy.h"
#include "../i_time.h"
#include "../i_threads.h"
#include "../d_main.h"
#include "../d_net.h"
#include "../d_clisrv.h"
#include "../d_event.h"
#include "../g_game.h"
#include "../g_demo.h"
#include "../g_input.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../console.h"
#include "../command.h"
#include "../hu_stuff.h"
#include "../keys.h"
#include "../screen.h"
#include "../w_wad.h"
#include "../z_zone.h"
#include "../core/thread_pool.h"

#include "n3ds_hw.h"
#include "n3ds_glue.h"

#ifdef LOGMESSAGES
extern FILE *logstream;
#endif

// --------------------------------------------------------------- globals ----

UINT8 graphics_started = 0;
INT32 numcontrollers = 1;
UINT8 keyboard_started = 0;
boolean consolevent = false;
boolean framebuffer = false;

static boolean n3ds_started_video_once = false;
static boolean is_quitting = false;
static boolean shutdowning = false;
static INT32 errorcount = 0;

#define MAX_QUIT_FUNCS 16
static quitfuncptr quit_funcs[MAX_QUIT_FUNCS];

// ---------------------------------------------------------------- output ----

void I_OutputMsg(const char *fmt, ...)
{
	char txt[8192];
	va_list argptr;
	size_t len;

	if (!fmt)
		return;

	va_start(argptr, fmt);
	vsnprintf(txt, sizeof txt, fmt, argptr);
	va_end(argptr);

	len = strlen(txt);
	if (!len)
		return;

#ifdef LOGMESSAGES
	if (logstream)
	{
		fwrite(txt, len, 1, logstream);
		fflush(logstream);
	}
#endif

	// Mirror to the bottom screen while booting, and whenever the user asked
	// for it (-bottomlog). Software text rendering is not free, so this is
	// switched off once the game is up.
	if (!n3ds_started_video_once || n3ds_bottomlog)
		N3DS_ConsolePrint(txt);
}

void I_GetConsoleEvents(void)
{
}

// Returns the last keydown in the event queue and empties it.
INT32 I_GetKey(void)
{
	event_t *ev;
	INT32 rc = 0;

	I_GetEvent();
	for (; eventtail != eventhead; eventtail = (eventtail+1)&(MAXEVENTS-1))
	{
		ev = &events[eventtail];
		if (ev->type == ev_keydown || ev->type == ev_console)
			rc = ev->data1;
	}
	return rc;
}

// ----------------------------------------------------------------- time -----

precise_t I_GetPreciseTime(void)
{
	return (precise_t)N3DS_Ticks();
}

UINT64 I_GetPrecisePrecision(void)
{
	return N3DS_TicksPerSecond();
}

void I_StartupTimer(void)
{
}

void I_Sleep(UINT32 ms)
{
	N3DS_SleepNs((INT64)ms * 1000000LL);
}

void I_SleepDuration(precise_t duration)
{
	UINT64 precision = I_GetPrecisePrecision();
	precise_t dest = I_GetPreciseTime() + duration;
	precise_t slack = precision / 2000; // 0.5 ms

	if (duration > slack)
	{
		duration -= slack;
		N3DS_SleepNs((INT64)(duration * 1000000000ULL / precision));
	}

	while (I_GetPreciseTime() < dest)
		;
}

// ------------------------------------------------------------ joysticks -----

// The built-in controls show up to the engine as joystick #1, laid out like an
// SDL GameController so the stock SRB2Kart default bindings just work:
//   A=JOY1 (accel)  B=JOY2 (brake)  X=JOY3 (look back)  Y=JOY4 (view)
//   SELECT=JOY5     START=JOY7      L=JOY10 (item)      R=JOY11 (drift)
//   ZL=JOY8         ZR=JOY9         D-pad=HAT1          Circle pad=axis 0
//   C-stick=axis 1

typedef struct
{
	UINT32 mask;
	INT32  key;
} btnmap_t;

static const btnmap_t buttonmap[] =
{
	{N3DS_BTN_A,      KEY_JOY1 + 0},
	{N3DS_BTN_B,      KEY_JOY1 + 1},
	{N3DS_BTN_X,      KEY_JOY1 + 2},
	{N3DS_BTN_Y,      KEY_JOY1 + 3},
	{N3DS_BTN_SELECT, KEY_JOY1 + 4},
	{N3DS_BTN_START,  KEY_JOY1 + 6},
	{N3DS_BTN_ZL,     KEY_JOY1 + 7},
	{N3DS_BTN_ZR,     KEY_JOY1 + 8},
	{N3DS_BTN_L,      KEY_JOY1 + 9},
	{N3DS_BTN_R,      KEY_JOY1 + 10},
	{N3DS_BTN_DUP,    KEY_HAT1 + 0},
	{N3DS_BTN_DDOWN,  KEY_HAT1 + 1},
	{N3DS_BTN_DLEFT,  KEY_HAT1 + 2},
	{N3DS_BTN_DRIGHT, KEY_HAT1 + 3},
};

static UINT32 prev_held = 0;
static INT32 prev_axis[2][2] = {{0, 0}, {0, 0}};
static boolean joystick_enabled = true;
static char joyname[] = "Nintendo 3DS";

void I_InitJoystick(UINT8 index)
{
	if (index == 0)
		joystick_enabled = cv_usejoystick[0].value != 0;
}

void I_InitJoystick1(void) { I_InitJoystick(0); }
void I_InitJoystick2(void) {}
void I_InitJoystick3(void) {}
void I_InitJoystick4(void) {}

void I_ShutdownJoystick(UINT8 index)
{
	(void)index;
}

void I_JoyScale(void)  { }
void I_JoyScale2(void) { }
void I_JoyScale3(void) { }
void I_JoyScale4(void) { }

void I_UpdateJoystickDeviceIndex(UINT8 player)        { (void)player; }
void I_UpdateJoystickDeviceIndices(UINT8 excludePlayer) { (void)excludePlayer; }
void I_SetJoystickFocus(void) {}

INT32 I_NumJoys(void)
{
	return 1;
}

const char *I_GetJoyName(INT32 joyindex)
{
	return joyindex == 1 ? joyname : "";
}

boolean I_GamepadHasLED(INT32 playernum)    { (void)playernum; return false; }
boolean I_GamepadHasRumble(INT32 playernum) { (void)playernum; return false; }
void I_GamepadRumble(INT32 playernum, UINT16 low_strength, UINT16 high_strength, UINT32 duration)
{
	(void)playernum; (void)low_strength; (void)high_strength; (void)duration;
}
void I_SetGamepadIndicatorColor(INT32 playernum, UINT8 red, UINT8 green, UINT8 blue)
{
	(void)playernum; (void)red; (void)green; (void)blue;
}

void I_StartupMouse(void) {}

#ifndef NOMUMBLE
void I_UpdateMumble(const mobj_t *mobj, const listener_t listener)
{
	(void)mobj; (void)listener;
}
#endif

static void post_key(evtype_t type, INT32 key)
{
	event_t ev;
	ev.type = type;
	ev.data1 = key;
	ev.data2 = ev.data3 = 0;
	D_PostEvent(&ev);
}

// 3DS pads read about +-156 at full tilt; scale to JOYAXISRANGE with a small
// dead zone so a resting pad stays at 0.
static INT32 scale_axis(int v)
{
	const int dead = 12, full = 150;
	INT32 out;

	if (v > -dead && v < dead)
		return 0;
	if (v > 0)
		v -= dead;
	else
		v += dead;

	out = (INT32)((v * JOYAXISRANGE) / (full - dead));
	if (out > JOYAXISRANGE) out = JOYAXISRANGE;
	if (out < -JOYAXISRANGE) out = -JOYAXISRANGE;

	if (DigitalGamepadStyle(0))
		out = (out < -(JOYAXISRANGE/2)) ? -1 : (out > (JOYAXISRANGE/2)) ? 1 : 0;

	return out;
}

static void post_axis(INT32 set, int x, int y)
{
	event_t ev;
	// engine: +y = down
	INT32 ax = scale_axis(x), ay = -scale_axis(y);

	if (ax == prev_axis[set][0] && ay == prev_axis[set][1])
		return;
	prev_axis[set][0] = ax;
	prev_axis[set][1] = ay;

	ev.type = ev_joystick;
	ev.data1 = set;
	ev.data2 = ax;
	ev.data3 = ay;
	D_PostEvent(&ev);
}

// ------------------------------------------------------------ touch / kbd ---

boolean n3ds_bottomlog = false;
static boolean native_input_active = false;
static boolean touch_down = false;
static boolean scores_held = false;

boolean I_UseNativeKeyboard(void)
{
	return native_input_active;
}

void I_SetTextInput(boolean enable)
{
	native_input_active = enable;
}

// Types `text` into whatever currently has keyboard focus.
static void inject_text(const char *text, boolean enter)
{
	boolean was = native_input_active;
	native_input_active = true; // so the console doesn't apply shift tables
	for (; *text; text++)
	{
		unsigned char c = (unsigned char)*text;
		if (c < 32 || c > 126)
			continue;
		post_key(ev_keydown, c);
		post_key(ev_keyup, c);
	}
	if (enter)
	{
		post_key(ev_keydown, KEY_ENTER);
		post_key(ev_keyup, KEY_ENTER);
	}
	native_input_active = was;
}

static void touch_pressed(int tx, int ty)
{
	char buf[256];

	// A text field / console / chat line has focus: just type into it.
	if (native_input_active || CON_Ready())
	{
		buf[0] = '\0';
		if (N3DS_Keyboard("Text", NULL, buf, sizeof buf))
			inject_text(buf, !menuactive);
		return;
	}

	// Bottom strip: hold to show the scoreboard.
	if (ty >= 200)
	{
		post_key(ev_keydown, KEY_TAB);
		scores_held = true;
		return;
	}

	if (tx < 160)
	{
		// Left half: run a console command.
		buf[0] = '\0';
		if (N3DS_Keyboard("Console command", NULL, buf, sizeof buf) && buf[0])
		{
			COM_BufAddText(buf);
			COM_BufAddText("\n");
		}
	}
	else
	{
		// Right half: chat.
		buf[0] = '\0';
		if (N3DS_Keyboard("Chat message", NULL, buf, sizeof buf) && buf[0])
		{
			char *p;
			for (p = buf; *p; p++)
				if (*p == '"') *p = '\'';
			COM_BufAddText(va("say \"%s\"\n", buf));
		}
	}
}

static void touch_released(void)
{
	if (scores_held)
	{
		post_key(ev_keyup, KEY_TAB);
		scores_held = false;
	}
}

// --------------------------------------------------------------- events -----

void I_GetEvent(void)
{
	n3ds_input_t in;
	UINT32 changed;
	size_t i;

	if (!N3DS_AptMainLoop())
	{
		// HOME -> Close, or power button.
		I_Quit();
	}

	N3DS_ReadInput(&in);

	// SELECT + START opens the console (there is no keyboard key for it).
	if ((in.held & (N3DS_BTN_SELECT|N3DS_BTN_START)) == (N3DS_BTN_SELECT|N3DS_BTN_START)
		&& (prev_held & (N3DS_BTN_SELECT|N3DS_BTN_START)) != (N3DS_BTN_SELECT|N3DS_BTN_START))
	{
		post_key(ev_keydown, KEY_CONSOLE);
		post_key(ev_keyup, KEY_CONSOLE);
		prev_held = in.held;
		return;
	}

	changed = in.held ^ prev_held;
	if (joystick_enabled && changed)
	{
		for (i = 0; i < sizeof buttonmap / sizeof buttonmap[0]; i++)
		{
			if (!(changed & buttonmap[i].mask))
				continue;
			post_key((in.held & buttonmap[i].mask) ? ev_keydown : ev_keyup, buttonmap[i].key);
		}
	}

	// Touch screen (rising / falling edge only).
	if ((in.held & N3DS_BTN_TOUCH) && !touch_down)
	{
		touch_down = true;
		prev_held = in.held;
		touch_pressed(in.touch_x, in.touch_y);
	}
	else if (!(in.held & N3DS_BTN_TOUCH) && touch_down)
	{
		touch_down = false;
		touch_released();
	}

	if (joystick_enabled)
	{
		post_axis(0, in.circle_x, in.circle_y);
		post_axis(1, in.cstick_x, in.cstick_y);
	}

	prev_held = in.held;
}

void I_OsPolling(void)
{
	I_GetEvent();
}

void I_HandleControllerHatRepeat(void)
{
}

// --------------------------------------------------------------- startup ----

INT32 I_StartupSystem(void)
{
	n3ds_bottomlog = M_CheckParm("-bottomlog") ? true : false;

#ifdef HAVE_THREADS
	I_start_threads();
	I_AddExitFunc(I_stop_threads);
	I_ThreadPoolInit();
	I_AddExitFunc(I_ThreadPoolShutdown);
#endif

	if (N3DS_NetInit() != 0)
		I_OutputMsg("Network unavailable (soc:u init failed), playing offline.\n");

	I_OutputMsg("Running on %s\n", N3DS_IsNew3DS() ? "New Nintendo 3DS" : "Nintendo 3DS (old model, expect low speed)");
	return 0;
}

void N3DS_FirstFrameShown(void)
{
	if (!n3ds_started_video_once)
	{
		n3ds_started_video_once = true;
		N3DS_ShowTouchLegend();
	}
}

void I_AddExitFunc(void (*func)())
{
	INT32 c;
	for (c = 0; c < MAX_QUIT_FUNCS; c++)
	{
		if (!quit_funcs[c])
		{
			quit_funcs[c] = func;
			break;
		}
	}
}

void I_RemoveExitFunc(void (*func)())
{
	INT32 c;
	for (c = 0; c < MAX_QUIT_FUNCS; c++)
	{
		if (quit_funcs[c] == func)
		{
			while (c < MAX_QUIT_FUNCS-1)
			{
				quit_funcs[c] = quit_funcs[c+1];
				c++;
			}
			quit_funcs[MAX_QUIT_FUNCS-1] = NULL;
			break;
		}
	}
}

void I_ShutdownSystem(void)
{
	INT32 c;

	for (c = MAX_QUIT_FUNCS-1; c >= 0; c--)
		if (quit_funcs[c])
			(*quit_funcs[c])();

#ifdef LOGMESSAGES
	if (logstream)
	{
		I_OutputMsg("I_ShutdownSystem(): end of logstream.\n");
		fclose(logstream);
		logstream = NULL;
	}
#endif

	N3DS_NetExit();
}

boolean I_In_Exiting_Signal_Handler(void)
{
	return false;
}

boolean I_Interrupted(void)
{
	return false;
}

FUNCNORETURN void ATTRNORETURN I_Quit(void)
{
	if (is_quitting)
		abort();
	is_quitting = true;

	M_SaveConfig(NULL);
	D_SaveBan();
	G_SaveGameData(false);

	if (demo.recording)
		G_CheckDemoStatus();

	D_QuitNetGame();
	I_ShutdownMusic();
	I_ShutdownSound();
	I_ShutdownGraphics();
	I_ShutdownSystem();

	N3DS_SysExit();

	if (myargmalloc)
		free(myargv);

	exit(0);
}

FUNCIERROR void ATTRNORETURN I_Error(const char *error, ...)
{
	va_list argptr;
	char buffer[8192];

	if (shutdowning)
	{
		errorcount++;
		if (errorcount == 2) I_ShutdownMusic();
		if (errorcount == 3) I_ShutdownSound();
		if (errorcount == 4) I_ShutdownGraphics();
		if (errorcount == 5) I_ShutdownSystem();
		if (errorcount == 6)
		{
			M_SaveConfig(NULL);
			G_SaveGameData(false);
		}
		if (errorcount > 20)
		{
			va_start(argptr, error);
			vsnprintf(buffer, sizeof buffer, error, argptr);
			va_end(argptr);
			N3DS_ErrorScreen("SRB2Kart " VERSIONSTRING " Recursive Error", buffer);
			exit(-1);
		}
	}

	shutdowning = true;

	va_start(argptr, error);
	vsnprintf(buffer, sizeof buffer, error, argptr);
	va_end(argptr);
	I_OutputMsg("\nI_Error(): %s\n", buffer);

	M_SaveConfig(NULL);
	D_SaveBan();
	G_SaveGameData(false);

	if (demo.recording)
		G_CheckDemoStatus();

	D_QuitNetGame();
	I_ShutdownMusic();
	I_ShutdownSound();
	I_ShutdownGraphics();
	I_ShutdownSystem();

	N3DS_ErrorScreen("SRB2Kart " VERSIONSTRING " Error", buffer);
	N3DS_SysExit();
	exit(-1);
}

// ------------------------------------------------------------ filesystem ----

void I_GetDiskFreeSpace(INT64 *freespace)
{
	*freespace = INT32_MAX;
}

char *I_GetUserName(void)
{
	static char username[] = "3DS";
	return username;
}

INT32 I_mkdir(const char *dirname, INT32 unixright)
{
	return mkdir(dirname, (mode_t)unixright);
}

INT32 I_ChDir(const char *path)
{
	return chdir(path);
}

char *I_GetCwd(char *buf, size_t size)
{
	return getcwd(buf, size);
}

char *I_GetEnv(const char *name)
{
	return getenv(name);
}

INT32 I_PutEnv(char *variable)
{
	return putenv(variable);
}

INT32 I_ClipboardCopy(const char *data, size_t size)
{
	(void)data; (void)size;
	return -1;
}

const char *I_ClipboardPaste(void)
{
	return NULL;
}

const char *I_LocateWad(void)
{
	static char waddir[] = N3DS_GAMEDIR;
	FILE *f = fopen(N3DS_GAMEDIR "/srb2.srb", "rb");
	if (f)
	{
		fclose(f);
		I_ChDir(waddir);
		return waddir;
	}
	return NULL;
}

size_t I_GetFreeMem(size_t *total)
{
	return N3DS_FreeMem(total);
}

const char *I_GetSysName(void)
{
	return "Nintendo 3DS";
}

int I_OpenURL(const char *url)
{
	(void)url;
	return -1;
}

void I_RegisterSysCommands(void)
{
}
