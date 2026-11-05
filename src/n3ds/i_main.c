// SRB2Kart Saturn - Nintendo 3DS port
// Entry point.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"
#include "../m_misc.h"
#include "../i_system.h"
#include "../command.h"
#include "../r_main.h"
#include "../r_fps.h"
#include "../d_netcmd.h"

#include "n3ds_hw.h"

#ifdef LOGMESSAGES
FILE *logstream = NULL;
char  logfilename[1024];
#endif

// Extra command line options can be put in sdmc:/3ds/srb2kart/args.txt,
// whitespace separated (e.g. "-bottomlog -nosound -warp 01").
#define MAXARGS 64
static char *argbuf = NULL;
static char *args[MAXARGS];

static int load_args(char *argv0)
{
	FILE *f;
	long size;
	int argc = 0;
	char *p;

	args[argc++] = argv0 ? argv0 : (char *)"srb2kart";

	f = fopen(N3DS_GAMEDIR "/args.txt", "rb");
	if (!f)
		return argc;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size > 0 && size < 4096)
	{
		argbuf = (char *)malloc(size + 1);
		if (argbuf && fread(argbuf, 1, size, f) == (size_t)size)
		{
			argbuf[size] = '\0';
			for (p = strtok(argbuf, " \t\r\n"); p && argc < MAXARGS; p = strtok(NULL, " \t\r\n"))
				args[argc++] = p;
		}
	}
	fclose(f);
	return argc;
}

#ifdef LOGMESSAGES
static void InitLogging(void)
{
	if (M_CheckParm("-nolog"))
		return;
	mkdir(N3DS_GAMEDIR "/logs", 0755);
	snprintf(logfilename, sizeof logfilename, "%s/logs/log.txt", N3DS_GAMEDIR);
	logstream = fopen(logfilename, "w");
}
#endif

// Performance-oriented defaults for the 3DS. These only replace the
// *defaults*: anything in kartconfig.cfg or set in the menus still wins.
// All of them are client-side visuals, so netgames stay in sync with PC.
static void N3DS_SetDefaults(void)
{
	cv_fpscap.defaultvalue          = "30";   // steady 30 instead of a jittery 35-60
	cv_fpscapbg.defaultvalue        = "30";
	cv_skybox.defaultvalue          = "Off";  // skyboxes render the scene twice
	cv_drawdist.defaultvalue        = "2048";
	cv_drawdist_precip.defaultvalue = "512";
	cv_lessprecip.defaultvalue      = "On";
	cv_ripplewater.defaultvalue     = "Off";
#ifdef WALLSPLATS
	cv_splats.defaultvalue          = "Off";
#endif
	cv_precachetextures.defaultvalue = "Off"; // saves a lot of RAM
}

int main(int argc, char **argv)
{
	N3DS_SysInit();
	N3DS_VideoInit();
	N3DS_SetDefaults();

	printf("\x1b[36mSRB2Kart Saturn\x1b[0m for Nintendo 3DS\n");
	printf("Data folder: %s\n\n", N3DS_GAMEDIR);

	mkdir(N3DS_GAMEDIR, 0755);
	chdir(N3DS_GAMEDIR);

	// D_Home() reads $HOME; DEFAULTDIR ("srb2kart") is appended to it.
	setenv("HOME", N3DS_HOME_PARENT, 1);

	myargc = load_args(argc > 0 ? argv[0] : NULL);
	myargv = args;

#ifdef LOGMESSAGES
	InitLogging();
#endif

	CONS_Printf("Setting up SRB2Kart...\n");
	I_StartupSystem();

	D_SRB2Main();
	CONS_Printf("Entering main game loop...\n");
	D_SRB2Loop();

	return 0;
}
