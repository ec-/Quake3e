#include "client.h"
#include "x_local.h"
#include "../botlib/l_crc.h"

#define X_CS_CONFIGURATION 1000

static cvar_t *x_gs_show_events = 0;

// ====================
//   Implementation

extern void CL_GetGameState(gameState_t *gs);

static void UpdateServerInfo(void);
static void UpdateSystemInfo(void);
static void UpdatePlayerInfo(void);
static void UpdateRoundStartTime(void);
static void UpdateWarmupTime(void);

static void NotifyOnConnectedPlayer(int client);
static void NotifyOnChangedPlayer(int client);
static void NotifyOnDisconnectedPlayer(int client);
static void ParsePlayerInfo(XPlayerState *ps, char *info);
static void LoadModelIcons(XPlayerState *ps, const char *model);

static unsigned short CalculatePlayerSignature(char *cmd);
static void UpdateXModeConfiguartion(void);
static void ParseXModCommands(const char *cmdset);
static void UpdatePlayerPositionOnFakeSound(int clientNum, const vec3_t origin);
static void LoadEntitiesToCache(snapshot_t *snapshot);
static void InterceptEvents(snapshot_t *snapshot);
static void UpdateEntityPositionInCache(int entity, const vec3_t origin);

static qboolean IsNewEvent(int event, int param, int flags, int client1, int client2);
static void RemoveExpiredEventsFromCache(void);

void X_GS_Init()
{
	X_Main_RegisterXCommand(x_gs_show_events, "0", "0", "1", 0);

	UpdateServerInfo();
	UpdateSystemInfo();
	UpdatePlayerInfo();
	UpdateXModeConfiguartion();
	UpdateRoundStartTime();
	UpdateWarmupTime();

	xmod.gs.events.count = 0;
	for (int i = 0; i < countof(xmod.gs.events.active); i++)
	{
		xmod.gs.events.active[i] = i;
	}
}

static void UpdateServerInfo(void)
{
	char *serverInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	xmod.gs.type = atoi(Info_ValueForKey(serverInfo, "g_gametype"));
	if (xmod.gs.type < 0 || xmod.gs.type > GameUnknown)
	{
		xmod.gs.type = GameUnknown;
	}

	int promode = atoi(Info_ValueForKey(serverInfo, "server_promode"));
	xmod.gs.promode = (promode ? qtrue : qfalse);

	int server_freezetag = atoi(Info_ValueForKey(serverInfo, "server_freezetag"));
	xmod.gs.freezetag = (server_freezetag ? qtrue : qfalse);

	xmod.gs.roundlimit = atoi(Info_ValueForKey(serverInfo, "roundlimit"));
	xmod.gs.timelimit = atoi(Info_ValueForKey(serverInfo, "timelimit"));
	xmod.gs.fraglimit = atoi(Info_ValueForKey(serverInfo, "fraglimit"));
	xmod.gs.capturelimit = atoi(Info_ValueForKey(serverInfo, "capturelimit"));

	ParseXModCommands(serverInfo);
}

static void UpdateSystemInfo(void)
{
	char *systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SYSTEMINFO];
	const char *fs_game = Info_ValueForKey(systemInfo, "fs_game");
	if (!strcmp(fs_game, "baseq3"))
	{
		xmod.gs.mode = ModeBaseQ3;
	}
	else if (!strcmp(fs_game, "osp"))
	{
		xmod.gs.mode = ModeOSP;
	}
	else if (!strcmp(fs_game, "cpma"))
	{
		xmod.gs.mode = ModeCPMA;
	}
	else if (!strcmp(fs_game, "excessiveplus"))
	{
		xmod.gs.mode = ModeExcessivePlus;
	}
	else
	{
		xmod.gs.mode = ModeUnknown;
	}

	int svcheats = atoi(Info_ValueForKey(systemInfo, "sv_cheats"));
	xmod.gs.svcheats = (svcheats ? qtrue : qfalse);

	ParseXModCommands(systemInfo);
}

static void UpdatePlayerInfo(void)
{
	for (int i = 0; i < MAX_CLIENTS - 1; i++)
	{
		NotifyOnConnectedPlayer(i);
	}
}

static void UpdateGameTimer(void)
{
	if (xmod.gs.timer.warmup)
	{
		xmod.gs.timer.type = GTimerWarmup;
	}
	else if (xmod.gs.timer.start)
	{
		if (cl.serverTime && cl.serverTime < xmod.gs.timer.start)
		{
			xmod.gs.timer.type = GTimerTimeout;
		}
		else
		{
			xmod.gs.timer.type = GTimerRoundTime;
		}
	}
	else
	{
		xmod.gs.timer.type = GTimerNone;
	}
}

static void UpdateRoundStartTime(void)
{
	xmod.gs.timer.start = atoi(X_Misc_GetConfigString(CS_LEVEL_START_TIME));
	UpdateGameTimer();
	xmod.gs.overtime = 0;
}

static void UpdateWarmupTime(void)
{
	xmod.gs.timer.warmup = atoi(X_Misc_GetConfigString(CS_WARMUP));
	UpdateGameTimer();
}

static void NotifyOnConnectedPlayer(int client)
{
	int offset = cl.gameState.stringOffsets[CS_PLAYERS + client];
	if (!offset)
	{
		return;
	}

	XPlayerState *ps = xmod.gs.ps + client;

	memset(ps, 0, sizeof(XPlayerState));

	char *cmd = cl.gameState.stringData + offset;
	ps->active = qtrue;
	ps->entity = -1;

	ParsePlayerInfo(ps, cmd);
}

static void NotifyOnChangedPlayer(int client)
{
	int offset = cl.gameState.stringOffsets[CS_PLAYERS + client];
	if (!offset)
	{
		return;
	}

	XPlayerState *ps = xmod.gs.ps + client;
	char *cmd = cl.gameState.stringData + offset;

	ParsePlayerInfo(ps, cmd);
	// ??? do we need it???
	//xmod.gs.ps[client].entity = -1; 
}

static void NotifyOnDisconnectedPlayer(int client)
{
	xmod.gs.ps[client].active = qfalse;
}

static void ParsePlayerInfo(XPlayerState *ps, char *info)
{
	ps->team = atoi(Info_ValueForKey(info, "t"));

	ps->isdead = qfalse;
	if (xmod.gs.mode == ModeOSP && ps->team == 3)
	{
		int rt = atoi(Info_ValueForKey(info, "rt"));
		int st = atoi(Info_ValueForKey(info, "st"));
		if (rt && !st)
		{
			ps->isdead = (ps->team != rt ? qtrue : qfalse); //TODO: change this shitty logic
			ps->team = rt;
		}
	}

	Q_strncpyz(ps->name, Info_ValueForKey(info, "n"), sizeof(ps->name));
	X_Misc_RemoveEffectsFromName(ps->name);

	const char *model = Info_ValueForKey(info, "model");
	if (Q_stricmp(ps->model, model))
	{
		Q_strncpyz(ps->model, model, sizeof(ps->model));
		LoadModelIcons(ps, model);
	}

	ps->sign = CalculatePlayerSignature(info);

	const char *lvl = Info_ValueForKey(info, "skill");
	if (strlen(lvl))
	{
		ps->isbot = qtrue;
		ps->botlvl = atoi(lvl);
	}
	else
	{
		ps->isbot = qfalse;
		ps->botlvl = 0;
	}

}

static void LoadModelIcons(XPlayerState *ps, const char *model)
{
	char buffer[128], name[64];

	Q_strncpyz(name, model, sizeof(name));
	char *sep = strchr(name, '/');
	if (sep)
	{
		*sep = '\0';
	}

	Com_sprintf(buffer, sizeof(buffer), "models/players/%s/icon_default.tga", name);
	ps->icons[0] = re.RegisterShaderNoMip(buffer);

	Com_sprintf(buffer, sizeof(buffer), "models/players/%s/icon_red.tga", name);
	ps->icons[1] = re.RegisterShaderNoMip(buffer);

	Com_sprintf(buffer, sizeof(buffer), "models/players/%s/icon_blue.tga", name);
	ps->icons[2] = re.RegisterShaderNoMip(buffer);
}

static unsigned short CalculatePlayerSignature(char *cmd)
{
	unsigned short sign = 0;

	CRC_Init(&sign);

	const char *data_c1 = Info_ValueForKey(cmd, "c1");
	CRC_ContinueProcessString(&sign, data_c1, strlen(data_c1));

	const char *data_c2 = Info_ValueForKey(cmd, "c2");
	CRC_ContinueProcessString(&sign, data_c2, strlen(data_c2));

	const char *data_c3 = Info_ValueForKey(cmd, "model");

	char *skin = strchr(data_c3, '/');
	if (skin)
	{
		*skin = '\0';
	}

	CRC_ContinueProcessString(&sign, data_c3, strlen(data_c3));

	const char *data_c4 = Info_ValueForKey(cmd, "hmodel");

	skin = strchr(data_c4, '/');
	if (skin)
	{
		*skin = '\0';
	}

	CRC_ContinueProcessString(&sign, data_c4, strlen(data_c4));

	return sign;
}

static void UpdateXModeConfiguartion(void)
{
	if (!cl.gameState.stringOffsets[X_CS_CONFIGURATION])
	{
		return;
	}

	char *conf = cl.gameState.stringData + cl.gameState.stringOffsets[X_CS_CONFIGURATION];

	if (Q_strncmp(conf, "\\xmode\\config", 13))
	{
		return;
	}

	ParseXModCommands(conf);
}

static void ParseXModCommands(const char *cmdset)
{
	static char key[BIG_INFO_KEY];
	static char value[BIG_INFO_VALUE];

	while (cmdset)
	{
		unsigned int cvar_flags;

		cmdset = Info_NextPair(cmdset, key, value);
		if (!key[0])
		{
			break;
		}

		if (!Q_stricmp("x_hck", key) && !Q_stricmp("1", value))
		{
			xmod.hack = qtrue;
			continue;
		}

		if (!Q_stricmpn("x_", key, 2))
		{
			cvar_flags = Cvar_Flags(key);
			if (cvar_flags != CVAR_NONEXISTENT && cvar_flags & CVAR_XMOD)
			{
				cvar_t *cvar = Cvar_Get(key, "", 0);
				cvar->flags &= ~(CVAR_XHCK_ON | CVAR_XHCK_OFF);

				if (!strcmp(value, "1"))
				{
					cvar->flags |= CVAR_XHCK_ON;
				}
				else if (!strcmp(value, "0"))
				{
					cvar->flags |= CVAR_XHCK_OFF;
				}

				continue;
			}
		}
	}
}

void X_GS_UpdateGameStateOnConfigStringModified(int index)
{
	if (index == CS_SERVERINFO)
	{
		UpdateServerInfo();
		return;
	}

	if (index == CS_SYSTEMINFO)
	{
		UpdateSystemInfo();
		return;
	}

	if (index == X_CS_CONFIGURATION)
	{
		UpdateXModeConfiguartion();
		return;
	}

	if (index == CS_LEVEL_START_TIME)
	{
		UpdateRoundStartTime();
		return;
	}

	if (index == CS_WARMUP)
	{
		UpdateWarmupTime();
		return;
	}


	if (index < CS_PLAYERS || index >= CS_PLAYERS + MAX_CLIENTS - 1)
	{
		return;
	}

	int offset = cl.gameState.stringOffsets[index];
	int client = index - CS_PLAYERS;
	if (!offset)
	{
		// Disconnected
		NotifyOnDisconnectedPlayer(client);
	}
	else
	{
		if (xmod.gs.ps[client].active)
		{
			NotifyOnChangedPlayer(client);
		}
		else
		{
			NotifyOnConnectedPlayer(client);
		}
	}
}

void X_GS_UpdatePlayerStateBySnapshot(snapshot_t *snapshot)
{
	for (int i = 0; i < countof(xmod.gs.ps); i++)
	{
		xmod.gs.ps[i].visible = qfalse;
		xmod.gs.ps[i].dead.count = 0;
	}

	for (int i = 0; i < snapshot->numEntities; i++)
	{
		if (snapshot->entities[i].eType == ET_PLAYER)
		{
			int client = snapshot->entities[i].clientNum;
			if (client < 0 || client >= MAX_CLIENTS)
			{
				continue;
			}

			XPlayerState *state = xmod.gs.ps + client;
			if (!state->active)
			{
				continue;
			}

			if (snapshot->entities[i].eFlags & EF_DEAD)
			{
				int inx = state->dead.count;
				if (inx >= countof(state->dead.entities))
				{
					continue;
				}

				if (!state->visible)
				{
					state->entity = -1;
				}

				state->dead.entities[inx].entity = snapshot->entities[i].number;
				VectorClear(state->dead.entities[inx].origin);
				state->dead.count++;

				continue;
			}
			else
			{
				state->powerups = snapshot->entities[i].powerups;
				state->entity = snapshot->entities[i].number;
				state->visible = qtrue;
			}
		}
	}

	LoadEntitiesToCache(snapshot);
	InterceptEvents(snapshot);

	if (snapshot->ps.pm_type != PM_INTERMISSION)
	{
		xmod.gs.timer.current = cl.serverTime;
	}
}

void X_GS_UpdateEntityPosition(int entity, const vec3_t origin)
{
	UpdateEntityPositionInCache(entity, origin);
	UpdatePlayerPositionOnFakeSound(entity, origin);
}

void X_GS_UpdatePlayerScores(void)
{
	int scores = atoi(Cmd_Argv(1));

	if (scores > MAX_CLIENTS)
	{
		scores = MAX_CLIENTS;
	}

	xmod.scr.team_red = atoi(Cmd_Argv(2));
	xmod.scr.team_blue = atoi(Cmd_Argv(3));

	for (int i = 0; i < scores; i++)
	{
		int client = atoi(Cmd_Argv(i * 14 + 4));
		XPlayerState *ps = X_GS_GetStateByClientId(client);
		XPlayerScore *score = &ps->score;

		score->score = atoi(Cmd_Argv(i * 14 + 5));
		score->ping = atoi(Cmd_Argv(i * 14 + 6));
		score->time = atoi(Cmd_Argv(i * 14 + 7));
		score->scoreFlags = atoi(Cmd_Argv(i * 14 + 8));
		score->powerUps = atoi(Cmd_Argv(i * 14 + 9));
		score->accuracy = atoi(Cmd_Argv(i * 14 + 10));
		score->impressiveCount = atoi(Cmd_Argv(i * 14 + 11));
		score->excellentCount = atoi(Cmd_Argv(i * 14 + 12));
		score->guantletCount = atoi(Cmd_Argv(i * 14 + 13));
		score->defendCount = atoi(Cmd_Argv(i * 14 + 14));
		score->assistCount = atoi(Cmd_Argv(i * 14 + 15));
		score->perfect = atoi(Cmd_Argv(i * 14 + 16));
		score->captures = atoi(Cmd_Argv(i * 14 + 17));
		score->active = qtrue;
	}
}

void X_GS_UpdateOnOvertime(const char *msg)
{
	static char filter[] = "^3Overtime! ^5";
	char *find = strstr(msg, filter);

	if (!find)
	{
		return;
	}

	find += sizeof(filter) - 1;

	int extra = atoi(find);
	if (extra > 0 && extra < 60)
	{
		xmod.gs.overtime += extra;
	}
}

qboolean X_GS_UpdatePlayerXStats1(void)
{
	xmod.scr.hasstats = qtrue;

	int client = atoi(Cmd_Argv(1));
	XPlayerState *ps = X_GS_GetStateByClientId(client);
	XPlayerStats *stats = &ps->stats;

	int mask = atoi(Cmd_Argv(2));
	int inx = 3;

	for (int i = 0; i < 32; i++)
	{
		if (!(mask & (1 << i)))
		{
			continue;
		}

		if (i >= WP_NUM_WEAPONS)
		{
			inx += 4;
			continue;
		}

		stats->weapons[i].hit = atoi(Cmd_Argv(inx++));
		stats->weapons[i].shts = atoi(Cmd_Argv(inx++));
		stats->weapons[i].kills = atoi(Cmd_Argv(inx++));
		stats->weapons[i].deaths = atoi(Cmd_Argv(inx++));
		stats->mask |= (1 << i);
	}

	inx += 2;

	stats->givenDmg = atoi(Cmd_Argv(inx++));
	stats->receivedDmg = atoi(Cmd_Argv(inx++));

	stats->active = qtrue;

	return X_Hud_UpdatePlayerStats();
}

static void UpdatePlayerPositionOnFakeSound(int entityNum, const vec3_t origin)
{
	XEntity *entity = X_GS_GetEntityFromCache(entityNum);

	if (!entity || (entity->type != ET_PLAYER && entity->type != ET_INVISIBLE))
	{
		return;
	}

	if (entityNum >= 0 && entityNum < MAX_CLIENTS && !(entity->flags & EF_DEAD))
	{ // Alive
		XPlayerState *state = xmod.gs.ps + entityNum;

		if (!state->active)
		{
			return;
		}

		if (entity->client != entityNum)
		{
			return;
		}

		VectorCopy(origin, state->origin);

		X_DMG_PushDamageForDirectHit(entity->client, origin);
	}
	else
	{
		XPlayerState *state = X_GS_GetStateByClientId(entity->client);

		if (!state->active)
		{
			return;
		}

		if (entity->flags & EF_DEAD)
		{
			int dead = state->dead.count;

			if (dead < countof(state->dead.entities))
			{
				for (int i = 0; i < state->dead.count; i++)
				{
					if (state->dead.entities[i].entity == entityNum)
					{
						VectorCopy(origin, state->dead.entities[i].origin);
						return;
					}
				}

				state->dead.count++;
				state->dead.entities[dead].entity = entityNum;
				VectorCopy(origin, state->dead.entities[dead].origin);
			}
		}
		else
		{
			VectorCopy(origin, state->origin);
		}
	}
}

static qboolean VectorEqualInRange(vec3_t first, vec3_t second, float range)
{
	for (int i = 0; i < 3; i++)
	{
		if (second[i] < first[i] - range || first[i] + range < second[i])
		{
			return qfalse;
		}
	}

	return qtrue;
}

int X_GS_GetClientIDByOrigin(vec3_t origin)
{
	for (int i = 0; i < countof(xmod.gs.ps); i++)
	{
		if (!xmod.gs.ps[i].active)
		{
			continue;
		}

		if (VectorEqualInRange(xmod.gs.ps[i].origin, origin, 5.0f))
		{
			return i;
		}
	}

	return -1;
}

qboolean X_GS_IsClientDeadByOrigin(vec3_t origin)
{
	for (int i = 0; i < countof(xmod.gs.ps); i++)
	{
		if (!xmod.gs.ps[i].active)
		{
			continue;
		}

		for (int a = 0; a < xmod.gs.ps[i].dead.count; a++)
		{
			DeadBody *body = xmod.gs.ps[i].dead.entities + a;
			if (body->origin[0] == origin[0]
				&& body->origin[1] == origin[1]
				&& body->origin[2] == origin[2])
			{
				return qtrue;
			}
		}
	}

	return qfalse;
}

qboolean X_GS_IsIntermission(void)
{
	return (xmod.snap.ps.pm_type == PM_INTERMISSION || xmod.snap.ps.pm_type == PM_SPINTERMISSION ? qtrue : qfalse);
}

static void LoadEntitiesToCache(snapshot_t *snapshot)
{
	for (int i = 0; i < snapshot->numEntities; i++)
	{
		int num = snapshot->entities[i].number;
		if (num < 0 || num >= MAX_GENTITIES)
		{
			continue;
		}

		XEntity *entity = xmod.gs.entities + num;
		memset(entity->origin, 0, sizeof(entity->origin));

		entity->type = snapshot->entities[i].eType;
		entity->flags = snapshot->entities[i].eFlags;
		entity->client = snapshot->entities[i].clientNum;
		entity->snap = xmod.snapNum;

		if (entity->client < 0 || entity->client >= MAX_CLIENTS)
		{
			entity->client = 0;
		}
	}
}

static void InterceptEvents(snapshot_t *snapshot)
{
	for (int i = 0; i < snapshot->numEntities; i++)
	{
		entityState_t *entity = snapshot->entities + i;
		int event = 0;

		if (entity->eType > ET_EVENTS)
		{
			event = (entity->eType - ET_EVENTS) & ~EV_EVENT_BITS;
		}

		if (!event)
		{
			continue;
		}

		if (!IsNewEvent(event, entity->eventParm, entity->eFlags, entity->otherEntityNum, entity->otherEntityNum2))
		{
			continue;
		}

		if (x_gs_show_events->integer)
		{
			Com_Printf("^eENTITY EVENT: ^7event:%d, flags:%x, param:%d, source:%d, entity1:%d, entity2:%d\n",
					   event, entity->eFlags, entity->eventParm, i, entity->otherEntityNum, entity->otherEntityNum2);
		}

		if (event == EV_OBITUARY)
		{
			X_Main_OnDeathSound(entity->otherEntityNum, entity->otherEntityNum2);
		}
	}

	if (x_gs_show_events->integer && xmod.gs.psevseq != snapshot->ps.eventSequence)
	{
		xmod.gs.psevseq = snapshot->ps.eventSequence;
		for (int i = 0; i < MAX_PS_EVENTS; i++)
		{
			if (!snapshot->ps.events[i])
			{
				continue;
			}

			Com_Printf("^gPLAYER EVENT: ^7event:%d, flags:%x, param:%d\n", snapshot->ps.events[i], snapshot->ps.eFlags, snapshot->ps.eventParms[i]);
		}
	}

	RemoveExpiredEventsFromCache();
}

XEntity *X_GS_GetEntityFromCache(int num)
{
	if (num < 0 || num >= MAX_GENTITIES)
	{
		return 0;
	}

	XEntity *entity = xmod.gs.entities + num;
	if (entity->snap < xmod.snapNum - 2)
	{
		return 0;
	}

	return entity;
}

XEntity *X_GS_GetPlayerEntityFromCacheByOrigin(vec3_t origin)
{
	for (int i = 0; i < MAX_GENTITIES; i++)
	{
		XEntity *entity = xmod.gs.entities + i;

		if (entity->type != ET_PLAYER)
		{
			continue;
		}

		if (entity->snap < xmod.snapNum - 2)
		{
			continue;
		}

		if (!VectorCompare(origin, entity->origin))
		{
			continue;
		}

		return entity;
	}
	return 0;
}

static void UpdateEntityPositionInCache(int entity, const vec3_t origin)
{
	if (entity < 0 || entity >= MAX_GENTITIES)
	{
		return;
	}

	VectorCopy(origin, (xmod.gs.entities + entity)->origin);
}

XPlayerState *X_GS_GetStateByClientId(int client)
{
	if (client < 0 || client >= MAX_CLIENTS)
	{
		Com_Error(ERR_FATAL, "XQ3E: client ID overflow");
	}

	return xmod.gs.ps + client;
}

static qboolean IsNewEvent(int event, int param, int flags, int client1, int client2)
{
	XEventCache *cache = &xmod.gs.events;

	if (cache->count >= countof(cache->active))
	{
		Com_Error(ERR_FATAL, "XQ3E: event cache overflow");
	}

	for (int i = 0; i < cache->count; i++)
	{
		XEventCacheEntry *entry = cache->entry + *(cache->active + i);
		if (entry->event == event && entry->flags == flags && entry->param == param
			&& entry->client1 == client1 && entry->client2 == client2)
		{
			entry->snap = xmod.snapNum;
			return qfalse;
		}
	}

	int inx = cache->count++;
	XEventCacheEntry *entry = cache->entry + *(cache->active + inx);
	entry->event = event;
	entry->param = param;
	entry->flags = flags;
	entry->client1 = client1;
	entry->client2 = client2;
	entry->snap = xmod.snapNum;

	return qtrue;
}

static void RemoveExpiredEventsFromCache(void)
{
	XEventCache *cache = &xmod.gs.events;
	int count = cache->count;

	for (int i = 0; i < count; i++)
	{
		XEventCacheEntry *entry = cache->entry + *(cache->active + i);

		if (entry->snap == xmod.snapNum)
		{
			continue;
		}

		int last = --count;

		if (i == count)
		{
			break;
		}

		unsigned char old = cache->active[i];
		cache->active[i] = cache->active[last];
		cache->active[last] = old;
	}

	cache->count = count;
}
