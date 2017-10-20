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

#define QGL_Core_PROCS \
	GLE( void, glAlphaFunc, GLenum func, GLclampf ref ) \
	GLE( void, glArrayElement, GLint i ) \
	GLE( void, glBegin, GLenum mode ) \
	GLE( void, glBindTexture, GLenum target, GLuint texture ) \
	GLE( void, glBlendFunc, GLenum sfactor, GLenum dfactor ) \
	GLE( void, glClear, GLbitfield mask ) \
	GLE( void, glClearColor, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha ) \
	GLE( void, glClearDepth, GLclampd depth ) \
	GLE( void, glClearStencil, GLint s ) \
	GLE( void, glClipPlane, GLenum plane, const GLdouble *equation ) \
	GLE( void, glColor3f, GLfloat red, GLfloat green, GLfloat blue ) \
	GLE( void, glColor3fv, const GLfloat *v ) \
	GLE( void, glColor4f, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha ) \
	GLE( void, glColor4ubv, const GLubyte *v ) \
	GLE( void, glColorMask, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha ) \
	GLE( void, glColorPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glCopyPixels, GLint x, GLint y, GLsizei width, GLsizei height, GLenum type ) \
	GLE( void, glCopyTexImage2D, GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border ) \
	GLE( void, glCopyTexSubImage2D, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width, GLsizei height ) \
	GLE( void, glCullFace, GLenum mode ) \
	GLE( void, glDeleteTextures, GLsizei n, const GLuint *textures ) \
	GLE( void, glDepthFunc, GLenum func ) \
	GLE( void, glDepthMask, GLboolean flag ) \
	GLE( void, glDepthRange, GLclampd zNear, GLclampd zFar ) \
	GLE( void, glDisable, GLenum cap ) \
	GLE( void, glDisableClientState, GLenum array ) \
	GLE( void, glDrawBuffer, GLenum mode ) \
	GLE( void, glDrawElements, GLenum mode, GLsizei count, GLenum type, const GLvoid *indices ) \
	GLE( void, glEnable, GLenum cap ) \
	GLE( void, glEnableClientState, GLenum array ) \
	GLE( void, glEnd, void ) \
	GLE( void, glFinish, void ) \
	GLE( void, glGenTextures, GLsizei n, GLuint *textures ) \
	GLE( void, glGetBooleanv, GLenum pname, GLboolean *params ) \
	GLE( GLenum, glGetError, void ) \
	GLE( void, glGetIntegerv, GLenum pname, GLint *params ) \
	GLE( const GLubyte*, glGetString, GLenum name ) \
	GLE( void, glHint, GLenum target, GLenum mode ) \
	GLE( void, glLineWidth, GLfloat width ) \
	GLE( void, glLoadIdentity, void ) \
	GLE( void, glLoadMatrixf, const GLfloat *m ) \
	GLE( void, glMatrixMode, GLenum mode ) \
	GLE( void, glNormalPointer, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glOrtho, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar ) \
	GLE( void, glPolygonMode, GLenum face, GLenum mode ) \
	GLE( void, glPolygonOffset, GLfloat factor, GLfloat units ) \
	GLE( void, glPopMatrix, void ) \
	GLE( void, glPushMatrix, void ) \
	GLE( void, glReadBuffer, GLenum mode ) \
	GLE( void, glReadPixels, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels ) \
	GLE( void, glScissor, GLint x, GLint y, GLsizei width, GLsizei height ) \
	GLE( void, glShadeModel, GLenum mode ) \
	GLE( void, glStencilFunc, GLenum func, GLint ref, GLuint mask ) \
	GLE( void, glStencilMask, GLuint mask ) \
	GLE( void, glStencilOp, GLenum fail, GLenum zfail, GLenum zpass ) \
	GLE( void, glTexCoord2f, GLfloat s, GLfloat t ) \
	GLE( void, glTexCoord2fv, const GLfloat *v ) \
	GLE( void, glTexCoordPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glTexEnvi, GLenum target, GLenum pname, GLint param ) \
	GLE( void, glTexImage2D, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels ) \
	GLE( void, glTexParameterf, GLenum target, GLenum pname, GLfloat param ) \
	GLE( void, glTexParameteri, GLenum target, GLenum pname, GLint param ) \
	GLE( void, glTexSubImage1D, GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels ) \
	GLE( void, glTexSubImage2D, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels ) \
	GLE( void, glTranslatef, GLfloat x, GLfloat y, GLfloat z ) \
	GLE( void, glVertex2f, GLfloat x, GLfloat y ) \
	GLE( void, glVertex3f, GLfloat x, GLfloat y, GLfloat z ) \
	GLE( void, glVertex3fv, const GLfloat *v ) \
	GLE( void, glVertexPointer, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer ) \
	GLE( void, glViewport, GLint x, GLint y, GLsizei width, GLsizei height ) \

#define QGL_Ext_PROCS \
	GLE( void, glMultiTexCoord2fARB, GLenum texture, GLfloat s, GLfloat t ) \
	GLE( void, glActiveTextureARB, GLenum texture ) \
	GLE( void, glClientActiveTextureARB, GLenum texture ) \
	GLE( void, glLockArraysEXT, GLint, GLint) \
	GLE( void, glUnlockArraysEXT, void )

#define QGL_Win32_PROCS \
	GLE( HGLRC, wglCreateContext, HDC ) \
	GLE( BOOL,  wglDeleteContext ,HGLRC ) \
	GLE( HGLRC, wglGetCurrentContext, VOID ) \
	GLE( PROC,  wglGetProcAddress, LPCSTR ) \
	GLE( BOOL,  wglMakeCurrent, HDC, HGLRC )

#ifdef _WIN32
#define QGL_Swp_PROCS \
	GLE( BOOL,	wglSwapIntervalEXT, int interval )
#else
#define QGL_Swp_PROCS \
	GLE( void,	glXSwapIntervalEXT, Display *dpy, GLXDrawable drawable, int interval ) \
	GLE( int,	glXSwapIntervalMESA, unsigned interval ) \
	GLE( int,	glXSwapIntervalSGI, int interval )
#endif

#define QGL_LinX11_PROCS \
	GLE( XVisualInfo*, glXChooseVisual, Display *dpy, int screen, int *attribList ) \
	GLE( GLXContext, glXCreateContext, Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct ) \
	GLE( void, glXDestroyContext, Display *dpy, GLXContext ctx ) \
	GLE( Bool, glXMakeCurrent, Display *dpy, GLXDrawable drawable, GLXContext ctx) \
	GLE( void, glXCopyContext, Display *dpy, GLXContext src, GLXContext dst, GLuint mask ) \
	GLE( void, glXSwapBuffers, Display *dpy, GLXDrawable drawable )

#define QGL_LinGPA_PROC \
	GLE( void*, wglGetProcAddress, const char *symbol )

#define GLE( ret, name, ... ) extern ret ( APIENTRY * q##name )( __VA_ARGS__ );
	QGL_Core_PROCS;
	QGL_Ext_PROCS;
	QGL_Swp_PROCS;
#ifdef _WIN32
	QGL_Win32_PROCS;
//#endif
//#if ( (defined __linux__ )  || (defined __FreeBSD__ ) || (defined __sun) )
#else // assume in opposition to win32
	QGL_LinX11_PROCS;
	QGL_LinGPA_PROC;
#endif
#undef GLE
