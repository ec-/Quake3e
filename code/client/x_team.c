#include "client.h"
#include "x_local.h"

extern refexport_t re;        // interface to refresh .dll

// ====================
//   CVars

static cvar_t *x_team_freeze_foe = 0;
static cvar_t *x_team_lift_foe = 0;
static cvar_t *x_hck_team_unfreezing_foe = 0;

static cvar_t *x_team_freeze_effect_team_color = 0;
static cvar_t *x_team_freeze_effect_enemy_color = 0;

// ====================
//   Const vars

static char X_HELP_TEAM_FREEZE_FOE[] = "\n ^fx_team_freeze_foe^5 0|1|2^7\n\n"
									   "   On FreezeTag servers a team icon (triangle) indicates when your teammate is frozen\n"
									   "    1 - white triangle\n"
									   "    2 - white snowflake\n";
static char X_HELP_TEAM_LIFT_FOE[] = "\n ^fx_team_lift_foe^5 -5|...|10^7\n\n"
									 "   Lift a team icon (triangle) a bit up or down\n";
static char X_HELP_TEAM_UNFREEZING_FOE[] = "\n ^fx_hck_team_unfreezing_foe^5 0|1^7\n\n"
										   "   Changes team icon (triagle) when you or teammate unfreezing a player\n";

static char X_HELP_TEAM_FREEZE_EFFECT_COLOR1[] = "\n ^fx_team_freeze_effect_team_color^5 <color>^7\n\n"
												 "   Set the team color of frozen effect for teammates. (1-9 or #rrggbb and #rgb)\n";
static char X_HELP_TEAM_FREEZE_EFFECT_COLOR2[] = "\n ^fx_team_freeze_effect_enemy_color^5 <color>^7\n\n"
												 "   Set the enemy color of frozen effect for enemy players. (1-9 or #rrggbb and #rgb)\n";
// ====================
//   Static routines

static void FreezeFoeEffect(refEntity_t *ref, int client);

// ====================
//   Implementation

void X_Team_Init()
{
	X_Main_RegisterXCommand(x_team_freeze_foe, "0", "0", "2", X_HELP_TEAM_FREEZE_FOE);
	X_Main_RegisterXCommand(x_team_lift_foe, "0", "-5", "10", X_HELP_TEAM_LIFT_FOE);
	X_Main_RegisterHackXCommand(x_hck_team_unfreezing_foe, "1", "0", "1", X_HELP_TEAM_UNFREEZING_FOE);

	X_Main_RegisterXCommand(x_team_freeze_effect_team_color, "0", 0, 0, X_HELP_TEAM_FREEZE_EFFECT_COLOR1);
	X_Main_RegisterXCommand(x_team_freeze_effect_enemy_color, "0", 0, 0, X_HELP_TEAM_FREEZE_EFFECT_COLOR2);

	X_Misc_InitCustomColor(x_team_freeze_effect_team_color, &xmod.frz.team);
	X_Misc_InitCustomColor(x_team_freeze_effect_enemy_color, &xmod.frz.enemy);
}

static int GetClientIDByFoeOrigin(vec3_t origin)
{
	const int FoeZOffset = 48;
	int z = origin[2] - FoeZOffset;
	for (int i = 0; i < countof(xmod.gs.ps); i++)
	{
		if (xmod.gs.ps[i].origin[0] == origin[0]
			&& xmod.gs.ps[i].origin[1] == origin[1]
			&& (xmod.gs.ps[i].origin[2] >= z - 5 && xmod.gs.ps[i].origin[2] <= z + 5))
		{
			return i;
		}
	}

	return -1;
}

void X_Team_CustomizeFoe(refEntity_t *ref)
{
	if (ref->reType != RT_SPRITE)
	{
		return;
	}

	if (!xmod.rs.shaderFoe || ref->customShader != xmod.rs.shaderFoe)
	{
		return;
	}

	int client = GetClientIDByFoeOrigin(ref->origin);

	if (client != -1)
	{
		FreezeFoeEffect(ref, client);
	}

	if (x_team_lift_foe->integer)
	{
		ref->origin[2] += x_team_lift_foe->integer;
	}
}

static void FreezeFoeEffect(refEntity_t *ref, int client)
{
	if (!xmod.gs.freezetag)
	{
		return;
	}

	if (!x_team_freeze_foe->integer)
	{
		return;
	}

	if (xmod.gs.ps[client].entity >= MAX_CLIENTS && (xmod.gs.ps[client].powerups & (1 << PW_BATTLESUIT)))
	{
		if (xmod.gs.ps[client].unfreezing)
		{
			ref->customShader = xmod.rs.shaderXFoeUnfreeze;
		}
		else
		{
			ref->customShader = (x_team_freeze_foe->integer == 1 ? xmod.rs.shaderXFoe[0] : xmod.rs.shaderXFoe[1]);
		}

		ref->shader.rgba[0] = 255;
		ref->shader.rgba[1] = 255;
		ref->shader.rgba[2] = 255;
	}
}

#if 0
static void MarkUnfreezingTeamMembers(vec3_t origin, int team)
{
	for (int i = MAX_CLIENTS; i < MAX_GENTITIES; i++)
	{
		XEntity* entity = xmod.gs.entities + i;

		if (entity->type != ET_PLAYER)
			continue;

		if (entity->snap < xmod.snapNum - 2)
			continue;

		XPlayerState* ps = X_GS_GetStateByClientId(entity->client);

		if (!(ps->powerups & (1 << PW_BATTLESUIT)))
			continue;

		float dist = Distance(origin, entity->origin);
		ps->unfreezing = (dist <= 100.0f ? qtrue : qfalse);
	}
}
#endif

void X_Team_ValidateFrozenPlayers(const refdef_t *fd)
{
	if (!X_Main_IsXModeHackCommandActive(x_hck_team_unfreezing_foe))
	{
		return;
	}

	if (!x_hck_team_unfreezing_foe->integer)
	{
		return;
	}

	if (!xmod.gs.freezetag)
	{
		return;
	}

	if (X_Misc_IsNoWorldRender(fd))
	{
		return;
	}

	int team = xmod.snap.ps.persistant[PERS_TEAM];
	if (team != TEAM_RED && team != TEAM_BLUE)
	{
		return;
	}

	char frozen[MAX_CLIENTS], teamate[MAX_CLIENTS];
	int frozens = 0, teamates = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		XPlayerState *ps = X_GS_GetStateByClientId(i);

		if (!ps->active)
		{
			continue;
		}

		if (team != ps->team)
		{
			continue;
		}

		if (ps->powerups & (1 << PW_BATTLESUIT) && ps->entity >= MAX_CLIENTS && ps->entity < MAX_GENTITIES)
		{
			frozen[frozens++] = i;
		}
		else
		{
			teamate[teamates++] = i;
		}
	}

	for (int i = 0; i < frozens; i++)
	{
		XPlayerState *ps = X_GS_GetStateByClientId(frozen[i]);

		ps->unfreezing = qfalse;

		for (int a = 0; a < teamates; a++)
		{
			XPlayerState *tm = X_GS_GetStateByClientId(teamate[a]);

			if (teamate[a] == xmod.snap.ps.clientNum)
			{
				continue;
			}

			float dist = Distance(ps->origin, tm->origin);
			if (dist <= 100.0f)
			{
				ps->unfreezing = qtrue;
				break;
			}
		}

		if (ps->unfreezing)
		{
			continue;
		}

		if (xmod.snap.ps.clientNum < MAX_CLIENTS)
		{
			float dist = Distance(ps->origin, fd->vieworg);
			if (dist <= 100.0f)
			{
				ps->unfreezing = qtrue;
			}
		}
	}
}

PlayerModel X_Team_IsPlayerModel(qhandle_t model)
{
	const char *name = re.GetModelNameByHandle(model);

	if (strstr(name, "models/players/"))
	{
		if (strstr(name, "/lower.md3"))
		{
			return LegsModel;
		}

		if (strstr(name, "/upper.md3"))
		{
			return TorsoModel;
		}

		if (strstr(name, "/head.md3"))
		{
			return HeadModel;
		}
	}

	return NotPlayer;
}

void X_Team_CustomizeFreezeEffect(refEntity_t *ref)
{
	if (ref->reType != RT_MODEL)
	{
		return;
	}

	if (!ref->customShader || ref->customShader != xmod.rs.shaderFreeze)
	{
		return;
	}

	PlayerModel modelType = X_Team_IsPlayerModel(ref->hModel);
	if (modelType == NotPlayer)
	{
		return;
	}

	int client = -1;

	if (modelType == LegsModel)
	{
		client = X_GS_GetClientIDByOrigin(ref->origin);
		xmod.frz.model = LegsModel;
	}
	else
	{
		if (xmod.frz.model == LegsModel && modelType == TorsoModel)
		{
			client = xmod.frz.client;
			xmod.frz.model = TorsoModel;
		}
		else if (xmod.frz.model == TorsoModel && modelType == HeadModel)
		{
			client = xmod.frz.client;
			xmod.frz.model = 0;
		}
	}

	if (client == -1)
	{
		return;
	}

	xmod.frz.client = client;

	if (xmod.snap.ps.persistant[PERS_TEAM] == TEAM_SPECTATOR)
	{
		vec3_t color;

		if (xmod.gs.ps[client].team == TEAM_RED)
		{
			if (!X_Misc_IsCustomColorActive(&xmod.frz.team))
			{
				return;
			}

			MAKERGB(color, xmod.frz.team.rgb[0], xmod.frz.team.rgb[1], xmod.frz.team.rgb[2]);
			ref->customShader = xmod.rs.shaderXFreezeTeam;
		}
		else
		{
			if (!X_Misc_IsCustomColorActive(&xmod.frz.enemy))
			{
				return;
			}

			MAKERGB(color, xmod.frz.enemy.rgb[0], xmod.frz.enemy.rgb[1], xmod.frz.enemy.rgb[2]);
			ref->customShader = xmod.rs.shaderXFreezeEnemy;
		}

		re.UpdateShaderColorByHandle(ref->customShader, color);
	}
	else if (xmod.snap.ps.persistant[PERS_TEAM] == TEAM_RED || xmod.snap.ps.persistant[PERS_TEAM] == TEAM_BLUE)
	{
		vec3_t color;

		if (xmod.snap.ps.persistant[PERS_TEAM] == xmod.gs.ps[client].team)
		{
			if (!X_Misc_IsCustomColorActive(&xmod.frz.team))
			{
				return;
			}

			MAKERGB(color, xmod.frz.team.rgb[0], xmod.frz.team.rgb[1], xmod.frz.team.rgb[2]);
			ref->customShader = xmod.rs.shaderXFreezeTeam;
		}
		else
		{
			if (!X_Misc_IsCustomColorActive(&xmod.frz.enemy))
			{
				return;
			}

			MAKERGB(color, xmod.frz.enemy.rgb[0], xmod.frz.enemy.rgb[1], xmod.frz.enemy.rgb[2]);
			ref->customShader = xmod.rs.shaderXFreezeEnemy;
		}

		re.UpdateShaderColorByHandle(ref->customShader, color);
	}
}

qboolean X_Team_ClientIsInSameTeam(int client)
{
	if (client >= MAX_CLIENTS)
	{
		return qfalse;
	}

	if (xmod.snap.ps.persistant[PERS_TEAM] != TEAM_RED && xmod.snap.ps.persistant[PERS_TEAM] != TEAM_BLUE)
	{
		return qfalse;
	}

	return (xmod.gs.ps[client].team == xmod.snap.ps.persistant[PERS_TEAM]);
}
