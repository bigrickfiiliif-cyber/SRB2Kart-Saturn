// SRB2Kart Saturn - Nintendo 3DS port
// Software-renderer video output straight into the top-screen framebuffer.

#include <stdio.h>
#include <string.h>

#include "../doomdef.h"
#include "../doomstat.h"
#include "../i_video.h"
#include "../i_system.h"
#include "../v_video.h"
#include "../screen.h"
#include "../st_stuff.h"
#include "../console.h"
#include "../command.h"
#include "../m_argv.h"
#include "../d_clisrv.h"
#include "../g_game.h"

#include "n3ds_hw.h"
#include "n3ds_glue.h"

rendermode_t rendermode = render_none;

extern UINT8 graphics_started;
boolean allow_fullscreen = false;

static CV_PossibleValue_t keyboardlayout_cons_t[] = {{1,"Default US"}, {2, "Native"}, {3, "AZERTY"}, {0, NULL}};
consvar_t cv_keyboardlayout = {"keyboardlayout", "Native", CV_SAVE, keyboardlayout_cons_t, NULL, 0, NULL, NULL, 0, 0, NULL};

// The 3DS LCD runs at ~59.83 Hz; waiting for vblank avoids tearing at no real
// cost because we can't render faster than that anyway.
consvar_t cv_vidwait = {"vid_wait", "On", CV_SAVE, CV_OnOff, NULL, 0, NULL, NULL, 0, 0, NULL};

static uint16_t palette565[256];

// Top screen modes. 400x240 is pixel-perfect; 320x200 renders ~33% fewer
// pixels and is stretched to fill the screen.
#define NUMMODES 2
static const INT32 modes[NUMMODES][2] =
{
	{400, 240},
	{320, 200},
};
static char modenames[NUMMODES][16];

boolean I_CheckNativeRes(void)
{
	return true;
}

void I_SetBorderlessWindow(void)
{
}

void RefreshOGLSDLSurface(void)
{
}

UINT32 I_GetRefreshRate(void)
{
	return 60;
}

INT32 VID_NumModes(void)
{
	return NUMMODES;
}

const char *VID_GetModeName(INT32 modenum)
{
	if (modenum < 0 || modenum >= NUMMODES)
		modenum = 0;
	snprintf(modenames[modenum], sizeof modenames[modenum], "%dx%d",
		modes[modenum][0], modes[modenum][1]);
	return modenames[modenum];
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	INT32 i;
	for (i = 0; i < NUMMODES; i++)
		if (modes[i][0] == w && modes[i][1] == h)
			return i;
	return 0; // anything else -> native 400x240
}

void VID_PrepareModeList(void)
{
	allow_fullscreen = true;
}

INT32 VID_SetMode(INT32 modenum)
{
	if (modenum < 0 || modenum >= NUMMODES)
		modenum = 0;

	vid.width = modes[modenum][0];
	vid.height = modes[modenum][1];
	vid.modenum = modenum;
	vid.recalc = true;
	return 1;
}

void I_SetPalette(RGBA_t *palette)
{
	size_t i;
	for (i = 0; i < 256; i++)
	{
		palette565[i] = (uint16_t)(((palette[i].s.red   & 0xF8) << 8)
		                         | ((palette[i].s.green & 0xFC) << 3)
		                         |  (palette[i].s.blue  >> 3));
	}
}

static void VID_Command_ModeList_f(void)
{
	INT32 i;
	for (i = 0; i < NUMMODES; i++)
		CONS_Printf("%d: %s\n", i, VID_GetModeName(i));
}

static void VID_Command_Mode_f(void)
{
	if (COM_Argc() < 2)
	{
		CONS_Printf("vid_mode <modenum>\n");
		return;
	}
	setmodeneeded = atoi(COM_Argv(1)) + 1;
}

static void VID_Command_NumModes_f(void)
{
	CONS_Printf("%d video mode(s) available\n", VID_NumModes());
}

void I_StartupGraphics(void)
{
	if (dedicated)
	{
		rendermode = render_none;
		return;
	}
	if (graphics_started)
		return;

	COM_AddCommand("vid_nummodes", VID_Command_NumModes_f);
	COM_AddCommand("vid_modelist", VID_Command_ModeList_f);
	COM_AddCommand("vid_mode", VID_Command_Mode_f);
	CV_RegisterVar(&cv_vidwait);
	CV_RegisterVar(&cv_keyboardlayout);

	rendermode = render_soft;

	VID_SetMode(0);
	vid.width = BASEVIDWIDTH;
	vid.height = BASEVIDHEIGHT;
	vid.recalc = true;
	VID_SetMode(VID_GetModeForSize(cv_scr_width.value, cv_scr_height.value));

	graphics_started = true;
}

void I_ShutdownGraphics(void)
{
	rendermode = render_none;
	graphics_started = false;
	// The framebuffers are owned by i_main / n3ds_hw and torn down at exit,
	// so an I_Error can still use the screen.
}

void N3DS_ShowTouchLegend(void)
{
	N3DS_ConsolePrint(
		"\x1b[2J"
		"\x1b[1;1H\x1b[36mSRB2Kart Saturn for 3DS\x1b[0m\n\n"
		"  Circle pad / D-pad : steer, menus\n"
		"  A : accelerate     B : brake/back\n"
		"  R : drift          L : use item\n"
		"  X : look back      Y : change view\n"
		"  START : menu       SELECT : pause\n"
		"  SELECT+START : console\n\n"
		"\x1b[33mTouch screen\x1b[0m\n"
		"  left half  : type console command\n"
		"  right half : chat message\n"
		"  bottom row : hold for scores\n"
		"  (while a text field is active,\n"
		"   any touch opens the keyboard)\n"
		"\x1b[24;1H\x1b[0m");
}

void I_FinishUpdate(void)
{
	if (rendermode == render_none)
		return;

	SCR_CalculateFPS();

	if (st_overlay)
	{
		if (cv_ticrate.value)
			SCR_DisplayTicRate();

		if (cv_showping.value && netgame && consoleplayer != serverplayer)
			SCR_DisplayLocalPing();
	}

	if (vid.screens[0])
		N3DS_VideoBlit8(vid.screens[0], vid.width, vid.height, vid.width, palette565);

	N3DS_VideoPresent(cv_vidwait.value);
	N3DS_FirstFrameShown();
}

void I_UpdateNoVsync(void)
{
	INT32 real_vidwait = cv_vidwait.value;
	cv_vidwait.value = 0;
	I_FinishUpdate();
	cv_vidwait.value = real_vidwait;
}

void I_ReadScreen(UINT8 *restrict scr, INT32 scale)
{
	if (rendermode != render_soft)
		I_Error("I_ReadScreen: called while in non-software mode");

	if (scale == 1)
		VID_BlitLinearScreen(vid.screens[0], scr, vid.width, vid.height, vid.width, vid.width);
	else
	{
		UINT8 *restrict source = vid.screens[0];
		uintptr_t w = vid.width/scale*scale, h = vid.height/scale*scale;
		uintptr_t x, y;
		for (y = 0; y < h; y += scale)
			for (x = 0; x < w; x += scale)
				*scr++ = source[y*vid.width + x];
	}
}
