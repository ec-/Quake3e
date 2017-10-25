/*
===========================================================================
Copyright (C) 2011 James Canete (use.less01@gmail.com)

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
// tr_extensions.c - extensions needed by the renderer not in sdl_glimp.c


#ifdef _MSC_VER
#pragma warning (disable : 4054 )
#endif

#define SDL_GL_GetProcAddress( a ) ri.GL_GetProcAddress( a )

#include "tr_local.h"
#include "tr_dsa.h"

#define GLE(ret, name, ...) name##proc * qgl##name;
QGL_1_1_PROCS;
QGL_DESKTOP_1_1_PROCS;
QGL_1_3_PROCS;
QGL_1_5_PROCS;
QGL_2_0_PROCS;
QGL_3_0_PROCS;
QGL_ARB_framebuffer_object_PROCS;
QGL_ARB_vertex_array_object_PROCS;
QGL_EXT_direct_state_access_PROCS;
#undef GLE

int qglMajorVersion = 2, qglMinorVersion = 0;
int qglesMajorVersion, qglesMinorVersion;

/*
** GLimp_HaveExtension
*/
static char gl_extensions[ 32768 ];
static qboolean GLimp_HaveExtension( const char *ext )
{
	const char *ptr = Q_stristr( gl_extensions, ext );
	if (ptr == NULL)
		return qfalse;
	ptr += strlen(ext);
	return ((*ptr == ' ') || (*ptr == '\0'));  // verify it's complete string.
}


void GLimp_InitExtraExtensions( void )
{
	int len;
	char *extension;
	const char* result[3] = { "...ignoring %s\n", "...using %s\n", "...%s not found\n" };
	qboolean q_gl_version_at_least_3_0;
	qboolean q_gl_version_at_least_3_2;

	// set DSA fallbacks
#define GLE(ret, name, ...) qgl##name = GLDSA_##name;
	QGL_EXT_direct_state_access_PROCS;
#undef GLE

	// GL function loader, based on https://gist.github.com/rygorous/16796a0c876cf8a5f542caddb55bce8a
#define GLE(ret, name, ...) qgl##name = (name##proc *) ri.GL_GetProcAddress( "gl" #name );

	QGL_1_1_PROCS;
	QGL_DESKTOP_1_1_PROCS;

	// OpenGL 1.3, was GL_ARB_texture_compression
	QGL_1_3_PROCS;

	// OpenGL 1.5, was GL_ARB_vertex_buffer_object and GL_ARB_occlusion_query
	QGL_1_5_PROCS;
	glRefConfig.occlusionQuery = qtrue;

	// OpenGL 2.0, was GL_ARB_shading_language_100, GL_ARB_vertex_program, GL_ARB_shader_objects, and GL_ARB_vertex_shader
	QGL_2_0_PROCS;

	QGL_3_0_PROCS;

	if ( !qglGetString ) {
		ri.Error( ERR_FATAL, "glGetString is NULL" );
	}

	// get our config strings
	Q_strncpyz( glConfig.vendor_string, (char *)qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
	Q_strncpyz( glConfig.renderer_string, (char *)qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
	len = strlen( glConfig.renderer_string );
	if ( len && glConfig.renderer_string[ len - 1 ] == '\n' )
		glConfig.renderer_string[ len - 1 ] = '\0';
	Q_strncpyz( glConfig.version_string, (char *)qglGetString( GL_VERSION ), sizeof( glConfig.version_string ) );

	Q_strncpyz( gl_extensions, (char *)qglGetString( GL_EXTENSIONS ), sizeof( gl_extensions ) );
	Q_strncpyz( glConfig.extensions_string, gl_extensions, sizeof( glConfig.extensions_string ) );

	sscanf( glConfig.version_string, "%d.%d", &qglMajorVersion, &qglMinorVersion );

	// Check OpenGL version
	if ( !QGL_VERSION_ATLEAST( 2, 0 ) )
		ri.Error( ERR_FATAL, "OpenGL 2.0 required!" );
	ri.Printf( PRINT_ALL, "...using OpenGL %s\n", glConfig.version_string );

	if ( !r_ignorehwgamma->integer )
	{
		ri.GLimp_InitGamma( &glConfig );
	}

	q_gl_version_at_least_3_0 = QGL_VERSION_ATLEAST( 3, 0 );
	q_gl_version_at_least_3_2 = QGL_VERSION_ATLEAST( 3, 2 );

	// Check if we need Intel graphics specific fixes.
	glRefConfig.intelGraphics = qfalse;
	if ( strstr((char *)qglGetString(GL_RENDERER), "Intel") )
		glRefConfig.intelGraphics = qtrue;

	// OpenGL 3.0 - GL_ARB_framebuffer_object
	extension = "GL_ARB_framebuffer_object";
	glRefConfig.framebufferObject = qfalse;
	glRefConfig.framebufferBlit = qfalse;
	glRefConfig.framebufferMultisample = qfalse;

	if (q_gl_version_at_least_3_0 || GLimp_HaveExtension(extension))
	{
		glRefConfig.framebufferObject = !!r_ext_framebuffer_object->integer;
		glRefConfig.framebufferBlit = qtrue;
		glRefConfig.framebufferMultisample = qtrue;

		qglGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &glRefConfig.maxRenderbufferSize);
		qglGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &glRefConfig.maxColorAttachments);

		QGL_ARB_framebuffer_object_PROCS;

		ri.Printf(PRINT_ALL, result[glRefConfig.framebufferObject], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// OpenGL 3.0 - GL_ARB_vertex_array_object
	extension = "GL_ARB_vertex_array_object";
	glRefConfig.vertexArrayObject = qfalse;
	if (q_gl_version_at_least_3_0 || GLimp_HaveExtension(extension))
	{
		if (q_gl_version_at_least_3_0)
		{
			// force VAO, core context requires it
			glRefConfig.vertexArrayObject = qtrue;
		}
		else
		{
			glRefConfig.vertexArrayObject = !!r_arb_vertex_array_object->integer;
		}

		QGL_ARB_vertex_array_object_PROCS;

		ri.Printf(PRINT_ALL, result[glRefConfig.vertexArrayObject], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// OpenGL 3.0 - GL_ARB_texture_float
	extension = "GL_ARB_texture_float";
	glRefConfig.textureFloat = qfalse;
	if (q_gl_version_at_least_3_0 || GLimp_HaveExtension(extension))
	{
		glRefConfig.textureFloat = !!r_ext_texture_float->integer;

		ri.Printf(PRINT_ALL, result[glRefConfig.textureFloat], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// OpenGL 3.2 - GL_ARB_depth_clamp
	extension = "GL_ARB_depth_clamp";
	glRefConfig.depthClamp = qfalse;
	if (q_gl_version_at_least_3_2 || GLimp_HaveExtension(extension))
	{
		glRefConfig.depthClamp = qtrue;

		ri.Printf(PRINT_ALL, result[glRefConfig.depthClamp], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// OpenGL 3.2 - GL_ARB_seamless_cube_map
	extension = "GL_ARB_seamless_cube_map";
	glRefConfig.seamlessCubeMap = qfalse;
	if (q_gl_version_at_least_3_2 || GLimp_HaveExtension(extension))
	{
		glRefConfig.seamlessCubeMap = !!r_arb_seamless_cube_map->integer;

		ri.Printf(PRINT_ALL, result[glRefConfig.seamlessCubeMap], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// Determine GLSL version
	if (1)
	{
		char version[256];

		Q_strncpyz(version, (char *)qglGetString(GL_SHADING_LANGUAGE_VERSION), sizeof(version));

		sscanf(version, "%d.%d", &glRefConfig.glslMajorVersion, &glRefConfig.glslMinorVersion);

		ri.Printf(PRINT_ALL, "...using GLSL version %s\n", version);
	}

	glRefConfig.memInfo = MI_NONE;

	// GL_NVX_gpu_memory_info
	extension = "GL_NVX_gpu_memory_info";
	if( GLimp_HaveExtension( extension ) )
	{
		glRefConfig.memInfo = MI_NVX;

		ri.Printf(PRINT_ALL, result[1], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// GL_ATI_meminfo
	extension = "GL_ATI_meminfo";
	if( GLimp_HaveExtension( extension ) )
	{
		if (glRefConfig.memInfo == MI_NONE)
		{
			glRefConfig.memInfo = MI_ATI;

			ri.Printf(PRINT_ALL, result[1], extension);
		}
		else
		{
			ri.Printf(PRINT_ALL, result[0], extension);
		}
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	glRefConfig.textureCompression = TCR_NONE;

	// GL_ARB_texture_compression_rgtc
	extension = "GL_ARB_texture_compression_rgtc";
	if (GLimp_HaveExtension(extension))
	{
		qboolean useRgtc = r_ext_compressed_textures->integer >= 1;

		if (useRgtc)
			glRefConfig.textureCompression |= TCR_RGTC;

		ri.Printf(PRINT_ALL, result[useRgtc], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	glRefConfig.swizzleNormalmap = r_ext_compressed_textures->integer && !(glRefConfig.textureCompression & TCR_RGTC);

	// GL_ARB_texture_compression_bptc
	extension = "GL_ARB_texture_compression_bptc";
	if (GLimp_HaveExtension(extension))
	{
		qboolean useBptc = r_ext_compressed_textures->integer >= 2;

		if (useBptc)
			glRefConfig.textureCompression |= TCR_BPTC;

		ri.Printf(PRINT_ALL, result[useBptc], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

	// GL_EXT_direct_state_access
	extension = "GL_EXT_direct_state_access";
	glRefConfig.directStateAccess = qfalse;
	if (GLimp_HaveExtension(extension))
	{
		glRefConfig.directStateAccess = !!r_ext_direct_state_access->integer;

		// QGL_*_PROCS becomes several functions, do not remove {}
		if (glRefConfig.directStateAccess)
		{
			QGL_EXT_direct_state_access_PROCS;
		}

		ri.Printf(PRINT_ALL, result[glRefConfig.directStateAccess], extension);
	}
	else
	{
		ri.Printf(PRINT_ALL, result[2], extension);
	}

#undef GLE
}
#ifdef _MSC_VER
#pragma warning (default : 4054 )
#endif
