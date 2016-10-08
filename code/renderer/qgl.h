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
/*
** QGL.H
*/

#ifndef __QGL_H__
#define __QGL_H__

#if defined( _WIN32 )
#if _MSC_VER
#pragma warning (disable: 4201)
#pragma warning (disable: 4214)
#pragma warning (disable: 4514)
#pragma warning (disable: 4032)
#pragma warning (disable: 4201)
#pragma warning (disable: 4214)
#endif
#include <windows.h>
#include <GL/gl.h>
#elif defined( __linux__ ) || defined(__FreeBSD__) || defined( __sun )
#include <GL/gl.h>
#include <GL/glx.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif

//===========================================================================
// <Timbo> I hate this section so much

/*
** multitexture extension definitions
*/
#if !defined(__sun)

#define GL_ACTIVE_TEXTURE_ARB               0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB        0x84E1
#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#define GL_TEXTURE0_ARB                     0x84C0
#define GL_TEXTURE1_ARB                     0x84C1
#define GL_TEXTURE2_ARB                     0x84C2
#define GL_TEXTURE3_ARB                     0x84C3

#else

#define GL_MAX_ACTIVE_TEXTURES_ARB          0x84E2

#endif /* defined(__sun) */

// anisotropic filtering constants
#define GL_TEXTURE_MAX_ANISOTROPY_EXT       0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT   0x84FF

// define for skyboxes without black seams
#if !defined(GL_VERSION_1_2) && !defined(GL_CLAMP_TO_EDGE)
   #define GL_CLAMP_TO_EDGE                  0x812F
#endif

//WGL functions
#if defined (WIN32)

typedef void (APIENTRY * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRY * PFNGLACTIVETEXTUREARBPROC) (GLenum target);
typedef void (APIENTRY * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum target);

extern HGLRC ( WINAPI * qwglCreateContext)(HDC);
extern BOOL  ( WINAPI * qwglDeleteContext)(HGLRC);
extern HGLRC ( WINAPI * qwglGetCurrentContext)(VOID);
extern PROC  ( WINAPI * qwglGetProcAddress)(LPCSTR);
extern BOOL  ( WINAPI * qwglMakeCurrent)(HDC, HGLRC);
extern BOOL ( WINAPI * qwglSwapIntervalEXT)( int interval );

#endif // WIN32


//GLX Functions
#if ( (defined __linux__ )  || (defined __FreeBSD__ ) || (defined __sun) )

extern void* (*qwglGetProcAddress)( const char *symbol );
extern XVisualInfo * (*qglXChooseVisual)( Display *dpy, int screen, int *attribList );
extern GLXContext (*qglXCreateContext)( Display *dpy, XVisualInfo *vis, GLXContext shareList, Bool direct );
extern void (*qglXDestroyContext)( Display *dpy, GLXContext ctx );
extern Bool (*qglXMakeCurrent)( Display *dpy, GLXDrawable drawable, GLXContext ctx);
extern void (*qglXCopyContext)( Display *dpy, GLXContext src, GLXContext dst, GLuint mask );
extern void (*qglXSwapBuffers)( Display *dpy, GLXDrawable drawable );

#endif // __linux__ || __FreeBSD__ || __sun


/*
** extension constants
*/
// S3TC compression constants
#define GL_RGB_S3TC							0x83A0
#define GL_RGB4_S3TC						0x83A1

#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT   0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT  0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT  0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT  0x83F3

#ifndef GL_EXT_texture_sRGB
#define GL_EXT_texture_sRGB
#define GL_SRGB_EXT                                       0x8C40
#define GL_SRGB8_EXT                                      0x8C41
#define GL_SRGB_ALPHA_EXT                                 0x8C42
#define GL_SRGB8_ALPHA8_EXT                               0x8C43
#define GL_SLUMINANCE_ALPHA_EXT                           0x8C44
#define GL_SLUMINANCE8_ALPHA8_EXT                         0x8C45
#define GL_SLUMINANCE_EXT                                 0x8C46
#define GL_SLUMINANCE8_EXT                                0x8C47
#define GL_COMPRESSED_SRGB_EXT                            0x8C48
#define GL_COMPRESSED_SRGB_ALPHA_EXT                      0x8C49
#define GL_COMPRESSED_SLUMINANCE_EXT                      0x8C4A
#define GL_COMPRESSED_SLUMINANCE_ALPHA_EXT                0x8C4B
#define GL_COMPRESSED_SRGB_S3TC_DXT1_EXT                  0x8C4C
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT            0x8C4D
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT            0x8C4E
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT            0x8C4F
#endif

// extensions will be function pointers on all platforms
extern	void ( APIENTRY * qglMultiTexCoord2fARB )( GLenum texture, GLfloat s, GLfloat t );
extern	void ( APIENTRY * qglActiveTextureARB )( GLenum texture );
extern	void ( APIENTRY * qglClientActiveTextureARB )( GLenum texture );

extern	void ( APIENTRY * qglLockArraysEXT) (GLint, GLint);
extern	void ( APIENTRY * qglUnlockArraysEXT) (void);

//===========================================================================

// non-dlopening systems will just redefine qgl* to gl*
#include "qgl_linked.h"

// renderer2 extensions
#include "glext.h"

// GL function loader, based on https://gist.github.com/rygorous/16796a0c876cf8a5f542caddb55bce8a

// OpenGL 1.2, was GL_EXT_draw_range_elements
#define QGL_1_2_PROCS \
	GLE(void, DrawRangeElements, GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const GLvoid *indices) \

// OpenGL 1.3, was GL_ARB_texture_compression
#define QGL_1_3_PROCS \
	GLE(void, CompressedTexImage2D, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data) \
	GLE(void, CompressedTexSubImage2D, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const void *data) \

// OpenGL 1.4, was GL_EXT_multi_draw_arrays
#define QGL_1_4_PROCS \
	GLE(void, MultiDrawElements, GLenum mode, const GLsizei *count, GLenum type, const GLvoid* *indices, GLsizei primcount) \

// OpenGL 1.5, was GL_ARB_vertex_buffer_object and GL_ARB_occlusion_query
#define QGL_1_5_PROCS \
	GLE(void, GenQueries, GLsizei n, GLuint *ids) \
	GLE(void, DeleteQueries, GLsizei n, const GLuint *ids) \
	GLE(void, BeginQuery, GLenum target, GLuint id) \
	GLE(void, EndQuery, GLenum target) \
	GLE(void, GetQueryObjectiv, GLuint id, GLenum pname, GLint *params) \
	GLE(void, GetQueryObjectuiv, GLuint id, GLenum pname, GLuint *params) \
	GLE(void, BindBuffer, GLenum target, GLuint buffer) \
	GLE(void, DeleteBuffers, GLsizei n, const GLuint *buffers) \
	GLE(void, GenBuffers, GLsizei n, GLuint *buffers) \
	GLE(void, BufferData, GLenum target, GLsizeiptr size, const void *data, GLenum usage) \
	GLE(void, BufferSubData, GLenum target, GLintptr offset, GLsizeiptr size, const void *data) \

// OpenGL 2.0, was GL_ARB_shading_language_100, GL_ARB_vertex_program, GL_ARB_shader_objects, and GL_ARB_vertex_shader
#define QGL_2_0_PROCS \
	GLE(void, AttachShader, GLuint program, GLuint shader) \
	GLE(void, BindAttribLocation, GLuint program, GLuint index, const GLchar *name) \
	GLE(void, CompileShader, GLuint shader) \
	GLE(GLuint, CreateProgram, void) \
	GLE(GLuint, CreateShader, GLenum type) \
	GLE(void, DeleteProgram, GLuint program) \
	GLE(void, DeleteShader, GLuint shader) \
	GLE(void, DetachShader, GLuint program, GLuint shader) \
	GLE(void, DisableVertexAttribArray, GLuint index) \
	GLE(void, EnableVertexAttribArray, GLuint index) \
	GLE(void, GetActiveUniform, GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) \
	GLE(void, GetProgramiv, GLuint program, GLenum pname, GLint *params) \
	GLE(void, GetProgramInfoLog, GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
	GLE(void, GetShaderiv, GLuint shader, GLenum pname, GLint *params) \
	GLE(void, GetShaderInfoLog, GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) \
	GLE(void, GetShaderSource, GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source) \
	GLE(GLint, GetUniformLocation, GLuint program, const GLchar *name) \
	GLE(void, LinkProgram, GLuint program) \
	GLE(void, ShaderSource, GLuint shader, GLsizei count, const GLchar* *string, const GLint *length) \
	GLE(void, UseProgram, GLuint program) \
	GLE(void, Uniform1f, GLint location, GLfloat v0) \
	GLE(void, Uniform2f, GLint location, GLfloat v0, GLfloat v1) \
	GLE(void, Uniform3f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) \
	GLE(void, Uniform4f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) \
	GLE(void, Uniform1i, GLint location, GLint v0) \
	GLE(void, Uniform1fv, GLint location, GLsizei count, const GLfloat *value) \
	GLE(void, UniformMatrix4fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) \
	GLE(void, ValidateProgram, GLuint program) \
	GLE(void, VertexAttribPointer, GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) \

// GL_NVX_gpu_memory_info
#ifndef GL_NVX_gpu_memory_info
#define GL_NVX_gpu_memory_info
#define GL_GPU_MEMORY_INFO_DEDICATED_VIDMEM_NVX          0x9047
#define GL_GPU_MEMORY_INFO_TOTAL_AVAILABLE_MEMORY_NVX    0x9048
#define GL_GPU_MEMORY_INFO_CURRENT_AVAILABLE_VIDMEM_NVX  0x9049
#define GL_GPU_MEMORY_INFO_EVICTION_COUNT_NVX            0x904A
#define GL_GPU_MEMORY_INFO_EVICTED_MEMORY_NVX            0x904B
#endif

// GL_ATI_meminfo
#ifndef GL_ATI_meminfo
#define GL_ATI_meminfo
#define GL_VBO_FREE_MEMORY_ATI                    0x87FB
#define GL_TEXTURE_FREE_MEMORY_ATI                0x87FC
#define GL_RENDERBUFFER_FREE_MEMORY_ATI           0x87FD
#endif

// GL_ARB_texture_float
#ifndef GL_ARB_texture_float
#define GL_ARB_texture_float
#define GL_TEXTURE_RED_TYPE_ARB             0x8C10
#define GL_TEXTURE_GREEN_TYPE_ARB           0x8C11
#define GL_TEXTURE_BLUE_TYPE_ARB            0x8C12
#define GL_TEXTURE_ALPHA_TYPE_ARB           0x8C13
#define GL_TEXTURE_LUMINANCE_TYPE_ARB       0x8C14
#define GL_TEXTURE_INTENSITY_TYPE_ARB       0x8C15
#define GL_TEXTURE_DEPTH_TYPE_ARB           0x8C16
#define GL_UNSIGNED_NORMALIZED_ARB          0x8C17
#define GL_RGBA32F_ARB                      0x8814
#define GL_RGB32F_ARB                       0x8815
#define GL_ALPHA32F_ARB                     0x8816
#define GL_INTENSITY32F_ARB                 0x8817
#define GL_LUMINANCE32F_ARB                 0x8818
#define GL_LUMINANCE_ALPHA32F_ARB           0x8819
#define GL_RGBA16F_ARB                      0x881A
#define GL_RGB16F_ARB                       0x881B
#define GL_ALPHA16F_ARB                     0x881C
#define GL_INTENSITY16F_ARB                 0x881D
#define GL_LUMINANCE16F_ARB                 0x881E
#define GL_LUMINANCE_ALPHA16F_ARB           0x881F
#endif

#ifndef GL_ARB_half_float_pixel
#define GL_ARB_half_float_pixel
#define GL_HALF_FLOAT_ARB                   0x140B
#endif

// GL_EXT_framebuffer_object
#define QGL_EXT_framebuffer_object_PROCS \
	GLE(void, BindRenderbufferEXT, GLenum target, GLuint renderbuffer) \
	GLE(void, DeleteRenderbuffersEXT, GLsizei n, const GLuint *renderbuffers) \
	GLE(void, GenRenderbuffersEXT, GLsizei n, GLuint *renderbuffers) \
	GLE(void, RenderbufferStorageEXT, GLenum target, GLenum internalformat, GLsizei width, GLsizei height) \
	GLE(void, BindFramebufferEXT, GLenum target, GLuint framebuffer) \
	GLE(void, DeleteFramebuffersEXT, GLsizei n, const GLuint *framebuffers) \
	GLE(void, GenFramebuffersEXT, GLsizei n, GLuint *framebuffers) \
	GLE(GLenum, CheckFramebufferStatusEXT, GLenum target) \
	GLE(void, FramebufferTexture2DEXT, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) \
	GLE(void, FramebufferRenderbufferEXT, GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) \
	GLE(void, GenerateMipmapEXT, GLenum target) \

#ifndef GL_EXT_framebuffer_object
#define GL_EXT_framebuffer_object
#define GL_FRAMEBUFFER_EXT                     0x8D40
#define GL_RENDERBUFFER_EXT                    0x8D41
#define GL_STENCIL_INDEX1_EXT                  0x8D46
#define GL_STENCIL_INDEX4_EXT                  0x8D47
#define GL_STENCIL_INDEX8_EXT                  0x8D48
#define GL_STENCIL_INDEX16_EXT                 0x8D49
#define GL_RENDERBUFFER_WIDTH_EXT              0x8D42
#define GL_RENDERBUFFER_HEIGHT_EXT             0x8D43
#define GL_RENDERBUFFER_INTERNAL_FORMAT_EXT    0x8D44
#define GL_RENDERBUFFER_RED_SIZE_EXT           0x8D50
#define GL_RENDERBUFFER_GREEN_SIZE_EXT         0x8D51
#define GL_RENDERBUFFER_BLUE_SIZE_EXT          0x8D52
#define GL_RENDERBUFFER_ALPHA_SIZE_EXT         0x8D53
#define GL_RENDERBUFFER_DEPTH_SIZE_EXT         0x8D54
#define GL_RENDERBUFFER_STENCIL_SIZE_EXT       0x8D55
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT            0x8CD0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT            0x8CD1
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT          0x8CD2
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT  0x8CD3
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT     0x8CD4
#define GL_COLOR_ATTACHMENT0_EXT                0x8CE0
#define GL_COLOR_ATTACHMENT1_EXT                0x8CE1
#define GL_COLOR_ATTACHMENT2_EXT                0x8CE2
#define GL_COLOR_ATTACHMENT3_EXT                0x8CE3
#define GL_COLOR_ATTACHMENT4_EXT                0x8CE4
#define GL_COLOR_ATTACHMENT5_EXT                0x8CE5
#define GL_COLOR_ATTACHMENT6_EXT                0x8CE6
#define GL_COLOR_ATTACHMENT7_EXT                0x8CE7
#define GL_COLOR_ATTACHMENT8_EXT                0x8CE8
#define GL_COLOR_ATTACHMENT9_EXT                0x8CE9
#define GL_COLOR_ATTACHMENT10_EXT               0x8CEA
#define GL_COLOR_ATTACHMENT11_EXT               0x8CEB
#define GL_COLOR_ATTACHMENT12_EXT               0x8CEC
#define GL_COLOR_ATTACHMENT13_EXT               0x8CED
#define GL_COLOR_ATTACHMENT14_EXT               0x8CEE
#define GL_COLOR_ATTACHMENT15_EXT               0x8CEF
#define GL_DEPTH_ATTACHMENT_EXT                 0x8D00
#define GL_STENCIL_ATTACHMENT_EXT               0x8D20
#define GL_FRAMEBUFFER_COMPLETE_EXT                          0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT             0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT     0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT             0x8CD9
#define GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT                0x8CDA
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT            0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT            0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED_EXT                       0x8CDD
#define GL_FRAMEBUFFER_BINDING_EXT             0x8CA6
#define GL_RENDERBUFFER_BINDING_EXT            0x8CA7
#define GL_MAX_COLOR_ATTACHMENTS_EXT           0x8CDF
#define GL_MAX_RENDERBUFFER_SIZE_EXT           0x84E8
#define GL_INVALID_FRAMEBUFFER_OPERATION_EXT   0x0506
#endif

// GL_EXT_framebuffer_blit
#define QGL_EXT_framebuffer_blit_PROCS \
	GLE(void, BlitFramebufferEXT, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) \

#ifndef GL_EXT_framebuffer_blit
#define GL_EXT_framebuffer_blit
#define GL_READ_FRAMEBUFFER_EXT                0x8CA8
#define GL_DRAW_FRAMEBUFFER_EXT                0x8CA9
#define GL_DRAW_FRAMEBUFFER_BINDING_EXT        0x8CA6
#define GL_READ_FRAMEBUFFER_BINDING_EXT        0x8CAA
#endif

// GL_EXT_framebuffer_multisample
#define QGL_EXT_framebuffer_multisample_PROCS \
	GLE(void, RenderbufferStorageMultisampleEXT, GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) \

#ifndef GL_EXT_framebuffer_multisample
#define GL_EXT_framebuffer_multisample
#define GL_RENDERBUFFER_SAMPLES_EXT                0x8CAB
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE_EXT  0x8D56
#define GL_MAX_SAMPLES_EXT                         0x8D57
#endif

#ifndef GL_ARB_texture_compression_rgtc
#define GL_ARB_texture_compression_rgtc
#define GL_COMPRESSED_RED_RGTC1                       0x8DBB
#define GL_COMPRESSED_SIGNED_RED_RGTC1                0x8DBC
#define GL_COMPRESSED_RG_RGTC2                        0x8DBD
#define GL_COMPRESSED_SIGNED_RG_RGTC2                 0x8DBE
#endif

#ifndef GL_ARB_texture_compression_bptc
#define GL_ARB_texture_compression_bptc
#define GL_COMPRESSED_RGBA_BPTC_UNORM_ARB                 0x8E8C
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM_ARB           0x8E8D
#define GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT_ARB           0x8E8E
#define GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_ARB         0x8E8F
#endif

#ifndef GL_ARB_depth_clamp
#define GL_ARB_depth_clamp
#define GL_DEPTH_CLAMP				      0x864F
#endif

#ifndef GL_ARB_seamless_cube_map
#define GL_ARB_seamless_cube_map
#define GL_TEXTURE_CUBE_MAP_SEAMLESS               0x884F
#endif

// GL_ARB_vertex_array_object
#define QGL_ARB_vertex_array_object_PROCS \
	GLE(void, BindVertexArray, GLuint array) \
	GLE(void, DeleteVertexArrays, GLsizei n, const GLuint *arrays) \
	GLE(void, GenVertexArrays, GLsizei n, GLuint *arrays) \

#ifndef GL_ARB_vertex_array_object
#define GL_ARB_vertex_array_object
#define GL_VERTEX_ARRAY_BINDING_ARB                0x85B5
#endif

// GL_EXT_direct_state_access
#define QGL_EXT_direct_state_access_PROCS \
	GLE(GLvoid, BindMultiTextureEXT, GLenum texunit, GLenum target, GLuint texture) \
	GLE(GLvoid, TextureParameterfEXT, GLuint texture, GLenum target, GLenum pname, GLfloat param) \
	GLE(GLvoid, TextureParameteriEXT, GLuint texture, GLenum target, GLenum pname, GLint param) \
	GLE(GLvoid, TextureImage2DEXT, GLuint texture, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) \
	GLE(GLvoid, TextureSubImage2DEXT, GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) \
	GLE(GLvoid, CopyTextureImage2DEXT, GLuint texture, GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height, GLint border) \
	GLE(GLvoid, CompressedTextureImage2DEXT, GLuint texture, GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data) \
	GLE(GLvoid, CompressedTextureSubImage2DEXT, GLuint texture, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLsizei imageSize, const GLvoid *data) \
	GLE(GLvoid, GenerateTextureMipmapEXT, GLuint texture, GLenum target) \
	GLE(GLvoid, ProgramUniform1iEXT, GLuint program, GLint location, GLint v0) \
	GLE(GLvoid, ProgramUniform1fEXT, GLuint program, GLint location, GLfloat v0) \
	GLE(GLvoid, ProgramUniform2fEXT, GLuint program, GLint location, GLfloat v0, GLfloat v1) \
	GLE(GLvoid, ProgramUniform3fEXT, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2) \
	GLE(GLvoid, ProgramUniform4fEXT, GLuint program, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) \
	GLE(GLvoid, ProgramUniform1fvEXT, GLuint program, GLint location, GLsizei count, const GLfloat *value) \
	GLE(GLvoid, ProgramUniformMatrix4fvEXT, GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) \
	GLE(GLvoid, NamedRenderbufferStorageEXT, GLuint renderbuffer, GLenum internalformat, GLsizei width, GLsizei height) \
	GLE(GLvoid, NamedRenderbufferStorageMultisampleEXT, GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) \
	GLE(GLenum, CheckNamedFramebufferStatusEXT, GLuint framebuffer, GLenum target) \
	GLE(GLvoid, NamedFramebufferTexture2DEXT, GLuint framebuffer, GLenum attachment, GLenum textarget, GLuint texture, GLint level) \
	GLE(GLvoid, NamedFramebufferRenderbufferEXT, GLuint framebuffer, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) \

#define GLE(ret, name, ...) typedef ret APIENTRY name##proc(__VA_ARGS__); extern name##proc * qgl##name;
QGL_1_2_PROCS;
QGL_1_3_PROCS;
QGL_1_4_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_EXT_framebuffer_object_PROCS;
QGL_EXT_framebuffer_blit_PROCS;
QGL_EXT_framebuffer_multisample_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

#endif // __QGL_H__
