#include <math.h>
#include <string.h>
#include <stdio.h>
#include "rt.h"

Obj g_objs[BW_MAX_OBJS];
int g_nobjs;
float g_cam_pos[3], g_cam_target[3];

static unsigned int s_prev_held, s_held;
static float s_time;
static int s_restart;
static unsigned char s_touch[BW_MAX_OBJS][BW_MAX_OBJS];
static unsigned int s_rand = 12345u;

#define DEG2RAD 0.017453292519943295f

static void run_starts(void)
{
  int i;
  for (i = 0; i < g_nobjs; i++)
    if (GAME_SCRIPTS[i].start) GAME_SCRIPTS[i].start(&g_objs[i]);
}

void rt_reset(void)
{
  int i;
  g_nobjs = GAME_NOBJS > BW_MAX_OBJS ? BW_MAX_OBJS : GAME_NOBJS;
  for (i = 0; i < g_nobjs; i++) {
    const ObjInit *in = &GAME_OBJS[i];
    Obj *o = &g_objs[i];
    memset(o, 0, sizeof(*o));
    o->name = in->name;
    o->shape = in->shape;
    memcpy(o->pos, in->pos, sizeof(o->pos));
    memcpy(o->prev, in->pos, sizeof(o->prev));
    memcpy(o->rot, in->rot, sizeof(o->rot));
    memcpy(o->scale, in->scale, sizeof(o->scale));
    o->color = in->color;
    o->visible = 1;
  }
  memcpy(g_cam_pos, GAME_CAM_POS, sizeof(g_cam_pos));
  memcpy(g_cam_target, GAME_CAM_TARGET, sizeof(g_cam_target));
  memset(s_touch, 0, sizeof(s_touch));
  s_time = 0.0f;
  s_restart = 0;
  s_prev_held = s_held;
  game_reset_vars();
  run_starts();
}

static void half_ext(const Obj *o, float h[3])
{
  int k;
  for (k = 0; k < 3; k++) h[k] = fabsf(o->scale[k]) * 0.5f;
  if (o->shape == SHAPE_PLANE) h[1] = 0.05f;
}

int rt_touching(Obj *a, Obj *b)
{
  float ha[3], hb[3];
  int k;
  if (!a || !b || a == b || !a->visible || !b->visible) return 0;
  half_ext(a, ha);
  half_ext(b, hb);
  for (k = 0; k < 3; k++)
    if (fabsf(a->pos[k] - b->pos[k]) > ha[k] + hb[k]) return 0;
  return 1;
}

int rt_touching_any(Obj *a)
{
  int i;
  for (i = 0; i < g_nobjs; i++)
    if (rt_touching(a, &g_objs[i])) return 1;
  return 0;
}

void rt_frame(unsigned int held_mask)
{
  int i, j, b;
  unsigned int pressed;
  s_held = held_mask;
  pressed = s_held & ~s_prev_held;
  s_prev_held = s_held;

  for (i = 0; i < g_nobjs; i++)
    memcpy(g_objs[i].prev, g_objs[i].pos, sizeof(g_objs[i].pos));

  for (i = 0; i < g_nobjs; i++) {
    const Script *s = &GAME_SCRIPTS[i];
    if (s->press && pressed)
      for (b = 0; b < BTN_COUNT; b++)
        if (pressed & (1u << b)) s->press(&g_objs[i], b);
    if (s->tick) s->tick(&g_objs[i]);
  }

  for (i = 0; i < g_nobjs; i++) {
    for (j = i + 1; j < g_nobjs; j++) {
      int t = rt_touching(&g_objs[i], &g_objs[j]);
      if (t && !s_touch[i][j]) {
        s_touch[i][j] = 1;
        if (GAME_SCRIPTS[i].collide) GAME_SCRIPTS[i].collide(&g_objs[i], &g_objs[j]);
        if (GAME_SCRIPTS[j].collide) GAME_SCRIPTS[j].collide(&g_objs[j], &g_objs[i]);
        /* a handler may have moved the objects apart (e.g. "gehe zurück") */
        s_touch[i][j] = (unsigned char)rt_touching(&g_objs[i], &g_objs[j]);
      } else if (!t) {
        s_touch[i][j] = 0;
      }
    }
  }

  s_time += 1.0f / 60.0f;
  if (s_restart) rt_reset();
}

void rt_move(Obj *o, float x, float y, float z) { if (!o) return; o->pos[0] += x; o->pos[1] += y; o->pos[2] += z; }
void rt_setpos(Obj *o, float x, float y, float z) { if (!o) return; o->pos[0] = x; o->pos[1] = y; o->pos[2] = z; }
void rt_rotate(Obj *o, float x, float y, float z) { if (!o) return; o->rot[0] += x; o->rot[1] += y; o->rot[2] += z; }
void rt_setrot(Obj *o, float x, float y, float z) { if (!o) return; o->rot[0] = x; o->rot[1] = y; o->rot[2] = z; }
void rt_forward(Obj *o, float n)
{
  float yaw;
  if (!o) return;
  yaw = o->rot[1] * DEG2RAD;
  o->pos[0] += -sinf(yaw) * n;
  o->pos[2] += -cosf(yaw) * n;
}
void rt_setscale(Obj *o, float x, float y, float z) { if (!o) return; o->scale[0] = x; o->scale[1] = y; o->scale[2] = z; }
void rt_setcolor(Obj *o, unsigned int rgb) { if (o) o->color = rgb; }
void rt_setvisible(Obj *o, int v) { if (o) o->visible = v ? 1 : 0; }
void rt_hud(Obj *o, const char *text, float value)
{
  if (!o) return;
  if (value == (float)(int)value) snprintf(o->hud, BW_HUD_LEN, "%s %d", text, (int)value);
  else snprintf(o->hud, BW_HUD_LEN, "%s %.2f", text, value);
}
void rt_hudtext(Obj *o, const char *text) { if (o) snprintf(o->hud, BW_HUD_LEN, "%s", text); }
void rt_undo(Obj *o) { if (o) memcpy(o->pos, o->prev, sizeof(o->pos)); }
void rt_camfollow(Obj *o, float dist, float height)
{
  float yaw;
  if (!o) return;
  yaw = o->rot[1] * DEG2RAD;
  g_cam_pos[0] = o->pos[0] + sinf(yaw) * dist;
  g_cam_pos[1] = o->pos[1] + height;
  g_cam_pos[2] = o->pos[2] + cosf(yaw) * dist;
  memcpy(g_cam_target, o->pos, sizeof(g_cam_target));
}
void rt_campos(float x, float y, float z) { g_cam_pos[0] = x; g_cam_pos[1] = y; g_cam_pos[2] = z; }
void rt_camlook(Obj *o) { if (o) memcpy(g_cam_target, o->pos, sizeof(g_cam_target)); }
void rt_restart(void) { s_restart = 1; }
int rt_held(int btn) { return (s_held >> btn) & 1u; }
float rt_get(Obj *o, int what)
{
  if (!o) return 0.0f;
  if (what < 3) return o->pos[what];
  if (what < 6) return o->rot[what - 3];
  if (what < 9) return o->scale[what - 6];
  return 0.0f;
}
Obj *rt_find(const char *name)
{
  int i;
  for (i = 0; i < g_nobjs; i++)
    if (strcmp(g_objs[i].name, name) == 0) return &g_objs[i];
  return 0;
}
float rt_distance(Obj *a, Obj *b)
{
  float dx, dy, dz;
  if (!a || !b) return 0.0f;
  dx = a->pos[0] - b->pos[0]; dy = a->pos[1] - b->pos[1]; dz = a->pos[2] - b->pos[2];
  return sqrtf(dx * dx + dy * dy + dz * dz);
}
float rt_time(void) { return s_time; }
float rt_randint(float a, float b)
{
  int lo = (int)floorf(a < b ? a : b), hi = (int)floorf(a < b ? b : a);
  s_rand = s_rand * 1103515245u + 12345u;
  return (float)(lo + (int)((s_rand >> 8) % (unsigned int)(hi - lo + 1)));
}
float rt_div(float a, float b) { return b == 0.0f ? 0.0f : a / b; }
float rt_mod(float a, float b) { return b == 0.0f ? 0.0f : fmodf(a, b); }
