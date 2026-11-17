/* Blockwerk runtime - platform independent game logic.
 * Generated projects: game.c holds the scene and the compiled block scripts. */
#ifndef BW_RT_H
#define BW_RT_H

#define BW_MAX_OBJS 128
#define BW_HUD_LEN 64

enum { SHAPE_CUBE, SHAPE_SPHERE, SHAPE_PLANE, SHAPE_CYLINDER, SHAPE_COUNT };
enum {
  BTN_CROSS, BTN_CIRCLE, BTN_SQUARE, BTN_TRIANGLE,
  BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
  BTN_START, BTN_SELECT, BTN_L1, BTN_R1, BTN_COUNT
};

typedef struct Obj {
  const char *name;
  int shape;
  float pos[3], rot[3], scale[3], prev[3];
  unsigned int color; /* 0xRRGGBB */
  int visible;
  char hud[BW_HUD_LEN];
} Obj;

typedef struct {
  void (*start)(Obj *self);
  void (*tick)(Obj *self);
  void (*press)(Obj *self, int btn);
  void (*collide)(Obj *self, Obj *other);
} Script;

typedef struct {
  const char *name;
  int shape;
  float pos[3], rot[3], scale[3];
  unsigned int color;
} ObjInit;

/* provided by game.c (generated) */
extern const int GAME_NOBJS;
extern const ObjInit GAME_OBJS[];
extern const Script GAME_SCRIPTS[];
extern const unsigned int GAME_SKY;
extern const float GAME_CAM_POS[3];
extern const float GAME_CAM_TARGET[3];
extern const char GAME_NAME[];
void game_reset_vars(void);

/* runtime state */
extern Obj g_objs[BW_MAX_OBJS];
extern int g_nobjs;
extern float g_cam_pos[3], g_cam_target[3];

void rt_reset(void);
void rt_frame(unsigned int held_mask); /* one 1/60 s step */

/* block API */
void rt_move(Obj *o, float x, float y, float z);
void rt_setpos(Obj *o, float x, float y, float z);
void rt_rotate(Obj *o, float x, float y, float z);
void rt_setrot(Obj *o, float x, float y, float z);
void rt_forward(Obj *o, float n);
void rt_setscale(Obj *o, float x, float y, float z);
void rt_setcolor(Obj *o, unsigned int rgb);
void rt_setvisible(Obj *o, int v);
void rt_hud(Obj *o, const char *text, float value);
void rt_hudtext(Obj *o, const char *text);
void rt_undo(Obj *o);
void rt_camfollow(Obj *o, float dist, float height);
void rt_campos(float x, float y, float z);
void rt_camlook(Obj *o);
void rt_restart(void);
int rt_held(int btn);
float rt_get(Obj *o, int what); /* 0..2 pos, 3..5 rot, 6..8 scale */
Obj *rt_find(const char *name);
float rt_distance(Obj *a, Obj *b);
int rt_touching(Obj *a, Obj *b);
int rt_touching_any(Obj *a);
float rt_time(void);
float rt_randint(float a, float b);
float rt_div(float a, float b);
float rt_mod(float a, float b);

/* renderer (render.c): calls emit for each screen-space triangle */
typedef void (*bw_emit_fn)(float x1, float y1, float z1,
                           float x2, float y2, float z2,
                           float x3, float y3, float z3,
                           unsigned int rgb, void *user);
void bw_render(float width, float height, bw_emit_fn emit, void *user);
void bw_mesh_init(void);

#endif
