/*
 * The OpenGL 1.1 scalar types, as the specification's table 2.2 fixes them
 * for a 32-bit target, in a header of the project's own.
 *
 * Here rather than <GL/gl.h> because the dispatch table is generated from
 * src\opengl\gl_entrypoints.psd1 and compiled in three places: the ICD, the
 * host tests, and whatever else the OpenGL plan adds. <GL/gl.h> drags
 * <windows.h> in on this toolchain, and the host test must compile without
 * an OS header (docs\specifications\win9x-driver-boundaries.md). These are
 * the sizes every Win32 OpenGL binary agrees on; a slot's parameter list
 * names them and nothing else.
 */
#ifndef VELOCITY9X_GL_TYPES_H
#define VELOCITY9X_GL_TYPES_H

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef void           GLvoid;

typedef char v9x_gl_assert_enum_4[sizeof(GLenum) == 4 ? 1 : -1];
typedef char v9x_gl_assert_int_4[sizeof(GLint) == 4 ? 1 : -1];
typedef char v9x_gl_assert_float_4[sizeof(GLfloat) == 4 ? 1 : -1];
typedef char v9x_gl_assert_double_8[sizeof(GLdouble) == 8 ? 1 : -1];

#endif
