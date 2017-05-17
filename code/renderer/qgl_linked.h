/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifdef USE_STATIC_GL

#define qglAlphaFunc glAlphaFunc
#define qglArrayElement glArrayElement
#define qglBegin glBegin
#define qglBindTexture glBindTexture
#define qglBlendFunc glBlendFunc
#define qglClear glClear
#define qglClearColor glClearColor
#define qglClearDepth glClearDepth
#define qglClearStencil glClearStencil
#define qglClipPlane glClipPlane
#define qglColor3f glColor3f
#define qglColor3fv glColor3fv
#define qglColor4f glColor4f
#define qglColor4ubv glColor4ubv
#define qglColorMask glColorMask
#define qglColorPointer glColorPointer
#define qglCopyPixels glCopyPixels
#define qglCopyTexImage2D glCopyTexImage2D
#define qglCopyTexSubImage2D glCopyTexSubImage2D
#define qglCullFace glCullFace
#define qglDeleteTextures glDeleteTextures
#define qglDepthFunc glDepthFunc
#define qglDepthMask glDepthMask
#define qglDepthRange glDepthRange
#define qglDisable glDisable
#define qglDisableClientState glDisableClientState
#define qglDrawBuffer glDrawBuffer
#define qglDrawElements glDrawElements
#define qglEnable glEnable
#define qglEnableClientState glEnableClientState
#define qglEnd glEnd
#define qglFinish glFinish
#define qglGetBooleanv glGetBooleanv
#define qglGetError glGetError
#define qglGetIntegerv glGetIntegerv
#define qglGetString glGetString
#define qglHint glHint
#define qglLineWidth glLineWidth
#define qglLoadIdentity glLoadIdentity
#define qglLoadMatrixf glLoadMatrixf
#define qglMatrixMode glMatrixMode
#define qglNormalPointer glNormalPointer
#define qglOrtho glOrtho
#define qglPolygonMode glPolygonMode
#define qglPolygonOffset glPolygonOffset
#define qglPopMatrix glPopMatrix
#define qglPushMatrix glPushMatrix
#define qglReadPixels glReadPixels
#define qglScissor glScissor
#define qglShadeModel glShadeModel
#define qglStencilFunc glStencilFunc
#define qglStencilMask glStencilMask
#define qglStencilOp glStencilOp
#define qglTexCoord2f glTexCoord2f
#define qglTexCoord2fv glTexCoord2fv
#define qglTexCoordPointer glTexCoordPointer
#define	qglTexEnvi glTexEnvi
#define qglTexImage1D glTexImage1D
#define qglTexImage2D glTexImage2D
#define qglTexParameterf glTexParameterf
#define qglTexParameteri glTexParameteri
#define qglTexSubImage1D glTexSubImage1D
#define qglTexSubImage2D glTexSubImage2D
#define qglTranslatef glTranslatef
#define qglVertex2f glVertex2f
#define qglVertex3f glVertex3f
#define qglVertex3fv glVertex3fv
#define qglVertexPointer glVertexPointer
#define qglViewport glViewport

#else // !USE_STATIC_GL

extern  void ( APIENTRY * qglAlphaFunc )(GLenum func, GLclampf ref);
extern  void ( APIENTRY * qglArrayElement )(GLint i);
extern  void ( APIENTRY * qglBegin )(GLenum mode);
extern  void ( APIENTRY * qglBindTexture )(GLenum target, GLuint texture);
extern  void ( APIENTRY * qglBlendFunc )(GLenum sfactor, GLenum dfactor);
extern  void ( APIENTRY * qglClear )(GLbitfield mask);
extern  void ( APIENTRY * qglClearColor )(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
extern  void ( APIENTRY * qglClearDepth )(GLclampd depth);
extern  void ( APIENTRY * qglClearStencil )(GLint s);
extern  void ( APIENTRY * qglClipPlane )(GLenum plane, const GLdouble *equation);
extern  void ( APIENTRY * qglColor3f )(GLfloat red, GLfloat green, GLfloat blue);
extern  void ( APIENTRY * qglColor3fv )(const GLfloat *v);
extern  void ( APIENTRY * qglColor4f )(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
extern  void ( APIENTRY * qglColor4ubv )(const GLubyte *v);
extern  void ( APIENTRY * qglColorMask )(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
extern  void ( APIENTRY * qglColorPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( APIENTRY * qglCopyPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type);
extern  void ( APIENTRY * qglCopyTexImage2D )(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border);
extern  void ( APIENTRY * qglCopyTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height);
extern  void ( APIENTRY * qglCullFace )(GLenum mode);
extern  void ( APIENTRY * qglDeleteTextures )(GLsizei n, const GLuint *textures);
extern  void ( APIENTRY * qglDepthFunc )(GLenum func);
extern  void ( APIENTRY * qglDepthMask )(GLboolean flag);
extern  void ( APIENTRY * qglDepthRange )(GLclampd zNear, GLclampd zFar);
extern  void ( APIENTRY * qglDisable )(GLenum cap);
extern  void ( APIENTRY * qglDisableClientState )(GLenum array);
extern  void ( APIENTRY * qglDrawBuffer )(GLenum mode);
extern  void ( APIENTRY * qglDrawElements )(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);
extern  void ( APIENTRY * qglEnable )(GLenum cap);
extern  void ( APIENTRY * qglEnableClientState )(GLenum array);
extern  void ( APIENTRY * qglEnd )(void);
extern  void ( APIENTRY * qglFinish )(void);
extern  void ( APIENTRY * qglGenTextures )(GLsizei n, GLuint *textures);
extern  void ( APIENTRY * qglGetBooleanv )(GLenum pname, GLboolean *params);
extern  GLenum ( APIENTRY * qglGetError )(void);
extern  void ( APIENTRY * qglGetIntegerv )(GLenum pname, GLint *params);
const GLubyte * ( APIENTRY * qglGetString )(GLenum name);
extern  void ( APIENTRY * qglHint )(GLenum target, GLenum mode);
extern  void ( APIENTRY * qglLineWidth )(GLfloat width);
extern  void ( APIENTRY * qglLoadIdentity )(void);
extern  void ( APIENTRY * qglLoadMatrixf )(const GLfloat *m);
extern  void ( APIENTRY * qglMatrixMode )(GLenum mode);
extern  void ( APIENTRY * qglNormalPointer )(GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( APIENTRY * qglOrtho )(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
extern  void ( APIENTRY * qglPolygonMode )(GLenum face, GLenum mode);
extern  void ( APIENTRY * qglPolygonOffset )(GLfloat factor, GLfloat units);
extern  void ( APIENTRY * qglPopMatrix )(void);
extern  void ( APIENTRY * qglPushMatrix )(void);
extern  void ( APIENTRY * qglReadBuffer )(GLenum mode);
extern  void ( APIENTRY * qglReadPixels )(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels);
extern  void ( APIENTRY * qglScissor )(GLint x, GLint y, GLsizei width, GLsizei height);
extern  void ( APIENTRY * qglShadeModel )(GLenum mode);
extern  void ( APIENTRY * qglStencilFunc )(GLenum func, GLint ref, GLuint mask);
extern  void ( APIENTRY * qglStencilMask )(GLuint mask);
extern  void ( APIENTRY * qglStencilOp )(GLenum fail, GLenum zfail, GLenum zpass);
extern  void ( APIENTRY * qglTexCoord2f )(GLfloat s, GLfloat t);
extern  void ( APIENTRY * qglTexCoord2fv )(const GLfloat *v);
extern  void ( APIENTRY * qglTexCoordPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( APIENTRY * qglTexEnvi )(GLenum target, GLenum pname, GLint param);
extern  void ( APIENTRY * qglTexImage1D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( APIENTRY * qglTexImage2D )(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( APIENTRY * qglTexParameterf )(GLenum target, GLenum pname, GLfloat param);
extern  void ( APIENTRY * qglTexParameteri )(GLenum target, GLenum pname, GLint param);
extern  void ( APIENTRY * qglTexSubImage1D )(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( APIENTRY * qglTexSubImage2D )(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
extern  void ( APIENTRY * qglTranslatef )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( APIENTRY * qglVertex2f )(GLfloat x, GLfloat y);
extern  void ( APIENTRY * qglVertex3f )(GLfloat x, GLfloat y, GLfloat z);
extern  void ( APIENTRY * qglVertex3fv )(const GLfloat *v);
extern  void ( APIENTRY * qglVertexPointer )(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
extern  void ( APIENTRY * qglViewport )(GLint x, GLint y, GLsizei width, GLsizei height);

#endif // !USE_STATIC_GL