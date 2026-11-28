# How each OpenGL 1.1 slot behaves while a display list is compiled
# (OpenGL 1.1 section 5.4), read by scripts\lib\gl-dispatch.ps1 beside
# gl_entrypoints.psd1. docs\plans\opengl-display-lists-and-texgen.md.
#
# Every slot is one of:
#   compiled  the default: its arguments, and what its pointers point at,
#             are copied into the list, and replay calls the slot's
#             implementation with them;
#   Execute   5.4's commands that are executed immediately and never
#             compiled (every glGet* too, by name), and glNewList/glEndList;
#   Special   compiled by hand in gl_icd.c (the vertex array draws, which
#             take their data from the arrays when compiled);
#   Deferred  compiled by the specification, executed now here and logged
#             once by slot: the image-carrying commands, not done in the
#             first version.
#
# A pointer argument's byte count is inferred for the vector forms
# (glColor3fv: three GLfloats; glRectfv: two of its type per pointer); any
# other compiled slot with a pointer needs a Bytes entry, a C expression
# over the slot's parameter names, or generation fails.
@{
    Execute = @(
        'glNewList', 'glEndList', 'glGenLists', 'glDeleteLists', 'glIsList',
        'glFeedbackBuffer', 'glSelectBuffer', 'glRenderMode', 'glReadPixels',
        'glPixelStoref', 'glPixelStorei', 'glFinish', 'glFlush',
        'glIsEnabled',
        'glEnableClientState', 'glDisableClientState', 'glVertexPointer',
        'glNormalPointer', 'glColorPointer', 'glIndexPointer',
        'glTexCoordPointer', 'glEdgeFlagPointer', 'glInterleavedArrays',
        'glGenTextures', 'glDeleteTextures', 'glAreTexturesResident',
        'glIsTexture', 'glPushClientAttrib', 'glPopClientAttrib'
    )
    Special = @('glArrayElement', 'glDrawArrays', 'glDrawElements')
    Deferred = @(
        'glTexImage1D', 'glTexImage2D', 'glTexSubImage1D', 'glTexSubImage2D',
        'glDrawPixels', 'glBitmap', 'glPolygonStipple', 'glPixelMapfv',
        'glPixelMapuiv', 'glPixelMapusv', 'glMap1d', 'glMap1f', 'glMap2d',
        'glMap2f', 'glPrioritizeTextures'
    )
    Bytes = @{
        glCallLists       = 'v9x_gl_dlist_lists_bytes(n, type)'
        glClipPlane       = '4u * (v9x_u32)sizeof(GLdouble)'
        glLoadMatrixf     = '16u * (v9x_u32)sizeof(GLfloat)'
        glLoadMatrixd     = '16u * (v9x_u32)sizeof(GLdouble)'
        glMultMatrixf     = '16u * (v9x_u32)sizeof(GLfloat)'
        glMultMatrixd     = '16u * (v9x_u32)sizeof(GLdouble)'
        glFogfv           = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glFogiv           = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glLightfv         = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glLightiv         = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glLightModelfv    = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glLightModeliv    = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glMaterialfv      = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glMaterialiv      = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glTexParameterfv  = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glTexParameteriv  = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glTexEnvfv        = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glTexEnviv        = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glTexGenfv        = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLfloat)'
        glTexGeniv        = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLint)'
        glTexGendv        = 'v9x_gl_dlist_pname_count(pname) * (v9x_u32)sizeof(GLdouble)'
        glIndexubv        = '(v9x_u32)sizeof(GLubyte)'
        glEdgeFlagv       = '(v9x_u32)sizeof(GLboolean)'
    }
}
