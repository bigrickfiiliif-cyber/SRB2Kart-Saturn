// SRB2Kart Saturn - Nintendo 3DS port
// Shared state between the engine-facing glue files.

#ifndef __N3DS_GLUE_H__
#define __N3DS_GLUE_H__

#include "../doomtype.h"

extern boolean n3ds_bottomlog;

void N3DS_FirstFrameShown(void);  // i_system.c
void N3DS_ShowTouchLegend(void);  // i_video.c

#endif
