/* Blockwerk software transform + clipping. Platform independent:
 * produces flat shaded screen-space triangles through a callback. */
#include <math.h>
#include "rt.h"

#define MAX_TRIS 512
typedef struct { float v[3][3]; } Tri;
static Tri s_mesh[SHAPE_COUNT][MAX_TRIS];
static int s_ntris[SHAPE_COUNT];

static void add_tri(int shape, const float *a, const float *b, const float *c)
{
  int n = s_ntris[shape], k;
  if (n >= MAX_TRIS) return;
  for (k = 0; k < 3; k++) {
    s_mesh[shape][n].v[0][k] = a[k];
    s_mesh[shape][n].v[1][k] = b[k];
    s_mesh[shape][n].v[2][k] = c[k];
  }
  s_ntris[shape] = n + 1;
}
static void add_quad(int shape, const float *a, const float *b, const float *c, const float *d)
{
  add_tri(shape, a, b, c);
  add_tri(shape, a, c, d);
}

void bw_mesh_init(void)
{
  int i, j, f;
  const float PI = 3.14159265f;
  for (i = 0; i < SHAPE_COUNT; i++) s_ntris[i] = 0;

  /* cube 1x1x1 */
  for (f = 0; f < 6; f++) {
    int ax = f % 3; float s = f < 3 ? 0.5f : -0.5f;
    int u = (ax + 1) % 3, w = (ax + 2) % 3;
    float p[4][3];
    for (i = 0; i < 4; i++) {
      p[i][ax] = s;
      p[i][u] = (i == 1 || i == 2) ? 0.5f : -0.5f;
      p[i][w] = (i >= 2) ? 0.5f : -0.5f;
    }
    add_quad(SHAPE_CUBE, p[0], p[1], p[2], p[3]);
  }

  /* plane 1x1 on XZ, 4x4 grid (smaller triangles clip better) */
  for (i = 0; i < 4; i++)
    for (j = 0; j < 4; j++) {
      float x0 = -0.5f + i * 0.25f, x1 = x0 + 0.25f;
      float z0 = -0.5f + j * 0.25f, z1 = z0 + 0.25f;
      float a[3] = {x0, 0, z0}, b[3] = {x1, 0, z0}, c[3] = {x1, 0, z1}, d[3] = {x0, 0, z1};
      add_quad(SHAPE_PLANE, a, b, c, d);
    }

  /* sphere r=0.5, 12 segments x 8 rings */
  {
    const int SEG = 12, RING = 8;
    for (j = 0; j < RING; j++) {
      float t0 = PI * j / RING, t1 = PI * (j + 1) / RING;
      for (i = 0; i < SEG; i++) {
        float p0 = 2 * PI * i / SEG, p1 = 2 * PI * (i + 1) / SEG;
        float a[3] = {0.5f * sinf(t0) * cosf(p0), 0.5f * cosf(t0), 0.5f * sinf(t0) * sinf(p0)};
        float b[3] = {0.5f * sinf(t0) * cosf(p1), 0.5f * cosf(t0), 0.5f * sinf(t0) * sinf(p1)};
        float c[3] = {0.5f * sinf(t1) * cosf(p1), 0.5f * cosf(t1), 0.5f * sinf(t1) * sinf(p1)};
        float d[3] = {0.5f * sinf(t1) * cosf(p0), 0.5f * cosf(t1), 0.5f * sinf(t1) * sinf(p0)};
        if (j == 0) add_tri(SHAPE_SPHERE, a, c, d);
        else if (j == RING - 1) add_tri(SHAPE_SPHERE, a, b, c);
        else add_quad(SHAPE_SPHERE, a, b, c, d);
      }
    }
  }

  /* cylinder r=0.5 h=1, 12 segments */
  {
    const int SEG = 12;
    float top[3] = {0, 0.5f, 0}, bot[3] = {0, -0.5f, 0};
    for (i = 0; i < SEG; i++) {
      float p0 = 2 * PI * i / SEG, p1 = 2 * PI * (i + 1) / SEG;
      float a[3] = {0.5f * cosf(p0), 0.5f, 0.5f * sinf(p0)};
      float b[3] = {0.5f * cosf(p1), 0.5f, 0.5f * sinf(p1)};
      float c[3] = {0.5f * cosf(p1), -0.5f, 0.5f * sinf(p1)};
      float d[3] = {0.5f * cosf(p0), -0.5f, 0.5f * sinf(p0)};
      add_quad(SHAPE_CYLINDER, a, b, c, d);
      add_tri(SHAPE_CYLINDER, top, b, a);
      add_tri(SHAPE_CYLINDER, bot, d, c);
    }
  }
}

/* ---- math ---- */
typedef struct { float m[3][4]; } Mat; /* rows: 3x3 rotation/scale + translation */

static void obj_matrix(const Obj *o, Mat *M)
{
  /* three.js Euler order XYZ: R = Rx * Ry * Rz, then M = T * R * S */
  const float d = 0.017453292519943295f;
  float cx = cosf(o->rot[0] * d), sx = sinf(o->rot[0] * d);
  float cy = cosf(o->rot[1] * d), sy = sinf(o->rot[1] * d);
  float cz = cosf(o->rot[2] * d), sz = sinf(o->rot[2] * d);
  float R[3][3];
  int r, c;
  R[0][0] = cy * cz;               R[0][1] = -cy * sz;              R[0][2] = sy;
  R[1][0] = cx * sz + sx * sy * cz; R[1][1] = cx * cz - sx * sy * sz; R[1][2] = -sx * cy;
  R[2][0] = sx * sz - cx * sy * cz; R[2][1] = sx * cz + cx * sy * sz; R[2][2] = cx * cy;
  for (r = 0; r < 3; r++) {
    for (c = 0; c < 3; c++) M->m[r][c] = R[r][c] * o->scale[c];
    M->m[r][3] = o->pos[r];
  }
}

static void xform(const Mat *M, const float *v, float *out)
{
  int r;
  for (r = 0; r < 3; r++)
    out[r] = M->m[r][0] * v[0] + M->m[r][1] * v[1] + M->m[r][2] * v[2] + M->m[r][3];
}

static void normalize(float *v)
{
  float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void cross(const float *a, const float *b, float *o)
{
  o[0] = a[1] * b[2] - a[2] * b[1];
  o[1] = a[2] * b[0] - a[0] * b[2];
  o[2] = a[0] * b[1] - a[1] * b[0];
}
static float dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* ---- camera ---- */
static float s_view[3][4];
static const float NEAR_Z = 0.1f, FAR_Z = 500.0f;
static float s_kx, s_ky; /* tan(half fov) */

static void make_view(void)
{
  float f[3], r[3], u[3], up[3] = {0, 1, 0};
  int k;
  for (k = 0; k < 3; k++) f[k] = g_cam_target[k] - g_cam_pos[k];
  normalize(f);
  if (fabsf(f[1]) > 0.999f) { up[1] = 0; up[2] = -1; }
  cross(f, up, r); normalize(r);
  cross(r, f, u);
  /* view space: x=right, y=up, -z=forward */
  for (k = 0; k < 3; k++) { s_view[0][k] = r[k]; s_view[1][k] = u[k]; s_view[2][k] = -f[k]; }
  s_view[0][3] = -dot(r, g_cam_pos);
  s_view[1][3] = -dot(u, g_cam_pos);
  s_view[2][3] = dot(f, g_cam_pos);
}

static void to_view(const float *w, float *v)
{
  int r;
  for (r = 0; r < 3; r++)
    v[r] = s_view[r][0] * w[0] + s_view[r][1] * w[1] + s_view[r][2] * w[2] + s_view[r][3];
}

/* clip against near plane and guard-band side planes (keep d >= 0) */
#define GUARD 1.6f
static float plane_dist(int p, const float *v)
{
  float depth = -v[2];
  switch (p) {
    case 0: return depth - NEAR_Z;
    case 1: return GUARD * s_kx * depth - v[0];
    case 2: return GUARD * s_kx * depth + v[0];
    case 3: return GUARD * s_ky * depth - v[1];
    case 4: return GUARD * s_ky * depth + v[1];
    default: return FAR_Z - depth;
  }
}

static int clip_poly(float in[][3], int n, float out[][3])
{
  int p, i, k;
  float buf[16][3];
  float (*src)[3] = in, (*dst)[3] = buf;
  for (p = 0; p < 6; p++) {
    int m = 0;
    dst = (p % 2 == 0) ? buf : out;
    for (i = 0; i < n; i++) {
      const float *a = src[i], *b = src[(i + 1) % n];
      float da = plane_dist(p, a), db = plane_dist(p, b);
      if (da >= 0) { for (k = 0; k < 3; k++) dst[m][k] = a[k]; m++; }
      if ((da >= 0) != (db >= 0)) {
        float t = da / (da - db);
        for (k = 0; k < 3; k++) dst[m][k] = a[k] + (b[k] - a[k]) * t;
        m++;
      }
      if (m >= 14) break;
    }
    n = m;
    if (n < 3) return 0;
    src = dst;
  }
  if (src != out)
    for (i = 0; i < n; i++) for (k = 0; k < 3; k++) out[i][k] = src[i][k];
  return n;
}

void bw_render(float width, float height, bw_emit_fn emit, void *user)
{
  int i, t, k;
  float light[3] = {0.45f, 1.0f, 0.3f};
  normalize(light);
  s_ky = tanf(30.0f * 0.017453292519943295f); /* 60 deg vertical fov like the editor */
  s_kx = s_ky * (4.0f / 3.0f); /* TV aspect is 4:3 regardless of framebuffer size */
  make_view();

  for (i = 0; i < g_nobjs; i++) {
    const Obj *o = &g_objs[i];
    Mat M;
    int shape = o->shape;
    if (!o->visible || shape < 0 || shape >= SHAPE_COUNT) continue;
    obj_matrix(o, &M);
    for (t = 0; t < s_ntris[shape]; t++) {
      float w[3][3], e1[3], e2[3], n[3], tocam[3], poly[16][3], out[16][3];
      float lum, sx[16], sy[16], sz[16];
      int np, r, g, b;
      unsigned int rgb;
      for (k = 0; k < 3; k++) xform(&M, s_mesh[shape][t].v[k], w[k]);
      for (k = 0; k < 3; k++) { e1[k] = w[1][k] - w[0][k]; e2[k] = w[2][k] - w[0][k]; }
      cross(e1, e2, n);
      normalize(n);
      for (k = 0; k < 3; k++) tocam[k] = g_cam_pos[k] - w[0][k];
      if (shape == SHAPE_PLANE) {
        if (dot(n, tocam) < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
      } else {
        float out_dir[3];
        for (k = 0; k < 3; k++) out_dir[k] = w[0][k] - o->pos[k];
        if (dot(n, out_dir) < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        if (dot(n, tocam) <= 0) continue; /* back face */
      }
      lum = 0.4f + 0.6f * (dot(n, light) > 0 ? dot(n, light) : 0);
      r = (int)(((o->color >> 16) & 255) * lum);
      g = (int)(((o->color >> 8) & 255) * lum);
      b = (int)((o->color & 255) * lum);
      rgb = ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;

      for (k = 0; k < 3; k++) to_view(w[k], poly[k]);
      np = clip_poly(poly, 3, out);
      for (k = 0; k < np; k++) {
        float depth = -out[k][2];
        sx[k] = width * 0.5f + (out[k][0] / (depth * s_kx)) * width * 0.5f;
        sy[k] = height * 0.5f - (out[k][1] / (depth * s_ky)) * height * 0.5f;
        sz[k] = NEAR_Z / depth; /* 1 = near, ~0 = far */
      }
      for (k = 1; k + 1 < np; k++)
        emit(sx[0], sy[0], sz[0], sx[k], sy[k], sz[k], sx[k + 1], sy[k + 1], sz[k + 1], rgb, user);
    }
  }
}
