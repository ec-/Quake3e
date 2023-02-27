#include "client.h"
#include "x_local.h"
#include <float.h>

//TODO: 
// - baseq3 and cpma doesn't work
// - add rotated hit crosshairs

// ====================
//   CVars

//
static cvar_t *x_ch_fix_resolution = 0;
static cvar_t *x_ch_auto_scale = 0;
//
static cvar_t *x_ch_hit_icon = 0;
static cvar_t *x_ch_hit_icon_scale = 0;
static cvar_t *x_ch_hit_lock_icon = 0;
// 
static cvar_t *x_ch_opaque = 0;
static cvar_t *x_ch_color = 0;
static cvar_t *x_ch_rotate45 = 0;
static cvar_t *x_ch_action = 0;
static cvar_t *x_ch_action_color = 0;
// 
static cvar_t *x_ch_decor = 0;
static cvar_t *x_ch_decor_size = 0;
static cvar_t *x_ch_decor_opaque = 0;
static cvar_t *x_ch_decor_color = 0;
static cvar_t *x_ch_decor_rotate45 = 0;
static cvar_t *x_ch_decor_action = 0;
static cvar_t *x_ch_decor_action_color = 0;
//
static cvar_t *x_hck_ch_enemy_aim_fix_lg_range = 0;
//
static cvar_t *cg_crosshairSize = 0;
static cvar_t *cg_drawCrosshair = 0;

// ====================
//   Const vars

static char X_HELP_CH_FIX_RESOLUTION[] = "\n ^fx_ch_fix_resolution^5 0|1^7\n\n"
										 "   Turn on\\off fix for crosshair resolution. The fix removes crosshair deformation on wide-screen resolutions like 16:9.\n";
static char X_HELP_CH_AUTO_SCALE[] = "\n ^fx_ch_auto_scale^5 0|1^7\n\n"
									 "   Crosshair size controlled depends on a distance to the nearest target.\n";

static char X_HELP_CH_HIT_ICON[] = "\n ^fx_ch_hit_icon^5 0|1^7\n\n"
								   "   Show hit icons on a crosshair when you give a damage to an enemy.\n"
								   "   If promode is enabled icons indicate a damage impact.\n";
static char X_HELP_CH_HIT_ICON_SCALE[] = "\n ^fx_ch_hit_icon_scale^5 0.5|...|1.5^7\n\n"
										 "   Change size of the hit icons. \n";
static char X_HELP_CH_HIT_LOCK_ICON[] = "\n ^fx_ch_hit_lock_icon^5 0|...|4^7\n\n"
										"   Force to show a specific hit icon.\n";
static char X_HELP_CH_OPAQUE[] = "\n ^fx_ch_opaque^5 0.0|...|1.0^7\n\n"
								 "   Change a crosshair transparency where 1.0 is opaque and 0.0 is completely transparent.\n";
static char X_HELP_CH_COLOR[] = "\n ^fx_ch_color <color>^7\n\n"
								"   Change a crosshair color, 0 switch a crosshair color to default mode (ch_crosshairColor, etc).\n";
static char X_HELP_CH_ROTATE45[] = "\n ^fx_ch_rotate45^5 0|1^7\n\n"
								   "   Rotate a crosshair to 45 degrees.\n";
static char X_HELP_CH_ACTION[] = "\n ^fx_ch_action^5 0|...|4^7\n\n"
								 "   Enable an effect on a crosshair when specific event happens:\n"
								 "    1 - always visible but pulsates when you hit an enemy\n"
								 "    2 - isn't visible by default but appears when you hit an enemy\n"
								 "    3 - changes color on a hit\n"
								 "    4 - changes color when you aimed at an enemy\n"
								 "   * use ^fx_ch_action_color^7 to control an effect color\n";
static char X_HELP_CH_ACTION_COLOR[] = "\n ^fx_ch_action_color^5 <color>^7\n\n"
									   "   Change a crosshair effect color when an action happens (1-9 or #RRGGBB and #RGB).\n";

static char X_HELP_CH_DECOR[] = "\n ^fx_ch_decor^5 0|...|100^7\n\n"
								"   Display a crosshair decoration as a secondary layer.\n";
static char X_HELP_CH_DECOR_SIZE[] = "\n ^fx_ch_decor_size^5 0|...|100^7\n\n"
									 "   Set a size of the crosshair decoration.\n";
static char X_HELP_CH_DECOR_OPAQUE[] = "\n ^fx_ch_decor_opaque^5 0.0|...|1.0^7\n\n"
									   "   Control a decoration transparency where 1.0 is opaque and 0.0 is completely transparent.\n";
static char X_HELP_CH_DECOR_COLOR[] = "\n ^fx_ch_decor_color^5 <color>^7\n\n"
									  "   Change a decoration color, 0 makes a decoration color the same to a crosshair.\n";//TODO: color format
static char X_HELP_CH_DECOR_ROTATE45[] = "\n ^fx_ch_decor_rotate45^5 0|1^7\n\n"
										 "   Rotate a decoration to 45 degrees.\n";
static char X_HELP_CH_DECOR_ACTION[] = "\n ^fx_ch_decor_action^5 0|...|4^7\n\n"
									   "   Enable action effect for crosshair decoration on a specific event:\n"
									   "    1 - always visible but pulsates when you hit an enemy\n"
									   "    2 - isn't visible by default but appears when you hit an enemy\n"
									   "    3 - changes color on a hit\n"
									   "    4 - changes color when you aimed at an enemy\n"
									   "   * use ^fx_ch_decor_action_color^7 to control an effect color\n";
static char X_HELP_CH_DECOR_ACTION_COLOR[] = "\n ^fx_ch_decor_action_color^5 <color>^7\n\n"
											 "   Change a decoration effect color when an action happens (1-9 or #RRGGBB and #RGB).\n";

static char X_HELP_HCK_ENEMY_AIM_FIX_LG_RANGE[] = "\n ^fx_hck_ch_enemy_aim_fix_lg_range^5 0|1^7\n\n"
												  "   Display the crosshair decoration when enemy is on LG hit range (working only with ^fx_ch_action^7 or ^fx_ch_decor_action^7 = 4).\n";

// ====================
//   Static routines

static qboolean IsCrosshairShader(qhandle_t shader);
static void DrawCrosshairHitIcon(float x, float y, float w, float h);
static qhandle_t LoadCrosshairWithoutLimit(qhandle_t shader);
static void ChangeCrosshairOnHit(HitCrosshairDamage damage);
static void DrawCustomizedCrosshair(float x, float y, float w, float h, qhandle_t shader);
static void TracePlayerOnAim(const vec3_t origin, float distance, const vec3_t axis);
static void ValidateAndSetAimedTarget(int client, float distance, const vec3_t source, vec3_t target);

// ====================
//   Implementation

void X_CH_Init()
{
	HitCrosshairIcon *hc = &xmod.ch.hc;
	hc->dmg = HitNone;
	hc->startMS = 0;
	hc->durationMS = 300; // ms

	HitCrosshairPulse *hp = &xmod.ch.hp;
	hp->durationMS = 0;
	hp->startMS = 0;

	cg_crosshairSize = Cvar_Get("cg_crosshairSize", "24", CVAR_ARCHIVE);
	cg_drawCrosshair = Cvar_Get("cg_drawCrosshair", "4", CVAR_ARCHIVE);

	X_Main_RegisterXCommand(x_ch_fix_resolution, "1", "0", "1", X_HELP_CH_FIX_RESOLUTION);

	X_Main_RegisterXCommand(x_ch_auto_scale, "0", "0", "1", X_HELP_CH_AUTO_SCALE);

	X_Main_RegisterXCommand(x_ch_hit_icon, "0", "0", "1", X_HELP_CH_HIT_ICON);

	X_Main_RegisterFloatXCommand(x_ch_hit_icon_scale, "1.0", "0.5", "1.5", X_HELP_CH_HIT_ICON_SCALE);

	X_Main_RegisterXCommand(x_ch_hit_lock_icon, "0", "0", "4", X_HELP_CH_HIT_LOCK_ICON);

	X_Main_RegisterFloatXCommand(x_ch_opaque, "1.0", "0.0", "1.0", X_HELP_CH_OPAQUE);

	X_Main_RegisterXCommand(x_ch_color, "0", 0, 0, X_HELP_CH_COLOR);

	X_Main_RegisterXCommand(x_ch_rotate45, "0", "0", "1", X_HELP_CH_ROTATE45);

	X_Main_RegisterXCommand(x_ch_action, "4", "0", "4", X_HELP_CH_ACTION);

	X_Main_RegisterXCommand(x_ch_action_color, "#C00", 0, 0, X_HELP_CH_ACTION_COLOR);

	X_Main_RegisterXCommand(x_ch_decor, "0", "0", "100", X_HELP_CH_DECOR);

	X_Main_RegisterXCommand(x_ch_decor_size, "24", "0", "100", X_HELP_CH_DECOR_SIZE);

	X_Main_RegisterFloatXCommand(x_ch_decor_opaque, "1.0", "0.0", "1.0", X_HELP_CH_DECOR_OPAQUE);

	X_Main_RegisterXCommand(x_ch_decor_color, "0", 0, 0, X_HELP_CH_DECOR_COLOR);

	X_Main_RegisterXCommand(x_ch_decor_rotate45, "0", "0", "1", X_HELP_CH_DECOR_ROTATE45);

	X_Main_RegisterXCommand(x_ch_decor_action, "0", "0", "4", X_HELP_CH_DECOR_ACTION);

	X_Main_RegisterXCommand(x_ch_decor_action_color, "1", 0, 0, X_HELP_CH_DECOR_ACTION_COLOR);

	X_Main_RegisterHackXCommand(x_hck_ch_enemy_aim_fix_lg_range, "0", "0", "1", X_HELP_HCK_ENEMY_AIM_FIX_LG_RANGE);

	X_Misc_InitCustomColor(x_ch_color, &xmod.ch.front);
	X_Misc_InitCustomColor(x_ch_decor_color, &xmod.ch.decor);

	X_Misc_InitCustomColor(x_ch_action_color, &xmod.ch.actionFront);
	X_Misc_InitCustomColor(x_ch_decor_action_color, &xmod.ch.actionDecor);
}

qboolean X_CH_CustomizeCrosshair(float x, float y, float w, float h, qhandle_t shader)
{
	if (!IsCrosshairShader(shader))
	{
		return qfalse;
	}

	shader = LoadCrosshairWithoutLimit(shader);

	DrawCustomizedCrosshair(x, y, w, h, shader);

	DrawCrosshairHitIcon(x, y, w, h);

	return qtrue;
}

static qboolean IsCrosshairShader(qhandle_t shader)
{
	XModResources *rs = &xmod.rs;

	for (int i = 0; i < countof(*rs->shaderCrosshairs); i++)
	{
		if (rs->shaderCrosshairs[0][i] && rs->shaderCrosshairs[0][i] == shader)
		{
			return qtrue;
		}

		if (rs->shaderCrosshairs[1][i] && rs->shaderCrosshairs[1][i] == shader)
		{
			return qtrue;
		}
	}

	return qfalse;
}

static qhandle_t LoadCrosshairWithoutLimit(qhandle_t shader)
{
	XModResources *rs = &xmod.rs;

	int inx = cg_drawCrosshair->integer;
	if (inx < 10)
	{
		return shader;
	}

	inx -= 10; // Skip default 10 crosshairs

	if (inx >= countof(rs->shaderXCrosshairs))
	{
		return shader;
	}

	if (x_ch_rotate45->integer)
	{
		return (rs->shaderXCrosshairsR45[inx] ? rs->shaderXCrosshairsR45[inx] : shader);
	}

	return (rs->shaderXCrosshairs[inx] ? rs->shaderXCrosshairs[inx] : shader);
}

void X_CH_ChangeCrosshairOnSoundTrigger(const char *soundName)
{
	if (x_ch_hit_icon->integer || x_ch_action->integer || x_ch_decor_action->integer)
	{
		if (!strcmp(soundName, X_SOUND_HIT_LOWEST))
		{
			ChangeCrosshairOnHit(HitLowest);
		}
		else if (!strcmp(soundName, X_SOUND_HIT_LOW))
		{
			ChangeCrosshairOnHit(HitLow);
		}
		else if (!strcmp(soundName, X_SOUND_HIT_MEDIUM))
		{
			ChangeCrosshairOnHit(HitMedium);
		}
		else if (!strcmp(soundName, X_SOUND_HIT_HIGH))
		{
			ChangeCrosshairOnHit(HitHigh);
		}
	}
}

static void ChangeCrosshairOnHit(HitCrosshairDamage damage)
{
	if (x_ch_hit_icon->integer == 1 || x_ch_hit_lock_icon->integer)
	{
		HitCrosshairIcon *hc = &xmod.ch.hc;
		hc->dmg = damage;
		hc->durationMS = 300;
		hc->color[0] = 1.0f;
		hc->color[1] = 0.1f;
		hc->color[2] = 0.1f;
		hc->color[3] = 1.0f; // Alpha

		if (x_ch_hit_lock_icon->integer)
		{
			hc->dmg = x_ch_hit_lock_icon->integer;
		}

		hc->startMS = Sys_Milliseconds();
	}

	if (x_ch_action->integer || x_ch_decor_action->integer)
	{
		xmod.ch.hp.durationMS = 200;
		xmod.ch.hp.startMS = Sys_Milliseconds();
		xmod.ch.hp.increament = 0.4 + (0.1 * damage);
	}
}

static void ScaleCrosshairSize(float *x, float *y, float *w, float *h, int size, float mult)
{
	*x = 640 / 2;
	*y = 480 / 2;

	*w = (size ? size : *w) * mult;
	*h = (size ? size : *h) * mult;

	SCR_AdjustFrom640(x, y, w, h);

	*x -= *w * 0.5;
	*y -= *h * 0.5;
}

static void DrawCrosshairHitIcon(float x, float y, float w, float h)
{
	HitCrosshairIcon *hc = &xmod.ch.hc;
	XModResources *rs = &xmod.rs;

	if (hc->dmg == HitNone)
	{
		return;
	}

	if (!hc->startMS)
	{
		return;
	}

	int deltaMs = Sys_Milliseconds() - hc->startMS;
	if (deltaMs > hc->durationMS / 2)
	{
		float dur = hc->durationMS / 2;
		float step = 1.0 / dur;

		float delta = deltaMs - dur;
		if (delta > dur)
		{
			delta = dur;
		}

		hc->color[3] = 1.0 - (delta * step);
	}
	else
	{
		hc->color[3] = 1.0;
	}

	Original_SetColor(hc->color);

	h = w = cg_crosshairSize->integer;
	ScaleCrosshairSize(&x, &y, &w, &h, 0, x_ch_hit_icon_scale->value);

	int shader = rs->shaderHit[hc->dmg - 1];
	if (shader)
	{
		Original_DrawStretchPic(x, y, w, h, 0, 0, 1, 1, shader);
	}

	if (deltaMs > hc->durationMS)
	{
		hc->startMS = 0;
		hc->dmg = HitNone;
	}
}

static void DrawCrosshairPic(int size, float scale, qhandle_t shader)
{
	float x = 640 / 2;
	float y = 480 / 2;
	float w, h;

	if (x_ch_fix_resolution->integer)
	{
		SCR_AdjustFrom640(&x, &y, 0, 0);
		h = w = size * 2.4 * scale;
	}
	else
	{
		h = w = size * scale;
		SCR_AdjustFrom640(&x, &y, &w, &h);
	}

	Original_DrawStretchPic(x - (w * 0.5), y - (h * 0.5), w, h, 0, 0, 1, 1, shader);
}

static float GetPulseMultiplier(void)
{
	HitCrosshairPulse *hp = &xmod.ch.hp;

	if (!hp->startMS)
	{
		return 1.0f;
	}

	float inc = xmod.ch.hp.increament;
	float step = inc / hp->durationMS;
	int current = Sys_Milliseconds();
	int delta = current - hp->startMS;

	if (delta > hp->durationMS)
	{
		delta = hp->durationMS;
		xmod.ch.hp.increament = 0.0;
		hp->durationMS = hp->startMS = 0;
	}

	float mult = 1.0f + inc - (delta * step);
	if (mult < 1.0f)
	{
		mult = 1.0f;
	}

	return mult;
}

static float GetPulseOpaqueMultiplier(void)
{
	HitCrosshairPulse *hp = &xmod.ch.hp;

	if (!hp->startMS)
	{
		return 0.0f;
	}

	float inc = 1.0f;
	float step = inc / hp->durationMS;
	int current = Sys_Milliseconds();
	int delta = current - hp->startMS;

	if (delta > hp->durationMS)
	{
		delta = hp->durationMS;
	}

	return inc - (delta * step);
}

static float CalculateCrosshairScale(void)
{
	float scale = 1.0f;
	float chdistance = (xmod.aimed ? xmod.aimedDistance : xmod.ch.distance);

	if (x_ch_auto_scale->integer)
	{
		const float distance = 2000;
		const float startFrom = 200;
		const float scaleRange = 0.40f;

		if (chdistance > startFrom)
		{
			float mult = 1.0f - scaleRange;

			if (chdistance < startFrom + distance)
			{
				float d = (chdistance - startFrom);
				float s = scaleRange / distance;
				mult = 1.0f - (d * s);
			}

			scale *= mult;
		}
	}

	return scale;
}

static qboolean IsPulseAction(int action)
{
	return (action == 1 || action == 2 ? qtrue : qfalse);
}

static void ChooseCrosshairColor(float *rgba, qboolean active, XCustomColor *color, XCustomColor *actionColor, float opaque)
{
	//TODO: for action 3 and probably other not hidden actions we might make a float color change from actionColor to color
	if (active && X_Misc_IsCustomColorActive(actionColor))
	{
		MAKERGBA(rgba, actionColor->rgb[0], actionColor->rgb[1], actionColor->rgb[2], opaque);
	}
	else if (X_Misc_IsCustomColorActive(color))
	{
		MAKERGBA(rgba, color->rgb[0], color->rgb[1], color->rgb[2], opaque);
	}
	else
	{
		MAKERGBA(rgba, xmod.currentColor[0], xmod.currentColor[1], xmod.currentColor[2], opaque);
	}
}

static qboolean IsActionActive(int action)
{
	if (!action)
	{
		return qfalse;
	}

	if (action == 4)
	{
		return (xmod.aimed && !X_Team_ClientIsInSameTeam(xmod.aimedClient));
	}

	if (!xmod.ch.hp.startMS)
	{
		return qfalse;
	}

	return qtrue;
}

static void DrawMainCrosshair(float scale, float pulse, float pulseOpaque, qhandle_t shader)
{
	int action = x_ch_action->integer;
	qboolean active = IsActionActive(action);
	float opaque = x_ch_opaque->value;

	if (active)
	{
		if (IsPulseAction(action))
		{
			scale *= pulse;
		}

		if (action == 2)
		{
			opaque *= pulseOpaque;
		}
	}

	//Fix: crosshair size for custom crosshairs is different to default ones
	if (cg_drawCrosshair->integer > 10)
	{
		scale *= 2.0f;
	}

	float rgba[4];
	ChooseCrosshairColor(rgba, active, &xmod.ch.front, &xmod.ch.actionFront, opaque);
	Original_SetColor(rgba);

	if (action == 2 && !active)
	{
		return;
	}

	DrawCrosshairPic(cg_crosshairSize->integer, scale, shader);
}

static void DrawDecorCrosshair(float scale, float pulse, float pulseOpaque)
{
	if (!x_ch_decor->integer)
	{
		return;
	}

	int action = x_ch_decor_action->integer;
	qboolean active = IsActionActive(action);
	float opaque = x_ch_decor_opaque->value;

	qhandle_t decor = (
	x_ch_decor_rotate45->integer ?
	xmod.rs.shaderDecorsR45[x_ch_decor->integer - 1]
								 : xmod.rs.shaderDecors[x_ch_decor->integer - 1]);

	if (active)
	{
		if (IsPulseAction(action))
		{
			scale *= pulse;
		}

		if (action == 2)
		{
			opaque *= pulseOpaque;
		}
	}

	float rgba[4];
	ChooseCrosshairColor(rgba, active, &xmod.ch.decor, &xmod.ch.actionDecor, opaque);
	Original_SetColor(rgba);

	if (action == 2 && !active)
	{
		return;
	}

	DrawCrosshairPic(x_ch_decor_size->integer, scale, decor);
}

static void DrawCustomizedCrosshair(float x, float y, float w, float h, qhandle_t shader)
{
	float pulse = GetPulseMultiplier();
	float scale = CalculateCrosshairScale();
	float opaque = GetPulseOpaqueMultiplier();

	DrawDecorCrosshair(scale, pulse, opaque);
	DrawMainCrosshair(scale, pulse, opaque, shader);
}

void X_CH_CalculateDistance(const refdef_t *fd)
{
	vec3_t start, end;
	trace_t trace;

	//Fix: When RDF_NOWORLDMODEL presented a render is a part of hud scene, not a game
	if (X_Misc_IsNoWorldRender(fd))
	{
		return;
	}

	VectorCopy(fd->vieworg, start);
	VectorMA(start, 131072, fd->viewaxis[0], end);

	// Make trace over map (doesn't include player entity model)
	CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID | CONTENTS_BODY, qfalse);
	xmod.ch.distance = Distance(start, trace.endpos);

	TracePlayerOnAim(fd->vieworg, xmod.ch.distance, fd->viewaxis[0]);
}

static void TracePlayerOnAim(const vec3_t origin, float distance, const vec3_t axis)
{
	xmod.aimed = qfalse;
	xmod.aimedClient = 0;
	xmod.aimedDistance = 0;

	if (x_ch_action->integer != 4 && x_ch_decor_action->integer != 4)
	{
		return;
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (xmod.gs.ps[i].active != qtrue)
		{
			continue;
		}

		if (xmod.gs.ps[i].origin[0] == 0 && xmod.gs.ps[i].origin[1] == 0 && xmod.gs.ps[i].origin[2] == 0)
		{
			continue;
		}

		vec3_t hitbox_min = {xmod.gs.ps[i].origin[0] - 15, xmod.gs.ps[i].origin[1] - 15, xmod.gs.ps[i].origin[2] - 17};
		vec3_t hitbox_max = {xmod.gs.ps[i].origin[0] + 15, xmod.gs.ps[i].origin[1] + 15, xmod.gs.ps[i].origin[2] + 32};

		// Check first does a ray position located in a hitbox (mb possible)

		if (origin[0] >= hitbox_min[0] && origin[0] <= hitbox_max[0] &&
			origin[1] >= hitbox_min[1] && origin[1] <= hitbox_max[1] &&
			origin[2] >= hitbox_min[2] && origin[2] <= hitbox_max[2])
		{
			ValidateAndSetAimedTarget(i, distance, origin, xmod.gs.ps[i].origin);
			continue;
		}

		// Does a ray cross over a hitbox?

		double t_near = DBL_MIN,
		t_far = DBL_MAX;
		double t1, t2;

		qboolean completed = qtrue;

		for (int a = 0; a < 3; a++) // go over x, y, z
		{
			if (fabs(axis[a]) >= DBL_EPSILON)
			{
				t1 = (hitbox_min[a] - origin[a]) / axis[a];
				t2 = (hitbox_max[a] - origin[a]) / axis[a];

				if (t1 > t2)
				{
					double t = t1;
					t1 = t2;
					t2 = t;
				}

				if (t1 > t_near)
				{
					t_near = t1;
				}

				if (t2 < t_far)
				{
					t_far = t2;
				}

				if (t_near > t_far)
				{
					completed = qfalse;
					break;
				}
				if (t_far < 0.0)
				{
					completed = qfalse;
					break;
				}
			}
			else
			{
				if (origin[a] < hitbox_min[a] || origin[a] > hitbox_max[a])
				{
					completed = qfalse;
					break;
				}
			}
		}

		if (!completed)
		{
			continue;
		}

		if (t_near <= t_far && t_far >= 0)
		{
			ValidateAndSetAimedTarget(i, distance, origin, xmod.gs.ps[i].origin);
			continue;
		}
	}
}

static void ValidateAndSetAimedTarget(int client, float distance, const vec3_t source, vec3_t target)
{
	XPlayerState *state = X_GS_GetStateByClientId(client);

	if (!state->visible)
	{
		return;
	}

	// Don't aim to a player with invisibility
	if (state->powerups & (1 << PW_INVIS))
	{
		return;
	}

	float tdistance = Distance(source, target);

	// Don't aim to a frozen player 
	if (xmod.gs.freezetag && state->powerups & (1 << PW_BATTLESUIT) && state->entity >= MAX_CLIENTS)
	{
		// Fix: a frozen player stay closer on aim
		if (!xmod.aimedDistance || xmod.aimedDistance >= tdistance)
		{
			xmod.aimedClient = client;
			xmod.aimedDistance = tdistance;
		}
		return;
	}

	// Don't aim over walls
	if (distance < tdistance)
	{
		return;
	}

	// Don't aim with lightning gun if a range is higher than lg range
	if (X_Main_IsXModeHackCommandActive(x_hck_ch_enemy_aim_fix_lg_range)
		&& x_hck_ch_enemy_aim_fix_lg_range->integer
		&& xmod.snap.ps.weapon == WP_LIGHTNING
		&& tdistance > LIGHTNING_RANGE + 25)
	{
		return;
	}

	// Don't aim if we already aimed to a player that is near then current one
	if (xmod.aimedDistance && xmod.aimedDistance < tdistance)
	{
		return;
	}

	// Don't aim to a player in a fog
	if (CM_PointContents(state->origin, 0) & CONTENTS_FOG)
	{
		return;
	}

	xmod.aimed = qtrue;
	xmod.aimedClient = client;
	xmod.aimedDistance = tdistance;
}
