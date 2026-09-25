# OpenGL 1.1 ICD dispatch table (GLCLTPROCTABLE.glDispatchTable / GLDISPATCHTABLE), slots 0..335.
# Generated 2026-09-26 from the sources named in Source below; slot order cross-checked
# between the ReactOS and Mesa tables, prototypes cross-checked against Mesa include/GL/gl.h.
# The ReactOS sdk/include/psdk/gl/gl.h URL given in the task returned 404; Mesa gl.h was used instead.
# Total entries: 336
# Slot 305: glViewport
# Slot 306: glArrayElement
# Slot 335: glPushClientAttrib
# ReactOS/Mesa disagreements: none (names, return types and parameter types agree at every slot)
# Type differences between the ICD tables (ReactOS and Mesa agree) and gl.h; the ICD table
# type is used below. On Win32 GLint and GLsizei are both int, so the ABI is identical:
#   glPixelMapfv: ICD tables: void(GLenum, GLint, const GLfloat *); gl.h: void(GLenum map, GLsizei mapsize, const GLfloat *values)
#   glPixelMapuiv: ICD tables: void(GLenum, GLint, const GLuint *); gl.h: void(GLenum map, GLsizei mapsize, const GLuint *values)
#   glPixelMapusv: ICD tables: void(GLenum, GLint, const GLushort *); gl.h: void(GLenum map, GLsizei mapsize, const GLushort *values)
# UNVERIFIED prototypes: none
@{
    Source = @{
        ReactOS  = 'https://raw.githubusercontent.com/reactos/reactos/master/dll/opengl/opengl32/icd.h'
        Mesa     = 'https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/src/gallium/frontends/wgl/gldrv.h'
        Header   = 'https://gitlab.freedesktop.org/mesa/mesa/-/raw/main/include/GL/gl.h'
        Verified = '2026-09-26'
    }
    Entries = @(
        @{ Slot = 0; Name = 'glNewList'; Return = 'void'; Params = 'GLuint list, GLenum mode' }
        @{ Slot = 1; Name = 'glEndList'; Return = 'void'; Params = 'void' }
        @{ Slot = 2; Name = 'glCallList'; Return = 'void'; Params = 'GLuint list' }
        @{ Slot = 3; Name = 'glCallLists'; Return = 'void'; Params = 'GLsizei n, GLenum type, const GLvoid *lists' }
        @{ Slot = 4; Name = 'glDeleteLists'; Return = 'void'; Params = 'GLuint list, GLsizei range' }
        @{ Slot = 5; Name = 'glGenLists'; Return = 'GLuint'; Params = 'GLsizei range' }
        @{ Slot = 6; Name = 'glListBase'; Return = 'void'; Params = 'GLuint base' }
        @{ Slot = 7; Name = 'glBegin'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 8; Name = 'glBitmap'; Return = 'void'; Params = 'GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap' }
        @{ Slot = 9; Name = 'glColor3b'; Return = 'void'; Params = 'GLbyte red, GLbyte green, GLbyte blue' }
        @{ Slot = 10; Name = 'glColor3bv'; Return = 'void'; Params = 'const GLbyte *v' }
        @{ Slot = 11; Name = 'glColor3d'; Return = 'void'; Params = 'GLdouble red, GLdouble green, GLdouble blue' }
        @{ Slot = 12; Name = 'glColor3dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 13; Name = 'glColor3f'; Return = 'void'; Params = 'GLfloat red, GLfloat green, GLfloat blue' }
        @{ Slot = 14; Name = 'glColor3fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 15; Name = 'glColor3i'; Return = 'void'; Params = 'GLint red, GLint green, GLint blue' }
        @{ Slot = 16; Name = 'glColor3iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 17; Name = 'glColor3s'; Return = 'void'; Params = 'GLshort red, GLshort green, GLshort blue' }
        @{ Slot = 18; Name = 'glColor3sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 19; Name = 'glColor3ub'; Return = 'void'; Params = 'GLubyte red, GLubyte green, GLubyte blue' }
        @{ Slot = 20; Name = 'glColor3ubv'; Return = 'void'; Params = 'const GLubyte *v' }
        @{ Slot = 21; Name = 'glColor3ui'; Return = 'void'; Params = 'GLuint red, GLuint green, GLuint blue' }
        @{ Slot = 22; Name = 'glColor3uiv'; Return = 'void'; Params = 'const GLuint *v' }
        @{ Slot = 23; Name = 'glColor3us'; Return = 'void'; Params = 'GLushort red, GLushort green, GLushort blue' }
        @{ Slot = 24; Name = 'glColor3usv'; Return = 'void'; Params = 'const GLushort *v' }
        @{ Slot = 25; Name = 'glColor4b'; Return = 'void'; Params = 'GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha' }
        @{ Slot = 26; Name = 'glColor4bv'; Return = 'void'; Params = 'const GLbyte *v' }
        @{ Slot = 27; Name = 'glColor4d'; Return = 'void'; Params = 'GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha' }
        @{ Slot = 28; Name = 'glColor4dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 29; Name = 'glColor4f'; Return = 'void'; Params = 'GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha' }
        @{ Slot = 30; Name = 'glColor4fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 31; Name = 'glColor4i'; Return = 'void'; Params = 'GLint red, GLint green, GLint blue, GLint alpha' }
        @{ Slot = 32; Name = 'glColor4iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 33; Name = 'glColor4s'; Return = 'void'; Params = 'GLshort red, GLshort green, GLshort blue, GLshort alpha' }
        @{ Slot = 34; Name = 'glColor4sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 35; Name = 'glColor4ub'; Return = 'void'; Params = 'GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha' }
        @{ Slot = 36; Name = 'glColor4ubv'; Return = 'void'; Params = 'const GLubyte *v' }
        @{ Slot = 37; Name = 'glColor4ui'; Return = 'void'; Params = 'GLuint red, GLuint green, GLuint blue, GLuint alpha' }
        @{ Slot = 38; Name = 'glColor4uiv'; Return = 'void'; Params = 'const GLuint *v' }
        @{ Slot = 39; Name = 'glColor4us'; Return = 'void'; Params = 'GLushort red, GLushort green, GLushort blue, GLushort alpha' }
        @{ Slot = 40; Name = 'glColor4usv'; Return = 'void'; Params = 'const GLushort *v' }
        @{ Slot = 41; Name = 'glEdgeFlag'; Return = 'void'; Params = 'GLboolean flag' }
        @{ Slot = 42; Name = 'glEdgeFlagv'; Return = 'void'; Params = 'const GLboolean *flag' }
        @{ Slot = 43; Name = 'glEnd'; Return = 'void'; Params = 'void' }
        @{ Slot = 44; Name = 'glIndexd'; Return = 'void'; Params = 'GLdouble c' }
        @{ Slot = 45; Name = 'glIndexdv'; Return = 'void'; Params = 'const GLdouble *c' }
        @{ Slot = 46; Name = 'glIndexf'; Return = 'void'; Params = 'GLfloat c' }
        @{ Slot = 47; Name = 'glIndexfv'; Return = 'void'; Params = 'const GLfloat *c' }
        @{ Slot = 48; Name = 'glIndexi'; Return = 'void'; Params = 'GLint c' }
        @{ Slot = 49; Name = 'glIndexiv'; Return = 'void'; Params = 'const GLint *c' }
        @{ Slot = 50; Name = 'glIndexs'; Return = 'void'; Params = 'GLshort c' }
        @{ Slot = 51; Name = 'glIndexsv'; Return = 'void'; Params = 'const GLshort *c' }
        @{ Slot = 52; Name = 'glNormal3b'; Return = 'void'; Params = 'GLbyte nx, GLbyte ny, GLbyte nz' }
        @{ Slot = 53; Name = 'glNormal3bv'; Return = 'void'; Params = 'const GLbyte *v' }
        @{ Slot = 54; Name = 'glNormal3d'; Return = 'void'; Params = 'GLdouble nx, GLdouble ny, GLdouble nz' }
        @{ Slot = 55; Name = 'glNormal3dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 56; Name = 'glNormal3f'; Return = 'void'; Params = 'GLfloat nx, GLfloat ny, GLfloat nz' }
        @{ Slot = 57; Name = 'glNormal3fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 58; Name = 'glNormal3i'; Return = 'void'; Params = 'GLint nx, GLint ny, GLint nz' }
        @{ Slot = 59; Name = 'glNormal3iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 60; Name = 'glNormal3s'; Return = 'void'; Params = 'GLshort nx, GLshort ny, GLshort nz' }
        @{ Slot = 61; Name = 'glNormal3sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 62; Name = 'glRasterPos2d'; Return = 'void'; Params = 'GLdouble x, GLdouble y' }
        @{ Slot = 63; Name = 'glRasterPos2dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 64; Name = 'glRasterPos2f'; Return = 'void'; Params = 'GLfloat x, GLfloat y' }
        @{ Slot = 65; Name = 'glRasterPos2fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 66; Name = 'glRasterPos2i'; Return = 'void'; Params = 'GLint x, GLint y' }
        @{ Slot = 67; Name = 'glRasterPos2iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 68; Name = 'glRasterPos2s'; Return = 'void'; Params = 'GLshort x, GLshort y' }
        @{ Slot = 69; Name = 'glRasterPos2sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 70; Name = 'glRasterPos3d'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z' }
        @{ Slot = 71; Name = 'glRasterPos3dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 72; Name = 'glRasterPos3f'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z' }
        @{ Slot = 73; Name = 'glRasterPos3fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 74; Name = 'glRasterPos3i'; Return = 'void'; Params = 'GLint x, GLint y, GLint z' }
        @{ Slot = 75; Name = 'glRasterPos3iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 76; Name = 'glRasterPos3s'; Return = 'void'; Params = 'GLshort x, GLshort y, GLshort z' }
        @{ Slot = 77; Name = 'glRasterPos3sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 78; Name = 'glRasterPos4d'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z, GLdouble w' }
        @{ Slot = 79; Name = 'glRasterPos4dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 80; Name = 'glRasterPos4f'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z, GLfloat w' }
        @{ Slot = 81; Name = 'glRasterPos4fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 82; Name = 'glRasterPos4i'; Return = 'void'; Params = 'GLint x, GLint y, GLint z, GLint w' }
        @{ Slot = 83; Name = 'glRasterPos4iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 84; Name = 'glRasterPos4s'; Return = 'void'; Params = 'GLshort x, GLshort y, GLshort z, GLshort w' }
        @{ Slot = 85; Name = 'glRasterPos4sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 86; Name = 'glRectd'; Return = 'void'; Params = 'GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2' }
        @{ Slot = 87; Name = 'glRectdv'; Return = 'void'; Params = 'const GLdouble *v1, const GLdouble *v2' }
        @{ Slot = 88; Name = 'glRectf'; Return = 'void'; Params = 'GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2' }
        @{ Slot = 89; Name = 'glRectfv'; Return = 'void'; Params = 'const GLfloat *v1, const GLfloat *v2' }
        @{ Slot = 90; Name = 'glRecti'; Return = 'void'; Params = 'GLint x1, GLint y1, GLint x2, GLint y2' }
        @{ Slot = 91; Name = 'glRectiv'; Return = 'void'; Params = 'const GLint *v1, const GLint *v2' }
        @{ Slot = 92; Name = 'glRects'; Return = 'void'; Params = 'GLshort x1, GLshort y1, GLshort x2, GLshort y2' }
        @{ Slot = 93; Name = 'glRectsv'; Return = 'void'; Params = 'const GLshort *v1, const GLshort *v2' }
        @{ Slot = 94; Name = 'glTexCoord1d'; Return = 'void'; Params = 'GLdouble s' }
        @{ Slot = 95; Name = 'glTexCoord1dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 96; Name = 'glTexCoord1f'; Return = 'void'; Params = 'GLfloat s' }
        @{ Slot = 97; Name = 'glTexCoord1fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 98; Name = 'glTexCoord1i'; Return = 'void'; Params = 'GLint s' }
        @{ Slot = 99; Name = 'glTexCoord1iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 100; Name = 'glTexCoord1s'; Return = 'void'; Params = 'GLshort s' }
        @{ Slot = 101; Name = 'glTexCoord1sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 102; Name = 'glTexCoord2d'; Return = 'void'; Params = 'GLdouble s, GLdouble t' }
        @{ Slot = 103; Name = 'glTexCoord2dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 104; Name = 'glTexCoord2f'; Return = 'void'; Params = 'GLfloat s, GLfloat t' }
        @{ Slot = 105; Name = 'glTexCoord2fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 106; Name = 'glTexCoord2i'; Return = 'void'; Params = 'GLint s, GLint t' }
        @{ Slot = 107; Name = 'glTexCoord2iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 108; Name = 'glTexCoord2s'; Return = 'void'; Params = 'GLshort s, GLshort t' }
        @{ Slot = 109; Name = 'glTexCoord2sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 110; Name = 'glTexCoord3d'; Return = 'void'; Params = 'GLdouble s, GLdouble t, GLdouble r' }
        @{ Slot = 111; Name = 'glTexCoord3dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 112; Name = 'glTexCoord3f'; Return = 'void'; Params = 'GLfloat s, GLfloat t, GLfloat r' }
        @{ Slot = 113; Name = 'glTexCoord3fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 114; Name = 'glTexCoord3i'; Return = 'void'; Params = 'GLint s, GLint t, GLint r' }
        @{ Slot = 115; Name = 'glTexCoord3iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 116; Name = 'glTexCoord3s'; Return = 'void'; Params = 'GLshort s, GLshort t, GLshort r' }
        @{ Slot = 117; Name = 'glTexCoord3sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 118; Name = 'glTexCoord4d'; Return = 'void'; Params = 'GLdouble s, GLdouble t, GLdouble r, GLdouble q' }
        @{ Slot = 119; Name = 'glTexCoord4dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 120; Name = 'glTexCoord4f'; Return = 'void'; Params = 'GLfloat s, GLfloat t, GLfloat r, GLfloat q' }
        @{ Slot = 121; Name = 'glTexCoord4fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 122; Name = 'glTexCoord4i'; Return = 'void'; Params = 'GLint s, GLint t, GLint r, GLint q' }
        @{ Slot = 123; Name = 'glTexCoord4iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 124; Name = 'glTexCoord4s'; Return = 'void'; Params = 'GLshort s, GLshort t, GLshort r, GLshort q' }
        @{ Slot = 125; Name = 'glTexCoord4sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 126; Name = 'glVertex2d'; Return = 'void'; Params = 'GLdouble x, GLdouble y' }
        @{ Slot = 127; Name = 'glVertex2dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 128; Name = 'glVertex2f'; Return = 'void'; Params = 'GLfloat x, GLfloat y' }
        @{ Slot = 129; Name = 'glVertex2fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 130; Name = 'glVertex2i'; Return = 'void'; Params = 'GLint x, GLint y' }
        @{ Slot = 131; Name = 'glVertex2iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 132; Name = 'glVertex2s'; Return = 'void'; Params = 'GLshort x, GLshort y' }
        @{ Slot = 133; Name = 'glVertex2sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 134; Name = 'glVertex3d'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z' }
        @{ Slot = 135; Name = 'glVertex3dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 136; Name = 'glVertex3f'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z' }
        @{ Slot = 137; Name = 'glVertex3fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 138; Name = 'glVertex3i'; Return = 'void'; Params = 'GLint x, GLint y, GLint z' }
        @{ Slot = 139; Name = 'glVertex3iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 140; Name = 'glVertex3s'; Return = 'void'; Params = 'GLshort x, GLshort y, GLshort z' }
        @{ Slot = 141; Name = 'glVertex3sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 142; Name = 'glVertex4d'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z, GLdouble w' }
        @{ Slot = 143; Name = 'glVertex4dv'; Return = 'void'; Params = 'const GLdouble *v' }
        @{ Slot = 144; Name = 'glVertex4f'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z, GLfloat w' }
        @{ Slot = 145; Name = 'glVertex4fv'; Return = 'void'; Params = 'const GLfloat *v' }
        @{ Slot = 146; Name = 'glVertex4i'; Return = 'void'; Params = 'GLint x, GLint y, GLint z, GLint w' }
        @{ Slot = 147; Name = 'glVertex4iv'; Return = 'void'; Params = 'const GLint *v' }
        @{ Slot = 148; Name = 'glVertex4s'; Return = 'void'; Params = 'GLshort x, GLshort y, GLshort z, GLshort w' }
        @{ Slot = 149; Name = 'glVertex4sv'; Return = 'void'; Params = 'const GLshort *v' }
        @{ Slot = 150; Name = 'glClipPlane'; Return = 'void'; Params = 'GLenum plane, const GLdouble *equation' }
        @{ Slot = 151; Name = 'glColorMaterial'; Return = 'void'; Params = 'GLenum face, GLenum mode' }
        @{ Slot = 152; Name = 'glCullFace'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 153; Name = 'glFogf'; Return = 'void'; Params = 'GLenum pname, GLfloat param' }
        @{ Slot = 154; Name = 'glFogfv'; Return = 'void'; Params = 'GLenum pname, const GLfloat *params' }
        @{ Slot = 155; Name = 'glFogi'; Return = 'void'; Params = 'GLenum pname, GLint param' }
        @{ Slot = 156; Name = 'glFogiv'; Return = 'void'; Params = 'GLenum pname, const GLint *params' }
        @{ Slot = 157; Name = 'glFrontFace'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 158; Name = 'glHint'; Return = 'void'; Params = 'GLenum target, GLenum mode' }
        @{ Slot = 159; Name = 'glLightf'; Return = 'void'; Params = 'GLenum light, GLenum pname, GLfloat param' }
        @{ Slot = 160; Name = 'glLightfv'; Return = 'void'; Params = 'GLenum light, GLenum pname, const GLfloat *params' }
        @{ Slot = 161; Name = 'glLighti'; Return = 'void'; Params = 'GLenum light, GLenum pname, GLint param' }
        @{ Slot = 162; Name = 'glLightiv'; Return = 'void'; Params = 'GLenum light, GLenum pname, const GLint *params' }
        @{ Slot = 163; Name = 'glLightModelf'; Return = 'void'; Params = 'GLenum pname, GLfloat param' }
        @{ Slot = 164; Name = 'glLightModelfv'; Return = 'void'; Params = 'GLenum pname, const GLfloat *params' }
        @{ Slot = 165; Name = 'glLightModeli'; Return = 'void'; Params = 'GLenum pname, GLint param' }
        @{ Slot = 166; Name = 'glLightModeliv'; Return = 'void'; Params = 'GLenum pname, const GLint *params' }
        @{ Slot = 167; Name = 'glLineStipple'; Return = 'void'; Params = 'GLint factor, GLushort pattern' }
        @{ Slot = 168; Name = 'glLineWidth'; Return = 'void'; Params = 'GLfloat width' }
        @{ Slot = 169; Name = 'glMaterialf'; Return = 'void'; Params = 'GLenum face, GLenum pname, GLfloat param' }
        @{ Slot = 170; Name = 'glMaterialfv'; Return = 'void'; Params = 'GLenum face, GLenum pname, const GLfloat *params' }
        @{ Slot = 171; Name = 'glMateriali'; Return = 'void'; Params = 'GLenum face, GLenum pname, GLint param' }
        @{ Slot = 172; Name = 'glMaterialiv'; Return = 'void'; Params = 'GLenum face, GLenum pname, const GLint *params' }
        @{ Slot = 173; Name = 'glPointSize'; Return = 'void'; Params = 'GLfloat size' }
        @{ Slot = 174; Name = 'glPolygonMode'; Return = 'void'; Params = 'GLenum face, GLenum mode' }
        @{ Slot = 175; Name = 'glPolygonStipple'; Return = 'void'; Params = 'const GLubyte *mask' }
        @{ Slot = 176; Name = 'glScissor'; Return = 'void'; Params = 'GLint x, GLint y, GLsizei width, GLsizei height' }
        @{ Slot = 177; Name = 'glShadeModel'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 178; Name = 'glTexParameterf'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLfloat param' }
        @{ Slot = 179; Name = 'glTexParameterfv'; Return = 'void'; Params = 'GLenum target, GLenum pname, const GLfloat *params' }
        @{ Slot = 180; Name = 'glTexParameteri'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLint param' }
        @{ Slot = 181; Name = 'glTexParameteriv'; Return = 'void'; Params = 'GLenum target, GLenum pname, const GLint *params' }
        @{ Slot = 182; Name = 'glTexImage1D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint internalFormat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels' }
        @{ Slot = 183; Name = 'glTexImage2D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels' }
        @{ Slot = 184; Name = 'glTexEnvf'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLfloat param' }
        @{ Slot = 185; Name = 'glTexEnvfv'; Return = 'void'; Params = 'GLenum target, GLenum pname, const GLfloat *params' }
        @{ Slot = 186; Name = 'glTexEnvi'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLint param' }
        @{ Slot = 187; Name = 'glTexEnviv'; Return = 'void'; Params = 'GLenum target, GLenum pname, const GLint *params' }
        @{ Slot = 188; Name = 'glTexGend'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLdouble param' }
        @{ Slot = 189; Name = 'glTexGendv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, const GLdouble *params' }
        @{ Slot = 190; Name = 'glTexGenf'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLfloat param' }
        @{ Slot = 191; Name = 'glTexGenfv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, const GLfloat *params' }
        @{ Slot = 192; Name = 'glTexGeni'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLint param' }
        @{ Slot = 193; Name = 'glTexGeniv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, const GLint *params' }
        @{ Slot = 194; Name = 'glFeedbackBuffer'; Return = 'void'; Params = 'GLsizei size, GLenum type, GLfloat *buffer' }
        @{ Slot = 195; Name = 'glSelectBuffer'; Return = 'void'; Params = 'GLsizei size, GLuint *buffer' }
        @{ Slot = 196; Name = 'glRenderMode'; Return = 'GLint'; Params = 'GLenum mode' }
        @{ Slot = 197; Name = 'glInitNames'; Return = 'void'; Params = 'void' }
        @{ Slot = 198; Name = 'glLoadName'; Return = 'void'; Params = 'GLuint name' }
        @{ Slot = 199; Name = 'glPassThrough'; Return = 'void'; Params = 'GLfloat token' }
        @{ Slot = 200; Name = 'glPopName'; Return = 'void'; Params = 'void' }
        @{ Slot = 201; Name = 'glPushName'; Return = 'void'; Params = 'GLuint name' }
        @{ Slot = 202; Name = 'glDrawBuffer'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 203; Name = 'glClear'; Return = 'void'; Params = 'GLbitfield mask' }
        @{ Slot = 204; Name = 'glClearAccum'; Return = 'void'; Params = 'GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha' }
        @{ Slot = 205; Name = 'glClearIndex'; Return = 'void'; Params = 'GLfloat c' }
        @{ Slot = 206; Name = 'glClearColor'; Return = 'void'; Params = 'GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha' }
        @{ Slot = 207; Name = 'glClearStencil'; Return = 'void'; Params = 'GLint s' }
        @{ Slot = 208; Name = 'glClearDepth'; Return = 'void'; Params = 'GLclampd depth' }
        @{ Slot = 209; Name = 'glStencilMask'; Return = 'void'; Params = 'GLuint mask' }
        @{ Slot = 210; Name = 'glColorMask'; Return = 'void'; Params = 'GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha' }
        @{ Slot = 211; Name = 'glDepthMask'; Return = 'void'; Params = 'GLboolean flag' }
        @{ Slot = 212; Name = 'glIndexMask'; Return = 'void'; Params = 'GLuint mask' }
        @{ Slot = 213; Name = 'glAccum'; Return = 'void'; Params = 'GLenum op, GLfloat value' }
        @{ Slot = 214; Name = 'glDisable'; Return = 'void'; Params = 'GLenum cap' }
        @{ Slot = 215; Name = 'glEnable'; Return = 'void'; Params = 'GLenum cap' }
        @{ Slot = 216; Name = 'glFinish'; Return = 'void'; Params = 'void' }
        @{ Slot = 217; Name = 'glFlush'; Return = 'void'; Params = 'void' }
        @{ Slot = 218; Name = 'glPopAttrib'; Return = 'void'; Params = 'void' }
        @{ Slot = 219; Name = 'glPushAttrib'; Return = 'void'; Params = 'GLbitfield mask' }
        @{ Slot = 220; Name = 'glMap1d'; Return = 'void'; Params = 'GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points' }
        @{ Slot = 221; Name = 'glMap1f'; Return = 'void'; Params = 'GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points' }
        @{ Slot = 222; Name = 'glMap2d'; Return = 'void'; Params = 'GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points' }
        @{ Slot = 223; Name = 'glMap2f'; Return = 'void'; Params = 'GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points' }
        @{ Slot = 224; Name = 'glMapGrid1d'; Return = 'void'; Params = 'GLint un, GLdouble u1, GLdouble u2' }
        @{ Slot = 225; Name = 'glMapGrid1f'; Return = 'void'; Params = 'GLint un, GLfloat u1, GLfloat u2' }
        @{ Slot = 226; Name = 'glMapGrid2d'; Return = 'void'; Params = 'GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2' }
        @{ Slot = 227; Name = 'glMapGrid2f'; Return = 'void'; Params = 'GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2' }
        @{ Slot = 228; Name = 'glEvalCoord1d'; Return = 'void'; Params = 'GLdouble u' }
        @{ Slot = 229; Name = 'glEvalCoord1dv'; Return = 'void'; Params = 'const GLdouble *u' }
        @{ Slot = 230; Name = 'glEvalCoord1f'; Return = 'void'; Params = 'GLfloat u' }
        @{ Slot = 231; Name = 'glEvalCoord1fv'; Return = 'void'; Params = 'const GLfloat *u' }
        @{ Slot = 232; Name = 'glEvalCoord2d'; Return = 'void'; Params = 'GLdouble u, GLdouble v' }
        @{ Slot = 233; Name = 'glEvalCoord2dv'; Return = 'void'; Params = 'const GLdouble *u' }
        @{ Slot = 234; Name = 'glEvalCoord2f'; Return = 'void'; Params = 'GLfloat u, GLfloat v' }
        @{ Slot = 235; Name = 'glEvalCoord2fv'; Return = 'void'; Params = 'const GLfloat *u' }
        @{ Slot = 236; Name = 'glEvalMesh1'; Return = 'void'; Params = 'GLenum mode, GLint i1, GLint i2' }
        @{ Slot = 237; Name = 'glEvalPoint1'; Return = 'void'; Params = 'GLint i' }
        @{ Slot = 238; Name = 'glEvalMesh2'; Return = 'void'; Params = 'GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2' }
        @{ Slot = 239; Name = 'glEvalPoint2'; Return = 'void'; Params = 'GLint i, GLint j' }
        @{ Slot = 240; Name = 'glAlphaFunc'; Return = 'void'; Params = 'GLenum func, GLclampf ref' }
        @{ Slot = 241; Name = 'glBlendFunc'; Return = 'void'; Params = 'GLenum sfactor, GLenum dfactor' }
        @{ Slot = 242; Name = 'glLogicOp'; Return = 'void'; Params = 'GLenum opcode' }
        @{ Slot = 243; Name = 'glStencilFunc'; Return = 'void'; Params = 'GLenum func, GLint ref, GLuint mask' }
        @{ Slot = 244; Name = 'glStencilOp'; Return = 'void'; Params = 'GLenum fail, GLenum zfail, GLenum zpass' }
        @{ Slot = 245; Name = 'glDepthFunc'; Return = 'void'; Params = 'GLenum func' }
        @{ Slot = 246; Name = 'glPixelZoom'; Return = 'void'; Params = 'GLfloat xfactor, GLfloat yfactor' }
        @{ Slot = 247; Name = 'glPixelTransferf'; Return = 'void'; Params = 'GLenum pname, GLfloat param' }
        @{ Slot = 248; Name = 'glPixelTransferi'; Return = 'void'; Params = 'GLenum pname, GLint param' }
        @{ Slot = 249; Name = 'glPixelStoref'; Return = 'void'; Params = 'GLenum pname, GLfloat param' }
        @{ Slot = 250; Name = 'glPixelStorei'; Return = 'void'; Params = 'GLenum pname, GLint param' }
        @{ Slot = 251; Name = 'glPixelMapfv'; Return = 'void'; Params = 'GLenum map, GLint mapsize, const GLfloat *values' }
        @{ Slot = 252; Name = 'glPixelMapuiv'; Return = 'void'; Params = 'GLenum map, GLint mapsize, const GLuint *values' }
        @{ Slot = 253; Name = 'glPixelMapusv'; Return = 'void'; Params = 'GLenum map, GLint mapsize, const GLushort *values' }
        @{ Slot = 254; Name = 'glReadBuffer'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 255; Name = 'glCopyPixels'; Return = 'void'; Params = 'GLint x, GLint y, GLsizei width, GLsizei height, GLenum type' }
        @{ Slot = 256; Name = 'glReadPixels'; Return = 'void'; Params = 'GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels' }
        @{ Slot = 257; Name = 'glDrawPixels'; Return = 'void'; Params = 'GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels' }
        @{ Slot = 258; Name = 'glGetBooleanv'; Return = 'void'; Params = 'GLenum pname, GLboolean *params' }
        @{ Slot = 259; Name = 'glGetClipPlane'; Return = 'void'; Params = 'GLenum plane, GLdouble *equation' }
        @{ Slot = 260; Name = 'glGetDoublev'; Return = 'void'; Params = 'GLenum pname, GLdouble *params' }
        @{ Slot = 261; Name = 'glGetError'; Return = 'GLenum'; Params = 'void' }
        @{ Slot = 262; Name = 'glGetFloatv'; Return = 'void'; Params = 'GLenum pname, GLfloat *params' }
        @{ Slot = 263; Name = 'glGetIntegerv'; Return = 'void'; Params = 'GLenum pname, GLint *params' }
        @{ Slot = 264; Name = 'glGetLightfv'; Return = 'void'; Params = 'GLenum light, GLenum pname, GLfloat *params' }
        @{ Slot = 265; Name = 'glGetLightiv'; Return = 'void'; Params = 'GLenum light, GLenum pname, GLint *params' }
        @{ Slot = 266; Name = 'glGetMapdv'; Return = 'void'; Params = 'GLenum target, GLenum query, GLdouble *v' }
        @{ Slot = 267; Name = 'glGetMapfv'; Return = 'void'; Params = 'GLenum target, GLenum query, GLfloat *v' }
        @{ Slot = 268; Name = 'glGetMapiv'; Return = 'void'; Params = 'GLenum target, GLenum query, GLint *v' }
        @{ Slot = 269; Name = 'glGetMaterialfv'; Return = 'void'; Params = 'GLenum face, GLenum pname, GLfloat *params' }
        @{ Slot = 270; Name = 'glGetMaterialiv'; Return = 'void'; Params = 'GLenum face, GLenum pname, GLint *params' }
        @{ Slot = 271; Name = 'glGetPixelMapfv'; Return = 'void'; Params = 'GLenum map, GLfloat *values' }
        @{ Slot = 272; Name = 'glGetPixelMapuiv'; Return = 'void'; Params = 'GLenum map, GLuint *values' }
        @{ Slot = 273; Name = 'glGetPixelMapusv'; Return = 'void'; Params = 'GLenum map, GLushort *values' }
        @{ Slot = 274; Name = 'glGetPolygonStipple'; Return = 'void'; Params = 'GLubyte *mask' }
        @{ Slot = 275; Name = 'glGetString'; Return = 'const GLubyte *'; Params = 'GLenum name' }
        @{ Slot = 276; Name = 'glGetTexEnvfv'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLfloat *params' }
        @{ Slot = 277; Name = 'glGetTexEnviv'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLint *params' }
        @{ Slot = 278; Name = 'glGetTexGendv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLdouble *params' }
        @{ Slot = 279; Name = 'glGetTexGenfv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLfloat *params' }
        @{ Slot = 280; Name = 'glGetTexGeniv'; Return = 'void'; Params = 'GLenum coord, GLenum pname, GLint *params' }
        @{ Slot = 281; Name = 'glGetTexImage'; Return = 'void'; Params = 'GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels' }
        @{ Slot = 282; Name = 'glGetTexParameterfv'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLfloat *params' }
        @{ Slot = 283; Name = 'glGetTexParameteriv'; Return = 'void'; Params = 'GLenum target, GLenum pname, GLint *params' }
        @{ Slot = 284; Name = 'glGetTexLevelParameterfv'; Return = 'void'; Params = 'GLenum target, GLint level, GLenum pname, GLfloat *params' }
        @{ Slot = 285; Name = 'glGetTexLevelParameteriv'; Return = 'void'; Params = 'GLenum target, GLint level, GLenum pname, GLint *params' }
        @{ Slot = 286; Name = 'glIsEnabled'; Return = 'GLboolean'; Params = 'GLenum cap' }
        @{ Slot = 287; Name = 'glIsList'; Return = 'GLboolean'; Params = 'GLuint list' }
        @{ Slot = 288; Name = 'glDepthRange'; Return = 'void'; Params = 'GLclampd near_val, GLclampd far_val' }
        @{ Slot = 289; Name = 'glFrustum'; Return = 'void'; Params = 'GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val' }
        @{ Slot = 290; Name = 'glLoadIdentity'; Return = 'void'; Params = 'void' }
        @{ Slot = 291; Name = 'glLoadMatrixf'; Return = 'void'; Params = 'const GLfloat *m' }
        @{ Slot = 292; Name = 'glLoadMatrixd'; Return = 'void'; Params = 'const GLdouble *m' }
        @{ Slot = 293; Name = 'glMatrixMode'; Return = 'void'; Params = 'GLenum mode' }
        @{ Slot = 294; Name = 'glMultMatrixf'; Return = 'void'; Params = 'const GLfloat *m' }
        @{ Slot = 295; Name = 'glMultMatrixd'; Return = 'void'; Params = 'const GLdouble *m' }
        @{ Slot = 296; Name = 'glOrtho'; Return = 'void'; Params = 'GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val' }
        @{ Slot = 297; Name = 'glPopMatrix'; Return = 'void'; Params = 'void' }
        @{ Slot = 298; Name = 'glPushMatrix'; Return = 'void'; Params = 'void' }
        @{ Slot = 299; Name = 'glRotated'; Return = 'void'; Params = 'GLdouble angle, GLdouble x, GLdouble y, GLdouble z' }
        @{ Slot = 300; Name = 'glRotatef'; Return = 'void'; Params = 'GLfloat angle, GLfloat x, GLfloat y, GLfloat z' }
        @{ Slot = 301; Name = 'glScaled'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z' }
        @{ Slot = 302; Name = 'glScalef'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z' }
        @{ Slot = 303; Name = 'glTranslated'; Return = 'void'; Params = 'GLdouble x, GLdouble y, GLdouble z' }
        @{ Slot = 304; Name = 'glTranslatef'; Return = 'void'; Params = 'GLfloat x, GLfloat y, GLfloat z' }
        @{ Slot = 305; Name = 'glViewport'; Return = 'void'; Params = 'GLint x, GLint y, GLsizei width, GLsizei height' }
        @{ Slot = 306; Name = 'glArrayElement'; Return = 'void'; Params = 'GLint i' }
        @{ Slot = 307; Name = 'glBindTexture'; Return = 'void'; Params = 'GLenum target, GLuint texture' }
        @{ Slot = 308; Name = 'glColorPointer'; Return = 'void'; Params = 'GLint size, GLenum type, GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 309; Name = 'glDisableClientState'; Return = 'void'; Params = 'GLenum cap' }
        @{ Slot = 310; Name = 'glDrawArrays'; Return = 'void'; Params = 'GLenum mode, GLint first, GLsizei count' }
        @{ Slot = 311; Name = 'glDrawElements'; Return = 'void'; Params = 'GLenum mode, GLsizei count, GLenum type, const GLvoid *indices' }
        @{ Slot = 312; Name = 'glEdgeFlagPointer'; Return = 'void'; Params = 'GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 313; Name = 'glEnableClientState'; Return = 'void'; Params = 'GLenum cap' }
        @{ Slot = 314; Name = 'glIndexPointer'; Return = 'void'; Params = 'GLenum type, GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 315; Name = 'glIndexub'; Return = 'void'; Params = 'GLubyte c' }
        @{ Slot = 316; Name = 'glIndexubv'; Return = 'void'; Params = 'const GLubyte *c' }
        @{ Slot = 317; Name = 'glInterleavedArrays'; Return = 'void'; Params = 'GLenum format, GLsizei stride, const GLvoid *pointer' }
        @{ Slot = 318; Name = 'glNormalPointer'; Return = 'void'; Params = 'GLenum type, GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 319; Name = 'glPolygonOffset'; Return = 'void'; Params = 'GLfloat factor, GLfloat units' }
        @{ Slot = 320; Name = 'glTexCoordPointer'; Return = 'void'; Params = 'GLint size, GLenum type, GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 321; Name = 'glVertexPointer'; Return = 'void'; Params = 'GLint size, GLenum type, GLsizei stride, const GLvoid *ptr' }
        @{ Slot = 322; Name = 'glAreTexturesResident'; Return = 'GLboolean'; Params = 'GLsizei n, const GLuint *textures, GLboolean *residences' }
        @{ Slot = 323; Name = 'glCopyTexImage1D'; Return = 'void'; Params = 'GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border' }
        @{ Slot = 324; Name = 'glCopyTexImage2D'; Return = 'void'; Params = 'GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border' }
        @{ Slot = 325; Name = 'glCopyTexSubImage1D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width' }
        @{ Slot = 326; Name = 'glCopyTexSubImage2D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height' }
        @{ Slot = 327; Name = 'glDeleteTextures'; Return = 'void'; Params = 'GLsizei n, const GLuint *textures' }
        @{ Slot = 328; Name = 'glGenTextures'; Return = 'void'; Params = 'GLsizei n, GLuint *textures' }
        @{ Slot = 329; Name = 'glGetPointerv'; Return = 'void'; Params = 'GLenum pname, GLvoid **params' }
        @{ Slot = 330; Name = 'glIsTexture'; Return = 'GLboolean'; Params = 'GLuint texture' }
        @{ Slot = 331; Name = 'glPrioritizeTextures'; Return = 'void'; Params = 'GLsizei n, const GLuint *textures, const GLclampf *priorities' }
        @{ Slot = 332; Name = 'glTexSubImage1D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels' }
        @{ Slot = 333; Name = 'glTexSubImage2D'; Return = 'void'; Params = 'GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels' }
        @{ Slot = 334; Name = 'glPopClientAttrib'; Return = 'void'; Params = 'void' }
        @{ Slot = 335; Name = 'glPushClientAttrib'; Return = 'void'; Params = 'GLbitfield mask' }
    )
}
