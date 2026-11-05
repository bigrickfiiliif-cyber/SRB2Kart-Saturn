// SRB2Kart Saturn - Nintendo 3DS port
// The 3DS has no OpenGL. The hardware renderer only needs the GL scalar
// types from <GL/gl.h> for a few shared declarations; the actual driver is
// n3ds/r_citro3d.c (citro3d / PICA200).

#ifndef __N3DS_GL_SHIM_H__
#define __N3DS_GL_SHIM_H__

#include <stdint.h>

typedef unsigned int   GLenum;
typedef unsigned int   GLbitfield;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLboolean;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef void           GLvoid;

#ifndef APIENTRY
#define APIENTRY
#endif

#define STATIC_OPENGL 1 // no function-pointer loader on 3DS

#endif
