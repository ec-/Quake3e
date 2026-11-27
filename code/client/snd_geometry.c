/*
===========================================================================
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
 * snd_geometry.c -- level geometry for sound reflections
 *
 * Reads the world's visible surfaces from a BSP file as triangles with an
 * acoustic material each. Uses the draw surfaces rather than the collision
 * brushes: the compiler has already removed hidden faces, and curves are
 * included. Only the world model is used (no doors or platforms); sky,
 * nodraw and non-solid surfaces (water, fog, decals) are skipped.
 */

#include "snd_local.h"

#define SPG_PATCH_STEPS		4		// subdivisions per bezier span

typedef struct {
	vec3_t	*verts;
	int		numVerts, maxVerts;
	int		(*tris)[3];
	int		*materials;
	int		numTris, maxTris;
} spgBuild_t;


static void SPG_Grow( spgBuild_t *b, int verts, int tris ) {
	if ( b->numVerts + verts > b->maxVerts ) {
		b->maxVerts = ( b->numVerts + verts ) * 2;
		b->verts = realloc( b->verts, b->maxVerts * sizeof( *b->verts ) );
	}
	if ( b->numTris + tris > b->maxTris ) {
		b->maxTris = ( b->numTris + tris ) * 2;
		b->tris = realloc( b->tris, b->maxTris * sizeof( *b->tris ) );
		b->materials = realloc( b->materials, b->maxTris * sizeof( *b->materials ) );
	}
}


/*
Adds a triangle wound so that (b - a) x (c - a) points along normal.
*/
static void SPG_AddTri( spgBuild_t *b, int i0, int i1, int i2, const vec3_t normal, int material ) {
	vec3_t e1, e2, n;
	VectorSubtract( b->verts[i1], b->verts[i0], e1 );
	VectorSubtract( b->verts[i2], b->verts[i0], e2 );
	CrossProduct( e1, e2, n );
	if ( VectorLengthSquared( n ) < 1e-6f )
		return;		// degenerate
	b->tris[b->numTris][0] = i0;
	if ( DotProduct( n, normal ) >= 0.0f ) {
		b->tris[b->numTris][1] = i1;
		b->tris[b->numTris][2] = i2;
	} else {
		b->tris[b->numTris][1] = i2;
		b->tris[b->numTris][2] = i1;
	}
	b->materials[b->numTris] = material;
	b->numTris++;
}


/*
Picks an acoustic material from the shader name and surface flags. Quake
III has no acoustic data, so this is a guess from naming conventions.
*/
static spMaterial_t SPG_Material( const char *shader, int surfaceFlags ) {
	char name[MAX_QPATH];
	Q_strncpyz( name, shader, sizeof( name ) );
	Q_strlwr( name );

	if ( surfaceFlags & SURF_METALSTEPS )
		return SP_MAT_METAL;
	if ( strstr( name, "glass" ) || strstr( name, "window" ) )
		return SP_MAT_GLASS;
	if ( strstr( name, "wood" ) || strstr( name, "plank" ) || strstr( name, "crate" ) )
		return SP_MAT_WOOD;
	if ( strstr( name, "metal" ) || strstr( name, "steel" ) || strstr( name, "iron" ) || strstr( name, "grate" )
		|| strstr( name, "pipe" ) || strstr( name, "tech" ) || strstr( name, "girder" ) || strstr( name, "rust" ) )
		return SP_MAT_METAL;
	if ( strstr( name, "grass" ) || strstr( name, "dirt" ) || strstr( name, "sand" ) || strstr( name, "gravel" )
		|| strstr( name, "organics" ) || strstr( name, "skin" ) || strstr( name, "flesh" ) )
		return SP_MAT_SOFT;
	if ( strstr( name, "carpet" ) || strstr( name, "cloth" ) || strstr( name, "curtain" ) || strstr( name, "banner" ) )
		return SP_MAT_FABRIC;
	if ( strstr( name, "marble" ) || strstr( name, "tile" ) )
		return SP_MAT_TILE;
	return SP_MAT_STONE;
}


static void SPG_BezierPoint( const drawVert_t *ctrl[9], float s, float t, vec3_t xyz, vec3_t normal ) {
	const float bs[3] = { ( 1 - s ) * ( 1 - s ), 2 * s * ( 1 - s ), s * s };
	const float bt[3] = { ( 1 - t ) * ( 1 - t ), 2 * t * ( 1 - t ), t * t };
	int i, j, k;
	VectorClear( xyz );
	VectorClear( normal );
	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 3; i++ ) {
			const float w = bs[i] * bt[j];
			for ( k = 0; k < 3; k++ ) {
				xyz[k] += w * LittleFloat( ctrl[j * 3 + i]->xyz[k] );
				normal[k] += w * LittleFloat( ctrl[j * 3 + i]->normal[k] );
			}
		}
	}
}


/*
Tessellates a curved surface: a grid of biquadratic bezier spans.
*/
static void SPG_AddPatch( spgBuild_t *b, const drawVert_t *dv, int width, int height, int material ) {
	const int n = SPG_PATCH_STEPS;
	int px, py, i, j;

	if ( width < 3 || height < 3 || !( width & 1 ) || !( height & 1 ) )
		return;

	for ( py = 0; py + 2 < height; py += 2 ) {
		for ( px = 0; px + 2 < width; px += 2 ) {
			const drawVert_t *ctrl[9];
			vec3_t normals[( SPG_PATCH_STEPS + 1 ) * ( SPG_PATCH_STEPS + 1 )];
			int base;

			for ( j = 0; j < 3; j++ )
				for ( i = 0; i < 3; i++ )
					ctrl[j * 3 + i] = &dv[( py + j ) * width + px + i];

			SPG_Grow( b, ( n + 1 ) * ( n + 1 ), n * n * 2 );
			base = b->numVerts;
			for ( j = 0; j <= n; j++ ) {
				for ( i = 0; i <= n; i++ ) {
					SPG_BezierPoint( ctrl, (float)i / n, (float)j / n, b->verts[b->numVerts], normals[j * ( n + 1 ) + i] );
					b->numVerts++;
				}
			}
			for ( j = 0; j < n; j++ ) {
				for ( i = 0; i < n; i++ ) {
					const int v00 = base + j * ( n + 1 ) + i, v10 = v00 + 1;
					const int v01 = v00 + n + 1, v11 = v01 + 1;
					vec3_t nrm;
					VectorAdd( normals[v00 - base], normals[v11 - base], nrm );
					SPG_AddTri( b, v00, v10, v11, nrm, material );
					SPG_AddTri( b, v00, v11, v01, nrm, material );
				}
			}
		}
	}
}


/*
================
SP_LoadMapGeometry

Returns NULL if the map can't be read or has no usable surfaces.
================
*/
spGeometry_t *SP_LoadMapGeometry( const char *bspName ) {
	union { void *v; byte *b; } buf;
	const dheader_t *header;
	const dshader_t *shaders;
	const dmodel_t *model;
	const dsurface_t *surfs;
	const drawVert_t *dverts;
	const int *dindexes;
	int numShaders, numSurfs, numDVerts, numDIndexes, length, i, j;
	int first, count;
	spgBuild_t b;
	spGeometry_t *geom;

	length = FS_ReadFile( bspName, &buf.v );
	if ( !buf.v )
		return NULL;

	header = (const dheader_t *)buf.b;
	if ( length < (int)sizeof( *header ) || LittleLong( header->ident ) != BSP_IDENT || LittleLong( header->version ) != BSP_VERSION ) {
		Com_Printf( S_COLOR_YELLOW "sound geometry: %s is not a Quake III BSP\n", bspName );
		FS_FreeFile( buf.v );
		return NULL;
	}
	for ( i = 0; i < HEADER_LUMPS; i++ ) {
		const int ofs = LittleLong( header->lumps[i].fileofs ), len = LittleLong( header->lumps[i].filelen );
		if ( ofs < 0 || len < 0 || ofs + len > length ) {
			Com_Printf( S_COLOR_YELLOW "sound geometry: %s is damaged\n", bspName );
			FS_FreeFile( buf.v );
			return NULL;
		}
	}

#define LUMP( type, lump, count ) \
	( count = LittleLong( header->lumps[lump].filelen ) / (int)sizeof( type ), \
	  (const type *)( buf.b + LittleLong( header->lumps[lump].fileofs ) ) )

	shaders = LUMP( dshader_t, LUMP_SHADERS, numShaders );
	surfs = LUMP( dsurface_t, LUMP_SURFACES, numSurfs );
	dverts = LUMP( drawVert_t, LUMP_DRAWVERTS, numDVerts );
	dindexes = LUMP( int, LUMP_DRAWINDEXES, numDIndexes );
	model = LUMP( dmodel_t, LUMP_MODELS, count );
#undef LUMP
	if ( count < 1 ) {
		FS_FreeFile( buf.v );
		return NULL;
	}

	// the world model only
	first = LittleLong( model->firstSurface );
	count = LittleLong( model->numSurfaces );
	if ( first < 0 || first + count > numSurfs )
		count = 0;

	Com_Memset( &b, 0, sizeof( b ) );
	for ( i = first; i < first + count; i++ ) {
		const dsurface_t *s = &surfs[i];
		const int type = LittleLong( s->surfaceType );
		const int shaderNum = LittleLong( s->shaderNum );
		const int firstVert = LittleLong( s->firstVert ), numVerts = LittleLong( s->numVerts );
		const int firstIndex = LittleLong( s->firstIndex ), numIndexes = LittleLong( s->numIndexes );
		int surfaceFlags, contents, material;

		if ( shaderNum < 0 || shaderNum >= numShaders )
			continue;
		if ( firstVert < 0 || numVerts < 0 || firstVert + numVerts > numDVerts )
			continue;
		surfaceFlags = LittleLong( shaders[shaderNum].surfaceFlags );
		contents = LittleLong( shaders[shaderNum].contentFlags );
		if ( surfaceFlags & ( SURF_SKY | SURF_NODRAW ) )
			continue;
		if ( !( contents & CONTENTS_SOLID ) )
			continue;
		material = SPG_Material( shaders[shaderNum].shader, surfaceFlags );

		if ( type == MST_PLANAR || type == MST_TRIANGLE_SOUP ) {
			int base;
			if ( firstIndex < 0 || numIndexes < 3 || firstIndex + numIndexes > numDIndexes )
				continue;
			SPG_Grow( &b, numVerts, numIndexes / 3 );
			base = b.numVerts;
			for ( j = 0; j < numVerts; j++ ) {
				b.verts[base + j][0] = LittleFloat( dverts[firstVert + j].xyz[0] );
				b.verts[base + j][1] = LittleFloat( dverts[firstVert + j].xyz[1] );
				b.verts[base + j][2] = LittleFloat( dverts[firstVert + j].xyz[2] );
			}
			b.numVerts += numVerts;
			for ( j = 0; j + 2 < numIndexes; j += 3 ) {
				const int i0 = LittleLong( dindexes[firstIndex + j] );
				const int i1 = LittleLong( dindexes[firstIndex + j + 1] );
				const int i2 = LittleLong( dindexes[firstIndex + j + 2] );
				vec3_t nrm;
				if ( i0 < 0 || i1 < 0 || i2 < 0 || i0 >= numVerts || i1 >= numVerts || i2 >= numVerts )
					continue;
				// face the way the surface does
				nrm[0] = LittleFloat( dverts[firstVert + i0].normal[0] ) + LittleFloat( dverts[firstVert + i1].normal[0] ) + LittleFloat( dverts[firstVert + i2].normal[0] );
				nrm[1] = LittleFloat( dverts[firstVert + i0].normal[1] ) + LittleFloat( dverts[firstVert + i1].normal[1] ) + LittleFloat( dverts[firstVert + i2].normal[1] );
				nrm[2] = LittleFloat( dverts[firstVert + i0].normal[2] ) + LittleFloat( dverts[firstVert + i1].normal[2] ) + LittleFloat( dverts[firstVert + i2].normal[2] );
				SPG_AddTri( &b, base + i0, base + i1, base + i2, nrm, material );
			}
		} else if ( type == MST_PATCH ) {
			const int w = LittleLong( s->patchWidth ), h = LittleLong( s->patchHeight );
			if ( w * h != numVerts )
				continue;
			SPG_AddPatch( &b, dverts + firstVert, w, h, material );
		}
	}
	FS_FreeFile( buf.v );

	if ( !b.numTris ) {
		free( b.verts );
		free( b.tris );
		free( b.materials );
		return NULL;
	}

	geom = malloc( sizeof( *geom ) );
	geom->verts = b.verts;
	geom->numVerts = b.numVerts;
	geom->tris = b.tris;
	geom->materials = b.materials;
	geom->numTris = b.numTris;
	Q_strncpyz( geom->name, bspName, sizeof( geom->name ) );
	return geom;
}


void SP_FreeGeometry( spGeometry_t *geom ) {
	if ( !geom )
		return;
	free( geom->verts );
	free( geom->tris );
	free( geom->materials );
	free( geom );
}
