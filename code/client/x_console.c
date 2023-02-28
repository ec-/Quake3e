#include "client.h"
#include "x_local.h"

// ====================
//   CVars

static char X_HELP_CON_CHAT_ANTISPAM_ALL[] = "\n ^fx_con_chat_antispam_all^5 0|1^7\n\n"
										 "   Turn on\\off antispam filter for common chat\n";
static char X_HELP_CON_CHAT_ANTISPAM_TEAM[] = "\n ^fx_con_chat_antispam_all^5 0|1^7\n\n"
											 "   Turn on\\off antispam filter for team chat\n";
static char X_HELP_CON_CHAT_ANTISPAM_PRIVATE[] = "\n ^fx_con_chat_antispam_all^5 0|1^7\n\n"
											 "   Turn on\\off antispam filter for private chat\n";

cvar_t *x_con_chat_section = 0;
cvar_t *x_con_overlay_size = 0;

// ====================
//   Const vars

// ====================
//   Static routines

typedef enum
{
	ScopePublic,
	ScopePublicEncrypted,
	ScopeTeam,
	ScopePrivate,
} MessageScope;

static MessageScope GetScopeAndNormalizeName(char *name);
static void RemoveEffectsFromName(char *name);

#define X_CON_FILTER_TIMEOUT_MS  5000ul

typedef struct
{
	cvar_t *filter_variable;
	int message_time;
	size_t spam_counter;
	char player_name[X_CON_MAX_NAME_LENGTH];
	char player_msg[MAXPRINTMSG];
} SpamFilter;

typedef enum
{
	SPAM_FILTER_TYPE_ALL = 0,
	SPAM_FILTER_TYPE_TEAM,
	SPAM_FILTER_TYPE_PRIVATE,
} SpamFilterTypes;

// ====================
//   Implementation

typedef struct
{
	SpamFilter filters[3];
} XConsolePrivateContext;

static XConsolePrivateContext x_con_context =  {};

void X_Con_Init(void)
{
	X_Main_RegisterXCommand(x_con_chat_section, "1", "0", "1", 0);
	Cvar_Get("x_con_chat_section", "1", CVAR_LATCH);
	X_Main_RegisterXCommand(x_con_overlay_size, "10", "0", "20", 0);

	x_con_context.filters[SPAM_FILTER_TYPE_ALL].filter_variable = X_Main_RegisterXModeCmd("x_con_chat_antispam_all", "0", "0", "1", X_HELP_CON_CHAT_ANTISPAM_ALL,CVAR_ARCHIVE,CV_INTEGER);
	x_con_context.filters[SPAM_FILTER_TYPE_TEAM].filter_variable = X_Main_RegisterXModeCmd("x_con_chat_antispam_team", "0", "0", "1", X_HELP_CON_CHAT_ANTISPAM_TEAM,CVAR_ARCHIVE,CV_INTEGER);
	x_con_context.filters[SPAM_FILTER_TYPE_PRIVATE].filter_variable = X_Main_RegisterXModeCmd("x_con_chat_antispam_private", "0", "0", "1", X_HELP_CON_CHAT_ANTISPAM_PRIVATE,CVAR_ARCHIVE,CV_INTEGER);
}

void X_Con_PrintToChatSection(const char *fmt, ...)
{
	va_list argptr;
	static char msg[MAXPRINTMSG];

	va_start(argptr, fmt);
	(void) Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	qtime_t time;
	Com_RealTime(&time);
	char timestr[64];
	Com_sprintf(timestr, sizeof(timestr), "%02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
	X_Misc_MakeStringSymbolic(timestr);

	Com_Printf_Chat("^f%s ^l%s\n", timestr, msg);
}

static void process_filter_message_is_not_spam(SpamFilter *filter, const char *name, const char *msg, const int current_time)
{
	if(filter->spam_counter)
	{
		Com_Printf_Chat("Last message repeated %zu times.\n", filter->spam_counter);
	}
	filter->spam_counter = 0;
	Q_strncpyz(filter->player_name, name, X_CON_MAX_NAME_LENGTH);
	Q_strncpyz(filter->player_msg, msg, MAXPRINTMSG);
	filter->message_time = current_time;
}

static qboolean process_filter(SpamFilter *filter, const char *name, const char *msg)
{
	if (filter->filter_variable->integer == 0)
	{
		/* filter is disabled */
		return qfalse;
	}
	const int current_time = Sys_Milliseconds();

	if (filter->message_time == 0)
	{
		filter->message_time = current_time;
	}

	if (filter->spam_counter && (current_time - filter->message_time) > X_CON_FILTER_TIMEOUT_MS)
	{
		/* There was spam, but now it is not. */
		process_filter_message_is_not_spam(filter, name, msg, current_time);
		return qfalse;
	}

	if (strncmp(filter->player_name, name, X_CON_MAX_NAME_LENGTH) != 0)
	{
		process_filter_message_is_not_spam(filter, name, msg, current_time);
		return qfalse;
	}

	if (strncmp(filter->player_msg, msg, X_CON_MAX_NAME_LENGTH) != 0)
	{
		process_filter_message_is_not_spam(filter, name, msg, current_time);
		return qfalse;
	}

	++filter->spam_counter;
	return qtrue;
}

qboolean X_Con_OnChatMessage(const char *text, int client)
{
	char msgcolor = '2';
	char *msg = strstr(text, "\x19:");
	if (!msg)
	{
		return qtrue;
	}

	size_t namelen = msg - text;

	if (strlen(msg) <= 2)
	{
		return qtrue;
	}

	msg += 2;

	if (namelen > MAX_NAME_LEN)
	{
		return qtrue;
	}

	// Prepare name

	char name[MAX_NAME_LEN + 1];
	Q_strncpyz(name, text, (int)namelen + 1);

	MessageScope scope = GetScopeAndNormalizeName(name);
	RemoveEffectsFromName(name);

	if (scope == ScopePublic)
	{
		if( X_Misc_DecryptMessage(msg) )
		{
			scope = ScopePublicEncrypted;
			msgcolor = 'd';
		}
	}

	char clientid[32] = "";
	if (client >= 0)
	{
		Com_sprintf(clientid, sizeof(clientid), "^z[^7%d^z]", client);
	}

	// Make time tag

	qtime_t time;
	Com_RealTime(&time);

	char timestr[64];
	Com_sprintf(timestr, sizeof(timestr), "%02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
	X_Misc_MakeStringSymbolic(timestr);

	// Make scope tag

	char scopestr[64];
	qboolean is_spam;
	switch (scope)
	{
		case ScopePublic:
			is_spam = process_filter(&x_con_context.filters[SPAM_FILTER_TYPE_ALL], name, msg);
			Q_strncpyz(scopestr, "^9ALL  ", sizeof(scopestr));
			break;
		case ScopePublicEncrypted:
			is_spam = process_filter(&x_con_context.filters[SPAM_FILTER_TYPE_ALL], name, msg);
			Q_strncpyz(scopestr, "^8ENCR ", sizeof(scopestr));
			break;
		case ScopeTeam:
			is_spam = process_filter(&x_con_context.filters[SPAM_FILTER_TYPE_TEAM], name, msg);
			Q_strncpyz(scopestr, "^5TEAM ", sizeof(scopestr));
			break;
		case ScopePrivate:
			is_spam = process_filter(&x_con_context.filters[SPAM_FILTER_TYPE_PRIVATE], name, msg);
			Q_strncpyz(scopestr, "^6PRVT ", sizeof(scopestr));
			break;
	}

	if (is_spam)
	{
		return qfalse;
	}

	X_Misc_MakeStringSymbolic(scopestr);

	// Print to chat section

	Com_Printf_Chat("^f%s %s^7%s%s^z:^%c%s\n", timestr, scopestr, name, clientid, msgcolor, msg);

	if (scope == ScopePublicEncrypted)
	{
		char prefix[] = "encrypted";
		X_Misc_MakeStringSymbolic(prefix);
		Com_Printf("^c%s ^7%s :^%c%s\n", prefix, name, msgcolor, msg);
		return qfalse;
	}

	return qtrue;
}

static MessageScope GetScopeAndNormalizeName(char *name)
{
	char *meta;

	meta = strstr(name, "\x19[");
	if (meta)
	{
		memmove(meta, meta + 2, strlen(meta + 2) + 1);
		meta = strstr(name, "\x19]");
		if (meta)
		{
			*meta = '\0';
		}
		return ScopePrivate;
	}

	meta = strstr(name, "\x19(");
	if (meta)
	{
		memmove(meta, meta + 2, strlen(meta + 2) + 1);
		meta = strstr(name, "\x19)");
		if (meta)
		{
			*meta = '\0';
		}
		return ScopeTeam;
	}

	return ScopePublic;
}

static qboolean IsHexRGBString(const char *str)
{
	for (int i = 0; i < 6; i++)
	{
		char chr = str[++i];

		if (chr >= '0' && chr <= '9')
		{
			continue;
		}

		if (chr >= 'a' && chr <= 'f')
		{
			continue;
		}

		if (chr >= 'A' && chr <= 'F')
		{
			continue;
		}

		return qfalse;
	}

	return qtrue;
}

static void RemoveEffectsFromName(char *name)
{
	int i, a;

	if (xmod.gs.mode != ModeOSP)
	{
		return;
	}

	for (i = 0, a = 0; name[i]; i++)
	{
		if (name[i] != '^')
		{
			name[a++] = name[i];
			continue;
		}

		char chr = name[++i];
		if (!chr)
		{
			break;
		}

		if (chr == 'b' || chr == 'B')
		{
			continue;
		}

		if (chr == 'f' || chr == 'F')
		{
			continue;
		}

		if (chr == 'n' || chr == 'N')
		{
			continue;
		}

		if ((chr == 'x' || chr == 'X') && IsHexRGBString(name + i + 1))
		{
			i += 6;
			continue;
		}

		name[a++] = name[i - 1];
		name[a++] = name[i];
	}

	name[a] = '\0';
}

void X_Con_OnPlayerDeath(int target, int attacker, int reason)
{
/*
	if (target >= MAX_CLIENTS || attacker >= MAX_CLIENTS)
	{
		return;
	}

	switch (reason)
	{
		case MOD_SUICIDE:
			Com_Printf_Chat("^7%s ^fsuicided\n", xmod.gs.ps[target].name);
			return;
		case MOD_FALLING:
			Com_Printf_Chat("^7%s ^fcratered\n", xmod.gs.ps[target].name);
			return;
		case MOD_CRUSH:
			Com_Printf_Chat("^7%s ^fwas squished\n", xmod.gs.ps[target].name);
			return;
		case MOD_WATER:
			Com_Printf_Chat("^7%s ^fsank\n", xmod.gs.ps[target].name);
			return;
		case MOD_SLIME:
			Com_Printf_Chat("^7%s ^fmelted\n", xmod.gs.ps[target].name);
			return;
		case MOD_LAVA:
			Com_Printf_Chat("^7%s ^fburned\n", xmod.gs.ps[target].name);
			return;
		case MOD_TARGET_LASER:
			Com_Printf_Chat("^7%s ^flasered\n", xmod.gs.ps[target].name);
			return;
		case MOD_TRIGGER_HURT:
			Com_Printf_Chat("^7%s ^ftriggered\n", xmod.gs.ps[target].name);
			return;
		default:
			break;
	}

	if (attacker == target)
	{
		switch (reason)
		{
			case MOD_GRENADE_SPLASH:
				Com_Printf_Chat("^7%s ^fripped on his own grenade\n", xmod.gs.ps[target].name);
				break;
			case MOD_ROCKET_SPLASH:
				Com_Printf_Chat("^7%s ^fblew himself up\n", xmod.gs.ps[target].name);
				break;
			case MOD_PLASMA_SPLASH:
				Com_Printf_Chat("^7%s ^fmelted himself\n", xmod.gs.ps[target].name);
				break;
			case MOD_BFG_SPLASH:
				Com_Printf_Chat("^7%s ^fbfgered\n", xmod.gs.ps[target].name);
				break;
			default:
				Com_Printf_Chat("^7%s ^fkilled himself\n", xmod.gs.ps[target].name);
				break;
		}
		return;
	}

	if (attacker != ENTITYNUM_WORLD)
	{
		char kill;
		switch (reason)
		{
			case MOD_GRAPPLE:
				kill = '\xF0';
				break;
			case MOD_GAUNTLET:
				kill = '\xF1';
				break;
			case MOD_MACHINEGUN:
				kill = '\xF2';
				break;
			case MOD_SHOTGUN:
				kill = '\xF3';
				break;
			case MOD_GRENADE:
				kill = '\xF4';
				break;
			case MOD_GRENADE_SPLASH:
				kill = '\xF5';
				break;
			case MOD_ROCKET:
				kill = '\xF6';
				break;
			case MOD_ROCKET_SPLASH:
				kill = '\xF7';
				break;
			case MOD_PLASMA:
				kill = '\xF8';
				break;
			case MOD_PLASMA_SPLASH:
				kill = '\xF9';
				break;
			case MOD_RAILGUN:
				kill = '\xFA';
				break;
			case MOD_LIGHTNING:
				kill = '\xFB';
				break;
			case MOD_BFG:
				kill = '\xFC';
				break;
			case MOD_BFG_SPLASH:
				kill = '\xFD';
				break;
			case MOD_TELEFRAG:
				kill = '\xFE';
				break;
			default:
				kill = '\xFF';
				break;
		}

		char buffer[128];
		Com_sprintf(buffer, sizeof(buffer), "^7%s ^f%c ^7%s\n", xmod.gs.ps[attacker].name, kill, xmod.gs.ps[target].name);
		X_Cl_Con_OverlayPrint(buffer);
	}
 */
}
