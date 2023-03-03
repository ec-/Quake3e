#include "client.h"
#include "x_local.h"

// ====================
//   CVars

static cvar_t *x_hud_fix_font = 0;
static cvar_t *x_hud_xscore_by_default = 0;
static cvar_t *x_hud_xscore_opaque = 0;

// ====================
//   Const vars

#define xmax(X, Y) (((X) > (Y)) ? (X) : (Y))

#define XSCORE_UPDATE_INTERVAL 3000

// ====================
//   Static routines

typedef struct
{
	float scale;
	float x;
	float y;
	float w;
	float h;
} Bar;

typedef struct
{
	int count;
	unsigned char clients[MAX_CLIENTS];
} PlayersList;

typedef struct
{
	PlayersList ingame;
	PlayersList spect;
} FFAPlayersList;

typedef struct
{
	PlayersList ingame;
	PlayersList ingame2;
	PlayersList spect;
} SeparatedPlayersList;

typedef struct
{
	PlayersList red;
	PlayersList blue;
	PlayersList spect;
} TDMPlayersList;

typedef enum
{
	StringWithShadow = 1,
	TallString = 2,
	StringInCenter = 4,
	StringRightAligned = 8
} XStringFlags;

#define prv_DrawStr(xx, yy, sz, alp, fl, chrmap, str) \
            {\
                vec4_t prv_color; \
                MAKERGBA(prv_color, 1.0, 1.0, 1.0, alp); \
                X_Hud_DrawString(xx, yy, sz, prv_color, fl, chrmap, str); \
            }

#define DrawBarStr(bar, xx, yy, sz, alp, str) \
             prv_DrawStr((bar)->x + ((bar)->scale * (xx)), (bar)->y + ((bar)->scale * (yy)), (bar)->scale * (sz), alp, StringWithShadow, xmod.rs.shaderXCharmap, str)

#define DrawBarStrInCenter(bar, xx, yy, sz, alp, str) \
            prv_DrawStr((bar)->x + ((bar)->scale * (xx)), (bar)->y + ((bar)->scale * (yy)), (bar)->scale * (sz), alp, StringWithShadow|StringInCenter, xmod.rs.shaderXCharmap, str)

#define DrawBarStrRightAligned(bar, xx, yy, sz, alp, str) \
            prv_DrawStr((bar)->x + ((bar)->scale * (xx)), (bar)->y + ((bar)->scale * (yy)), (bar)->scale * (sz), alp, StringWithShadow|StringRightAligned, xmod.rs.shaderXCharmap, str)

#define DrawFatBarStr(bar, xx, yy, sz, alp, str) \
            prv_DrawStr((bar)->x + ((bar)->scale * (xx)), (bar)->y + ((bar)->scale * (yy)), (bar)->scale * (sz), alp, StringWithShadow, xmod.rs.shaderCharmap[2], str)

#define DrawFatBarStrInCenter(bar, xx, yy, sz, alp, str) \
            prv_DrawStr((bar)->x + ((bar)->scale * (xx)), (bar)->y + ((bar)->scale * (yy)), (bar)->scale * (sz), alp, StringWithShadow|StringInCenter, xmod.rs.shaderCharmap[2], str)

static void ShowScoreTable(void);
static void HideScoreTable(void);
static void RedirectScoreCommandToCgame(void);

static void DrawScoreTable(void);
static void CheckAndTurnOfAutoshow(void);

static void DrawTopHud(void);

static void DrawFFAScoreTable(void);
static void Draw1v1ScoreTable(void);
static void DrawTeamScoreTable(void);
static void DrawCTFScoreTable(void);

static void GetSortedFFAPlayersList(FFAPlayersList *players);
static void GetSeparatedFFAPlayersList(FFAPlayersList *players, SeparatedPlayersList *separated);
static void GetSortedTDMPlayersList(TDMPlayersList *players);
static void GetSortedTourneyPlayersList(SeparatedPlayersList *players);

static const char *GetCurrentModeName(void);

static char *GetColoredMS(int ms);

static void CalculateCenterBar(Bar *bar, float w, float h);
static void CalculateChildBar(Bar *parent, Bar *bar, float x, float y, float w, float h);

static void DrawPlayerName(float x, float y, float size, float alpha, int maxlen, char *name);

static void DrawBar(Bar *bar, vec4_t rgba, float border);

static void DrawCharacter(float x, float y, float size, unsigned char ch, qhandle_t charmap);

static void DrawProgressBar(XUIProgressBar *bar, Bar *b, float value);

static void DrawSquareIcon(Bar *bar, float x, float y, float sz, float alpha, qhandle_t shader);

static const char *CovertTimeToString(int time);

static void InitFading(XUIFading *fad, int interval, float min, float max);
static void ResetFading(XUIFading *fad);
static float GetFadingAlpha(XUIFading *fad);


// ======================
//   Score routines

enum ScoreFlags
{
	SFlagOneTeam = 1,
	SFlagDrawScore = 2,
	SFlagVoteBar = 4
};

typedef struct
{
	char title[64];
	char descr[64];
	vec4_t color;
	PlayersList *players;
	int score;
	float (*header)(Bar *);
	float (*draw)(Bar *, int, int, int);
	float (*total)(Bar *);
} TableSection;

static void CreateSingleScoreTable(float width, TableSection *section1, TableSection *section2, float(*total)(Bar *, int team), int flags);
static void CreateDoubleScoreTable(float width, TableSection *section1, TableSection *section2, TableSection *section3, float(*total)(Bar *, int team), int flags);

static float DrawPlayerRowFFA(Bar *bar, int pos, int client, int flags);
static float DrawPlayerRowHeaderFFA(Bar *bar);
static float DrawSpectatorRowHeaderFFA(Bar *bar);
static float DrawPlayerRowTotalFFA(Bar *bar, int team);

static float DrawPlayerRowTDM(Bar *bar, int pos, int client, int flags);
static float DrawPlayerRowCTF(Bar *bar, int pos, int client, int flags);
static float DrawPlayerRowHeaderTDM(Bar *bar);
static float DrawPlayerRowTotalTDM(Bar *bar, int team);

static void DrawTableSectionTitle(Bar *bar, TableSection *sect);
static void DrawTableSectionTitleWithScore(Bar *bar, TableSection *sect, int place);

static char *ConvertPlaceToString(int place);
static void DrawBotLevel(Bar *bar, float x, float y, float skill);

// ====================
//   Implementation

void X_Hud_Init()
{
	Cmd_AddCommand("+xscore", ShowScoreTable);
	Cmd_AddCommand("-xscore", HideScoreTable);

	X_Main_RegisterXCommand(x_hud_fix_font, "1", "0", "1", 0);
	X_Main_RegisterXCommand(x_hud_xscore_by_default, "1", "0", "1", 0);
	X_Main_RegisterFloatXCommand(x_hud_xscore_opaque, "0.9", "0.0", "1.0", 0);

	X_Hud_ValidateDefaultScores();

	InitFading(&xmod.scr.fading, 1000, 0.3f, 0.9f);
}

void X_Hud_Destroy()
{
	Cmd_RemoveCommand("+xscore");
	Cmd_RemoveCommand("-xscore");
	Cmd_ReplaceCommand("+scores", xmod.scr.scoresShow);
	Cmd_ReplaceCommand("-scores", xmod.scr.scoresHide);
}

void X_Hud_ValidateDefaultScores()
{
	if (!X_Main_IsXModeActive())
	{
		Cmd_ReplaceCommand("+scores", xmod.scr.scoresShow);
		Cmd_ReplaceCommand("-scores", xmod.scr.scoresShow);
		return;
	}

	if (xmod.scr.hud != x_hud_xscore_by_default->integer)
	{
		if (x_hud_xscore_by_default->integer)
		{
			xmod.scr.scoresShow = Cmd_ReplaceCommand("+scores", ShowScoreTable);
			xmod.scr.scoresHide = Cmd_ReplaceCommand("-scores", HideScoreTable);
		}
		else
		{
			Cmd_ReplaceCommand("+scores", xmod.scr.scoresShow);
			Cmd_ReplaceCommand("-scores", xmod.scr.scoresShow);
		}

		xmod.scr.hud = x_hud_xscore_by_default->integer;
	}
}

void X_Hud_DrawHud()
{
	DrawScoreTable();
}

qboolean X_Hud_UpdatePlayerStats()
{
	int current = Sys_Milliseconds();

	if (!xmod.scr.show)
	{
		return (current >= xmod.scr.lastUpd ? qfalse : qtrue);
	}

	if (xmod.gs.mode == ModeOSP)
	{
		if (current >= xmod.scr.lastUpd)
		{
			CL_AddReliableCommand("score", qfalse);
			CL_AddReliableCommand("statsall", qfalse);
			xmod.scr.lastUpd = Sys_Milliseconds() + XSCORE_UPDATE_INTERVAL;
		}

		return qtrue;
	}

	return qfalse;
}

void X_Hud_HackTurnOffDefaultScores(snapshot_t *snapshot)
{
	if (!x_hud_xscore_by_default->integer)
	{
		return;
	}

	if (snapshot->ps.pm_type != PM_DEAD && snapshot->ps.pm_type != PM_INTERMISSION)
	{
		return;
	}

	ResetFading(&xmod.scr.fading);

	xmod.scr.show = qtrue;
	xmod.scr.autoshow = qtrue;
}

qhandle_t X_Hud_GetConsoleFontShader(void)
{
	if (!X_Main_IsXModeActive())
	{
		return cls.charSetShader;
	}

	if (x_hud_fix_font->integer)
	{
		return xmod.rs.shaderCharmap[0];
	}

	return cls.charSetShader;
}

qhandle_t X_Hud_GetOverlayFontShader(void)
{
	if (!X_Main_IsXModeActive())
	{
		return cls.charSetShader;
	}

	return xmod.rs.shaderXOverlayChars;
}

void X_Hud_CustomizeConsoleFont(qhandle_t *shader)
{
	if (!x_hud_fix_font->integer)
	{
		return;
	}

	if (*shader != cls.charSetShader)
	{
		return;
	}

	if (!xmod.rs.shaderCharmap[2])
	{
		return;
	}

	*shader = xmod.rs.shaderCharmap[2];
}

qboolean X_Hud_HideOnXScore(void)
{
	if (!xmod.scr.show)
	{
		return qfalse;
	}

	if (xmod.scr.opaque)
	{
		return qfalse;
	}

	return qtrue;
}

void X_Hud_TurnOffForcedTransparency(void)
{
	xmod.scr.opaque = qtrue;
}

void X_Hud_TurnOnForcedTransparency(void)
{
	xmod.scr.opaque = qfalse;
}

// ======================
//  Score table

static void ShowScoreTable(void)
{
	if (!X_Main_IsXModeActive())
	{
		return;
	}

	if (xmod.scr.show)
	{
		return;
	}

	X_Hud_UpdatePlayerStats();
	ResetFading(&xmod.scr.fading);

	xmod.scr.show = qtrue;

	RedirectScoreCommandToCgame();
}

static void HideScoreTable(void)
{
	if (!X_Main_IsXModeActive())
	{
		return;
	}

	if (!xmod.scr.show)
	{
		return;
	}

	xmod.scr.show = qfalse;

	RedirectScoreCommandToCgame();
}

static void RedirectScoreCommandToCgame(void)
{
	if (com_cl_running && com_cl_running->integer)
	{
		CL_GameCommand();
	}
}

static void DrawScoreTable(void)
{
	CheckAndTurnOfAutoshow();

	if (!xmod.scr.show)
	{
		return;
	}

	X_Hud_UpdatePlayerStats();

	xmod.scr.warmup = (atoi(X_Misc_GetConfigString(CS_WARMUP)) != 0 ? qtrue : qfalse);

	DrawTopHud();

	if (xmod.gs.type == GameFFA)
	{
		DrawFFAScoreTable();
	}
	else if (xmod.gs.type == Game1v1)
	{
		Draw1v1ScoreTable();
	}
	else if (xmod.gs.type == GameTDM || xmod.gs.type == GameCA)
	{
		DrawTeamScoreTable();
	}
	else if (xmod.gs.type == GameCTF)
	{
		DrawCTFScoreTable();
	}
}

static void CheckAndTurnOfAutoshow(void)
{
	if (!xmod.scr.autoshow)
	{
		return;
	}

	if (!x_hud_xscore_by_default->integer
		|| (xmod.snap.ps.pm_type != PM_DEAD
			&& xmod.snap.ps.pm_type != PM_INTERMISSION))
	{
		xmod.scr.show = qfalse;
		xmod.scr.autoshow = qfalse;
	}
}

static void DrawTopHud(void)
{
	char text[64];
	Bar bar;
	qtime_t qtime;

	CalculateCenterBar(&bar, 640.0f, 480.0f);

	Com_RealTime(&qtime);
	Com_sprintf(text, sizeof(text), "^7CURRENT TIME^z:^d%02d:%02d:%02d", qtime.tm_hour, qtime.tm_min, qtime.tm_sec);
	DrawBarStrRightAligned(&bar, 635.f, 5.f, 7.5f, 0.8f, text)

	Com_sprintf(text, sizeof(text), "^7PLAYING^z:^d%s", CovertTimeToString(Sys_Milliseconds()));
	DrawBarStrRightAligned(&bar, 635.f, 15.f, 7.5f, 0.8f, text)
}

static int GetVoteSFlag(void)
{
	int votestart = atoi(X_Misc_GetConfigString(CS_VOTE_TIME));
	return (votestart && cl.serverTime <= votestart + VOTE_TIME ? SFlagVoteBar : 0);
}

static void DrawFFAScoreTable(void)
{
	char text[64];

	FFAPlayersList ffa;
	GetSortedFFAPlayersList(&ffa);

	float (*total)(Bar *, int) = (ffa.ingame.count > 1 ? DrawPlayerRowTotalFFA : 0);

	if (ffa.ingame.count > 16 || ffa.spect.count > 16 || ffa.ingame.count + ffa.spect.count > 18)
	{
		SeparatedPlayersList sep;
		GetSeparatedFFAPlayersList(&ffa, &sep);

		TableSection ingame;
		Q_strncpyz(ingame.title, "^tPLAYING", sizeof(ingame.title));
		Com_sprintf(text, sizeof(text), "^t%d players", ffa.ingame.count);
		Q_strncpyz(ingame.descr, text, sizeof(ingame.descr));
		MAKERGBA(ingame.color, 0.5f, 0.0f, 0.5f, 0.3f);
		ingame.players = &sep.ingame;
		ingame.header = DrawPlayerRowHeaderFFA;
		ingame.draw = DrawPlayerRowFFA;

		TableSection ingame2;
		*ingame2.title = '\0';
		*ingame2.descr = '\0';
		MAKERGBA(ingame2.color, 0.5f, 0.0f, 0.5f, 0.3f);
		ingame2.players = &sep.ingame2;
		ingame2.header = DrawPlayerRowHeaderFFA;
		ingame2.draw = DrawPlayerRowFFA;

		TableSection spects;
		Q_strncpyz(spects.title, "^7SPECTATORS", sizeof(spects.title));
		Com_sprintf(text, sizeof(text), "^7%d players", ffa.spect.count);
		Q_strncpyz(spects.descr, text, sizeof(spects.descr));
		MAKERGBA(spects.color, 0.8f, 0.8f, 0.8f, 0.3f);
		spects.players = &ffa.spect;
		spects.header = DrawSpectatorRowHeaderFFA;
		spects.draw = DrawPlayerRowFFA;

		CreateDoubleScoreTable(300.f, &ingame, &ingame2, &spects, total, SFlagOneTeam | GetVoteSFlag());
	}
	else
	{
		TableSection ingame;
		Q_strncpyz(ingame.title, "^tPLAYING", sizeof(ingame.title));
		Com_sprintf(text, sizeof(text), "^t%d players", ffa.ingame.count);
		Q_strncpyz(ingame.descr, text, sizeof(ingame.descr));
		MAKERGBA(ingame.color, 0.5f, 0.0f, 0.5f, 0.3f);
		ingame.players = &ffa.ingame;
		ingame.header = DrawPlayerRowHeaderFFA;
		ingame.draw = DrawPlayerRowFFA;

		TableSection spects;
		Q_strncpyz(spects.title, "^7SPECTATORS", sizeof(spects.title));
		Com_sprintf(text, sizeof(text), "^7%d players", ffa.spect.count);
		Q_strncpyz(spects.descr, text, sizeof(spects.descr));
		MAKERGBA(spects.color, 0.8f, 0.8f, 0.8f, 0.3f);
		spects.players = &ffa.spect;
		spects.header = DrawSpectatorRowHeaderFFA;
		spects.draw = DrawPlayerRowFFA;

		CreateSingleScoreTable(300.f, &ingame, &spects, total, SFlagOneTeam | GetVoteSFlag());
	}
}

static void Draw1v1ScoreTable(void)
{
	char text[64];
	SeparatedPlayersList tourney;

	GetSortedTourneyPlayersList(&tourney);

	TableSection ingame;
	*ingame.title = '\0';
	*ingame.descr = '\0';
	MAKERGBA(ingame.color, 0.5f, 0.0f, 0.5f, 0.3f);
	ingame.score = xmod.scr.team_red;
	ingame.players = &tourney.ingame;
	ingame.header = DrawPlayerRowHeaderTDM;
	ingame.draw = DrawPlayerRowTDM;

	TableSection ingame2;
	*ingame2.title = '\0';
	*ingame2.descr = '\0';
	MAKERGBA(ingame2.color, 0.5f, 0.0f, 0.5f, 0.3f);
	ingame2.score = xmod.scr.team_blue;
	ingame2.players = &tourney.ingame2;
	ingame2.header = DrawPlayerRowHeaderTDM;
	ingame2.draw = DrawPlayerRowTDM;

	TableSection spects;
	Q_strncpyz(spects.title, "^7SPECTATORS", sizeof(spects.title));
	Com_sprintf(text, sizeof(text), "^7%d players", tourney.spect.count);
	Q_strncpyz(spects.descr, text, sizeof(spects.descr));
	MAKERGBA(spects.color, 0.8f, 0.8f, 0.8f, 0.3f);
	spects.players = &tourney.spect;
	spects.header = DrawSpectatorRowHeaderFFA;
	spects.draw = DrawPlayerRowFFA;

	CreateDoubleScoreTable(300.f, &ingame, &ingame2, &spects, 0, SFlagDrawScore | GetVoteSFlag());
}

static void DrawTeamScoreTable(void)
{
	char text[64];
	TDMPlayersList tdm;
	GetSortedTDMPlayersList(&tdm);

	float (*total)(Bar *, int) = (tdm.red.count > 1 || tdm.blue.count > 1 ? DrawPlayerRowTotalTDM : 0);

	TableSection red;
	Q_strncpyz(red.title, "^1RED TEAM", sizeof(red.title));
	Com_sprintf(text, sizeof(text), "^1%d players", tdm.red.count);
	Q_strncpyz(red.descr, text, sizeof(red.descr));
	MAKERGBA(red.color, 1.0f, 0.0f, 0.0f, 0.2f);
	red.score = xmod.scr.team_red;
	red.players = &tdm.red;
	red.header = DrawPlayerRowHeaderTDM;
	red.draw = DrawPlayerRowTDM;

	TableSection blue;
	Q_strncpyz(blue.title, "^nBLUE TEAM", sizeof(blue.title));
	Com_sprintf(text, sizeof(text), "^n%d players", tdm.blue.count);
	Q_strncpyz(blue.descr, text, sizeof(blue.descr));
	MAKERGBA(blue.color, 0.0f, 0.0f, 1.0f, 0.25f);
	blue.score = xmod.scr.team_blue;
	blue.players = &tdm.blue;
	blue.header = DrawPlayerRowHeaderTDM;
	blue.draw = DrawPlayerRowTDM;

	TableSection spects;
	Q_strncpyz(spects.title, "^7SPECTATORS", sizeof(spects.title));
	Com_sprintf(text, sizeof(text), "^7%d players", tdm.spect.count);
	Q_strncpyz(spects.descr, text, sizeof(spects.descr));
	MAKERGBA(spects.color, 0.8f, 0.8f, 0.8f, 0.3f);
	spects.players = &tdm.spect;
	spects.header = DrawPlayerRowHeaderTDM;
	spects.draw = DrawPlayerRowTDM;

	CreateDoubleScoreTable(300.f, &red, &blue, &spects, total, SFlagDrawScore | GetVoteSFlag());
}

static void DrawCTFScoreTable()
{
	char text[64];
	TDMPlayersList tdm;
	GetSortedTDMPlayersList(&tdm);

	float (*total)(Bar *, int) = (tdm.red.count > 1 || tdm.blue.count > 1 ? DrawPlayerRowTotalTDM : 0);

	TableSection red;
	Q_strncpyz(red.title, "^1RED TEAM", sizeof(red.title));
	Com_sprintf(text, sizeof(text), "^1%d players", tdm.red.count);
	Q_strncpyz(red.descr, text, sizeof(red.descr));
	MAKERGBA(red.color, 1.0f, 0.0f, 0.0f, 0.2f);
	red.score = xmod.scr.team_red;
	red.players = &tdm.red;
	red.header = DrawPlayerRowHeaderTDM;
	red.draw = DrawPlayerRowCTF;

	TableSection blue;
	Q_strncpyz(blue.title, "^nBLUE TEAM", sizeof(blue.title));
	Com_sprintf(text, sizeof(text), "^n%d players", tdm.blue.count);
	Q_strncpyz(blue.descr, text, sizeof(blue.descr));
	MAKERGBA(blue.color, 0.0f, 0.0f, 1.0f, 0.25f);
	blue.score = xmod.scr.team_blue;
	blue.players = &tdm.blue;
	blue.header = DrawPlayerRowHeaderTDM;
	blue.draw = DrawPlayerRowCTF;

	TableSection spects;
	Q_strncpyz(spects.title, "^7SPECTATORS", sizeof(spects.title));
	Com_sprintf(text, sizeof(text), "^7%d players", tdm.spect.count);
	Q_strncpyz(spects.descr, text, sizeof(spects.descr));
	MAKERGBA(spects.color, 0.8f, 0.8f, 0.8f, 0.3f);
	spects.players = &tdm.spect;
	spects.header = DrawPlayerRowHeaderTDM;
	spects.draw = DrawPlayerRowCTF;

	CreateDoubleScoreTable(300.f, &red, &blue, &spects, total, SFlagDrawScore | GetVoteSFlag());
}

static void CalculateCenterBar(Bar *bar, float w, float h)
{
	float width = 640.0f;
	float height = 480.0f;

	float xscale = cls.glconfig.vidWidth / width;
	float yscale = cls.glconfig.vidHeight / height;

	if (w > width)
	{
		yscale = cls.glconfig.vidWidth / w;
		width = w;
	}

	if (h > height)
	{
		yscale = cls.glconfig.vidHeight / h;
		height = h;
	}

	bar->y = (height - h) / 2;
	bar->h = height - (bar->y * 2.0);

	bar->y *= yscale;
	bar->h *= yscale;

	bar->x = ((width / 2) * xscale) - ((w / 2.0) * yscale);
	bar->w = w * yscale;

	bar->scale = yscale;
}

static void CalculateChildBar(Bar *parent, Bar *bar, float x, float y, float w, float h)
{
	bar->scale = parent->scale;
	bar->x = parent->x + (parent->scale * x);
	bar->y = parent->y + (parent->scale * y);
	bar->w = w * parent->scale;
	bar->h = h * parent->scale;
}

static void DrawBar(Bar *bar, vec4_t rgba, float border)
{
	re.SetColor(rgba);

	re.DrawStretchPic(bar->x, bar->y, bar->w, bar->h, 0, 0, 0, 0, cls.whiteShader);

	if (border)
	{
		re.DrawStretchPic(
		bar->x,
		bar->y,
		border,
		bar->h,
		0, 0, 0, 0, cls.whiteShader
		);
		re.DrawStretchPic(
		bar->x + border,
		bar->y,
		bar->w - border,
		border,
		0, 0, 0, 0, cls.whiteShader
		);
		re.DrawStretchPic(
		bar->x + bar->w - border,
		bar->y + border,
		border,
		bar->h - border,
		0, 0, 0, 0, cls.whiteShader
		);
		re.DrawStretchPic(
		bar->x + border,
		bar->y + bar->h - border,
		bar->w - (border * 2),
		border,
		0, 0, 0, 0, cls.whiteShader
		);
	}

	re.SetColor(0);
}

static char *GetColoredMS(int ms)
{
	static char buffer[64];
	char color = '7';

	if (ms < 20)
	{
		color = '7';
	}
	else if (ms < 40)
	{
		color = '2';
	}
	else if (ms < 60)
	{
		color = '3';
	}
	else if (ms < 80)
	{
		color = '8';
	}
	else
	{
		color = '1';
	}

	Com_sprintf(buffer, sizeof(buffer), "^%c%d^zms", color, ms);

	return buffer;
}

static void DrawPlayerName(float x, float y, float size, float alpha, int maxlen, char *name)
{
	int len = 0;
	char buffer[MAX_NAME_LEN + 1];
	vec4_t color;

	MAKERGBA(color, 1.0, 1.0, 1.0, alpha);

	for (int i = 0, l = strlen(name); i < l; i++)
	{
		if (name[i] == '^')
		{
			i++;
			continue;
		}
		len++;
	}

	Q_strncpyz(buffer, name, sizeof(buffer));


	if (maxlen > 2 && len > maxlen)
	{
		int inx = maxlen - 1;
		buffer[inx] = '\0';
		X_Hud_DrawString(x + (size * inx), y + size / 2.f, size / 2.f, color, StringWithShadow, xmod.rs.shaderCharmap[2], "..");
	}

	X_Hud_DrawString(x, y, size, color, StringWithShadow, xmod.rs.shaderCharmap[2], buffer);
}

static const char *GetCurrentModeName()
{
	static char buffer[64];
	const char *mode = "UNKNWON MODE";

	if (xmod.gs.type == GameFFA)
	{
		mode = "FREE FOR ALL";
	}
	else if (xmod.gs.type == Game1v1)
	{
		mode = "TOURNEY";
	}
	else if (xmod.gs.type == GameCA)
	{
		mode = "CLAN ARENA";
	}
	else if (xmod.gs.type == GameTDM && xmod.gs.freezetag)
	{
		mode = "FREEZE TAG";
	}
	else if (xmod.gs.type == GameTDM)
	{
		mode = "TEAM DEATHMATCH";
	}
	else if (xmod.gs.type == GameCTF)
	{
		mode = "CAPTURE THE FLAG";
	}

	Com_sprintf(buffer, sizeof(buffer), "^8%s%s", (xmod.gs.promode ? "CPM " : ""), mode);
	return buffer;
}

#if 0
static const char* GetGameInfo(void)
{
	static char buffer[256];
	Com_sprintf(buffer, sizeof(buffer), "%s%s", (xmod.gs.promode ? "^fPROMODE" : "^fVQ3"), (xmod.gs.instagib ? " ^2INSTAGIB" : ""));
	return buffer;
}
#endif

static const char *CovertTimerToString(int start, int current)
{
	static char buffer[256];

	if (!start || start < 0)
	{
		return "--:--";
	}

	if (!current || current < 0)
	{
		return "--:--";
	}

	int timer = (current - start) / 1000;
	if (timer < 0)
	{
		timer *= -1;
	}

	int mins = timer / 60;
	int secs = timer % 60;

	if (mins >= 60)
	{
		int hours = mins / 60;
		mins = mins % 60;
		Com_sprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, mins, secs);
	}
	else
	{
		Com_sprintf(buffer, sizeof(buffer), "%d:%02d", mins, secs);
	}

	return buffer;
}

static const char *CovertTimeToString(int time)
{
	static char buffer[256];

	time /= 1000;

	int mins = time / 60;
	int secs = time % 60;
	int hours = mins / 60;
	mins = mins % 60;

	Com_sprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, mins, secs);

	return buffer;
}

static const char *CovertMinsToString(int mins)
{
	static char buffer[256];

	if (mins >= 60)
	{
		int hours = mins / 60;
		mins = mins % 60;
		Com_sprintf(buffer, sizeof(buffer), "%d:%02d:00", hours, mins);
	}
	else
	{
		Com_sprintf(buffer, sizeof(buffer), "%d:00", mins);
	}

	return buffer;
}

static char *GetGameTimer(void)
{
	static char buffer[256];

	if (xmod.gs.timer.type == GTimerRoundTime)
	{
		if (xmod.gs.timelimit)
		{
			Com_sprintf(buffer, sizeof(buffer), "^9ROUND ^7%s^f\xaf^z%s%s",
						CovertTimerToString(xmod.gs.timer.start, xmod.gs.timer.current),
						CovertMinsToString(xmod.gs.timelimit + xmod.gs.overtime),
						xmod.gs.overtime ? " ^7OVERTIME" : "");
		}
		else
		{
			Com_sprintf(buffer, sizeof(buffer), "^9ROUND ^7%s", CovertTimerToString(xmod.gs.timer.start, xmod.gs.timer.current));
		}
	}
	else if (xmod.gs.timer.type == GTimerWarmup)
	{
		if (xmod.gs.timer.warmup == -1)
		{
			Com_sprintf(buffer, sizeof(buffer), "^1WARMUP ^7--:--");
		}
		else
		{
			Com_sprintf(buffer, sizeof(buffer), "^1WARMUP ^7%s", CovertTimerToString(xmod.gs.timer.current, xmod.gs.timer.warmup));
		}
	}
	else if (xmod.gs.timer.type == GTimerTimeout)
	{
		Com_sprintf(buffer, sizeof(buffer), "^3TIMEOUT ^7%s", CovertTimerToString(xmod.gs.timer.current, xmod.gs.timer.start));
	}
	else
	{
		return "^7--:--";
	}

	return buffer;
}

static const char *GetCurrentModeLimits(void)
{
	static char buffer[256];
	static char round_limit[64], time_limit[64], frag_limit[64], capture_limit[64];

	round_limit[0] = frag_limit[0] = time_limit[0] = capture_limit[0] = '\0';

	if (xmod.gs.roundlimit > 0)
	{
		Com_sprintf(round_limit, sizeof(round_limit), "^7ROUNDS:^2%d ", xmod.gs.roundlimit);
	}

	if (xmod.gs.fraglimit > 0)
	{
		Com_sprintf(frag_limit, sizeof(frag_limit), "^7SCORE:^2%d ", xmod.gs.fraglimit);
	}

	if (xmod.gs.timelimit > 0)
	{
		Com_sprintf(time_limit, sizeof(time_limit), "^7TIME:^2%d^zmin ", xmod.gs.timelimit);
	}

	if (xmod.gs.capturelimit > 0)
	{
		Com_sprintf(capture_limit, sizeof(capture_limit), "^7FLAGS:^2%d ", xmod.gs.capturelimit);
	}

	Com_sprintf(buffer, sizeof(buffer), "%s%s%s%s", round_limit, frag_limit, capture_limit, time_limit);

	return buffer;
}

static void SortPlayersList(PlayersList *list)
{
	int rating[MAX_CLIENTS];

	if (list->count < 2)
	{
		return;
	}

	for (int i = 0; i < list->count; i++)
	{
		int client = list->clients[i];
		XPlayerState *ps = xmod.gs.ps + list->clients[i];

		if (ps->score.active)
		{
			rating[client] = ps->score.score * 1000;
			continue;
		}

		if (ps->stats.active)
		{
			float total = ps->stats.givenDmg + ps->stats.receivedDmg;
			float percent = total / 100;
			rating[client] = (ps->stats.givenDmg / percent) + 1;
			continue;
		}

		rating[client] = 0;
	}

	for (int i = 0; i < list->count - 1; i++)
	{
		for (int a = 1; a < list->count; a++)
		{
			int score1 = rating[list->clients[a - 1]];
			int score2 = rating[list->clients[a]];

			if (score1 < score2)
			{
				int inx = list->clients[a];
				list->clients[a] = list->clients[a - 1];
				list->clients[a - 1] = inx;
			}
		}
	}
}

static void GetSortedFFAPlayersList(FFAPlayersList *players)
{
	players->ingame.count = 0;
	players->spect.count = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!xmod.gs.ps[i].active)
		{
			continue;
		}

		if (xmod.gs.ps[i].team == TEAM_FREE)
		{
			players->ingame.clients[players->ingame.count++] = i;
		}
		else
		{
			players->spect.clients[players->spect.count++] = i;
		}
	}

	SortPlayersList(&players->ingame);
	SortPlayersList(&players->spect);
}

static void GetSeparatedFFAPlayersList(FFAPlayersList *players, SeparatedPlayersList *separated)
{
	int start = (players->ingame.count / 2) + (players->ingame.count % 2);

	for (int i = 0, a = 0; i < players->ingame.count; i++)
	{
		if (i >= start)
		{
			separated->ingame2.clients[a++] = players->ingame.clients[i];
		}
		else
		{
			separated->ingame.clients[i] = players->ingame.clients[i];
		}
	}

	separated->ingame.count = start;
	separated->ingame2.count = players->ingame.count - start;
	separated->spect = players->spect;
}

static void GetSortedTDMPlayersList(TDMPlayersList *players)
{
	players->red.count = 0;
	players->blue.count = 0;
	players->spect.count = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!xmod.gs.ps[i].active)
		{
			continue;
		}

		if (xmod.gs.ps[i].team == TEAM_RED)
		{
			players->red.clients[players->red.count++] = i;
		}
		else if (xmod.gs.ps[i].team == TEAM_BLUE)
		{
			players->blue.clients[players->blue.count++] = i;
		}
		else
		{
			players->spect.clients[players->spect.count++] = i;
		}
	}

	SortPlayersList(&players->red);
	SortPlayersList(&players->blue);
	SortPlayersList(&players->spect);
}

static void GetSortedTourneyPlayersList(SeparatedPlayersList *players)
{
	players->ingame.count = 0;
	players->ingame2.count = 0;
	players->spect.count = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!xmod.gs.ps[i].active)
		{
			continue;
		}

		if (xmod.gs.ps[i].team == TEAM_FREE)
		{
			PlayersList *pl = (!players->ingame.count ? &players->ingame : &players->ingame2);
			pl->clients[pl->count++] = i;
		}
		else
		{
			players->spect.clients[players->spect.count++] = i;
		}
	}

	SortPlayersList(&players->spect);
}

void X_Hud_DrawString(float x, float y, float size, vec4_t rgba, int flags, qhandle_t shader, const char *string)
{
	vec4_t color;
	const char *s;
	float xx;

	if (flags & StringInCenter)
	{//TODO: seporate function for colored string calculation
		int len = 0;
		s = string;
		while (*s)
		{
			if (Q_IsColorString(s))
			{
				s += 2;
				continue;
			}
			len++;
			s++;
		}
		x -= (size * len) / 2;
	}
	else if (flags & StringRightAligned)
	{
		int len = 0;
		s = string;
		while (*s)
		{
			if (Q_IsColorString(s))
			{
				s += 2;
				continue;
			}
			len++;
			s++;
		}
		x -= size * len;
	}

	if (flags & StringWithShadow)
	{
		s = string;
		xx = x;

		MAKERGBA(color, 0, 0, 0, (rgba ? rgba[3] : 1.0));
		re.SetColor(color);

		while (*s)
		{
			if (Q_IsColorString(s))
			{
				s += 2;
				continue;
			}

			DrawCharacter(xx + 2, y + 2, size, *s, shader);
			xx += size;
			s++;
		}

		re.SetColor(0);
	}

	s = string;
	xx = x;

	if (rgba)
	{
		re.SetColor(rgba);
	}
	else
	{
		re.SetColor(0);
	}

	while (*s)
	{
		if (Q_IsColorString(s))
		{
			Com_Memcpy(color, g_color_table[ColorIndexFromChar(*(s + 1))], sizeof(color));
			color[3] = rgba ? rgba[3] : 1.0;
			re.SetColor(color);
			s += 2;
			continue;
		}

		DrawCharacter(xx, y, size, *s, shader);
		xx += size;
		s++;
	}

	re.SetColor(0);
}

static void DrawCharacter(float x, float y, float size, unsigned char ch, qhandle_t charmap)
{
	int row, col;
	float frow, fcol;
	float ax, ay, aw, ah;

	ch &= 255;

	if (ch == ' ')
	{
		return;
	}

	if (y < -size)
	{
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	size = 0.0625;

	re.DrawStretchPic(ax, ay, aw, ah, fcol, frow, fcol + size, frow + size, charmap);
}

static void DrawVoteBar(Bar *bar, float height)
{
	char buffer[256];

	int votetime = atoi(X_Misc_GetConfigString(CS_VOTE_TIME));
	if (!votetime)
	{
		return;
	}

	int sec = (votetime + VOTE_TIME - cl.serverTime) / 1000;

	Com_sprintf(buffer, sizeof(buffer),
				"^7VOTE(%d): ^f%s ^7yes:^2%d^z/^7no:^2%d",
				sec,
				X_Misc_GetConfigString(CS_VOTE_STRING),
				atoi(X_Misc_GetConfigString(CS_VOTE_YES)),
				atoi(X_Misc_GetConfigString(CS_VOTE_NO))
	);

	DrawBarStr(bar, 5, height * 0.1, height * 0.8, 0.8f, buffer);

	return;
}

#define XSCORE_CENTER_TABLE_MIN_HEIGHT 250.f
#define XSCORE_PLAYER_ROW_HEIGHT 20.f
#define XSCORE_PLAYER_TITLE_HEIGHT 14.f
#define XSCORE_VOTE_BAR_HEIGHT 10.f

static void CreateSingleScoreTable(float sectionWidth, TableSection *section1, TableSection *section2, float(*total)(Bar *, int team), int flags)
{
	PlayersList *ingame = section1->players;
	PlayersList *spects = section2->players;
	float rowh = section1->draw(0, 0, 0, 0) + 1.f;
	float headerh = section1->header(0);
	float totalh = (total ? total(0, 0) : 0.f);
	float voteh = (flags & SFlagVoteBar ? XSCORE_VOTE_BAR_HEIGHT : 0);
	float width = sectionWidth + 10.f;
	float height, spectsY;
	float infoh = 10.f;
	vec4_t color;

	spectsY = height = rowh * ingame->count + 35.f + headerh + infoh + totalh;

	if (spects->count)
	{
		float offset = 10.f;
		height += (rowh * spects->count) + 20.f + headerh + offset;
		spectsY += offset;
	}

	height += 14.f;

	if (height < XSCORE_CENTER_TABLE_MIN_HEIGHT)
	{
		height = XSCORE_CENTER_TABLE_MIN_HEIGHT;
	}

	Bar score, bar, head;

	CalculateCenterBar(&score, width, height + voteh);

	if (flags & SFlagVoteBar)
	{
		DrawVoteBar(&score, voteh);
		CalculateChildBar(&score, &score, 0.f, voteh, width, height);
	}

	MAKERGBA(color, 0.f, 0.f, 0.f, x_hud_xscore_opaque->value);
	DrawBar(&score, color, 2);

	DrawBarStrInCenter(&score, (width / 2.f), 3.f, 14.f, 0.5f, GetCurrentModeName());

	DrawBarStr(&score, 5.f, 18.f, 6.f, 0.5f, GetGameTimer());
	DrawBarStrRightAligned(&score, width - 5.f, 18.f, 6.f, 0.5f, GetCurrentModeLimits());

	CalculateChildBar(&score, &bar, 5.f, 18.f + infoh, sectionWidth, 14.f);
	DrawTableSectionTitle(&bar, section1);

	CalculateChildBar(&score, &head, 5.f, 35.f + infoh, sectionWidth, headerh);
	section1->header(&head);

	for (int i = 0; i < ingame->count; i++)
	{
		CalculateChildBar(&head, &bar, 0.f, headerh + (i * rowh), sectionWidth, rowh - 1.f);
		section1->draw(&bar, i, ingame->clients[i], 0);
	}

	if (total)
	{
		CalculateChildBar(&head, &bar, 0.0f, headerh + (ingame->count * rowh) + 5.f, sectionWidth, totalh - 5.f);
		total(&bar, 0);
	}

	if (spects->count)
	{
		CalculateChildBar(&score, &bar, 5.f, spectsY, sectionWidth, 14.f);
		DrawTableSectionTitle(&bar, section2);

		CalculateChildBar(&bar, &head, 0.f, 19.f, sectionWidth, headerh);
		section2->header(&head);

		for (int i = 0; i < spects->count; i++)
		{
			CalculateChildBar(&head, &bar, 0.f, headerh + (i * rowh), sectionWidth, rowh - 1.f);

			section2->draw(&bar, i, spects->clients[i], 0);
		}
	}

	// Footer

	CalculateChildBar(&score, &bar, 5.f, height - 16.f, sectionWidth, 18.f);

	char *serverInfo = X_Misc_GetConfigString(4);
	char buffer[256];
	if (strlen(serverInfo))
	{
		Com_sprintf(buffer, sizeof(buffer), "^d%s", serverInfo);
		DrawBarStr(&bar, 0.f, 6.f, 6.f, 0.5f, buffer);
	}

	Com_sprintf(buffer, sizeof(buffer), "^7ONLINE:^2%d", ingame->count + spects->count);
	DrawBarStr(&bar, sectionWidth - 55.f, 6.f, 6.f, 0.5f, buffer);

}

static void CreateDoubleScoreTable(float sectionWidth, TableSection *section1, TableSection *section2, TableSection *section3, float(*total)(Bar *, int team), int flags)
{
	PlayersList *ingame = section1->players;
	PlayersList *ingame2 = section2->players;
	PlayersList *spects = section3->players;
	float rowh = section1->draw(0, 0, 0, 0) + 1.f;
	float headerh = section1->header(0);
	float totalh = (total ? total(0, 0) : 0.f);
	float voteh = (flags & SFlagVoteBar ? XSCORE_VOTE_BAR_HEIGHT : 0);
	float width = sectionWidth + 7.5f;
	float height, spectsY;
	vec4_t color;
	char buffer[256];

	spectsY = height = rowh * xmax(ingame->count, ingame2->count) + 35.f + headerh + totalh;

	if (spects->count)
	{
		float offset = 10.f;
		height += (rowh * (spects->count / 2)) + (rowh * (spects->count % 2)) + 20.f + headerh + offset;
		spectsY += offset;
	}

	height += 14.f;

	if (height < XSCORE_CENTER_TABLE_MIN_HEIGHT)
	{
		height = XSCORE_CENTER_TABLE_MIN_HEIGHT;
	}

	// Title

	Bar score, bar, head;

	CalculateCenterBar(&score, width * 2, height + voteh);

	if (flags & SFlagVoteBar)
	{
		DrawVoteBar(&score, voteh);
		CalculateChildBar(&score, &score, 0.f, voteh, width * 2, height);
	}

	MAKERGBA(color, 0.f, 0.f, 0.f, x_hud_xscore_opaque->value);
	DrawBar(&score, color, 2);

	DrawBarStrInCenter(&score, width, 3.f, 14.f, 0.5f, GetCurrentModeName());

	DrawBarStr(&score, 5.f, 7.f, 7.f, 0.5f, GetGameTimer());
	DrawBarStrRightAligned(&score, (width * 2.f) - 5.f, 7.f, 7.f, 0.5f, GetCurrentModeLimits());

	// Left section

	CalculateChildBar(&score, &bar, 5.0f, 18.f, sectionWidth, 14.f);

	if (flags & SFlagDrawScore)
	{
		DrawTableSectionTitleWithScore(&bar, section1, 0);
	}
	else
	{
		DrawTableSectionTitle(&bar, section1);
	}

	CalculateChildBar(&score, &head, 5.f, 35.f, sectionWidth, headerh);
	section1->header(&head);

	for (int i = 0; i < ingame->count; i++)
	{
		CalculateChildBar(&head, &bar, 0.0f, headerh + (i * rowh), sectionWidth, rowh - 1.f);
		section1->draw(&bar, i, ingame->clients[i], 0);
	}

	if (total)
	{
		CalculateChildBar(&head, &bar, 0.0f, headerh + (xmax(ingame->count, ingame2->count) * rowh) + 5.f, sectionWidth, totalh - 5.f);
		total(&bar, 0);
	}

	// Right section

	CalculateChildBar(&score, &bar, 2.5f + width, 18.0f, sectionWidth, 14.0f);

	if (flags & SFlagDrawScore)
	{
		DrawTableSectionTitleWithScore(&bar, section2, 1);
	}
	else if (!(flags & SFlagOneTeam))
	{
		DrawTableSectionTitle(&bar, section2);
	}

	CalculateChildBar(&score, &head, 2.5f + width, 35.f, sectionWidth, headerh);
	section2->header(&head);

	for (int i = 0, n = 0; i < ingame2->count; i++)
	{
		CalculateChildBar(&head, &bar, 0.f, headerh + (n++ * rowh), sectionWidth, rowh - 1.f);
		int index = i + (flags & SFlagOneTeam ? ingame->count : 0);
		section2->draw(&bar, index, ingame2->clients[i], 0);
	}

	if (total && !(flags & SFlagOneTeam))
	{
		CalculateChildBar(&head, &bar, 0.0f, headerh + (xmax(ingame->count, ingame2->count) * rowh) + 5.f, sectionWidth, totalh - 5.f);
		total(&bar, 1);
	}

	// Spects

	if (spects->count)
	{
		CalculateChildBar(&score, &bar, 5.f, spectsY, sectionWidth, 14.f);
		DrawTableSectionTitle(&bar, section3);

		CalculateChildBar(&bar, &head, 0.f, 19.f, sectionWidth, headerh);
		section3->header(&head);

		for (int i = 0, n = 0; i < spects->count; i++)
		{
			if (i % 2)
			{
				CalculateChildBar(&head, &bar, width, headerh + (n++ * rowh), sectionWidth, rowh - 1.f);
			}
			else
			{
				CalculateChildBar(&head, &bar, 0.0f, headerh + (n * rowh), sectionWidth, rowh - 1.f);
			}

			section3->draw(&bar, i, spects->clients[i], 0);
		}
	}

	// Footer

	CalculateChildBar(&score, &bar, 5.f, height - 16.f, sectionWidth, 18.0f);

	char *serverInfo = X_Misc_GetConfigString(4);
	if (strlen(serverInfo))
	{
		Com_sprintf(buffer, sizeof(buffer), "^d%s", serverInfo);
		DrawBarStr(&bar, 0.f, 6.f, 6.f, 0.5f, buffer);
	}

	Com_sprintf(buffer, sizeof(buffer), "^7ONLINE:^2%d", ingame->count + ingame2->count + spects->count);
	DrawBarStr(&bar, (width + sectionWidth - 55.f), 6.f, 6.f, 0.5f, buffer);
}


// ======================

static float DrawPlayerRowFFA(Bar *bar, int pos, int client, int flags)
{
	vec4_t color;
	char buffer[512];
	qboolean isdark;
	qboolean isready;
	float opaque;

	if (!bar)
	{
		return XSCORE_PLAYER_ROW_HEIGHT;
	}

	XPlayerState *ps = xmod.gs.ps + client;
	XPlayerScore *sc = &ps->score;
	//XPlayerStats* st = &ps->stats;

	isdark = ps->isdead;
	isready = qfalse;
	opaque = 1.f;

	if (client < 32 && xmod.snap.ps.stats[STAT_CLIENTS_READY] & (1 << client))
	{
		if (xmod.scr.warmup)
		{
			isready = qtrue;
		}

		isdark = qtrue;
	}

	if (isdark)
	{
		opaque = 0.7f;
	}

	if (client == clc.clientNum)
	{
		MAKERGBA(color, 0.0f, 0.8f, 0.0f, isdark ? 0.1f : 0.2f);
	}
	else
	{
		MAKERGBA(color, 0.8f, 0.8f, 0.8f, isdark ? 0.05f : 0.1f);
	}

	DrawBar(bar, color, 1.0f);

	if (ps->team == TEAM_FREE) DrawFatBarStrInCenter(bar, 12.f, 6.f, 7.f, 0.8f * opaque, ConvertPlaceToString(pos));

	DrawSquareIcon(
	bar,
	30.f, 2.f, 15.f,
	(isdark ? 0.4f : 1.f),
	(ps->icons[0] ? ps->icons[0] : xmod.rs.shaderNoModel)
	);

	if (isready) DrawFatBarStr(bar, 2.f, 6.f, 8.f, GetFadingAlpha(&xmod.scr.fading), "^3READY");

	if (ps->active && sc->ping != -1)
	{
		Com_sprintf(buffer, sizeof(buffer), "^7%d", sc->score);
		DrawFatBarStrInCenter(bar, 62.f, 6.f, 9.f, 0.8f * opaque, buffer);

		if (ps->isbot)
		{
			DrawBarStrInCenter(bar, 96.f, 3.f, 8.f, 0.7f * opaque, "^zBOT");
			DrawBotLevel(bar, 85.0f, 12.0f, ps->botlvl);
		}
		else
		{
			DrawBarStrInCenter(bar, 93.f, 3.f, 6.f, 0.8f * opaque, GetColoredMS(sc->ping));

			Com_sprintf(buffer, sizeof(buffer), "^z%dm", sc->time);
			DrawBarStrInCenter(bar, 93.f, 10.f, 6.f, 0.9f * opaque, buffer);
		}
	}
	else if (ps->active && sc->ping == -1)
	{
		DrawBarStrInCenter(bar, 80.f, 7.f, 6.f, 0.8f, "^2connecting");
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", client);
	DrawBarStrInCenter(bar, 128.f, 3.f, 7.f, 0.8f * opaque, buffer);
	Com_sprintf(buffer, sizeof(buffer), (ps->isbot ? "^z--" : "^z%04X"), ps->sign);
	DrawBarStrInCenter(bar, 128.f, 10.f, 6.0f, 0.9f * opaque, buffer);

	DrawPlayerName(bar->x + (bar->scale * 149.f),
				   bar->y + (bar->scale * 1.f),
				   bar->scale * 8.f,
				   0.9f * opaque,
				   19,
				   ps->name
	);

	if (sc->active)
	{
		float offset = 0.f;
		float step = 21.f;

		for (int i = 0; i < 5; i++)
		{
			qhandle_t shader = 0;
			int medals = 0;

			switch (i)
			{
				case 0:
					medals = sc->guantletCount;
					shader = xmod.rs.shaderMedalGauntlet;
					break;
				case 1:
					medals = sc->excellentCount;
					shader = xmod.rs.shaderMedalExcellent;
					break;
				case 2:
					medals = sc->impressiveCount;
					shader = xmod.rs.shaderMedalImpressive;
					break;
				case 3:
					medals = sc->defendCount;
					shader = xmod.rs.shaderMedalDefend;
					break;
				case 4:
					medals = sc->assistCount;
					shader = xmod.rs.shaderMedalAssist;
					break;
				default:
					break;
			}

			if (!medals)
			{
				continue;
			}

			DrawSquareIcon(bar, (offset + 155.f), 10.f, 8.f, 0.4f * opaque, shader);

			Com_sprintf(buffer, sizeof(buffer), "^z%d", medals);
			DrawBarStr(bar, (offset + 165.f), 12.f, 6.f, 0.9f * opaque, buffer);

			offset += step + ((strlen(buffer) - 3) * 6.f);
		}
	}

	return 0.f;
}

static float DrawPlayerRowHeaderFFA(Bar *bar)
{
	if (!bar)
	{
		return XSCORE_PLAYER_TITLE_HEIGHT;
	}

	DrawBarStr(bar, 0.f, 3.0f, 6.0f, 0.6f, "^3PLACE");

	DrawBarStr(bar, 45.f, 3.0f, 6.0f, 0.6f, "^3SCORE");

	DrawBarStr(bar, 83.f, 1.f, 6.f, 0.6f, "^3PING");
	DrawBarStr(bar, 83.0f, 7.0f, 5.0f, 0.6f, "^zTIME");

	DrawBarStr(bar, 119.f, 1.f, 6.f, 0.6f, "^3ID");
	DrawBarStr(bar, 119.f, 7.f, 5.f, 0.6f, "^zSIGN");

	DrawBarStr(bar, 149.f, 1.f, 6.f, 0.6f, "^3NAME");
	DrawBarStr(bar, 149.f, 7.f, 5.f, 0.6f, "^zSTATS");

	return 0.f;
}

static float DrawSpectatorRowHeaderFFA(Bar *bar)
{
	if (!bar)
	{
		return XSCORE_PLAYER_TITLE_HEIGHT;
	}

	DrawBarStr(bar, 45.f, 3.f, 6.f, 0.6f, "^3SCORE");

	DrawBarStr(bar, 83.f, 1.f, 6.f, 0.6f, "^3PING");
	DrawBarStr(bar, 83.f, 7.f, 5.f, 0.6f, "^zTIME");

	DrawBarStr(bar, 119.f, 1.f, 6.f, 0.6f, "^3ID");
	DrawBarStr(bar, 119.f, 7.f, 5.f, 0.6f, "^zSIGN");

	DrawBarStr(bar, 149.f, 1.f, 6.f, 0.6f, "^3NAME");
	DrawBarStr(bar, 149.f, 7.f, 5.f, 0.6f, "^zSTATS");

	return 0.f;
}

static float DrawPlayerRowTotalFFA(Bar *bar, int team)
{
	char buffer[512];

	if (!bar)
	{
		return 17.f;
	}

	vec4_t color;
	MAKERGBA(color, 0.3f, 0.3f, 0.6f, 0.3f);
	DrawBar(bar, color, 0.f);

	DrawBarStr(bar, 2.f, 3.f, 6.f, 0.7f, "^zTOTAL");

	int score = 0, freezes = 0;
	int impressives = 0, guantlets = 0, excellents = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		XPlayerState *ps = xmod.gs.ps + i;

		if (!ps->active)
		{
			continue;
		}

		if (ps->team != TEAM_FREE)
		{
			continue;
		}

		if (ps->score.active)
		{
			score += ps->score.score;
			freezes += ps->score.scoreFlags;
			impressives += ps->score.impressiveCount;
			guantlets += ps->score.guantletCount;
			excellents += ps->score.excellentCount;
		}
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", score);
	DrawBarStrInCenter(bar, 62.f, 2.f, 9.f, 0.7f, buffer);

	float offset = 0.f;
	float step = 21.f;

	for (int i = 0; i < 3; i++)
	{
		qhandle_t shader = 0;
		int count = 0;

		if (i == 0 && impressives)
		{
			shader = xmod.rs.shaderMedalImpressive;
			count = impressives;
		}
		else if (i == 1 && excellents)
		{
			shader = xmod.rs.shaderMedalExcellent;
			count = excellents;
		}
		else if (i == 2 && guantlets)
		{
			shader = xmod.rs.shaderMedalGauntlet;
			count = guantlets;
		}
		else
		{
			continue;
		}

		float charsz = 6.f;
		DrawSquareIcon(bar, (offset + 115.f), 2.f, 8.f, 0.4f, shader);
		Com_sprintf(buffer, sizeof(buffer), "^z%d", count);
		DrawBarStr(bar, (offset + 125.f), 3.5f, charsz, 0.9f, buffer);
		offset += step + ((strlen(buffer) - 3) * charsz);
	}

	return 0.f;
}

static float DrawPlayerRowTDM(Bar *bar, int pos, int client, int flags)
{
	if (!bar)
	{
		return XSCORE_PLAYER_ROW_HEIGHT;
	}

	XPlayerState *ps = xmod.gs.ps + client;
	XPlayerScore *sc = &ps->score;
	XPlayerStats *st = &ps->stats;

	vec4_t color;
	char buffer[512];
	qboolean isdark = ps->isdead;
	qboolean isfrozen = qfalse;
	qboolean isready = qfalse;
	float opaque = 1.f;

	if (client < 32 && xmod.snap.ps.stats[STAT_CLIENTS_READY] & (1 << client))
	{
		if (xmod.gs.freezetag && !xmod.scr.warmup)
		{
			isfrozen = qtrue;
		}
		else
		{
			isready = qtrue;
		}

		isdark = qtrue;
	}

	// Fix for self freeze on OSP mode
	if (xmod.gs.freezetag
		&& client == clc.clientNum
		&& client == xmod.snap.ps.clientNum
		&& xmod.snap.ps.pm_type == PM_DEAD)
	{
		isfrozen = qtrue;
		isdark = qtrue;
	}

	if (isdark)
	{
		opaque = 0.7f;
	}

	if (client == clc.clientNum)
	{
		MAKERGBA(color, 0.0f, 0.8f, 0.0f, isdark ? 0.1f : 0.2f);
	}
	else
	{
		MAKERGBA(color, 0.8f, 0.8f, 0.8f, isdark ? 0.05f : 0.1f);
	}

	DrawBar(bar, color, 1.0f);

	qhandle_t hmodel = 0;
	if (ps->team == TEAM_RED)
	{
		hmodel = ps->icons[TEAM_RED];
	}
	else if (ps->team == TEAM_BLUE)
	{
		hmodel = ps->icons[TEAM_BLUE];
	}
	else
	{
		hmodel = ps->icons[0];
	}
	if (!hmodel)
	{
		hmodel = xmod.rs.shaderNoModel;
	}

	DrawSquareIcon(bar, 15.f, 2.f, 15.f, (isdark ? 0.4f : 1.f), hmodel);

	qhandle_t shader = xmod.rs.shaderSkill[0];
	if (st->active)
	{
		if (st->receivedDmg)
		{
			float eff = (float) st->givenDmg / (float) st->receivedDmg;

			if (eff >= 2.f && st->givenDmg > 2000)
			{
				shader = xmod.rs.shaderSkill[6];
			}
			else if (eff >= 1.6f && st->givenDmg > 1000)
			{
				shader = xmod.rs.shaderSkill[5];
			}
			else if (eff >= 1.2f)
			{
				shader = xmod.rs.shaderSkill[4];
			}
			else if (eff >= 0.8f)
			{
				shader = xmod.rs.shaderSkill[3];
			}
			else if (eff >= 0.4f)
			{
				shader = xmod.rs.shaderSkill[2];
			}
			else
			{
				shader = xmod.rs.shaderSkill[1];
			}
		}
	}

	DrawSquareIcon(bar, 2.f, 4.f, 12.f, (isdark ? 0.4f : 1.f), shader);

	if (isfrozen)
	{
		DrawSquareIcon(bar, 16.f, 4.f, 12.f, 0.8f, xmod.rs.shaderXFoe[1]);
	}

	if (sc->active && sc->ping != -1)
	{
		if (sc->guantletCount > 0)
		{
			re.DrawStretchPic(
			bar->x + (bar->scale * 28.0f),
			bar->y + (bar->scale * 3.0f),
			bar->scale * 8.0f, bar->scale * 8.0f,
			0, 0, 1, 1, xmod.rs.shaderMedalGauntlet);
		}

		if (sc->excellentCount > 0)
		{
			re.DrawStretchPic(
			bar->x + (bar->scale * 28.0f),
			bar->y + (bar->scale * 10.0f),
			bar->scale * 8.0f, bar->scale * 8.0f,
			0, 0, 1, 1, xmod.rs.shaderMedalExcellent);
		}

		if (xmod.gs.type == GameTDM && xmod.gs.freezetag)
		{
			Com_sprintf(buffer, sizeof(buffer), "^7%d", sc->score);
			DrawFatBarStrInCenter(bar, 55.f, 1.f, 9.f, 0.8f * opaque, buffer);
			Com_sprintf(buffer, sizeof(buffer), "^n%d", sc->scoreFlags);
			DrawBarStrInCenter(bar, 58.f, 10.f, 6.0f, 0.8f * opaque, buffer);
		}
		else
		{
			Com_sprintf(buffer, sizeof(buffer), "^7%d", sc->score);
			DrawFatBarStrInCenter(bar, 55.f, 6.f, 9.f, 0.8f * opaque, buffer);
		}

		if (ps->isbot)
		{
			DrawBarStrInCenter(bar, 86.f, 3.f, 8.f, 0.7f * opaque, "^zBOT");
			DrawBotLevel(bar, 75.0f, 12.0f, ps->botlvl);
		}
		else
		{
			DrawBarStrInCenter(bar, 88.f, 3.f, 6.f, 0.8f * opaque, GetColoredMS(sc->ping));

			Com_sprintf(buffer, sizeof(buffer), "^z%dm", sc->time);
			DrawBarStrInCenter(bar, 88.f, 10.f, 6.f, 0.9f * opaque, buffer);
		}
	}
	else if (ps->active && sc->ping == -1)
	{
		DrawBarStrInCenter(bar, 70.f, 7.f, 6.f, 0.8f, "^2connecting");
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", client);
	DrawBarStrInCenter(bar, 118.f, 3.f, 7.f, 0.8f * opaque, buffer);
	Com_sprintf(buffer, sizeof(buffer), (ps->isbot ? "^z--" : "^z%04X"), ps->sign);
	DrawBarStrInCenter(bar, 118.f, 10.f, 6.f, 0.9f * opaque, buffer);

	DrawPlayerName(
	bar->x + (bar->scale * 139.0f),
	bar->y + (bar->scale * 1.0f),
	bar->scale * 8.0f,
	0.9f * opaque,
	19,
	ps->name
	);

	if (xmod.scr.hasstats)
	{
		if (st->active)
		{
			if (st->weapons[WP_RAILGUN].shts)
			{
				DrawSquareIcon(bar, 140.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderIconRL);

				int acc = ((float) st->weapons[WP_RAILGUN].hit / (float) st->weapons[WP_RAILGUN].shts) * 100;
				Com_sprintf(buffer, sizeof(buffer), "^z%d%%", acc);
				DrawBarStr(bar, 150.f, 12.f, 6.0f, 0.9f * opaque, buffer);
			}

			if (st->weapons[WP_LIGHTNING].shts)
			{
				DrawSquareIcon(bar, 180.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderIconLG);

				int acc = ((float) st->weapons[WP_LIGHTNING].hit / (float) st->weapons[WP_LIGHTNING].shts) * 100;
				Com_sprintf(buffer, sizeof(buffer), "^z%d%%", acc);
				DrawBarStr(bar, 190.f, 12.f, 6.f, 0.9f * opaque, buffer);
			}

			if (st->givenDmg)
			{
				DrawFatBarStr(bar, 220.f, 12.f, 6.f, 0.5f * opaque, "^k+");
				Com_sprintf(buffer, sizeof(buffer), "^z%d", st->givenDmg);
				DrawBarStr(bar, 226.f, 12.f, 6.f, 0.9f * opaque, buffer);
			}

			if (st->receivedDmg)
			{
				DrawFatBarStr(bar, 256.f, 12.f, 6.f, 0.5f * opaque, "^1-");
				Com_sprintf(buffer, sizeof(buffer), "^z%d", st->receivedDmg);
				DrawBarStr(bar, 262.f, 12.f, 6.f, 0.9f * opaque, buffer);
			}
		}
	}
	else
	{
		if (sc->active)
		{
			float offset = 0.f;
			float step = 21.f;

			for (int i = 0; i < 5; i++)
			{
				qhandle_t shader = 0;
				int medals = 0;

				switch (i)
				{
					case 0:
						medals = sc->guantletCount;
						shader = xmod.rs.shaderMedalGauntlet;
						break;
					case 1:
						medals = sc->excellentCount;
						shader = xmod.rs.shaderMedalExcellent;
						break;
					case 2:
						medals = sc->impressiveCount;
						shader = xmod.rs.shaderMedalImpressive;
						break;
					case 3:
						medals = sc->defendCount;
						shader = xmod.rs.shaderMedalDefend;
						break;
					case 4:
						medals = sc->assistCount;
						shader = xmod.rs.shaderMedalAssist;
						break;
					default:
						break;
				}

				if (!medals)
				{
					continue;
				}

				DrawSquareIcon(bar, (offset + 140.f), 10.f, 8.f, 0.4f * opaque, shader);

				Com_sprintf(buffer, sizeof(buffer), "^z%d", medals);
				DrawBarStr(bar, (offset + 150.f), 12.f, 6.f, 0.9f * opaque, buffer);

				offset += step + ((strlen(buffer) - 3) * 6.f);
			}
		}
	}

	if (isready) DrawFatBarStr(bar, 2.f, 6.f, 8.f, GetFadingAlpha(&xmod.scr.fading), "^3READY");

	return 0.f;
}

static float DrawPlayerRowCTF(Bar *bar, int pos, int client, int flags)
{
	if (!bar)
	{
		return XSCORE_PLAYER_ROW_HEIGHT;
	}

	XPlayerState *ps = xmod.gs.ps + client;
	XPlayerScore *sc = &ps->score;
	XPlayerStats *st = &ps->stats;

	vec4_t color;
	char buffer[512];
	qboolean isdead = ps->isdead;
	qboolean isready = qfalse;
	float opaque = 1.f;

	if (client < 32 && xmod.snap.ps.stats[STAT_CLIENTS_READY] & (1 << client))
	{
		if (xmod.scr.warmup)
		{
			isready = qtrue;
		}

		isdead = qtrue;
	}

	if (isdead)
	{
		opaque = 0.7f;
	}

	if (client == clc.clientNum)
	{
		MAKERGBA(color, 0.0f, 0.8f, 0.0f, isdead ? 0.1f : 0.2f);
	}
	else
	{
		MAKERGBA(color, 0.8f, 0.8f, 0.8f, isdead ? 0.05f : 0.1f);
	}

	DrawBar(bar, color, 1.0f);

	qhandle_t hmodel = 0;
	if (ps->team == TEAM_RED)
	{
		hmodel = ps->icons[TEAM_RED];
	}
	else if (ps->team == TEAM_BLUE)
	{
		hmodel = ps->icons[TEAM_BLUE];
	}
	else
	{
		hmodel = ps->icons[0];
	}
	if (!hmodel)
	{
		hmodel = xmod.rs.shaderNoModel;
	}

	DrawSquareIcon(bar, 15.f, 2.f, 15.f, (isdead ? 0.4f : 1.f), hmodel);

	qhandle_t shader = xmod.rs.shaderSkill[0];
	if (st->active)
	{
		if (st->receivedDmg)
		{
			float eff = (float) st->givenDmg / (float) st->receivedDmg;

			if (eff >= 2.f && st->givenDmg > 2000)
			{
				shader = xmod.rs.shaderSkill[6];
			}
			else if (eff >= 1.6f && st->givenDmg > 1000)
			{
				shader = xmod.rs.shaderSkill[5];
			}
			else if (eff >= 1.2f)
			{
				shader = xmod.rs.shaderSkill[4];
			}
			else if (eff >= 0.8f)
			{
				shader = xmod.rs.shaderSkill[3];
			}
			else if (eff >= 0.4f)
			{
				shader = xmod.rs.shaderSkill[2];
			}
			else
			{
				shader = xmod.rs.shaderSkill[1];
			}
		}
	}

	DrawSquareIcon(bar, 2.f, 4.f, 12.f, (isdead ? 0.4f : 1.f), shader);

	if (sc->active && sc->ping != -1)
	{
		if (sc->guantletCount > 0)
		{
			re.DrawStretchPic(
			bar->x + (bar->scale * 28.0f),
			bar->y + (bar->scale * 3.0f),
			bar->scale * 8.0f, bar->scale * 8.0f,
			0, 0, 1, 1, xmod.rs.shaderMedalGauntlet);
		}

		if (sc->excellentCount > 0)
		{
			re.DrawStretchPic(
			bar->x + (bar->scale * 28.0f),
			bar->y + (bar->scale * 10.0f),
			bar->scale * 8.0f, bar->scale * 8.0f,
			0, 0, 1, 1, xmod.rs.shaderMedalExcellent);
		}

		if (xmod.gs.type == GameTDM && xmod.gs.freezetag)
		{
			Com_sprintf(buffer, sizeof(buffer), "^7%d", sc->score);
			DrawFatBarStrInCenter(bar, 55.f, 1.f, 9.f, 0.8f * opaque, buffer);
			Com_sprintf(buffer, sizeof(buffer), "^n%d", sc->scoreFlags);
			DrawBarStrInCenter(bar, 58.f, 10.f, 6.0f, 0.8f * opaque, buffer);
		}
		else
		{
			Com_sprintf(buffer, sizeof(buffer), "^7%d", sc->score);
			DrawFatBarStrInCenter(bar, 55.f, 6.f, 9.f, 0.8f * opaque, buffer);
		}

		if (ps->isbot)
		{
			DrawBarStrInCenter(bar, 86.f, 3.f, 8.f, 0.7f * opaque, "^zBOT");
			DrawBotLevel(bar, 75.0f, 12.0f, ps->botlvl);
		}
		else
		{
			DrawBarStrInCenter(bar, 88.f, 3.f, 6.f, 0.8f * opaque, GetColoredMS(sc->ping));

			Com_sprintf(buffer, sizeof(buffer), "^z%dm", sc->time);
			DrawBarStrInCenter(bar, 88.f, 10.f, 6.f, 0.9f * opaque, buffer);
		}

		if (sc->captures)
		{
			DrawSquareIcon(bar, 140.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderMedalCapture);
			Com_sprintf(buffer, sizeof(buffer), "^z%d", sc->captures);
			DrawBarStr(bar, 150.f, 12.f, 6.0f, 0.9f * opaque, buffer);
		}

		if (sc->defendCount)
		{
			DrawSquareIcon(bar, 165.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderMedalDefend);
			Com_sprintf(buffer, sizeof(buffer), "^z%d", sc->defendCount);
			DrawBarStr(bar, 175.f, 12.f, 6.f, 0.9f * opaque, buffer);
		}

		if (sc->assistCount)
		{
			DrawSquareIcon(bar, 190.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderMedalAssist);
			Com_sprintf(buffer, sizeof(buffer), "^z%d", sc->assistCount);
			DrawBarStr(bar, 200.f, 12.f, 6.0f, 0.9f * opaque, buffer);
		}
	}
	else if (ps->active && sc->ping == -1)
	{
		DrawBarStrInCenter(bar, 70.f, 7.f, 6.f, 0.8f, "^2connecting");
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", client);
	DrawBarStrInCenter(bar, 118.f, 3.f, 7.f, 0.8f * opaque, buffer);
	Com_sprintf(buffer, sizeof(buffer), (ps->isbot ? "^z--" : "^z%04X"), ps->sign);
	DrawBarStrInCenter(bar, 118.f, 10.f, 6.f, 0.9f * opaque, buffer);

	DrawPlayerName(
	bar->x + (bar->scale * 139.0f),
	bar->y + (bar->scale * 1.0f),
	bar->scale * 8.0f,
	0.9f * opaque,
	19,
	ps->name
	);

	/*if (st->active)
	{
		if (st->weapons[WP_RAILGUN].shts)
		{
			DrawSquareIcon(bar, 140.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderIconRL);

			int acc = ((float)st->weapons[WP_RAILGUN].hit / (float)st->weapons[WP_RAILGUN].shts) * 100;
			Com_sprintf(buffer, sizeof(buffer), "^z%d%%", acc);
			DrawBarStr(bar, 150.f, 12.f, 6.0f, 0.9f * opaque, buffer);
		}

		if (st->weapons[WP_LIGHTNING].shts)
		{
			DrawSquareIcon(bar, 180.f, 10.f, 8.f, 0.9f * opaque, xmod.rs.shaderIconLG);

			int acc = ((float)st->weapons[WP_LIGHTNING].hit / (float)st->weapons[WP_LIGHTNING].shts) * 100;
			Com_sprintf(buffer, sizeof(buffer), "^z%d%%", acc);
			DrawBarStr(bar, 190.f, 12.f, 6.f, 0.9f * opaque, buffer);
		}

		if (st->givenDmg)
		{
			DrawFatBarStr(bar, 220.f, 12.f, 6.f, 0.5f * opaque, "^k+");
			Com_sprintf(buffer, sizeof(buffer), "^z%d", st->givenDmg);
			DrawBarStr(bar, 226.f, 12.f, 6.f, 0.9f * opaque, buffer);
		}

		if (st->receivedDmg)
		{
			DrawFatBarStr(bar, 256.f, 12.f, 6.f, 0.5f * opaque, "^1-");
			Com_sprintf(buffer, sizeof(buffer), "^z%d", st->receivedDmg);
			DrawBarStr(bar, 262.f, 12.f, 6.f, 0.9f * opaque, buffer);
		}
	}*/

	if (isready) DrawFatBarStr(bar, 2.f, 6.f, 8.f, GetFadingAlpha(&xmod.scr.fading), "^3READY");

	return 0.f;
}


static float DrawPlayerRowTotalTDM(Bar *bar, int team)
{
	char buffer[512];

	if (!bar)
	{
		return 17.f;
	}

	vec4_t color;
	MAKERGBA(color, 0.3f, 0.3f, 0.6f, 0.3f);
	DrawBar(bar, color, 0.f);

	DrawBarStr(bar, 2.f, 3.f, 6.f, 0.7f, "^zTOTAL");

	int score = 0, freezes = 0, given = 0, received = 0;
	int impressives = 0, guantlets = 0, excellents = 0, defends = 0, assists = 0;
	int tm = (team ? TEAM_BLUE : TEAM_RED);
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		XPlayerState *ps = xmod.gs.ps + i;

		if (!ps->active)
		{
			continue;
		}

		if (ps->team != tm)
		{
			continue;
		}

		if (ps->score.active)
		{
			score += ps->score.score;
			freezes += ps->score.scoreFlags;
			impressives += ps->score.impressiveCount;
			guantlets += ps->score.guantletCount;
			excellents += ps->score.excellentCount;
			defends += ps->score.defendCount;
			assists += ps->score.assistCount;
		}

		if (ps->stats.active)
		{
			given += ps->stats.givenDmg;
			received += ps->stats.receivedDmg;
		}
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", score);
	DrawBarStrInCenter(bar, 55.f, 2.f, 9.f, 0.7f, buffer);

	if (xmod.gs.freezetag)
	{
		DrawSquareIcon(bar, 76.f, 2.f, 8.f, 0.8f, xmod.rs.shaderXFoe[1]);

		Com_sprintf(buffer, sizeof(buffer), "^n%d", freezes);
		DrawBarStr(bar, 85.f, 3.f, 7.5f, 0.7f, buffer);
	}

	float offset = 0.f;
	float step = 21.f;

	for (int i = 0; i < 5; i++)
	{
		qhandle_t shader = 0;
		int count = 0;

		if (i == 0 && impressives)
		{
			shader = xmod.rs.shaderMedalImpressive;
			count = impressives;
		}
		else if (i == 1 && excellents)
		{
			shader = xmod.rs.shaderMedalExcellent;
			count = excellents;
		}
		else if (i == 2 && guantlets)
		{
			shader = xmod.rs.shaderMedalGauntlet;
			count = guantlets;
		}
		else if (i == 3 && defends)
		{
			shader = xmod.rs.shaderMedalDefend;
			count = defends;
		}
		else if (i == 4 && assists)
		{
			shader = xmod.rs.shaderMedalAssist;
			count = assists;
		}
		else
		{
			continue;
		}

		float charsz = 6.f;
		DrawSquareIcon(bar, (offset + 115.f), 2.f, 8.f, 0.4f, shader);
		Com_sprintf(buffer, sizeof(buffer), "^z%d", count);
		DrawBarStr(bar, (offset + 125.f), 3.5f, charsz, 0.9f, buffer);
		offset += step + ((strlen(buffer) - 3) * charsz);
	}

	if (given)
	{
		float offset = (given > 99999 ? 6.f : 0.f); // Move left if a value is too big
		DrawFatBarStr(bar, 220.f - offset, 3.5f, 6.f, 0.7f, "^k+");
		Com_sprintf(buffer, sizeof(buffer), "^z%d", given);
		DrawBarStr(bar, 226.f - offset, 3.5f, 6.f, 0.7f, buffer);
	}

	if (received)
	{
		DrawFatBarStr(bar, 256.f, 3.5f, 6.f, 0.7f, "^1-");
		Com_sprintf(buffer, sizeof(buffer), "^z%d", received);
		DrawBarStr(bar, 262.f, 3.5f, 6.f, 0.7f, buffer);
	}

	return 0.f;
}

static float DrawPlayerRowHeaderTDM(Bar *bar)
{
	if (!bar)
	{
		return XSCORE_PLAYER_TITLE_HEIGHT;
	}

	DrawBarStr(bar, 0.f, 3.f, 6.f, 0.6f, "^3SKILL");

	if (xmod.gs.type == GameTDM && xmod.gs.freezetag)
	{
		DrawBarStr(bar, 40.f, 1.f, 6.f, 0.6f, "^3SCORE");
		DrawBarStr(bar, 40.f, 7.f, 5.f, 0.6f, "^zTHAWS");
	}
	else
	{
		DrawBarStr(bar, 40.f, 3.f, 6.f, 0.6f, "^3SCORE");
	}

	DrawBarStr(bar, 73.f, 1.f, 6.f, 0.6f, "^3PING");
	DrawBarStr(bar, 73.f, 7.f, 5.f, 0.6f, "^zTIME");

	DrawBarStr(bar, 109.f, 1.f, 6.f, 0.6f, "^3ID");
	DrawBarStr(bar, 109.f, 7.f, 5.f, 0.6f, "^zSIGN");

	DrawBarStr(bar, 139.f, 1.f, 6.f, 0.6f, "^3NAME");
	DrawBarStr(bar, 139.f, 7.f, 5.f, 0.6f, "^zSTATS");

	return 0.f;
}

// ======================

static void DrawTableSectionTitle(Bar *bar, TableSection *sect)
{
	DrawBar(bar, sect->color, 0.f);

	DrawBarStr(bar, 1.f, 1.f, 13.f, 1.f, sect->title);

	DrawBarStr(bar, 150.f, 4.f, 6.f, 0.4f, sect->descr);
}

static void DrawTableSectionTitleWithScore(Bar *bar, TableSection *sect, int place)
{
	char buffer[64];

	DrawBar(bar, sect->color, 0.f);

	if (place)
	{
		Com_sprintf(buffer, sizeof(buffer), "%10s", sect->title);
		DrawBarStr(bar, 175.f, 1.f, 13.f, 1.f, buffer);

		Com_sprintf(buffer, sizeof(buffer), "^7%d", sect->score);
		DrawBarStr(bar, 7.f, -3.f, 22.f, 0.8f, buffer);

		DrawBarStr(bar, 80.f, 4.f, 6.f, 0.4f, sect->descr);
	}
	else
	{
		DrawBarStr(bar, 5.f, 1.f, 13.f, 1.f, sect->title);

		DrawBarStr(bar, 150.f, 4.f, 6.f, 0.4f, sect->descr);

		Com_sprintf(buffer, sizeof(buffer), "^7%3d", sect->score);
		DrawBarStr(bar, 227.f, -3.f, 22.f, 0.8f, buffer);
	}
}

// ======================

static char *ConvertPlaceToString(int place)
{
	static char buffer[8];

	if (place == 0)
	{
		return "^41st";
	}

	if (place == 1)
	{
		return "^12nd";
	}

	if (place == 2)
	{
		return "^33rd";
	}

	Com_sprintf(buffer, sizeof(buffer), "^7%d", place + 1);
	return buffer;
}

static void DrawBotLevel(Bar *bar, float x, float y, float skill)
{
	vec4_t neitral;
	MAKERGBA(neitral, 0.8f, 0.8f, 0.8f, 0.1f);

	for (int i = 0; i < 5; i++)
	{
		float size = 4.f;
		vec4_t color;
		Bar child;

		if (skill >= i + 1)
		{
			MAKERGBA(color, 0.0f, 1.0f, 0.0f, 0.2f);
		}
		else
		{
			Vector4Copy(neitral, color);
		}

		CalculateChildBar(bar, &child, x + (i * size) + 1.f, y, size, size);
		DrawBar(&child, color, 1.f);
	}
}

static void DrawSquareIcon(Bar *bar, float x, float y, float sz, float alpha, qhandle_t shader)
{
	vec4_t color;
	MAKERGBA(color, 1.f, 1.f, 1.f, alpha);
	re.SetColor(color);
	re.DrawStretchPic(
	bar->x + (bar->scale * x),
	bar->y + (bar->scale * y),
	bar->scale * sz, bar->scale * sz,
	0, 0, 1, 1, shader);
	re.SetColor(0);
}

// ===========================

void X_Hud_InitProgressBar(XUIProgressBar *bar, vec4_t color1, vec4_t color2, float x, float y, float w, float h, float range)
{
	bar->x = x;
	bar->y = y;
	bar->w = w;
	bar->h = h;
	bar->step = w / range;

	Vector4Copy(color1, bar->color1);
	Vector4Copy(color2, bar->color2);
}

void X_Hud_DrawProgressBar(XUIProgressBar *bar, float value)
{
	Bar b;

	b.x = bar->x;
	b.y = bar->y;
	b.h = bar->h;
	b.w = bar->w;
	b.scale = 1.f;

	SCR_AdjustFrom640(&b.x, &b.y, 0, 0);

	DrawProgressBar(bar, &b, value);
}

void X_Hud_DrawProgressBarInCenter(XUIProgressBar *bar, float value)
{
	Bar b;

	b.x = bar->x;
	b.y = bar->y;
	b.h = bar->h;
	b.w = bar->w;
	b.scale = 1.f;

	SCR_AdjustFrom640(&b.x, &b.y, 0, 0);
	b.x -= (bar->w / 2.f);

	DrawProgressBar(bar, &b, value);
}

static void InitFading(XUIFading *fad, int interval, float min, float max)
{
	fad->start = Com_Milliseconds();
	fad->interval = interval;
	fad->min = min;
	fad->max = max;
	fad->step = interval / (max - min);
}

static void ResetFading(XUIFading *fad)
{
	fad->start = Com_Milliseconds();
}

static float GetFadingAlpha(XUIFading *fad)
{
	int range = fad->interval * 2;
	int ms = (Com_Milliseconds() - fad->start) % range;
	float opaque = 0.f;

	if (ms >= fad->interval)
	{
		ms -= fad->interval;
		opaque = fad->min + (ms / fad->step);
	}
	else
	{
		opaque = fad->max - (ms / fad->step);
	}

	return opaque;
}

static void DrawProgressBar(XUIProgressBar *bar, Bar *b, float value)
{
	DrawBar(b, bar->color1, 1.f);

	b->x += 3.f;
	b->y += 3.f;
	b->h -= 6.f;

	float max = b->w - 6.f;
	b->w = (value * bar->step);
	if (b->w > max)
	{
		b->w = max;
	}

	DrawBar(b, bar->color2, 0.f);
}