/*
* Huge thanks to breadsticks! Wouldn't have been able to do any of this
* without his patience nor guidance.
*/

/* kindly adapted from XPC32 and breadsticks */

#include "client.h"
#include "../qcommon/cm_local.h"
#include "../qcommon/cm_patch.h"

// if you dare to exceed this...
#define MAX_FACE_VERTS 64

typedef enum {TRIGGER_BRUSH, CLIP_BRUSH, SLICK_BRUSH} visBrushType_t;

typedef struct {
	int numVerts;
	polyVert_t *verts;
} visFace_t;

typedef struct visBrushNode_s {
	visBrushType_t type;
	qhandle_t shader;

	int numFaces;
	visFace_t *faces;

	// This is a linked list.
	// Why? I dont know.
	// Let me know if you do.
	struct visBrushNode_s *next;
} visBrushNode_t;


static void add_triggers(void);
static void add_clips(void);
static void add_slicks(void);
static void gen_visible_brush(int brushnum, vec3_t origin, visBrushType_t type, vec4_t color, qhandle_t shader);
static qboolean intersect_planes(cplane_t *p1, cplane_t *p2, cplane_t *p3, vec3_t p);
static qboolean point_in_brush(vec3_t point, cbrush_t *brush);
static int winding_cmp(const void *a, const void *b);
static void add_vert_to_face(visFace_t *face, vec3_t vert, vec4_t color, vec2_t tex_coords);
static float *get_uv_coords(vec2_t uv, vec3_t vert, vec3_t normal);
static void free_vis_brushes(visBrushNode_t *brushes);


static visBrushNode_t *head = NULL;

/* needed for winding_cmp */
static vec3_t w_center, w_normal, w_ref_vec;
static float w_ref_vec_len;

static cvar_t *triggers_draw;
static cvar_t *clips_draw;
static cvar_t *slicks_draw;

static cvar_t * trigger_shader_setting;
static cvar_t * clip_shader_setting;
static cvar_t * slick_shader_setting;

static qhandle_t trigger_shader;
static qhandle_t clip_shader;
static qhandle_t slick_shader;

static vec4_t trigger_color = { 0, 128, 0, 255 };
static vec4_t clip_color = { 128, 0, 0, 255 };
static vec4_t slick_color = { 0, 64, 128, 255 };

void tc_vis_init(void) {
	free_vis_brushes(head);
	head = NULL;

	triggers_draw = Cvar_Get("r_renderTriggerBrushes", "0", CVAR_ARCHIVE);
	clips_draw = Cvar_Get("r_renderClipBrushes", "0", CVAR_ARCHIVE);
	slicks_draw = Cvar_Get("r_renderSlickSurfaces", "0", CVAR_ARCHIVE);

	trigger_shader_setting = Cvar_Get("r_renderTriggerBrushesShader", "tcRenderShader", CVAR_ARCHIVE);
	clip_shader_setting = Cvar_Get("r_renderClipBrushesShader", "tcRenderShader", CVAR_ARCHIVE);
	slick_shader_setting = Cvar_Get("r_renderSlickSurfacesShader", "tcRenderShader", CVAR_ARCHIVE);

	trigger_shader = re.RegisterShader(trigger_shader_setting->string);
	clip_shader = re.RegisterShader(clip_shader_setting->string);
	slick_shader = re.RegisterShader(slick_shader_setting->string);

	add_triggers();
	add_clips();
	add_slicks();
}

void tc_vis_render(void) {
	visBrushNode_t *brush = head;
	while ( brush ) {
		if ( ( brush->type == TRIGGER_BRUSH && triggers_draw->integer ) || ( brush->type == CLIP_BRUSH && clips_draw->integer ) || ( brush->type == SLICK_BRUSH && slicks_draw->integer ) )
		{
			for (int i = 0; i < brush->numFaces; i++)
				re.AddPolyToScene(brush->shader, brush->faces[i].numVerts, brush->faces[i].verts, 1);
		}
		brush = brush->next;
	}
}

// ripped from breadsticks
static void add_triggers(void) {
	const char *entities = cm.entityString;
	for (;; ) {
		qboolean is_trigger = qfalse;
		int model = -1;
		vec3_t origin;
		VectorCopy(vec3_origin, origin);

		char *token = COM_Parse(&entities);
		if (!entities)
			break;

		if (token[0] != '{')
			Com_Error(ERR_DROP, "mape is borked\n");

		for (;; ) {
			token = COM_Parse(&entities);

			if (token[0] == '}')
				break;

			if (!Q_stricmp(token, "model")) {
				token = COM_Parse(&entities);
				if (token[0] == '*')
					model = atoi(token + 1);
			}

			if (!Q_stricmp(token, "classname")) {
				token = COM_Parse(&entities);
				is_trigger = !!Q_stristr(token, "trigger");
			}

			if (!Q_stricmp(token, "origin")) {
				token = COM_Parse(&entities);
				sscanf(token, "%f %f %f", &origin[0], &origin[1], &origin[2]);
			}
		}

		if (is_trigger && model > 0) {
			cLeaf_t *leaf = &cm.cmodels[model].leaf;
			for (int i = 0; i < leaf->numLeafBrushes; i++) {
				gen_visible_brush(cm.leafbrushes[leaf->firstLeafBrush + i], origin, TRIGGER_BRUSH, trigger_color, trigger_shader);
			}
		}
	}
}

static void add_clips(void) {
	for (int i = 0; i < cm.numBrushes; i++) {
		cbrush_t *brush = &cm.brushes[i];
		if (brush->contents & CONTENTS_PLAYERCLIP) {
			gen_visible_brush(i, vec3_origin, CLIP_BRUSH, clip_color, clip_shader);
		}
	}
}

static inline qboolean walkable(cplane_t const *plane) {
	return plane->normal[2] >= 0.7 /*MIN_WALK_NORMAL*/;
}

static void add_slicks(void) {
	for (int i = 0; i < cm.numBrushes; i++) {
		cbrush_t *brush = &cm.brushes[i];
		for (int s = 0; s < brush->numsides; s++) {
			cbrushside_t* side = &brush->sides[s];
			if (side->surfaceFlags & SURF_SLICK && walkable(side->plane)) {
				gen_visible_brush(i, vec3_origin, SLICK_BRUSH, slick_color, slick_shader);
				break;
			}
		}
	}
}

static void gen_visible_brush(int brushnum, vec3_t origin, visBrushType_t type, vec4_t color, qhandle_t shader) {
	cbrush_t *brush = &cm.brushes[brushnum];
	visBrushNode_t *node = malloc(sizeof(visBrushNode_t));
	node->type = type;
	node->shader = shader;
	node->numFaces = brush->numsides;
	node->faces = malloc(node->numFaces * sizeof(visFace_t));
	for (int i = 0; i < node->numFaces; i++) {
		node->faces[i].numVerts = 0;
		node->faces[i].verts = malloc(MAX_FACE_VERTS * sizeof(polyVert_t));
	}

	for (int i = 0; i < brush->numsides; i++) {
		cplane_t *p1 = brush->sides[i].plane;
		for (int j = i+1; j < brush->numsides; j++) {
			cplane_t *p2 = brush->sides[j].plane;
			for (int k = j+1; k < brush->numsides; k++) {
				vec3_t p;
				cplane_t *p3 = brush->sides[k].plane;
				if (!intersect_planes(p1, p2, p3, p))
					continue;

				if (!point_in_brush(p, brush))
					continue;

				// translate point to be relative to provided origin
				// looking at you FM
				VectorAdd(p, origin, p);

				vec2_t uv;
				if (type != SLICK_BRUSH || walkable(p1))
					add_vert_to_face(&node->faces[i], p, color, get_uv_coords(uv, p, p1->normal));
				if (type != SLICK_BRUSH || walkable(p2))
					add_vert_to_face(&node->faces[j], p, color, get_uv_coords(uv, p, p2->normal));
				if (type != SLICK_BRUSH || walkable(p3))
					add_vert_to_face(&node->faces[k], p, color, get_uv_coords(uv, p, p3->normal));
			}
		}
	}

	// winding
	for (int i = 0; i < brush->numsides; i++) {
		visFace_t *face = &node->faces[i];
		VectorCopy(brush->sides[i].plane->normal, w_normal);
		VectorClear(w_center);
		for (int j = 0; j < face->numVerts; j++)
			VectorAdd(w_center, face->verts[j].xyz, w_center);
		VectorScale(w_center, 1.0f / face->numVerts, w_center);
		VectorSubtract(face->verts[0].xyz, w_center, w_ref_vec);
		w_ref_vec_len = VectorLength(w_ref_vec);
		qsort(face->verts, face->numVerts, sizeof(face->verts[0]), winding_cmp);
	}

	node->next = head;
	head = node;
}

static qboolean intersect_planes(cplane_t *p1, cplane_t *p2, cplane_t *p3, vec3_t p) {
	// thanks Real-Time Collision Detection
	vec3_t u, v;
	CrossProduct(p2->normal, p3->normal, u);
	float denom = DotProduct(p1->normal, u);
	if (fabs(denom) < 1e-5)
		return qfalse;

	for (int i = 0; i < 3; i++)
		p[i] = p3->dist * p2->normal[i] - p2->dist * p3->normal[i];

	CrossProduct(p1->normal, p, v);
	VectorMA(v, p1->dist, u, p);
	VectorScale(p, 1.0f / denom, p);
	return qtrue;
}

static qboolean point_in_brush(vec3_t point, cbrush_t *brush) {
	for (int i = 0; i < brush->numsides; i++) {
		float d = DotProduct(point, brush->sides[i].plane->normal);
		if (d - brush->sides[i].plane->dist > PLANE_TRI_EPSILON)
			return qfalse;
	}
	return qtrue;
}

// This function was initially supposed to obtain the ccw angle from w_ref_vec
// for ac and bc and compare them. However, we don't really need the exact angle.
// We just need to know which point lies further ccw relative to the ref.
// So a linear substitute is instead used to preserve the monotone decrease of acos.
static int winding_cmp(const void *a, const void *b) {
	vec3_t ac, bc, n1, n2;

	VectorSubtract(((polyVert_t *)a)->xyz, w_center, ac);
	VectorSubtract(((polyVert_t *)b)->xyz, w_center, bc);

	float proj_ac = DotProduct(ac, w_ref_vec) / VectorLength(ac);
	float proj_bc = DotProduct(bc, w_ref_vec) / VectorLength(bc);

	float a_diff = w_ref_vec_len - proj_ac;
	float b_diff  = w_ref_vec_len - proj_bc;

	// todo: get rid of cross products
	CrossProduct(ac, w_ref_vec, n1);
	CrossProduct(bc, w_ref_vec, n2);

	if (DotProduct(n1, w_normal) < 0)
		a_diff = 4.f * w_ref_vec_len - a_diff;
	if (DotProduct(n2, w_normal) < 0)
		b_diff = 4.f * w_ref_vec_len - b_diff;

	if (a_diff < b_diff)
		return -1;
	if (a_diff > b_diff)
		return 1;

	return 0;
}

static void add_vert_to_face(visFace_t *face, vec3_t vert, vec4_t color, vec2_t tex_coords) {
	if (face->numVerts >= MAX_FACE_VERTS)
		return;

	VectorCopy(vert, face->verts[face->numVerts].xyz);
	Vector4Copy(color, face->verts[face->numVerts].modulate);
	face->verts[face->numVerts].st[0] = tex_coords[0];
	face->verts[face->numVerts].st[1] = tex_coords[1];
	face->numVerts++;
}

static float *get_uv_coords(vec2_t uv, vec3_t vert, vec3_t normal) {
	float x = abs(normal[0]), y = abs(normal[1]), z = abs(normal[2]);
	if (x >= y && x >= z) {
		uv[0] = -vert[1] / 32.f;
		uv[1] = -vert[2] / 32.f;
	}
	else if (y > x && y >= z) {
		uv[0] = -vert[0] / 32.f;
		uv[1] = -vert[2] / 32.f;
	}
	else {
		uv[0] = -vert[0] / 32.f;
		uv[1] = -vert[1] / 32.f;
	}

	return uv;
}

static void free_vis_brushes(visBrushNode_t *brushes) {
	// one day this will be iterative?
	if (!brushes)
		return;

	free_vis_brushes(brushes->next);
	for (int i = 0; i < brushes->numFaces; i++)
		free(brushes->faces[i].verts);

	free(brushes->faces);
	free(brushes);
}
