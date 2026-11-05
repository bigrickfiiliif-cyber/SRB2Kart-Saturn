// SRB2Kart Saturn - Nintendo 3DS port
// Hardware renderer backend for the PICA200 GPU (citro3d).
//
// Saturn's hardware renderer (src/hardware) turns the level into polygons and
// talks to the GPU through the small GL_* interface in hw_gl.h. On PC that is
// r_opengl.c; this file implements the same interface with citro3d.
//
// Notes:
//  * Matrices are kept exactly like OpenGL's (column-major, same operations),
//    because hw_clip.c reads projMatrix / modelMatrix for its own clipping.
//    Only when uploading do we tilt them for the sideways 3DS screen and remap
//    depth from GL's [-1,1] to the PICA's [-1,0] (depth test is reversed).
//  * The per-polygon colour (lighting/tint) is a TEV constant colour.
//  * PICA textures must be power-of-two and tiled: non-POT textures are
//    resampled (nearest) to the next power of two so wrapping still works.
//  * Not implemented yet (the engine has fallbacks): GLSL shaders, stencil
//    portals, sky dome, 3D models, screen textures (wipes/VHS/post effects).

#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../doomdef.h"
#include "../hardware/hw_gl.h"
#include "../hardware/r_opengl/r_opengl.h"

#include "vshader_shbin.h"
#include "r_citro3d.h"

// ---------------------------------------------------------- GL globals -----
// (declared in r_opengl.h and used by the shared hardware renderer)

const GLubyte *gl_version    = (const GLubyte *)"PICA200 (citro3d)";
const GLubyte *gl_renderer   = (const GLubyte *)"Nintendo 3DS GPU";
const GLubyte *gl_extensions = (const GLubyte *)"";
const GLubyte *gl_vendor     = (const GLubyte *)"DMP";
GLuint gl_num_extensions = 0;
int majorGL = 1, minorGL = 1;

RGBA_t  myPaletteData[256];
GLint   screen_width = 0;
GLint   screen_height = 0;
GLbyte  screen_depth = 0;
GLint   maximumAnisotropy = 0;
boolean supportNPO2tex = false;
boolean supportMipMap = false;
boolean supportstencil = false;

GLfloat modelMatrix[16];
GLfloat projMatrix[16];

#ifdef DEBUG_TO_FILE
FILE *gllogstream = NULL;
#endif

#define ASPECT_RATIO 1.0f
static float NEAR_CLIPPING_PLANE = NZCLIP_PLANE;
static float FAR_CLIPPING_PLANE  = 32768.0f;
static float fov = 90.0f;

// ------------------------------------------------------------- matrices ----

static void m_identity(float *m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// r = a * b (column-major, like OpenGL)
static void m_mul(float *r, const float *a, const float *b)
{
	float t[16];
	int c, rr;
	for (c = 0; c < 4; c++)
		for (rr = 0; rr < 4; rr++)
			t[c*4+rr] = a[0*4+rr]*b[c*4+0] + a[1*4+rr]*b[c*4+1] + a[2*4+rr]*b[c*4+2] + a[3*4+rr]*b[c*4+3];
	memcpy(r, t, sizeof t);
}

static void m_scale(float *m, float x, float y, float z)
{
	float s[16];
	m_identity(s);
	s[0] = x; s[5] = y; s[10] = z;
	m_mul(m, m, s);
}

static void m_translate(float *m, float x, float y, float z)
{
	float t[16];
	m_identity(t);
	t[12] = x; t[13] = y; t[14] = z;
	m_mul(m, m, t);
}

static void m_rotate(float *m, float angle_deg, float x, float y, float z)
{
	float r[16];
	float a = angle_deg * (float)M_PI / 180.0f;
	float c = cosf(a), s = sinf(a), ic = 1.0f - c;
	float len = sqrtf(x*x + y*y + z*z);

	if (len < 1e-12f)
		return;
	x /= len; y /= len; z /= len;

	m_identity(r);
	r[0] = x*x*ic + c;   r[4] = x*y*ic - z*s; r[8]  = x*z*ic + y*s;
	r[1] = y*x*ic + z*s; r[5] = y*y*ic + c;   r[9]  = y*z*ic - x*s;
	r[2] = x*z*ic - y*s; r[6] = y*z*ic + x*s; r[10] = z*z*ic + c;
	m_mul(m, m, r);
}

// Same matrix as r_opengl.c's GL_Perspective (multiplied onto m).
static void m_perspective(float *m, float fovy, float aspect)
{
	float p[16];
	const float deltaZ = FAR_CLIPPING_PLANE - NEAR_CLIPPING_PLANE;
	float focallength;

	if (fabsf(deltaZ) < 1.0E-36f || aspect == 0.0f)
		return;

	focallength = 1.0f / tanf(fovy * (float)M_PI / 360.0f);

	memset(p, 0, sizeof p);
	p[0]  = focallength / aspect;
	p[5]  = focallength;
	p[10] = -(FAR_CLIPPING_PLANE + NEAR_CLIPPING_PLANE) / deltaZ;
	p[11] = -1.0f;
	p[14] = -2.0f * NEAR_CLIPPING_PLANE * FAR_CLIPPING_PLANE / deltaZ;
	m_mul(m, m, p);
}

// ---------------------------------------------------------------- state ----

#define VBO_MAX_VERTS  60000            // u16 indices
#define IBO_MAX        (VBO_MAX_VERTS * 3)

typedef struct
{
	float x, y, z, s, t;
} c3dvert_t;

static int            c3d_started = 0;
static C3D_RenderTarget *target = NULL;
static DVLB_s         *vshader_dvlb = NULL;
static shaderProgram_s program;
static int            uLoc_projection;

static c3dvert_t     *vbo = NULL;
static u16           *ibo = NULL;
static u32            vbo_used = 0, ibo_used = 0;
static int            in_frame = 0;

static float          mv[16], pj[16];
static int            matrix_dirty = 1;

static FBITFIELD      CurrentPolyFlags = 0xffffffff;
static RGBA_t         cur_color;
static int            cur_textured = 0;
static int            tev_textured = -1, tev_modulated = -1;
static u32            tev_color = 0;
static int            tev_valid = 0;

static FRGBAFloat     clear_color = {0, 0, 0, 1};
static int            drew_this_frame = 0;

static void flush_buffers(void)
{
	if (vbo_used)
		GSPGPU_FlushDataCache(vbo, vbo_used * sizeof(c3dvert_t));
	if (ibo_used)
		GSPGPU_FlushDataCache(ibo, ibo_used * sizeof(u16));
}

// ------------------------------------------------------------- textures ----

typedef struct
{
	C3D_Tex     tex;
	GLMipmap_t *owner;
	int         used;
	u32         bytes;
	GPU_TEXTURE_WRAP_PARAM wrap_s, wrap_t;
} c3dtex_t;

static c3dtex_t *textures = NULL;
static UINT32    num_textures = 0, max_textures = 0;
static UINT32    tex_bound = 0;      // index+1 of currently bound texture, 0 = none
static u32       tex_bytes_total = 0;
static GPU_TEXTURE_FILTER_PARAM tex_filter = GPU_NEAREST;

static UINT32 alloc_texture_slot(void)
{
	UINT32 i;
	for (i = 0; i < num_textures; i++)
		if (!textures[i].used)
			return i + 1;

	if (num_textures == max_textures)
	{
		UINT32 nmax = max_textures ? max_textures * 2 : 1024;
		c3dtex_t *n = realloc(textures, nmax * sizeof(c3dtex_t));
		if (!n)
			I_Error("GPU: out of memory for texture table");
		memset(n + max_textures, 0, (nmax - max_textures) * sizeof(c3dtex_t));
		textures = n;
		max_textures = nmax;
	}
	return ++num_textures;
}

// Textures freed while a frame is being recorded may still be referenced by
// commands the GPU hasn't run yet; delete them once that frame is done.
static C3D_Tex *pending_free = NULL;
static UINT32   num_pending = 0, max_pending = 0;

static void defer_tex_delete(C3D_Tex *tex)
{
	if (num_pending == max_pending)
	{
		UINT32 nmax = max_pending ? max_pending * 2 : 256;
		C3D_Tex *n = realloc(pending_free, nmax * sizeof(C3D_Tex));
		if (!n)
		{
			C3D_FrameSync(); // last resort: wait for the GPU, delete now
			C3D_TexDelete(tex);
			return;
		}
		pending_free = n;
		max_pending = nmax;
	}
	pending_free[num_pending++] = *tex;
}

static void process_pending_frees(void)
{
	UINT32 i;
	for (i = 0; i < num_pending; i++)
		C3D_TexDelete(&pending_free[i]);
	num_pending = 0;
}

static void free_texture(UINT32 id)
{
	c3dtex_t *t;
	if (!id || id > num_textures)
		return;
	t = &textures[id - 1];
	if (!t->used)
		return;
	if (c3d_started)
		defer_tex_delete(&t->tex);
	else
		C3D_TexDelete(&t->tex);
	tex_bytes_total -= t->bytes;
	if (t->owner && t->owner->downloaded == id)
		t->owner->downloaded = 0;
	memset(t, 0, sizeof *t);
	if (tex_bound == id)
		tex_bound = 0;
}

static u16 next_pot(u16 v)
{
	u16 p = 8;
	while (p < v && p < 1024)
		p <<= 1;
	return p;
}

// Morton offset of (x, y) inside an 8x8 PICA tile.
static inline u32 morton8(u32 x, u32 y)
{
	return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
}

static RGBA_t *conv_buf = NULL;
static size_t  conv_size = 0;

// Converts any GLMipmap_t format to a plain RGBA_t image of the same size.
static RGBA_t *to_rgba(GLMipmap_t *pTexInfo, int *has_partial_alpha)
{
	const UINT8 *src = (const UINT8 *)pTexInfo->data;
	INT32 w = pTexInfo->width, h = pTexInfo->height, i, n = w * h;
	RGBA_t *dst;

	if ((size_t)n > conv_size)
	{
		RGBA_t *nb = realloc(conv_buf, n * sizeof(RGBA_t));
		if (!nb)
			I_Error("GPU: out of memory converting a %dx%d texture", w, h);
		conv_buf = nb;
		conv_size = n;
	}
	dst = conv_buf;
	*has_partial_alpha = 0;

	switch (pTexInfo->format)
	{
		case GL_TEXFMT_P_8:
		case GL_TEXFMT_AP_88:
		{
			const int chromakeyed = (pTexInfo->flags & TF_CHROMAKEYED);
			const int ap = (pTexInfo->format == GL_TEXFMT_AP_88);
			for (i = 0; i < n; i++)
			{
				UINT8 idx = *src++;
				if (chromakeyed && idx == HWR_PATCHES_CHROMAKEY_COLORINDEX)
				{
					dst[i].rgba = 0;
					pTexInfo->flags |= TF_TRANSPARENT;
				}
				else
					dst[i] = myPaletteData[idx];
				if (ap)
				{
					if (!chromakeyed)
						dst[i].s.alpha = *src;
					src++;
				}
			}
			break;
		}
		case GL_TEXFMT_RGBA:
			memcpy(dst, src, n * sizeof(RGBA_t));
			break;
		case GL_TEXFMT_ALPHA_INTENSITY_88:
			for (i = 0; i < n; i++, src += 2)
			{
				dst[i].s.red = dst[i].s.green = dst[i].s.blue = src[0];
				dst[i].s.alpha = src[1];
			}
			break;
		case GL_TEXFMT_ALPHA_8:
			for (i = 0; i < n; i++)
			{
				dst[i].s.red = dst[i].s.green = dst[i].s.blue = 255;
				dst[i].s.alpha = *src++;
			}
			break;
		case GL_TEXFMT_INTENSITY_8:
			for (i = 0; i < n; i++)
			{
				dst[i].s.red = dst[i].s.green = dst[i].s.blue = *src++;
				dst[i].s.alpha = 255;
			}
			break;
		default:
			memset(dst, 0xFF, n * sizeof(RGBA_t));
			break;
	}

	for (i = 0; i < n; i++)
	{
		UINT8 a = dst[i].s.alpha;
		if (a != 0 && a != 255)
		{
			*has_partial_alpha = 1;
			break;
		}
	}
	return dst;
}

void GL_UpdateTexture(GLMipmap_t *pTexInfo)
{
	UINT32 id = pTexInfo->downloaded;
	c3dtex_t *t;
	RGBA_t *img;
	int partial;
	u16 w, h, pw, ph;
	u32 x, y;
	GPU_TEXCOLOR fmt;

	if (!c3d_started || !pTexInfo->data || !pTexInfo->width || !pTexInfo->height)
		return;

	w = pTexInfo->width;
	h = pTexInfo->height;
	pw = next_pot(w);
	ph = next_pot(h);

	img = to_rgba(pTexInfo, &partial);
	// 1-bit alpha (most SRB2 graphics) -> RGBA5551, half the memory/bandwidth
	fmt = partial ? GPU_RGBA8 : GPU_RGBA5551;

	if (id && id <= num_textures && textures[id-1].used)
	{
		t = &textures[id-1];
		if (t->tex.width != pw || t->tex.height != ph || t->tex.fmt != fmt)
		{
			free_texture(id);
			id = 0;
		}
	}
	else
		id = 0;

	if (!id)
	{
		id = alloc_texture_slot();
		t = &textures[id-1];
		if (!C3D_TexInit(&t->tex, pw, ph, fmt))
		{
			// Out of linear memory: drop everything cached and retry once.
			GL_Flush();
			id = alloc_texture_slot();
			t = &textures[id-1];
			if (!C3D_TexInit(&t->tex, pw, ph, fmt))
			{
				pTexInfo->downloaded = 0;
				return;
			}
		}
		t->used = 1;
		t->owner = pTexInfo;
		t->bytes = t->tex.size;
		tex_bytes_total += t->bytes;
		pTexInfo->downloaded = id;
	}

	t = &textures[id-1];

	// Swizzle (and nearest-resample to POT). Row 0 of the source = t 0.
	if (fmt == GPU_RGBA8)
	{
		u32 *out = (u32 *)t->tex.data;
		for (y = 0; y < ph; y++)
		{
			const RGBA_t *row = img + (u32)((y * h) / ph) * w;
			u32 tilerow = (y >> 3) * (pw >> 3);
			for (x = 0; x < pw; x++)
			{
				const RGBA_t *p = &row[(x * w) / pw];
				u32 off = ((tilerow + (x >> 3)) << 6) + morton8(x & 7, y & 7);
				out[off] = ((u32)p->s.red << 24) | ((u32)p->s.green << 16) | ((u32)p->s.blue << 8) | p->s.alpha;
			}
		}
	}
	else
	{
		u16 *out = (u16 *)t->tex.data;
		for (y = 0; y < ph; y++)
		{
			const RGBA_t *row = img + (u32)((y * h) / ph) * w;
			u32 tilerow = (y >> 3) * (pw >> 3);
			for (x = 0; x < pw; x++)
			{
				const RGBA_t *p = &row[(x * w) / pw];
				u32 off = ((tilerow + (x >> 3)) << 6) + morton8(x & 7, y & 7);
				out[off] = (u16)(((p->s.red >> 3) << 11) | ((p->s.green >> 3) << 6) | ((p->s.blue >> 3) << 1) | (p->s.alpha >= 128 ? 1 : 0));
			}
		}
	}

	C3D_TexFlush(&t->tex);

	t->wrap_s = (pTexInfo->flags & TF_WRAPX) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
	t->wrap_t = (pTexInfo->flags & TF_WRAPY) ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
	C3D_TexSetWrap(&t->tex, t->wrap_s, t->wrap_t);
	C3D_TexSetFilter(&t->tex, tex_filter, tex_filter);

	C3D_TexBind(0, &t->tex);
	tex_bound = id;
}

void GL_SetTexture(GLMipmap_t *pTexInfo)
{
	UINT32 id;

	if (!pTexInfo)
	{
		cur_textured = 0;
		return;
	}

	id = pTexInfo->downloaded;
	if (!id || id > num_textures || !textures[id-1].used)
	{
		pTexInfo->downloaded = 0;
		GL_UpdateTexture(pTexInfo);
		id = pTexInfo->downloaded;
	}

	if (id && id != tex_bound)
	{
		C3D_TexBind(0, &textures[id-1].tex);
		tex_bound = id;
	}
	cur_textured = (id != 0);
}

void GL_DeleteTexture(GLMipmap_t *pTexInfo)
{
	if (!pTexInfo)
		return;
	free_texture(pTexInfo->downloaded);
	pTexInfo->downloaded = 0;
}

void GL_Flush(void)
{
	UINT32 i;
	for (i = 1; i <= num_textures; i++)
		free_texture(i);
	num_textures = 0;
	tex_bound = 0;
}

INT32 GL_GetTextureUsed(void)
{
	return (INT32)tex_bytes_total;
}

// ----------------------------------------------------------------- frame ---

static void upload_matrix(void)
{
	// Tilt for the sideways screen and remap depth [-1,1] -> [-1,0]:
	//   x' = y, y' = -x, z' = (z - w) / 2
	static const float fix[16] =
	{
		0.0f, -1.0f, 0.0f, 0.0f,
		1.0f,  0.0f, 0.0f, 0.0f,
		0.0f,  0.0f, 0.5f, 0.0f,
		0.0f,  0.0f,-0.5f, 1.0f,
	};
	float m[16];
	C3D_Mtx out;
	int r;

	m_mul(m, fix, pj);
	m_mul(m, m, mv);

	for (r = 0; r < 4; r++)
	{
		out.r[r].x = m[0*4 + r];
		out.r[r].y = m[1*4 + r];
		out.r[r].z = m[2*4 + r];
		out.r[r].w = m[3*4 + r];
	}
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &out);
	matrix_dirty = 0;
}

static void begin_frame(void)
{
	C3D_AttrInfo *attr;
	C3D_BufInfo *buf;

	if (in_frame || !c3d_started)
		return;

	C3D_FrameBegin(C3D_FRAME_SYNCDRAW); // waits for the previous frame
	process_pending_frees();
	C3D_FrameDrawOn(target);
	in_frame = 1;
	vbo_used = ibo_used = 0;

	C3D_BindProgram(&program);

	attr = C3D_GetAttrInfo();
	AttrInfo_Init(attr);
	AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3); // x y z
	AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2); // s t

	buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, vbo, sizeof(c3dvert_t), 2, 0x10);

	C3D_CullFace(GPU_CULL_NONE);
	tev_valid = 0;
	matrix_dirty = 1;

	// Re-apply the GL state for this frame.
	{
		FBITFIELD f = CurrentPolyFlags;
		CurrentPolyFlags = ~f;
		GL_SetBlend(f);
	}
	if (tex_bound)
		C3D_TexBind(0, &textures[tex_bound-1].tex);
}

void N3DS_GPU_EndFrame(void)
{
	if (!c3d_started)
		return;
	if (!in_frame)
		begin_frame(); // keep the screen alive even if nothing was drawn

	flush_buffers();
	C3D_FrameEnd(0);
	in_frame = 0;
	drew_this_frame = 0;
}

// --------------------------------------------------------------- startup ---

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

boolean GL_Init(void)
{
	if (c3d_started)
		return true;

	if (!C3D_Init(1024 * 1024))
		return false;

	target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	if (!target)
		return false;
	C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	vshader_dvlb = DVLB_ParseFile((u32 *)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
	uLoc_projection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

	vbo = linearAlloc(VBO_MAX_VERTS * sizeof(c3dvert_t));
	ibo = linearAlloc(IBO_MAX * sizeof(u16));
	if (!vbo || !ibo)
		return false;

	m_identity(mv);
	m_identity(pj);
	c3d_started = 1;
	return true;
}

void SetupGLInfo(void)
{
}

boolean SetupGLfunc(void)
{
	return true;
}

void SetupGLFunc4(void)
{
}

void *GetGLFunc(const char *proc)
{
	(void)proc;
	return NULL;
}

INT32 GL_isExtAvailable(const char *extension, const GLubyte *start)
{
	(void)extension; (void)start;
	return 0;
}

void GL_DBG_Printf(const char *format, ...)
{
	(void)format;
}

void GL_SetModelView(GLint w, GLint h)
{
	screen_width = w;
	screen_height = h;
	C3D_SetViewport(0, 0, 240, 400);
	m_identity(pj);
	m_identity(mv);
	m_perspective(pj, fov, ASPECT_RATIO);
	memcpy(projMatrix, pj, sizeof pj);
	matrix_dirty = 1;
}

void GL_SetStates(void)
{
	CurrentPolyFlags = 0xffffffff;
	GL_SetBlend(0);
	cur_textured = 0;
	m_identity(mv);
	m_scale(mv, 1.0f, 1.0f, -1.0f);
	memcpy(modelMatrix, mv, sizeof mv);
	matrix_dirty = 1;
}

// ------------------------------------------------------------ transforms ---

void GL_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip, float farclip)
{
	// GL viewport (minx, H-maxy, w, h), rotated onto the 240x400 framebuffer
	INT32 gw = maxx - minx, gh = maxy - miny;
	INT32 gy = screen_height - maxy;

	begin_frame();
	C3D_SetViewport((u32)gy, (u32)(screen_width - maxx), (u32)gh, (u32)gw);

	NEAR_CLIPPING_PLANE = nearclip;
	FAR_CLIPPING_PLANE = farclip;

	m_identity(pj);
	m_perspective(pj, fov, ASPECT_RATIO);
	memcpy(projMatrix, pj, sizeof pj);
	matrix_dirty = 1;
}

void GL_SetTransform(FTransform *stransform)
{
	static boolean special_splitscreen;
	boolean shearing = false;
	float used_fov;

	m_identity(mv);

	if (stransform)
	{
		used_fov = stransform->fovangle;
		shearing = stransform->shearing;

		switch (stransform->fliptype)
		{
			case TRANSFORM_MIRROR:
				m_scale(mv, -stransform->scalex, stransform->scaley, -stransform->scalez);
				break;
			case TRANSFORM_MIRRORFLIP:
				m_scale(mv, -stransform->scalex, -stransform->scaley, -stransform->scalez);
				break;
			case TRANSFORM_FLIP:
				m_scale(mv, stransform->scalex, -stransform->scaley, -stransform->scalez);
				break;
			default:
				m_scale(mv, stransform->scalex, stransform->scaley, -stransform->scalez);
				break;
		}

		if (stransform->roll)
			m_rotate(mv, stransform->rollangle, 0.0f, 0.0f, 1.0f);
		m_rotate(mv, stransform->anglex, 1.0f, 0.0f, 0.0f);
		m_rotate(mv, stransform->angley + 270.0f, 0.0f, 1.0f, 0.0f);
		m_translate(mv, -stransform->x, -stransform->z, -stransform->y);

		special_splitscreen = (stransform->splitscreen == 1);
	}
	else
	{
		used_fov = fov;
		m_scale(mv, 1.0f, 1.0f, -1.0f);
	}

	m_identity(pj);

	if (shearing)
	{
		float dy = stransform->viewaiming * 2;
		if (stransform->fliptype == TRANSFORM_FLIP || stransform->fliptype == TRANSFORM_MIRRORFLIP)
			dy *= -1.0f;
		m_translate(pj, 0.0f, -dy / BASEVIDHEIGHT, 0.0f);
	}

	if (special_splitscreen)
	{
		used_fov = atanf(tanf(used_fov * (float)M_PI / 360.0f) * 0.8f) * 360.0f / (float)M_PI;
		m_perspective(pj, used_fov, 2 * ASPECT_RATIO);
	}
	else
		m_perspective(pj, used_fov, ASPECT_RATIO);

	memcpy(projMatrix, pj, sizeof pj);
	memcpy(modelMatrix, mv, sizeof mv);
	matrix_dirty = 1;
}

// ----------------------------------------------------------------- blend ---

void GL_SetBlend(FBITFIELD PolyFlags)
{
	const FBITFIELD Xor = CurrentPolyFlags ^ PolyFlags;
	const FBITFIELD blend = PolyFlags & PF_Blending;

	if (!c3d_started)
	{
		CurrentPolyFlags = PolyFlags;
		return;
	}

	if (Xor & (PF_Blending | PF_NoAlphaTest | PF_Invisible))
	{
		GPU_BLENDEQUATION eq = GPU_BLEND_ADD;
		GPU_BLENDFACTOR src = GPU_ONE, dst = GPU_ZERO;
		GPU_TESTFUNC afunc = GPU_GREATER;
		int aref = 127;

		switch (blend)
		{
			case PF_Translucent & PF_Blending:
				src = GPU_SRC_ALPHA; dst = GPU_ONE_MINUS_SRC_ALPHA;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_Masked & PF_Blending:
				src = GPU_SRC_ALPHA; dst = GPU_ZERO;
				break;
			case PF_Additive & PF_Blending:
				src = GPU_SRC_ALPHA; dst = GPU_ONE;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_Subtractive & PF_Blending:
				src = GPU_SRC_ALPHA; dst = GPU_ONE; eq = GPU_BLEND_SUBTRACT;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_ReverseSubtract & PF_Blending:
				src = GPU_SRC_ALPHA; dst = GPU_ONE; eq = GPU_BLEND_REVERSE_SUBTRACT;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_Environment & PF_Blending:
				src = GPU_ONE; dst = GPU_ONE_MINUS_SRC_ALPHA;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_Multiplicative & PF_Blending:
				src = GPU_DST_COLOR; dst = GPU_ZERO;
				afunc = GPU_NOTEQUAL; aref = 0;
				break;
			case PF_Fog & PF_Fog:
				src = GPU_SRC_ALPHA; dst = GPU_SRC_COLOR;
				afunc = GPU_ALWAYS; aref = 0;
				break;
			default:
				break;
		}

		if (PolyFlags & PF_Invisible)
		{
			src = GPU_ZERO; dst = GPU_ONE; eq = GPU_BLEND_ADD;
		}

		C3D_AlphaBlend(eq, eq, src, dst, src, dst);

		if (PolyFlags & PF_NoAlphaTest)
			C3D_AlphaTest(false, GPU_ALWAYS, 0);
		else
			C3D_AlphaTest(true, afunc, aref);
	}

	if (Xor & (PF_NoDepthTest | PF_Occlude))
	{
		C3D_DepthTest(true,
			(PolyFlags & PF_NoDepthTest) ? GPU_ALWAYS : GPU_GEQUAL,
			(PolyFlags & PF_Occlude) ? GPU_WRITE_ALL : GPU_WRITE_COLOR);
	}

	if (Xor & (PF_Decal | PF_Skydecal))
	{
		// depth buffer runs 1 (near) .. 0 (far)
		float offset = 0.0f;
		if (PolyFlags & PF_Decal)
			offset = 1.0f / 16384.0f;
		else if (PolyFlags & PF_Skydecal)
			offset = -0.45f / 16384.0f;
		C3D_DepthMap(true, -1.0f, offset);
	}

	if (PolyFlags & PF_NoTexture)
		cur_textured = 0;

	CurrentPolyFlags = PolyFlags;
}

static void setup_tev(int textured, int modulated, u32 color)
{
	C3D_TexEnv *env;

	if (tev_valid && textured == tev_textured && modulated == tev_modulated && (!modulated || color == tev_color))
		return;

	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);

	if (textured && modulated)
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		C3D_TexEnvColor(env, color);
	}
	else if (textured)
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
		C3D_TexEnvColor(env, modulated ? color : 0xFFFFFFFF);
	}

	tev_textured = textured;
	tev_modulated = modulated;
	tev_color = color;
	tev_valid = 1;
}

// ------------------------------------------------------------------ draw ---

static int prepare(FSurfaceInfo *pSurf, FBITFIELD PolyFlags)
{
	int textured;

	begin_frame();
	GL_SetBlend(PolyFlags);

	if (pSurf && (PolyFlags & PF_Modulated))
		cur_color = pSurf->PolyColor;

	textured = cur_textured && !(PolyFlags & PF_NoTexture) && tex_bound;
	setup_tev(textured, (PolyFlags & PF_Modulated) != 0, cur_color.rgba);

	if (matrix_dirty)
		upload_matrix();

	return textured;
}

// Temporarily override a texture's wrap mode for this draw (PF_ForceWrap*).
static int apply_forced_wrap(FBITFIELD PolyFlags, int textured)
{
	c3dtex_t *t;
	GPU_TEXTURE_WRAP_PARAM ws, wt;

	if (!textured || !(PolyFlags & (PF_ForceWrapX | PF_ForceWrapY | PF_RemoveYWrap)))
		return 0;

	t = &textures[tex_bound-1];
	ws = (PolyFlags & PF_ForceWrapX) ? GPU_REPEAT : t->wrap_s;
	wt = (PolyFlags & PF_ForceWrapY) ? GPU_REPEAT : (PolyFlags & PF_RemoveYWrap) ? GPU_CLAMP_TO_EDGE : t->wrap_t;
	if (ws == t->wrap_s && wt == t->wrap_t)
		return 0;
	C3D_TexSetWrap(&t->tex, ws, wt);
	C3D_TexBind(0, &t->tex);
	return 1;
}

static void restore_wrap(void)
{
	c3dtex_t *t = &textures[tex_bound-1];
	C3D_TexSetWrap(&t->tex, t->wrap_s, t->wrap_t);
	C3D_TexBind(0, &t->tex);
}

static int reserve(u32 nverts, u32 nidx)
{
	// The buffers are sized for far more than a frame needs; if a frame
	// ever overflows them, drop the rest rather than corrupt data the GPU
	// hasn't consumed yet.
	if (vbo_used + nverts > VBO_MAX_VERTS || ibo_used + nidx > IBO_MAX)
		return 0;
	drew_this_frame = 1;
	return 1;
}


void GL_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags)
{
	int textured, wrapped;
	u32 first;

	if (!c3d_started || iNumPts < 3)
		return;

	textured = prepare(pSurf, PolyFlags);
	if (!reserve(iNumPts, 0))
		return;

	first = vbo_used;
	memcpy(&vbo[first], pOutVerts, iNumPts * sizeof(c3dvert_t));
	vbo_used += iNumPts;

	wrapped = apply_forced_wrap(PolyFlags, textured);
	C3D_DrawArrays(GPU_TRIANGLE_FAN, first, iNumPts);
	if (wrapped)
		restore_wrap();
}

void GL_DrawIndexedTriangles(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, unsigned int *IndexArray)
{
	u32 i, maxidx = 0, first, ifirst;
	int textured, wrapped;

	if (!c3d_started || !iNumPts)
		return;

	for (i = 0; i < iNumPts; i++)
		if (IndexArray[i] > maxidx)
			maxidx = IndexArray[i];

	textured = prepare(pSurf, PolyFlags);
	if (!reserve(maxidx + 1, iNumPts))
		return;

	first = vbo_used;
	memcpy(&vbo[first], pOutVerts, (maxidx + 1) * sizeof(c3dvert_t));
	vbo_used += maxidx + 1;

	ifirst = ibo_used;
	for (i = 0; i < iNumPts; i++)
		ibo[ifirst + i] = (u16)(first + IndexArray[i]);
	ibo_used += iNumPts;

	wrapped = apply_forced_wrap(PolyFlags, textured);
	C3D_DrawElements(GPU_TRIANGLES, iNumPts, C3D_UNSIGNED_SHORT, &ibo[ifirst]);
	if (wrapped)
		restore_wrap();
}

void GL_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color)
{
	FOutVector p[4];
	float dx, dy, angle;
	FBITFIELD old = CurrentPolyFlags;
	RGBA_t oldc = cur_color;
	int oldt = cur_textured;

	if (fabsf(v2->x - v1->x) > 1.0e-6f)
		angle = atanf((v2->y - v1->y) / (v2->x - v1->x));
	else
		angle = (float)(M_PI / 2);

	dx = sinf(angle) / (float)(screen_width ? screen_width : 400);
	dy = cosf(angle) / (float)(screen_height ? screen_height : 240);

	p[0].x = v1->x - dx; p[0].y = -(v1->y + dy); p[0].z = 1;
	p[1].x = v2->x - dx; p[1].y = -(v2->y + dy); p[1].z = 1;
	p[2].x = v2->x + dx; p[2].y = -(v2->y - dy); p[2].z = 1;
	p[3].x = v1->x + dx; p[3].y = -(v1->y - dy); p[3].z = 1;
	p[0].s = p[0].t = p[1].s = p[1].t = p[2].s = p[2].t = p[3].s = p[3].t = 0;

	cur_color = Color;
	cur_textured = 0;
	{
		FSurfaceInfo surf;
		memset(&surf, 0, sizeof surf);
		surf.PolyColor = Color;
		GL_DrawPolygon(&surf, p, 4, (old | PF_Modulated | PF_NoTexture));
	}
	GL_SetBlend(old);
	cur_color = oldc;
	cur_textured = oldt;
}

void GL_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FBOOLEAN StencilMask, FRGBAFloat *ClearColor)
{
	C3D_ClearBits bits = 0;
	u32 color;

	if (!c3d_started)
		return;

	if (ClearColor)
		clear_color = *ClearColor;

	if (ColorMask)
		bits |= C3D_CLEAR_COLOR;
	if (DepthMask || StencilMask)
		bits |= C3D_CLEAR_DEPTH;

	begin_frame();

	color = ((u32)(clear_color.red   * 255.0f) << 24)
	      | ((u32)(clear_color.green * 255.0f) << 16)
	      | ((u32)(clear_color.blue  * 255.0f) << 8)
	      | 0xFF;

	if (bits)
	{
		// Memory fills run ahead of the frame's command list, so a clear in
		// the middle of a frame (skybox, splitscreen) must split the frame
		// first or it would happen before the earlier draws.
		if (drew_this_frame)
		{
			flush_buffers();
			C3D_FrameSplit(0);
		}
		C3D_RenderTargetClear(target, bits, color, 0); // depth: 0 = far
	}

	GL_SetBlend(DepthMask ? (PF_Occlude | CurrentPolyFlags) : (CurrentPolyFlags & ~PF_Occlude));
}

// ------------------------------------------------------------- misc state --

void GL_SetPalette(RGBA_t *palette)
{
	if (memcmp(myPaletteData, palette, sizeof myPaletteData))
	{
		memcpy(myPaletteData, palette, sizeof myPaletteData);
		GL_Flush();
	}
}

void GL_SetSpecialState(hwdspecialstate_t IdState, INT32 Value)
{
	switch (IdState)
	{
		case HWD_SET_TEXTUREFILTERMODE:
		{
			GPU_TEXTURE_FILTER_PARAM f = (Value == HWD_SET_TEXTUREFILTER_POINTSAMPLED) ? GPU_NEAREST : GPU_LINEAR;
			if (f != tex_filter)
			{
				tex_filter = f;
				GL_Flush();
			}
			break;
		}
		default:
			break;
	}
}

void GL_EnableStencilTest(void)  {}
void GL_DisableStencilTest(void) {}

// Not supported on the PICA200 path (yet); the engine falls back.
void GL_RenderSkyDome(gl_sky_t *sky) { (void)sky; }
void GL_CreateModelVBOs(model_t *model) { (void)model; }
void GL_DrawModelEx(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos, float hscale, float vscale, UINT8 flipped, UINT8 hflipped, FSurfaceInfo *Surface)
{
	(void)model; (void)frameIndex; (void)duration; (void)tics; (void)nextFrameIndex; (void)pos; (void)hscale; (void)vscale; (void)flipped; (void)hflipped; (void)Surface;
}
void GL_Draw2DModel(model_t *model, INT32 frameIndex, INT32 duration, INT32 tics, INT32 nextFrameIndex,
                    FTransform *pos, float hscale, float vscale, UINT8 flipped, UINT8 hflipped, FSurfaceInfo *Surface)
{
	(void)model; (void)frameIndex; (void)duration; (void)tics; (void)nextFrameIndex; (void)pos; (void)hscale; (void)vscale; (void)flipped; (void)hflipped; (void)Surface;
}

void GL_ReadScreenTexture(int tex, UINT8 *dst_data, INT32 scale)
{
	(void)tex; (void)scale;
	if (dst_data)
		memset(dst_data, 0, (size_t)(screen_width * screen_height * 3) / (scale > 0 ? (scale * scale) : 1));
}
void GL_MakeScreenTexture(int tex) { (void)tex; }
void GL_FlushScreenTextures(void) {}
void GL_DrawScreenTexture(int tex, FSurfaceInfo *surf, FBITFIELD polyflags) { (void)tex; (void)surf; (void)polyflags; }
void GL_DoScreenWipe(int wipeStart, int wipeEnd) { (void)wipeStart; (void)wipeEnd; }
void GL_RenderVhsEffect(fixed_t upbary, fixed_t downbary, UINT8 updistort, UINT8 downdistort, UINT8 barsize)
{
	(void)upbary; (void)downbary; (void)updistort; (void)downdistort; (void)barsize;
}
void GL_DrawScreenFinalTexture(int tex, INT32 width, INT32 height) { (void)tex; (void)width; (void)height; }
void GL_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]) { (void)points; }

boolean GL_InitShaders(void) { return false; }
void GL_LoadShader(int slot, char *code, hwdshaderstage_t stage) { (void)slot; (void)code; (void)stage; }
boolean GL_CompileShader(int slot) { (void)slot; return false; }
void GL_SetShader(int slot) { (void)slot; }
void GL_UnSetShader(void) {}
void GL_EnableShader(void) {}
void GL_SetShaderInfo(hwdshaderinfo_t info, INT32 value) { (void)info; (void)value; }
void GL_SetPaletteLookup(UINT8 *lut) { (void)lut; }
UINT32 GL_CreateLightTable(RGBA_t *hw_lighttable) { (void)hw_lighttable; return 0; }
void GL_ClearLightTables(void) {}
void GL_SetScreenPalette(RGBA_t *palette) { (void)palette; }
