// SRB2Kart Saturn - Nintendo 3DS port
// Networking goes through i_tcp.c (BSD sockets via libctru soc:u, started in
// I_StartupSystem). This is just the generic I_InitNetwork hook.

#include "../doomdef.h"
#include "../i_system.h"
#include "../i_net.h"
#include "../m_argv.h"

boolean I_InitNetwork(void)
{
	if (M_CheckParm("-net"))
		I_Error("-net not supported, use -server and -connect\nsee docs for more\n");
	return false;
}
