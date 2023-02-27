#include "client.h"

#define X_CON_MAX_NAME_LENGTH    (MAX_NAME_LENGTH + 5)
#define MAX_NAME_LEN (X_CON_MAX_NAME_LENGTH * 3)

#define X_MSG_ENCDEC_SIGN "^^x^q^3^e"

typedef struct
{
	float x;
	float y;
	float h;
	float w;
	float step;
	vec4_t color1;
	vec4_t color2;
} XUIProgressBar;

typedef struct
{
	int start;
	int interval;
	float min;
	float max;
	float step;
} XUIFading;

typedef enum
{
	ModeBaseQ3,
	ModeOSP,
	ModeCPMA,
	ModeExcessivePlus,
	ModeUnknown
} XGameMode;

typedef enum
{
	GameFFA,
	Game1v1,
	GameSingle,
	GameTDM,
	GameCTF,
	GameCA,
	GameUnknown
} XGameType;

typedef enum
{
	GTimerNone,
	GTimerWarmup,
	GTimerTimeout,
	GTimerRoundTime
} XGameTimerType;

typedef struct
{
	XGameTimerType type;
	int current;
	int start;
	int warmup;
} XGameTimer;

typedef struct
{
	int entity;
	vec3_t origin;
} DeadBody;

typedef struct
{
	int count;
	DeadBody entities[16];
} DeadBodies;

typedef struct
{
	qboolean active;
	int score;
	int ping;
	int time;
	int scoreFlags;
	int powerUps;
	int accuracy;
	int impressiveCount;
	int excellentCount;
	int guantletCount;
	int defendCount;
	int assistCount;
	int perfect;
	int captures;
} XPlayerScore;

typedef struct
{
	int hit;
	int shts;
	int kills;
	int deaths;
} XWeaponState;

typedef struct
{
	qboolean active;
	int mask;
	XWeaponState weapons[WP_NUM_WEAPONS];
	int givenDmg;
	int receivedDmg;
} XPlayerStats;

typedef struct
{
	qboolean show;
	qboolean opaque;
	qboolean autoshow;
	qboolean warmup;
	qboolean hasstats;
	int team_red;
	int team_blue;
	int lastUpd;
	xcommand_t scoresShow;
	xcommand_t scoresHide;
	int hud;
	XUIFading fading;
} XScore;

typedef struct
{
	qboolean active;
	qboolean visible;
	qboolean unfreezing;
	qboolean isdead;
	qboolean isbot;
	int botlvl;
	int entity;
	vec3_t origin;
	int powerups;
	int team;
	DeadBodies dead;
	char name[MAX_NAME_LEN + 1];
	char model[64];
	unsigned int sign;
	XPlayerScore score;
	XPlayerStats stats;
	qhandle_t icons[3];
} XPlayerState;

typedef struct
{
	int type;
	int flags;
	int client;
	int snap;
	vec3_t origin;
} XEntity;

typedef struct
{
	int event;
	int flags;
	int param;
	int client1;
	int client2;
	int snap;
} XEventCacheEntry;

typedef struct
{
	int count;
	XEventCacheEntry entry[MAX_CLIENTS * 2];
	unsigned char active[MAX_CLIENTS * 2];
} XEventCache;

typedef struct
{
	XGameMode mode;
	XGameType type;
	qboolean promode;
	qboolean instagib;
	qboolean freezetag;
	qboolean svcheats;
	XPlayerState ps[MAX_CLIENTS];
	XEntity entities[MAX_GENTITIES];
	XEventCache events;
	int roundlimit;
	int fraglimit;
	int timelimit;
	int capturelimit;
	int overtime;
	int psevseq;
	XGameTimer timer;
} XGameState;

typedef enum
{
	HitNone,
	HitLowest,
	HitLow,
	HitMedium,
	HitHigh
} HitCrosshairDamage;

typedef struct
{
	HitCrosshairDamage dmg;
	int startMS;
	int durationMS;
	vec4_t color;
} HitCrosshairIcon;

typedef struct
{
	int startMS;
	int durationMS;
	float increament;
} HitCrosshairPulse;

typedef struct
{
	qboolean active;
	cvar_t *cvar;
	int version;
	float rgb[3];
} XCustomColor;

typedef struct
{
	XCustomColor front;
	XCustomColor decor;
	XCustomColor actionFront;
	XCustomColor actionDecor;
	HitCrosshairIcon hc;
	HitCrosshairPulse hp;
	float distance;
} XModCrosshair;

typedef enum
{
	DmgNone,
	DmgUnknown,
	DmgDirect,
	DmgMissle,
	DmgRail,
	DmgShotgun
} DamageType;

typedef struct
{
	int start;
	int value;
	int dir;
	vec3_t origin;
	byte color[4];
	float params[4];
} DamageIcon;

typedef struct
{
	DamageType type;
	qboolean printed;
	int clientNum;
	int total;
	int damage;
	int target;
	int iconNum;
	int duration;
	int dir;
	int lastRedir;
	int redirDuration;
	DamageIcon icons[256];
} Damage;

typedef enum
{
	NotPlayer,
	LegsModel,
	TorsoModel,
	HeadModel
} PlayerModel;

#define X_MAX_NET_PORTS 20
#define X_NET_PORT_ATTEMPTS 16

typedef struct
{
	qboolean scan;
	qboolean silent;
	int scanTime;
	int currentPort;
	int currentSnap;
	int skipSnaps;
	int ports[X_MAX_NET_PORTS];
	int avrgMS[X_MAX_NET_PORTS];
	int snapMS[X_NET_PORT_ATTEMPTS];
	XUIProgressBar scanBar;
} Network;

typedef struct
{
	PlayerModel model;
	int client;
	XCustomColor team;
	XCustomColor enemy;
} Freeze;

typedef struct
{
	// Foe hack
	qhandle_t shaderFoe;
	qhandle_t shaderXFoe[2];
	qhandle_t shaderXFoeUnfreeze;
	// Hit crosshair
	qhandle_t shaderHit[4];
	qhandle_t shaderCrosshairs[2][10];
	// Draw damage
	qhandle_t shaderNumbers[10];
	qhandle_t shaderOneHPHit;
	qhandle_t modelMissle;
	qhandle_t modelRail;
	qhandle_t modelBullet;
	// Crosshair limit
	qhandle_t shaderXCrosshairs[100];
	qhandle_t shaderXCrosshairsR45[100];
	// Decors
	qhandle_t shaderDecors[100];
	qhandle_t shaderDecorsR45[100];
	// Freeze effects
	qhandle_t shaderFreeze;
	qhandle_t shaderXFreezeTeam;
	qhandle_t shaderXFreezeEnemy;
	qhandle_t shaderXFreeze;
	sfxHandle_t soundOldUnfreeze;
	sfxHandle_t soundUnfreeze;
	// Hitboxes
	qhandle_t modelHitbox;
	qhandle_t shaderHitbox;
	// Pickup items
	qhandle_t shaderPowerups[6];
	qhandle_t shaderArmors[3];
	qhandle_t shaderMega;
	// Font
	qhandle_t shaderCharmap[3];
	qhandle_t shaderXCharmap;
	qhandle_t shaderXOverlayChars;
	// Score
	qhandle_t shaderSkill[7];
	qhandle_t shaderMedalGauntlet;
	qhandle_t shaderMedalExcellent;
	qhandle_t shaderMedalImpressive;
	qhandle_t shaderMedalAssist;
	qhandle_t shaderMedalDefend;
	qhandle_t shaderMedalCapture;
	qhandle_t shaderIconLG;
	qhandle_t shaderIconRL;
	qhandle_t shaderNoModel;
} XModResources;

typedef enum
{
	StateOff,
	StateStopped,
	StateStarted,
} XModState;

typedef struct
{
	XModState state;
	qboolean hack;
	snapshot_t snap;
	int snapNum;
	XGameState gs;
	XModCrosshair ch;
	Damage dmg;
	XModResources rs;
	float currentColor[4];

	Freeze frz;

	PlayerModel deadPlayerParts;

	qboolean aimed;
	int aimedClient;
	float aimedDistance;

	Network net;

	XScore scr;

} XModContext;

typedef struct
{
	char lastserver[MAX_OSPATH];
	char lastmap[MAX_QPATH];
} XModStaticContext;

#if defined( _MSC_VER )
#define countof _countof
#else
#define countof(a) (sizeof(a)/sizeof(*(a)))
#endif

static const char X_SOUND_HIT_LOWEST[] = "sound/feedback/hitlowest.wav";
static const char X_SOUND_HIT_LOW[] = "sound/feedback/hit.wav";
static const char X_SOUND_HIT_MEDIUM[] = "sound/feedback/hitlow.wav";
static const char X_SOUND_HIT_HIGH[] = "sound/feedback/hithigh.wav";

static const char X_CROSSHAIR_SHADER[] = "gfx/2d/crosshair%c";
static const char X_CROSSHAIR_SHADER2[] = "gfx/2d/crosshair%c2";
static const char X_XCROSSHAIR_SHADER[] = "xmod/gfx/2d/crosshair%d";
static const char X_XCROSSHAIR_R45_SHADER[] = "xmod/gfx/2d/crosshair%d_r45";
static const char X_XCROSSHAIR_DECOR[] = "xmod/gfx/2d/decor%d";
static const char X_XCROSSHAIR_R45_DECOR[] = "xmod/gfx/2d/decor%d_r45";

static const char X_CROSSHAIR_DAMAGE[] = "xmod/gfx/misc/crosshit%c";

static const char X_NUMBERS_SHADER[] = "xmod/gfx/2d/numbers/%d_64a";
static const char X_HIT_SHADER[] = "xmod/gfx/2d/numbers/hit";

static const char X_TEAM_FOE_SHADER[] = "sprites/foe";
static const char X_TEAM_XFOE_SHADER[] = "xmod/sprites/xfoe%d";
static const char X_TEAM_XFOE_UNFREEZE_SHADER[] = "xmod/sprites/xfoe-unfreeze";

static const char X_TEAM_FREEZE_SHADER[] = "freezeShader";
static const char X_TEAM_XFREEZE_COLOR1[] = "xmod/freeze/red";
static const char X_TEAM_XFREEZE_COLOR2[] = "xmod/freeze/blue";

static const char X_MODEL_MISSLE_HIT[] = "models/weaphits/boom01.md3";
static const char X_MODEL_RAIL_HIT[] = "models/weaphits/ring02.md3";
static const char X_MODEL_BULLET_HIT[] = "models/weaphits/bullet.md3";

static const char X_MODEL_HITBOX_3D[] = "models/hitbox-3d.md3";
static const char X_SHADER_HITBOX_2D[] = "xmod/hitbox-2d";

static const char X_BATTLESUIT_SHADER[] = "icons/envirosuit";
static const char X_HASTE_SHADER[] = "icons/haste";
static const char X_INVISIBILITY_SHADER[] = "icons/invis";
static const char X_REGENERATION_SHADER[] = "icons/regen";
static const char X_FLIGHT_SHADER[] = "icons/flight";
static const char X_QUAD_SHADER[] = "icons/quad";

static const char X_GREEN_ARMOR_SHADER[] = "icons/iconr_green";
static const char X_YELLOW_ARMOR_SHADER[] = "icons/iconr_yellow";
static const char X_RED_ARMOR_SHADER[] = "icons/iconr_red";

static const char X_MEGA_SHADER[] = "icons/iconh_mega";

static const char X_SOUND_OLD_UNFREEZE[] = "sound/player/tankjr/jump1.wav";
static const char X_SOUND_UNFREEZE[] = "sound/xunfreeze.wav";

static const char X_CHARMAP_SHADER[] = "xmod/gfx/bigchars_%d";
static const char X_XCHARMAP_SHADER[] = "xmod/gfx/xbigchars";

static const char X_MENU_SKILL_SHADER[] = "gfx/misc/xskill%d.tga";
static const char X_MEDAL_GAUNTLET_SHADER[] = "medal_gauntlet";
static const char X_MEDAL_EXCELLENT_SHADER[] = "medal_excellent";
static const char X_MEDAL_IMPRESSIVE_SHADER[] = "medal_impressive";
static const char X_MEDAL_ASSIST_SHADER[] = "medal_assist";
static const char X_MEDAL_DEFEND_SHADER[] = "medal_defend";
static const char X_MEDAL_CAPTURE_SHADER[] = "medal_capture";

static const char X_ICON_LIGHTNING_SHADER[] = "xgfx/icon/lightning";
static const char X_ICON_RAILGUN_SHADER[] = "xgfx/icon/railgun";

static const char X_ICON_NO_MODEL[] = "gfx/misc/icon_unknown.tga";

extern XModContext xmod;
extern XModStaticContext sxmod;

// Hooks

extern void (*Original_SetColor)(const float *rgba);
extern void (*Original_RenderScene)(const refdef_t *fd);
extern void (*Original_DrawStretchPic)(float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t hShader);
extern void (*Original_AddRefEntityToScene)(const refEntity_t *re, qboolean intShaderTime);

// Init

qboolean IsXModeActive(void);
qboolean IsXModeHackActive(void);
qboolean IsXModeHackCommandActive(cvar_t *cmd);

void XModeDisableOutput(qboolean disable);

cvar_t *RegisterXModeCmd(char *cmd, char *dfault, char *start, char *stop, char *description, int flags, int checktype);

#define RegisterXCommand(cvar, dfault, start, stop, description) (cvar)=RegisterXModeCmd((#cvar),(dfault),(start),(stop),(description),CVAR_ARCHIVE,CV_INTEGER)
#define RegisterFloatXCommand(cvar, dfault, start, stop, description) (cvar)=RegisterXModeCmd((#cvar),(dfault),(start),(stop),(description),CVAR_ARCHIVE,CV_FLOAT)
#define RegisterHackXCommand(cvar, dfault, start, stop, description) (cvar)=RegisterXModeCmd((#cvar),(dfault),(start),(stop),(description),CVAR_ARCHIVE|CVAR_USERINFO|CVAR_XMOD,CV_INTEGER)

// Hit crosshair

void X_CH_Init(void);
qboolean X_CH_CustomizeCrosshair(float x, float y, float w, float h, qhandle_t shader);
void X_CH_ChangeCrosshairOnSoundTrigger(const char *soundName);
void X_CH_CalculateDistance(const refdef_t *fd);

// Draw damage

void X_DMG_Init(void);
void X_DMG_ParseSnapshotDamage(void);
void X_DMG_DrawDamage(const refdef_t *fd);
void X_DMG_PushDamageForDirectHit(int clientNum, const vec3_t origin);
void X_DMG_PushDamageForEntity(refEntity_t *ref);
void X_DMG_PushSplashDamageForDirectHit(const vec3_t origin);

// Player state

void X_GS_Init(void);

void X_GS_UpdateGameStateOnConfigStringModified(int index);
void X_GS_UpdatePlayerStateBySnapshot(snapshot_t *snapshot);
void X_GS_UpdateEntityPosition(int entity, const vec3_t origin);
void X_GS_UpdatePlayerScores(void);
void X_GS_UpdateOnOvertime(const char *msg);
qboolean X_GS_UpdatePlayerXStats1(void);

int X_GS_GetClientIDByOrigin(vec3_t origin);
qboolean X_GS_IsClientDeadByOrigin(vec3_t origin);

qboolean X_GS_IsIntermission(void);

XEntity *X_GS_GetEntityFromCache(int num);
XEntity *X_GS_GetPlayerEntityFromCacheByOrigin(vec3_t origin);

XPlayerState *X_GS_GetStateByClientId(int client);

// Team

void X_Team_Init(void);
void X_Team_CustomizeFoe(refEntity_t *ref);
void X_Team_CustomizeFreezeEffect(refEntity_t *ref);
PlayerModel X_Team_IsPlayerModel(qhandle_t model);
qboolean X_Team_ClientIsInSameTeam(int client);
void X_Team_ValidateFrozenPlayers(const refdef_t *fd);

// Weapon

void X_WP_Init(void);

// Network

void X_Net_Init(void);
void X_Net_Deinit(void);
void X_Net_RenewPortOnSnapshot(snapshot_t *snapshot);
void X_Net_CheckScanPortTimeout(void);
void X_Net_DrawScanProgress(void);

// Players

void X_PS_Init(void);
void X_PS_CustomizePlayerModel(refEntity_t *ref);
void X_PS_AutoRevival(snapshot_t *snapshot);

// Console

void X_Con_Init(void);
qboolean X_Con_OnChatMessage(const char *text, int client);
void X_Con_OnPlayerDeath(int client1, int client2, int reason);
void X_Con_PrintToChatSection(const char *fmt, ...);

void X_Cl_Con_OverlayPrint(const char *txt);

void X_Con_OnLocalChatCommand(field_t *field);

// Hud

void X_Hud_Init(void);
void X_Hud_Destroy(void);
void X_Hud_ValidateDefaultScores(void);
void X_Hud_DrawHud(void);
void X_Hud_HackTurnOffDefaultScores(snapshot_t *snapshot);
void X_Hud_CustomizeConsoleFont(qhandle_t *shader);
qboolean X_Hud_HideOnXScore(void);
void X_Hud_DrawString(float x, float y, float size, vec4_t rgba, int flags, qhandle_t shader, const char *string);
qboolean X_Hud_UpdatePlayerStats(void);

void X_Hud_InitProgressBar(XUIProgressBar *bar, vec4_t color1, vec4_t color2, float x, float y, float w, float h, float range);
void X_Hud_DrawProgressBar(XUIProgressBar *bar, float value);
void X_Hud_DrawProgressBarInCenter(XUIProgressBar *bar, float value);

void X_Hud_InitFading(XUIFading *fad, int interval, float min, float max);
void X_Hud_ResetFading(XUIFading *fad);
float X_Hud_GetFadingAlpha(XUIFading *fad);

// Render

void R_UpdateShaderColorByHandle(qhandle_t hShader, vec3_t color);

// Misc

void X_MakeStringSymbolic(char *str);

void X_InitCustomColor(cvar_t *cvar, XCustomColor *color);
qboolean X_IsCustomColorActive(XCustomColor *color);

qboolean VectorEqualInRange(vec3_t first, vec3_t second, float range);

void X_RemoveEffectsFromName(char *name);

char *X_GetConfigString(int index);

qboolean X_IsNoWorldRender(const refdef_t *fd);

qboolean X_DecryptMessage(char *text);
void X_SendEncryptedMessage(char *text);