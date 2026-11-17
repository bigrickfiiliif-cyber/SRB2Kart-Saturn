/* PC test driver: runs the game logic headless, prints the result and can
 * write a PPM screenshot rendered with the same 3D code the PS2 uses.
 *   cc -O2 -Isrc -o game_test src/main_host.c src/rt.c src/render.c src/game.c -lm
 *   ./game_test 600 shot.ppm */
#include <stdio.h>
#include <stdlib.h>
#include "rt.h"

#define W 640
#define H 448
static unsigned char s_rgb[H][W][3];
static float s_z[H][W];
static int s_tris;

static float edge(float ax, float ay, float bx, float by, float px, float py)
{ return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

static void raster(float x1, float y1, float z1, float x2, float y2, float z2,
                   float x3, float y3, float z3, unsigned int rgb, void *u)
{
  int x, y, x0, x1i, y0, y1i;
  float area = edge(x1, y1, x2, y2, x3, y3);
  (void)u;
  s_tris++;
  if (area == 0) return;
  x0 = (int)(x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3)); if (x0 < 0) x0 = 0;
  x1i = (int)(x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3)) + 1; if (x1i > W) x1i = W;
  y0 = (int)(y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3)); if (y0 < 0) y0 = 0;
  y1i = (int)(y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3)) + 1; if (y1i > H) y1i = H;
  for (y = y0; y < y1i; y++)
    for (x = x0; x < x1i; x++) {
      float px = x + 0.5f, py = y + 0.5f;
      float w0 = edge(x2, y2, x3, y3, px, py) / area, w1 = edge(x3, y3, x1, y1, px, py) / area, w2 = 1 - w0 - w1;
      float z;
      if (w0 < 0 || w1 < 0 || w2 < 0) continue;
      z = w0 * z1 + w1 * z2 + w2 * z3;
      if (z <= s_z[y][x]) continue; /* bigger = closer, like the PS2 z test */
      s_z[y][x] = z;
      s_rgb[y][x][0] = (rgb >> 16) & 255; s_rgb[y][x][1] = (rgb >> 8) & 255; s_rgb[y][x][2] = rgb & 255;
    }
}

int main(int argc, char **argv)
{
  int frames = argc > 1 ? atoi(argv[1]) : 300, f, i, x, y;
  bw_mesh_init();
  rt_reset();
  for (f = 0; f < frames; f++) {
    /* scripted input: drive forward, steer in turns, tap CROSS every 30 frames */
    unsigned int held = 0;
    if (f < frames * 4 / 5) held |= 1u << BTN_UP;
    if ((f / 40) % 3 == 0) held |= 1u << BTN_LEFT;
    if ((f / 40) % 3 == 2) held |= 1u << BTN_RIGHT;
    if (f % 30 < 2) held |= 1u << BTN_CROSS;
    rt_frame(held);
  }
  for (y = 0; y < H; y++)
    for (x = 0; x < W; x++) {
      s_z[y][x] = 0;
      s_rgb[y][x][0] = (GAME_SKY >> 16) & 255; s_rgb[y][x][1] = (GAME_SKY >> 8) & 255; s_rgb[y][x][2] = GAME_SKY & 255;
    }
  bw_render(W, H, raster, 0);
  printf("%s: %d frames, %d triangles drawn\n", GAME_NAME, frames, s_tris);
  for (i = 0; i < g_nobjs; i++)
    printf("  %s pos=(%.2f, %.2f, %.2f) rotY=%.1f visible=%d hud='%s'\n", g_objs[i].name,
           g_objs[i].pos[0], g_objs[i].pos[1], g_objs[i].pos[2], g_objs[i].rot[1],
           g_objs[i].visible, g_objs[i].hud);
  if (argc > 2) {
    FILE *fp = fopen(argv[2], "wb");
    if (fp) { fprintf(fp, "P6\n%d %d\n255\n", W, H); fwrite(s_rgb, 1, sizeof(s_rgb), fp); fclose(fp); printf("wrote %s\n", argv[2]); }
  }
  return 0;
}
