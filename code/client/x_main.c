#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../botlib/l_crc.h"
#include "client.h"
#include "x_local.h"

// ====================
//   Variables

XModContext xmod;

static qboolean xmod_disable_output;

// ====================
//   CVars

static cvar_t *x_enable = 0;

static cvar_t *x_item_fix_simpleitems_size = 0;
static cvar_t *cg_simpleitems = 0;

static cvar_t *x_snd_fix_unfreeze = 0;
static cvar_t *x_snd_kill = 0;

// ====================
//   Const vars

static char X_HELP_ENABLE[] = "\n ^fx_enable^5 0|1^7\n\n"
							  "   Turn on\\off XQ3E features.\n";

static char X_HELP_ITM_FIX_SIMPLEITEMS_SIZE[] = "\n ^fx_item_fix_simpleitems_size^5 0|1^7\n\n"
												"   Increases 2D items size to be similar to 3D items.\n";

static char X_HELP_SND_FIX_UNFREEZE[] = "\n ^fx_snd_fix_unfreeze^5 0|1^7\n\n"
										"   Changes regular OSP unfreezing sound (tankjr jump) to another sound.\n";

static char X_HELP_SND_KILL[] = "\n ^fx_snd_kill^5 0|4^7\n\n"
										"   Play sound each time player gets a frag.\n";

// ====================
//   Static routines

static void LoadXModeResources(void);
static void PrintWelcomeLogo(void);

static void Print_Version(void);
static void ChangeName(void);
static void Print_State(void);
static void Print_UserInfo(void);
static void Say_Encrypted(void);

static void CustomizeSimpleItems(refEntity_t *ref);

// ====================
//   Hooks

void (*Original_DrawStretchPic)(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader) = 0;
static void Hook_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader);

void (*Original_RenderScene)(const refdef_t *fd) = 0;
static void Hook_RenderScene(const refdef_t *fd);

void (*Original_AddRefEntityToScene)(const refEntity_t *re, qboolean intShaderTime) = 0;
static void Hook_AddRefEntityToScene(refEntity_t *re, qboolean intShaderTime);

void (*Original_SetColor)(const float *rgba) = 0;
static void Hook_SetColor(const float *rgba);

// ====================
//   Implementation

void X_Main_InitXMod(void)
{
	memset(&xmod, 0, sizeof(xmod));

	x_enable = Cvar_Get("x_enable", "1", CVAR_ARCHIVE | CVAR_USERINFO | CVAR_SYSTEMINFO | CVAR_XMOD);
	Cvar_SetDescription(x_enable, X_HELP_ENABLE);

	Cmd_AddCommand("x_version", Print_Version);
	Cmd_AddCommand("x_name", ChangeName);

	xmod.state = StateStopped;
}

void X_Main_InitAfterCGameVM(void)
{
	if (xmod.state != StateStopped)
	{
		Com_Error(ERR_FATAL, "Invalid XMod state %d", xmod.state);
	}

	// Renew a random generator to avoid same random results on the first client start
	srand(time(0));

	LoadXModeResources();

	X_CH_Init();

	X_DMG_Init();

	X_Team_Init();

	X_WP_Init();

	X_Net_Init();

	X_PS_Init();

	X_Con_Init();

	X_Hud_Init();

	X_GS_Init();

	// Hooks

	if (re.DrawStretchPic != Hook_DrawStretchPic)
	{
		Original_DrawStretchPic = re.DrawStretchPic;
		re.DrawStretchPic = Hook_DrawStretchPic;
	}

	if (re.RenderScene != Hook_RenderScene)
	{
		Original_RenderScene = re.RenderScene;
		re.RenderScene = Hook_RenderScene;
	}

	if (re.AddRefEntityToScene != (void (*)(const refEntity_t *, qboolean)) Hook_AddRefEntityToScene)
	{
		Original_AddRefEntityToScene = re.AddRefEntityToScene;
		re.AddRefEntityToScene = (void (*)(const refEntity_t *, qboolean)) Hook_AddRefEntityToScene;
	}

	if (re.SetColor != Hook_SetColor)
	{
		Original_SetColor = re.SetColor;
		re.SetColor = Hook_SetColor;
	}

	// Register commands

	MAKERGBA(xmod.currentColor, 1.0, 1.0, 1.0, 1.0);

	X_Main_RegisterXCommand(x_item_fix_simpleitems_size, "1", "0", "1", X_HELP_ITM_FIX_SIMPLEITEMS_SIZE);
	cg_simpleitems = Cvar_Get("cg_simpleitems", "0", CVAR_ARCHIVE);

	X_Main_RegisterXCommand(x_snd_fix_unfreeze, "1", "0", "1", X_HELP_SND_FIX_UNFREEZE);

	X_Main_RegisterXCommand(x_snd_kill, "1", "0", "4", X_HELP_SND_KILL);

	Cmd_AddCommand("x_state", Print_State);
	Cmd_AddCommand("x_userinfo", Print_UserInfo);
	Cmd_AddCommand("x_say", Say_Encrypted);

	if (xmod.gs.mode == ModeOSP)
	{
		Cvar_RemoveCheatProtected("cg_gunX");
		Cvar_RemoveCheatProtected("cg_gunY");
		Cvar_RemoveCheatProtected("cg_gunZ");
		Cvar_RemoveCheatProtected("cg_centertime");
	}

	PrintWelcomeLogo();

	xmod.state = StateStarted;
}

void X_Main_StopAfterCGameVM(void)
{
	X_Net_Teardown();

	X_Hud_Destroy();

	X_Main_XModeDisableOutput(qfalse);

	Cvar_ResetXmodProtection();

	Cmd_RemoveCommand("x_state");
	Cmd_RemoveCommand("x_userinfo");
	Cmd_RemoveCommand("x_say");

	// Cleanup context
	memset(&xmod, 0, sizeof(xmod));
	xmod.state = StateStopped;
}

qboolean X_Main_IsXModeActive(void)
{
	if (xmod.state != StateStarted)
	{
		return qfalse;
	}

	if (!x_enable->integer)
	{
		return qfalse;
	}

	if (x_enable->flags & CVAR_XHCK_OFF)
	{
		return qfalse;
	}

	if (xmod.gs.mode != ModeBaseQ3 && xmod.gs.mode != ModeOSP)
	{
		return qfalse;
	}

	return qtrue;
}

qboolean X_HckActive(void)
{
	return X_Main_IsXModeActive() && X_Main_IsXModeHackActive();
}

qboolean X_Main_IsXModeHackActive(void)
{
	if (clc.demoplaying)
	{
		return qtrue;
	}

	if (xmod.gs.svcheats)
	{
		return qtrue;
	}

	return xmod.hack;
}

qboolean X_Main_IsXModeHackCommandActive(cvar_t *cmd)
{
	if (clc.demoplaying)
	{
		return qtrue;
	}

	if (xmod.gs.svcheats)
	{
		return qtrue;
	}

	if (xmod.hack)
	{
		// Enable by default, check for disable by server
		if (cmd->flags & CVAR_XHCK_OFF)
		{
			return qfalse;
		}
	}
	else
	{
		// Disable by default, check for enable by server
		if (!(cmd->flags & CVAR_XHCK_ON))
		{
			return qfalse;
		}
	}

	return qtrue;
}

void X_Main_XModeDisableOutput(qboolean disable)
{
	xmod_disable_output = disable;
}

qboolean X_IsPK3PureCompatible(const char *mode, const char *basename)
{
	return (!Q_stricmp(mode, "baseq3") && !Q_stricmp(basename, "xq3e") ? qtrue : qfalse);
}

cvar_t *X_Main_RegisterXModeCmd(char *cmd, char *dfault, char *start, char *stop, char *description, int flags, int checktype)
{
	cvar_t *cvar = Cvar_Get(cmd, dfault, flags);

	if (start && stop)
	{
		Cvar_CheckRange(cvar, start, stop, checktype);
	}

	if (description)
	{
		Cvar_SetDescription(cvar, description);
	}

	return cvar;
}

static void LoadXModeResources(void)
{
	XModResources *rs = &xmod.rs;
	char shader[32];

	// Foe hack

	rs->shaderFoe = re.RegisterShader(X_TEAM_FOE_SHADER);
	rs->shaderXFoeUnfreeze = re.RegisterShader(X_TEAM_XFOE_UNFREEZE_SHADER);
	for (int i = 0; i < countof(rs->shaderXFoe); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_TEAM_XFOE_SHADER, i + 1);
		rs->shaderXFoe[i] = re.RegisterShader(shader);
	}

	// Hit crosshair

	for (int i = 0; i < countof(*rs->shaderCrosshairs); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_CROSSHAIR_SHADER, 'a' + i);
		rs->shaderCrosshairs[0][i] = re.RegisterShader(shader);
		Com_sprintf(shader, sizeof(shader), X_CROSSHAIR_SHADER2, 'a' + i);
		rs->shaderCrosshairs[1][i] = re.RegisterShader(shader);
	}

	for (int i = 0; i < 4; i++)
	{
		Com_sprintf(shader, sizeof(shader), X_CROSSHAIR_DAMAGE, 'a' + i);
		rs->shaderHit[i] = re.RegisterShader(shader);
	}

	// Draw damage

	rs->modelMissle = re.RegisterModel(X_MODEL_MISSLE_HIT);
	rs->modelRail = re.RegisterModel(X_MODEL_RAIL_HIT);
	rs->modelBullet = re.RegisterModel(X_MODEL_BULLET_HIT);

	for (int i = 0; i < countof(rs->shaderNumbers); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_NUMBERS_SHADER, i);
		rs->shaderNumbers[i] = re.RegisterShader(shader);
	}

	rs->shaderOneHPHit = re.RegisterShader(X_HIT_SHADER);

	// Crosshair limit

	for (int i = 0; i < countof(rs->shaderXCrosshairs); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_XCROSSHAIR_SHADER, i + 1);
		rs->shaderXCrosshairs[i] = re.RegisterShader(shader);

		Com_sprintf(shader, sizeof(shader), X_XCROSSHAIR_R45_SHADER, i + 1);
		rs->shaderXCrosshairsR45[i] = re.RegisterShader(shader);
	}

	for (int i = 0; i < countof(rs->shaderDecors); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_XCROSSHAIR_DECOR, i + 1);
		rs->shaderDecors[i] = re.RegisterShader(shader);

		Com_sprintf(shader, sizeof(shader), X_XCROSSHAIR_R45_DECOR, i + 1);
		rs->shaderDecorsR45[i] = re.RegisterShader(shader);
	}

	// Misc
	rs->shaderFreeze = re.RegisterShader(X_TEAM_FREEZE_SHADER);
	rs->shaderXFreezeTeam = re.RegisterShader(X_TEAM_XFREEZE_COLOR1);
	rs->shaderXFreezeEnemy = re.RegisterShader(X_TEAM_XFREEZE_COLOR2);

	rs->soundOldUnfreeze = S_RegisterSound(X_SOUND_OLD_UNFREEZE, qfalse);
	rs->soundUnfreeze = S_RegisterSound(X_SOUND_UNFREEZE, qfalse);
	rs->soundKill[0] = S_RegisterSound(X_SOUND_KILL_1, qfalse);
	rs->soundKill[1] = S_RegisterSound(X_SOUND_KILL_2, qfalse);
	rs->soundKill[2] = S_RegisterSound(X_SOUND_KILL_3, qfalse);
	rs->soundKill[3] = S_RegisterSound(X_SOUND_KILL_4, qfalse);

	// Hitboxes
	rs->modelHitbox = re.RegisterModel(X_MODEL_HITBOX_3D);
	rs->shaderHitbox = re.RegisterShader(X_SHADER_HITBOX_2D);

	// Pickups
	rs->shaderPowerups[0] = re.RegisterShader(X_BATTLESUIT_SHADER);
	rs->shaderPowerups[1] = re.RegisterShader(X_QUAD_SHADER);
	rs->shaderPowerups[2] = re.RegisterShader(X_HASTE_SHADER);
	rs->shaderPowerups[3] = re.RegisterShader(X_INVISIBILITY_SHADER);
	rs->shaderPowerups[4] = re.RegisterShader(X_REGENERATION_SHADER);
	rs->shaderPowerups[5] = re.RegisterShader(X_FLIGHT_SHADER);

	rs->shaderArmors[0] = re.RegisterShader(X_GREEN_ARMOR_SHADER);
	rs->shaderArmors[1] = re.RegisterShader(X_YELLOW_ARMOR_SHADER);
	rs->shaderArmors[2] = re.RegisterShader(X_RED_ARMOR_SHADER);

	rs->shaderMega = re.RegisterShader(X_MEGA_SHADER);

	// Charmap

	for (int i = 0; i < countof(rs->shaderCharmap); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_CHARMAP_SHADER, 8 << (i + 1));
		rs->shaderCharmap[i] = re.RegisterShader(shader);
	}

	rs->shaderXCharmap = re.RegisterShader(X_XCHARMAP_SHADER);
	rs->shaderXOverlayChars = re.RegisterShader("xmod/gfx/xoverlaychars");

	// Score

	for (int i = 0; i < countof(rs->shaderSkill); i++)
	{
		Com_sprintf(shader, sizeof(shader), X_MENU_SKILL_SHADER, i);
		rs->shaderSkill[i] = re.RegisterShaderNoMip(shader);
	}

	rs->shaderMedalGauntlet = re.RegisterShader(X_MEDAL_GAUNTLET_SHADER);
	rs->shaderMedalExcellent = re.RegisterShader(X_MEDAL_EXCELLENT_SHADER);
	rs->shaderMedalImpressive = re.RegisterShader(X_MEDAL_IMPRESSIVE_SHADER);
	rs->shaderMedalAssist = re.RegisterShader(X_MEDAL_ASSIST_SHADER);
	rs->shaderMedalDefend = re.RegisterShader(X_MEDAL_DEFEND_SHADER);
	rs->shaderMedalCapture = re.RegisterShader(X_MEDAL_CAPTURE_SHADER);
	rs->shaderIconLG = re.RegisterShader(X_ICON_LIGHTNING_SHADER);
	rs->shaderIconRL = re.RegisterShader(X_ICON_RAILGUN_SHADER);
	rs->shaderNoModel = re.RegisterShader(X_ICON_NO_MODEL);
}

static void PrintWelcomeLogo(void)
{
	static qboolean _shown = qfalse;

	if (_shown)
	{
		return;
	}

	char *messages[] = {
	"\n"
	"^b  _      _              _           _                  _      \n"
	"/_/\\    /\\ \\           /\\ \\       /\\ \\                /\\ \\    \n"
	"\\ \\ \\   \\ \\_\\         /  \\ \\     /  \\ \\              /  \\ \\   \n"
	" \\ \\ \\__/ / /        / /\\ \\ \\   / /\\ \\ \\            / /\\ \\ \\  \n"
	"  \\ \\__ \\/_/        / / /\\ \\ \\ / / /\\ \\ \\          / / /\\ \\_\\ \n"
	"   \\/_/\\__/\\       / / /  \\ \\_\\\\/_//_\\ \\ \\        / /_/_ \\/_/ \n"
	"    _/\\/__\\ \\     / / / _ / / /  __\\___ \\ \\      / /____/\\    \n"
	"   / _/_/\\ \\ \\   / / / /\\ \\/ /  / /\\   \\ \\ \\    / /\\____\\/    \n"
	"  / / /   \\ \\ \\ / / /__\\ \\ \\/  / /_/____\\ \\ \\  / / /______    \n"
	" / / /    /_/ // / /____\\ \\ \\ /__________\\ \\ \\/ / /_______\\   \n"
	" \\/_/     \\_\\/ \\/________\\_\\/ \\_____________\\/\\/__________/   \n\n"
	"                                                  ^dENGINE\n\n",

	"\n"
	"^j:::    :::  ::::::::   ::::::::  ::::::::::    :::::::::: ::::    :::  :::::::: ::::::::::: ::::    ::: ::::::::::\n"
	"^k:+:    :+: :+:    :+: :+:    :+: :+:           :+:        :+:+:   :+: :+:    :+:    :+:     :+:+:   :+: :+:       \n"
	"^l +:+  +:+  +:+    +:+        +:+ +:+           +:+        :+:+:+  +:+ +:+           +:+     :+:+:+  +:+ +:+       \n"
	"^m  +#++:+   +#+    +:+     +#++:  +#++:++#      +#++:++#   +#+ +:+ +#+ :#:           +#+     +#+ +:+ +#+ +#++:++#  \n"
	"^n +#+  +#+  +#+  # +#+        +#+ +#+           +#+        +#+  +#+#+# +#+   +#+#    +#+     +#+  +#+#+# +#+       \n"
	"^o#+#    #+# #+#   +#+  #+#    #+# #+#           #+#        #+#   #+#+# #+#    #+#    #+#     #+#   #+#+# #+#       \n"
	"^p###    ###  ###### ### ########  ##########    ########## ###    ####  ######## ########### ###    #### ##########\n\n"
	"^9       From ^7XQ3E^9 with love, have fun ;)\n\n",

	"\n"
	"^b __    __   ______    ______   ________        ________                      __                     \n"
	"|  \\  |  \\ /      \\  /      \\ |        \\      |        \\                    |  \\                    \n"
	"| ^1$$  ^b| ^1$$^b|  ^1$$$$$$^b\\|  ^1$$$$$$^b\\| ^1$$$$$$$$      ^b| ^1$$$$$$$$ ^b_______    ______   \\^1$$ ^b_______    ______  \n"
	" \\^1$$^b\\/  ^1$$^b| ^1$$  ^b| ^1$$ ^b\\^1$$^b__| ^1$$^b| ^1$$^b__          | ^1$$^b__    |       \\  /      \\ |  \\|       \\  /      \\ \n"
	"  >^1$$  $$ ^b| ^1$$  ^b| ^1$$  ^b|     ^1$$^b| ^1$$  ^b\\         | ^1$$  ^b\\   | ^1$$$$$$$^b\\|  ^1$$$$$$^b\\| ^1$$^b| ^1$$$$$$$^b\\|  ^1$$$$$$^b\\\n"
	" /  ^1$$$$^b\\ | ^1$$ ^b_| ^1$$ ^b__\\^1$$$$$^b\\| ^1$$$$$         ^b| ^1$$$$$   ^b| ^1$$  ^b| ^1$$^b| ^1$$  ^b| ^1$$^b| ^1$$^b| ^1$$  ^b| ^1$$^b| ^1$$    $$\n"
	"^b|  ^1$$ ^b\\^1$$^b\\| ^1$$^b/ \\ ^1$$^b|  \\__| ^1$$^b| ^1$$^b_____       | ^1$$^b_____ | ^1$$  ^b| ^1$$^b| ^1$$^b__| ^1$$^b| ^1$$^b| ^1$$  ^b| ^1$$^b| ^1$$$$$$$$\n"
	"^b| ^1$$  ^b| ^1$$ ^b\\^1$$ $$ $$ ^b\\^1$$    $$^b| ^1$$     ^b\\      | ^1$$     ^b\\| ^1$$  ^b| ^1$$ ^b\\^1$$    $$^b| ^1$$^b| ^1$$  ^b| ^1$$ ^b\\^1$$     ^b\\\n"
	" \\^1$$   ^b\\^1$$  ^b\\^1$$$$$$^b\\  \\^1$$$$$$  ^b\\^1$$$$$$$$       ^b\\^1$$$$$$$$ ^b\\^1$$   ^b\\^1$$ ^b_\\^1$$$$$$$ ^b\\^1$$ ^b\\^1$$   ^b\\^1$$  ^b\\^1$$$$$$$\n"
	"                ^b\\^1$$$                                              ^b|  \\__| ^1$$                        \n"
	"                                                                   ^b\\^1$$    $$                        \n"
	"                                                                    ^b\\^1$$$$$$                         \n\n",

	"\n"
	"^a __  _____  _____ _____               _                  _                _     __  \n"
	"^b \\ \\/ / _ \\|___ /| ____|  _ __   ___ | |_    __ _    ___| |__   ___  __ _| |_   \\ \\ \n"
	"^c  \\  / | | | |_ \\|  _|   | '_ \\ / _ \\| __|  / _` |  / __| '_ \\ / _ \\/ _` | __| (_) |\n"
	"^d  /  \\ |_| |___) | |___  | | | | (_) | |_  | (_| | | (__| | | |  __/ (_| | |_   _| |\n"
	"^e /_/\\_\\__\\_\\____/|_____| |_| |_|\\___/ \\__|  \\__,_|  \\___|_| |_|\\___|\\__,_|\\__| ( ) |\n"
	"^f                                                                               |/_/ \n\n"
	};

	char message[0x1000];
	Q_strncpyz(message, messages[rand() % countof(messages)], sizeof(message));

	X_Misc_MakeStringSymbolic(message);
	Com_Printf("%s", message);

	_shown = qtrue;
}

// =========================
//   On events zone

sfxHandle_t X_Main_Event_ReplaceSoundOnSoundStart(int entity, sfxHandle_t sound)
{
	if (!X_Main_IsXModeActive())
	{
		return sound;
	}

	if (x_snd_fix_unfreeze->integer)
	{
		if (xmod.gs.freezetag && sound == xmod.rs.soundOldUnfreeze && entity > MAX_CLIENTS)
		{
			return xmod.rs.soundUnfreeze;
		}
	}

	return sound;
}

void X_Main_Event_OnSoundStart(int entityNum, const vec3_t origin, const char *soundName)
{
	if (!X_Main_IsXModeActive())
	{
		return;
	}

	X_CH_ChangeCrosshairOnSoundTrigger(soundName);
}

void X_Main_Event_OnGetSnapshot(snapshot_t *snapshot)
{
	if (!X_Main_IsXModeActive())
	{
		return;
	}

	xmod.snap = *snapshot;
	xmod.snapNum++;

	X_GS_UpdatePlayerStateBySnapshot(snapshot);

	X_DMG_ParseSnapshotDamage();

	X_Net_RenewPortOnSnapshot(snapshot);

	X_PS_AutoRevival(snapshot);

	X_Hud_HackTurnOffDefaultScores(snapshot);
}

void X_Main_Event_OnConfigstringModified(int index)
{
	X_GS_UpdateGameStateOnConfigStringModified(index);
}

qboolean X_Main_Event_OnServerCommand(const char *cmd, qboolean *result)
{
	*result = qtrue;
	printf(">>>>server command %s \n", cmd);

	if (!strcmp(cmd, "chat") || !strcmp(cmd, "tchat"))
	{
		const char *id = Cmd_Argv(2);
		*result = X_Con_OnChatMessage(Cmd_Argv(1), (strlen(id) > 0 ? atoi(id) : -1));
		return qtrue;
	}

	if (!strcmp(cmd, "scores"))
	{
		X_GS_UpdatePlayerScores();
		return qtrue;
	}

	//TODO: support it when console is closed
	if (!strcmp(cmd, "xstats1"))
	{
		*result = qfalse;
		return X_GS_UpdatePlayerXStats1();
	}

	if (!strcmp(cmd, "cp"))
	{
		X_GS_UpdateOnOvertime(Cmd_Argv(1));
		return qtrue;
	}

	return qfalse;
}

void X_Main_Event_OnDrawScreen(void)
{
	X_Hud_ValidateDefaultScores();

	if (!X_Main_IsXModeActive())
	{
		return;
	}

	X_Hud_TurnOffForcedTransparency();

	X_Net_DrawScanProgress();
	X_Hud_DrawHud();

	X_Hud_TurnOnForcedTransparency();
}

void X_Main_Event_OnChatCommand(field_t *field)
{
	if (!X_Main_IsXModeActive())
	{
		return;
	}

	X_Con_OnLocalChatCommand(field);
}

// =========================
//   Hooks zone

static void Hook_DrawStretchPic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader)
{
	if (!X_Main_IsXModeActive())
	{
		Original_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader);
		return;
	}

	if (X_Hud_HideOnXScore())
	{
		return;
	}

	if (X_CH_CustomizeCrosshair(x, y, w, h, shader))
	{
		return;
	}

	X_Hud_CustomizeConsoleFont(&shader);

	Original_DrawStretchPic(x, y, w, h, s1, t1, s2, t2, shader);
}

void Hook_RenderScene(const refdef_t *fd)
{
	if (!X_Main_IsXModeActive())
	{
		Original_RenderScene(fd);
		return;
	}

	X_DMG_DrawDamage(fd);
	X_CH_CalculateDistance(fd);

	if (!X_Misc_IsNoWorldRender(fd) || !X_Hud_HideOnXScore())
	{
		Original_RenderScene(fd);
	}

	X_Net_CheckScanPortTimeout();
	X_Team_ValidateFrozenPlayers(fd);
}

void Hook_AddRefEntityToScene(refEntity_t *ref, qboolean intShaderTime)
{
	if (!X_Main_IsXModeActive())
	{
		Original_AddRefEntityToScene(ref, intShaderTime);
		return;
	}

	X_Team_CustomizeFoe(ref);
	X_Team_CustomizeFreezeEffect(ref);
	X_PS_CustomizePlayerModel(ref);

	CustomizeSimpleItems(ref);

	Original_AddRefEntityToScene(ref, intShaderTime);

	X_DMG_PushDamageForEntity(ref);
}

void Hook_SetColor(const float *rgba)
{
	if (!X_Main_IsXModeActive())
	{
		Original_SetColor(rgba);
		return;
	}

	Original_SetColor(rgba);

	if (rgba)
	{
		for (int i = 0; i < 4; i++)
		{
			xmod.currentColor[i] = rgba[i];
		}
	}
	else
	{
		MAKERGBA(xmod.currentColor, 1.0, 1.0, 1.0, 1.0);
	}
}

void X_Main_Hook_AddLoopingSound(int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx)
{
	S_AddLoopingSound(entityNum, origin, velocity, sfx);
}

void X_Main_Hook_UpdateEntityPosition(int entityNum, const vec3_t origin)
{
	X_GS_UpdateEntityPosition(entityNum, origin);
}

qboolean X_Main_Hook_CGame_Cvar_SetSafe(const char *var_name, const char *value)
{
	// Prevent reset of max fps
	if (X_Main_IsXModeActive() && var_name && strcmp(var_name, "com_maxfps") == 0)
	{
		return qfalse;
	}
	return qtrue;
}

int X_Main_Hook_FS_GetFileList(const char *path, const char *extension, char *listbuf, int bufsize)
{
	char buffer[64];
	if (!strcmp(path, "demos") && !strcmp(extension, "dm_48"))
	{
		Com_sprintf(buffer, sizeof(buffer), "dm_%d", Cvar_VariableIntegerValue("protocol"));
		extension = buffer;
	}

	return FS_GetFileList(path, extension, listbuf, bufsize);
}

// =========================
//   Commands zone

static void Print_Version(void)
{
	Com_Printf("\n  ^1\xd8\xd1\xb3\xc5 ^fengine\n\n");
	Com_Printf("     ^fVersion : ^7" XMOD_VERSION " ^f" XMOD_ARCH "\n");
	Com_Printf("     ^fBuild   : " __DATE__ " " __TIME__ "\n\n");
	Com_Printf("  ^fDeveloped by ^7x0ry^f and ^7amRa\n\n"
			   "  ^fSpecial thanks to\n\n"
			   "      ^7Progressor^f, ^7jotunn^f, ^7pdv^f, ^7Xyecckuu'^f, ^7Zenx^f,\n"
			   "      ^7Diff^f, ^7killarbyte^f, ^7neko^f and others\n\n"
			   "      Servers ^7q3msk.ru^f, ^7aim.pm^f\n\n");
}

static void ChangeName(void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf(
		"Usage: x_name \"<playername>\"\n\n"
		"Online nickname generator: ^nhttp://dos.ninja/projects/q3gfx.html\n\n"
		);
		return;
	}

	const char *name = Cmd_Argv(1);
	char reformated[256] = {0};
	size_t length = strlen(name);

	Q_strncpyz(reformated, name, countof(reformated));

	for (size_t i = 0; i < length; i++)
	{
		if (reformated[i] == '#' && i + 2 < length)
		{
			char hex[] = {'0', 'x', reformated[i + 1], reformated[i + 2], 0};
			int charnum = Com_HexStrToInt(hex);
			if (charnum > 255 || charnum < 1)
			{
				continue;
			}

			length -= 2;
			reformated[i] = (char) charnum;

			memmove(reformated + i + 1, reformated + i + 3, length - i);
		}
	}

	Cvar_Set("name", reformated);
}

static char *StateToString(qboolean state)
{
	return (state ? "^2ON " : "^1OFF");
}

static void PrintCommandState(const char *key, qboolean deflt)
{
	unsigned int flags = Cvar_Flags(key);

	if (flags & CVAR_XHCK_OFF)
	{
		Com_Printf("  ^f%-32s %s ^7server-forced\n", key, StateToString(qfalse));
	}
	else if ((flags & CVAR_XHCK_ON))
	{
		Com_Printf("  ^f%-32s %s ^7server-forced\n", key, StateToString(qtrue));
	}
	else
	{
		Com_Printf("  ^f%-32s %s ^zinherited\n", key, StateToString(deflt));
	}
}

static void Print_State(void)
{
	qboolean xmod_l = X_Main_IsXModeActive();
	qboolean xhck_l = X_HckActive();

	Com_Printf("\n^7XQ3E status\n\n");
	Com_Printf("  ^fenabled             %s\n", StateToString(xmod_l));
	Com_Printf("  ^fx_hck               %s\n\n", StateToString(xhck_l));

	static char key[BIG_INFO_KEY];
	static char value[BIG_INFO_VALUE];

	Com_Printf("\n^7Commands:\n\n");

	const char *info = Cvar_InfoString(CVAR_XMOD, 0);
	while (info)
	{
		info = Info_NextPair(info, key, value);

		if (!key[0])
		{
			break;
		}

		if (!Q_stricmp(key, "x_enable"))
		{
			PrintCommandState(key, xmod_l);
		}
		else
		{
			PrintCommandState(key, xhck_l);
		}
	}

	Com_Printf("\n");
}

static char *UntokenizeNickname(const char *nickname, char *output, int size)
{
	int b = 0;
	Com_Memset(output, 0, size);

	for (int a = 0; nickname[a]; a++)
	{
		if (b >= size - 1)
		{
			break;
		}

		if (nickname[a] == '^')
		{
			output[b++] = nickname[a];
			output[b++] = '^';
			output[b++] = '7';
		}
		else
		{
			output[b++] = nickname[a];
		}
	}

	output[b] = '\0';

	return output;
}

static char *TeamToString(int team)
{
	if (team == TEAM_FREE)
	{
		return "^mfree";
	}
	else if (team == TEAM_RED)
	{
		return "^1red";
	}
	else if (team == TEAM_BLUE)
	{
		return "^4blue";
	}
	else if (team == TEAM_SPECTATOR)
	{
		return "^zspectator";
	}

	static char msg[64];
	Com_sprintf(msg, sizeof(msg), "unknown (%d)", team);
	return msg;
}

static void Print_UserInfo(void)
{
	static char key[BIG_INFO_KEY];
	static char value[BIG_INFO_VALUE];
	int print_id = (Cmd_Argc() > 1 ? atoi(Cmd_Argv(1)) : -1);

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		int offset = cl.gameState.stringOffsets[CS_PLAYERS + i];
		if (!offset)
		{
			continue;
		}

		if (print_id != -1 && print_id != i)
		{
			continue;
		}

		unsigned short sign = 0;
		CRC_Init(&sign);

		const char *cmd = cl.gameState.stringData + offset;

		const char *data_c1 = Info_ValueForKey(cmd, "c1");
		CRC_ContinueProcessString(&sign, data_c1, strlen(data_c1));

		const char *data_c2 = Info_ValueForKey(cmd, "c2");
		CRC_ContinueProcessString(&sign, data_c2, strlen(data_c2));

		const char *data_model = Info_ValueForKey(cmd, "model");

		char *skin = strchr(data_model, '/');
		if (skin)
		{
			*skin = '\0';
		}

		CRC_ContinueProcessString(&sign, data_model, strlen(data_model));

		const char *data_hmodel = Info_ValueForKey(cmd, "hmodel");

		skin = strchr(data_hmodel, '/');
		if (skin)
		{
			*skin = '\0';
		}

		CRC_ContinueProcessString(&sign, data_hmodel, strlen(data_hmodel));

		Com_Printf("\n^7Client #%d [ sign : ^3%04X^7 ]\n\n", i, sign);

		cmd = cl.gameState.stringData + offset;
		while (cmd)
		{
			char temp[256];
			char *v = value;

			cmd = Info_NextPair(cmd, key, value);

			if (!key[0])
			{
				break;
			}

			if (!strcmp(key, "n"))
			{
				Q_strncpyz(key, "Name", sizeof(key));
				v = UntokenizeNickname(value, temp, sizeof(temp));
			}
			else if (!strcmp(key, "t"))
			{
				Q_strncpyz(key, "Team", sizeof(key));
				v = TeamToString(atoi(v));
			}
			else if (!strcmp(key, "c1"))
			{
				Q_strncpyz(key, "Color1", sizeof(key));
			}
			else if (!strcmp(key, "c2"))
			{
				Q_strncpyz(key, "Color2", sizeof(key));
			}
			else if (!strcmp(key, "model"))
			{
				Q_strncpyz(key, "Model", sizeof(key));
			}
			else if (!strcmp(key, "hmodel"))
			{
				Q_strncpyz(key, "Head Model", sizeof(key));
			}
			else if (!strcmp(key, "hc"))
			{
				Q_strncpyz(key, "Handicap", sizeof(key));
			}
			else if (!strcmp(key, "w"))
			{
				Q_strncpyz(key, "Wins", sizeof(key));
			}
			else if (!strcmp(key, "l"))
			{
				Q_strncpyz(key, "Losses", sizeof(key));
			}
			else if (!strcmp(key, "tl"))
			{
				Q_strncpyz(key, "Team leader", sizeof(key));
			}
			else if (!strcmp(key, "tt"))
			{
				Q_strncpyz(key, "Team task", sizeof(key));
			}

			Com_Printf("  ^f%-15s  ^7%s\n", key, v);
		}
	}
}

static void CustomizeSimpleItems(refEntity_t *ref)
{
	if (ref->reType != RT_SPRITE)
	{
		return;
	}

	if (!cg_simpleitems || !cg_simpleitems->integer)
	{
		return;
	}

	if (!x_item_fix_simpleitems_size->integer)
	{
		return;
	}

	// Health

	if (xmod.rs.shaderMega && xmod.rs.shaderMega == ref->customShader)
	{
		ref->origin[2] += 10;
		return;
	}

	// Powerups

	for (int i = 0; i < countof(xmod.rs.shaderPowerups); i++)
	{
		if (!xmod.rs.shaderPowerups[i])
		{
			continue;
		}

		if (xmod.rs.shaderPowerups[i] == ref->customShader)
		{
			ref->radius = 20;
			ref->origin[2] += 10;
			return;
		}
	}

	// Armors

	for (int i = 0; i < countof(xmod.rs.shaderArmors); i++)
	{
		if (!xmod.rs.shaderArmors[i])
		{
			continue;
		}

		if (xmod.rs.shaderArmors[i] == ref->customShader)
		{
			ref->radius = 20;
			ref->origin[2] += 15;
			return;
		}
	}
}

static void Say_Encrypted(void)
{
	if (Cmd_Argc() < 2)
	{
		return;
	}

	X_Misc_SendEncryptedMessage(Cmd_ArgsFrom(1));
}

static sfxHandle_t X_Main_GetSndKill(unsigned int variant)
{
	return (variant > X_SND_KILL_MAX) ? xmod.rs.soundKill[0] : xmod.rs.soundKill[variant - 1];
}

void X_Main_OnDeathSound(int target, int attacker)
{
	if (attacker == clc.clientNum && attacker != target && X_Main_IsXModeActive() && x_snd_kill && x_snd_kill->integer)
	{
		S_StartLocalSound(X_Main_GetSndKill(x_snd_kill->integer), CHAN_LOCAL_SOUND);
	}
}

qboolean X_Main_IsOutputDisabled(void)
{
	return xmod_disable_output;
}
