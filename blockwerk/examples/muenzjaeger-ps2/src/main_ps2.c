/* Blockwerk PS2 entry point: gsKit rendering + libpad input. */
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <stdio.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <libpad.h>
#include "rt.h"

static char s_padbuf[256] __attribute__((aligned(64)));
static int s_pad_ok;

static void pad_init(void)
{
  int state, tries = 0;
  SifInitRpc(0);
  if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0) return;
  if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0) return;
  padInit(0);
  if (padPortOpen(0, 0, s_padbuf) == 0) return;
  do {
    state = padGetState(0, 0);
  } while (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1 && ++tries < 200000);
  s_pad_ok = 1;
}

static unsigned int pad_read(void)
{
  static const unsigned short map[BTN_COUNT] = {
    PAD_CROSS, PAD_CIRCLE, PAD_SQUARE, PAD_TRIANGLE,
    PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
    PAD_START, PAD_SELECT, PAD_L1, PAD_R1
  };
  struct padButtonStatus st;
  unsigned int held = 0, data;
  int b, state;
  if (!s_pad_ok) return 0;
  state = padGetState(0, 0);
  if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) return 0;
  if (padRead(0, 0, &st) == 0) return 0;
  data = 0xffff ^ st.btns;
  for (b = 0; b < BTN_COUNT; b++)
    if (data & map[b]) held |= 1u << b;
  return held;
}

static GSGLOBAL *s_gs;

static int zval(float z) /* z: 1 near .. 0 far -> 24 bit, bigger is closer */
{
  int v = (int)(z * 16777000.0f);
  if (v < 1) v = 1;
  if (v > 16777215) v = 16777215;
  return v;
}

static void emit(float x1, float y1, float z1, float x2, float y2, float z2,
                 float x3, float y3, float z3, unsigned int rgb, void *user)
{
  u64 c = GS_SETREG_RGBAQ((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 0x80, 0x00);
  (void)user;
  gsKit_prim_triangle_3d(s_gs, x1, y1, zval(z1), x2, y2, zval(z2), x3, y3, zval(z3), c);
}

int main(int argc, char *argv[])
{
  GSFONTM *font;
  int i;
  (void)argc; (void)argv;

  s_gs = gsKit_init_global();
  s_gs->PSM = GS_PSM_CT24;
  s_gs->PSMZ = GS_PSMZ_24;
  s_gs->ZBuffering = GS_SETTING_ON;
  s_gs->DoubleBuffering = GS_SETTING_ON;
  s_gs->PrimAlphaEnable = GS_SETTING_ON;

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
              D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);

  gsKit_init_screen(s_gs);
  gsKit_mode_switch(s_gs, GS_ONESHOT);
  gsKit_set_test(s_gs, GS_ZTEST_ON);
  gsKit_set_primalpha(s_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  font = gsKit_init_fontm();
  gsKit_fontm_upload(s_gs, font);

  pad_init();
  bw_mesh_init();
  rt_reset();

  for (;;) {
    float y = 16.0f;
    rt_frame(pad_read());

    gsKit_clear(s_gs, GS_SETREG_RGBAQ((GAME_SKY >> 16) & 255, (GAME_SKY >> 8) & 255, GAME_SKY & 255, 0x80, 0x00));
    bw_render((float)s_gs->Width, (float)s_gs->Height, emit, 0);

    for (i = 0; i < g_nobjs; i++) {
      if (g_objs[i].hud[0]) {
        gsKit_fontm_print_scaled(s_gs, font, 20.0f, y, 0xFFFFFF, 0.6f,
                                 GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00), g_objs[i].hud);
        y += 22.0f;
      }
    }

    gsKit_queue_exec(s_gs);
    gsKit_sync_flip(s_gs);
    gsKit_TexManager_nextFrame(s_gs);
  }
  return 0;
}
