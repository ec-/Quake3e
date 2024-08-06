#include "client.h"
#include "x_local.h"

// ====================
//   CVars

//static cvar_t *x_hck_ps_enemy_hitbox = 0;

static cvar_t *x_ps_dead_body_black = 0;

static cvar_t *x_ps_auto_revival = 0;

// ====================
//   Const vars
//
//static char X_HELP_HCK_PS_ENEMY_HITBOX[] = "\n ^fx_hck_ps_enemy_hitbox_3d^5 0|...|1^7\n\n"
//										   "   Draw hitbox for enemy players.\n";

static char X_HELP_PS_DEAD_BODY_BLACK[] = "\n ^fx_ps_dead_body_black^5 0|...|3^7\n\n"
										  "   Make dead and frozen models dark.\n"
										  "    1 - light gray\n"
										  "    2 - gray\n"
										  "    3 - dark gray\n";

// ====================
//   Static routines

//static void DrawPlayerHitbox(PlayerModel model, refEntity_t *ref);

static void MakeDeadBodyBlack(PlayerModel model, refEntity_t *ref);

// ====================
//   Implementation

void X_PS_Init()
{
	//X_Main_RegisterHackXCommand(x_hck_ps_enemy_hitbox, "0", "0", "2", X_HELP_HCK_PS_ENEMY_HITBOX);
	X_Main_RegisterXCommand(x_ps_dead_body_black, "2", "0", "3", X_HELP_PS_DEAD_BODY_BLACK);
	X_Main_RegisterXCommand(x_ps_auto_revival, "1", "0", "1", 0);//TODO:
}

void X_PS_CustomizePlayerModel(refEntity_t *ref)
{
	PlayerModel model = X_Team_IsPlayerModel(ref->hModel);

	if (model == NotPlayer)
	{
		return;
	}

	//DrawPlayerHitbox(model, ref);
	MakeDeadBodyBlack(model, ref);
}

void X_PS_AutoRevival(snapshot_t *snapshot)
{
	if (!x_ps_auto_revival->integer)
	{
		return;
	}

	if (snapshot->ps.clientNum != clc.clientNum)
	{
		return;
	}

	if (snapshot->ps.pm_type != PM_DEAD)
	{
		return;
	}

	if (xmod.snap.ps.persistant[PERS_TEAM] == TEAM_SPECTATOR)
	{
		return;
	}

	if ((xmod.gs.type == GameTDM && xmod.gs.freezetag) || xmod.gs.type == GameCTF
		|| xmod.gs.type == Game1v1 || xmod.gs.type == GameFFA)
	{
		// Semulate mouse1 click
		
		Cmd_ExecuteString("+attack");
		Cmd_ExecuteString("-attack");
	}
}

//static void DrawPlayerHitbox(PlayerModel model, refEntity_t *ref)
//{
//	if (!x_hck_ps_enemy_hitbox->integer)
//	{
//		return;
//	}
//
//	if (!X_Main_IsXModeHackCommandActive(x_hck_ps_enemy_hitbox))
//	{
//		return;
//	}
//
//	if (ref->renderfx & RF_THIRD_PERSON)
//	{
//		return;
//	}
//
//	if (model != LegsModel)
//	{
//		return;
//	}
//
//	int client = X_GS_GetClientIDByOrigin(ref->origin);
//	if (client == -1)
//	{
//		return;
//	}
//
//	int powerups = xmod.gs.ps[client].powerups;
//
//	if (powerups & (1 << PW_INVIS))
//	{
//		return;
//	}
//
//	if (xmod.gs.freezetag && powerups & (1 << PW_BATTLESUIT) && xmod.gs.ps[client].entity >= MAX_CLIENTS)
//	{
//		return;
//	}
//
//	if (xmod.snap.ps.persistant[PERS_TEAM] == TEAM_SPECTATOR
//		|| xmod.snap.ps.persistant[PERS_TEAM] == TEAM_FREE
//		|| xmod.snap.ps.persistant[PERS_TEAM] != xmod.gs.ps[client].team)
//	{
//		refEntity_t entity;
//		memset(&entity, 0, sizeof(entity));
//
//		if (x_hck_ps_enemy_hitbox->integer == 1)
//		{
//			entity.reType = RT_MODEL;
//			entity.hModel = xmod.rs.modelHitbox;
//			entity.axis[PITCH][0] = 0;
//			entity.axis[PITCH][1] = 1;
//			entity.axis[PITCH][2] = 0;
//			entity.axis[YAW][0] = 1;
//			entity.axis[YAW][1] = 0;
//			entity.axis[YAW][2] = 0;
//			entity.axis[ROLL][0] = 0;
//			entity.axis[ROLL][1] = 0;
//			entity.axis[ROLL][2] = 1;
//			VectorCopy(ref->lightingOrigin, entity.origin);
//			Original_AddRefEntityToScene(&entity, 0);
//		}
//		else if (x_hck_ps_enemy_hitbox->integer == 2)
//		{
//			entity.reType = RT_SPRITE;
//			entity.customShader = xmod.rs.shaderHitbox;
//			entity.radius = 128;
//			entity.shader.rgba[0] = 0;
//			entity.shader.rgba[1] = 255;
//			entity.shader.rgba[2] = 0;
//			entity.shader.rgba[3] = 255;
//			VectorCopy(ref->lightingOrigin, entity.origin);
//		}
//		else
//		{
//			return;
//		}
//
//		Original_AddRefEntityToScene(&entity, qfalse);
//	}
//}

static void MakeDeadBodyBlack(PlayerModel model, refEntity_t *ref)
{
	if (xmod.gs.mode == ModeCPMA)
	{
		return;
	}

	if (!x_ps_dead_body_black->integer)
	{
		return;
	}

	if (xmod.deadPlayerParts == NotPlayer && model == LegsModel)
	{
		qboolean found = qfalse;

		if (xmod.gs.freezetag)
		{
			int client = X_GS_GetClientIDByOrigin(ref->origin);
			if (client >= 0 && xmod.gs.ps[client].powerups & (1 << PW_BATTLESUIT) && xmod.gs.ps[client].entity >= MAX_CLIENTS)
			{
				xmod.deadPlayerParts = model;
				found = qtrue;
			}
		}

		if (!found && X_GS_IsClientDeadByOrigin(ref->origin))
		{
			xmod.deadPlayerParts = model;
		}
	}

	if (xmod.deadPlayerParts != NotPlayer)
	{
		ref->shader.rgba[0] = ref->shader.rgba[1] = ref->shader.rgba[2] = 220 - (55 * x_ps_dead_body_black->integer);
		xmod.deadPlayerParts = (model == HeadModel ? NotPlayer : model);
	}
}
