#include "client.h"
#include "x_local.h"

// ====================
//   CVars

static cvar_t *x_hck_dmg_draw = 0;

// ====================
//   Const vars

static char X_HELP_DMG_DRAW[] = "\n ^fx_hck_dmg_draw^5 0|..|4^7\n\n"
								"   Show a damage when you hit an enemy\n";

// ====================
//   Static routines

static int GetClientNumByEntityNum(int entityNum);
static void DrawDamageIcon(refdef_t *fd, int inx);
static void AddDamageNumberToScene(const vec3_t origin, const byte *color, int value, const vec3_t axis, float radius);
static void AddDamageHitToScene(const vec3_t origin, float radius);
static void ChooseDamageColor(int damage, byte *rgba);

// ====================
//   Implementation

void X_DMG_Init()
{
	Damage *dmg = &xmod.dmg;
	dmg->type = DmgNone;
	dmg->printed = qfalse;
	dmg->duration = 600; // ms
	dmg->redirDuration = 200;

	X_Main_RegisterHackXCommand(x_hck_dmg_draw, "4", "0", "4", X_HELP_DMG_DRAW);
}

void X_DMG_ParseSnapshotDamage()
{
	if (!X_Main_IsXModeHackCommandActive(x_hck_dmg_draw))
	{
		return;
	}

	if (!x_hck_dmg_draw->integer)
	{
		return;
	}
	
	snapshot_t *snapshot = &xmod.snap;
	Damage *dmg = &xmod.dmg;
	int total = snapshot->ps.persistant[PERS_HITS];
	//int total = snapshot->ps.persistant[PERS_ATTACKEE_ARMOR] & 0xff;
	//int test = snapshot->ps.persistant[PERS_ATTACKEE_ARMOR] & 0xff;
	//Com_Printf("test: %d\n", test);
	dmg->type = DmgNone;
	dmg->printed = qfalse;
	dmg->damage = 0;

	if (dmg->clientNum != snapshot->ps.clientNum)
	{
		dmg->clientNum = snapshot->ps.clientNum;
		dmg->total = total;
		return;
	}

	if (!total || dmg->total > total)
	{
		dmg->total = total;
		
	}

	if (dmg->total == total)
	{
		return;
	}

	dmg->damage = total - dmg->total;
	dmg->type = DmgUnknown;

	// Try to find a direct hit target
	for (int i = 0; i < snapshot->numEntities; i++)
	{
		int event = 0;
		if (snapshot->entities[i].eType >= ET_EVENTS)
		{
			event = snapshot->entities[i].eType - ET_EVENTS;
		}
		else
		{
			if (!snapshot->entities[i].event)
			{
				continue;
			}

			event = snapshot->entities[i].event & ~EV_EVENT_BITS;
		}

		if (event == EV_MISSILE_HIT || event == EV_BULLET_HIT_FLESH)
		{
			xmod.dmg.type = DmgDirect;
			dmg->target = GetClientNumByEntityNum(snapshot->entities[i].otherEntityNum);
			break;
		}

		if (event == EV_MISSILE_MISS && xmod.dmg.type != DmgMissle)
		{
			xmod.dmg.type = DmgMissle;    
		}

		if (event == EV_RAILTRAIL && dmg->clientNum == snapshot->entities[i].clientNum)
		{
			xmod.dmg.type = DmgRail;
			break;
		}

		if (event == EV_SHOTGUN && dmg->clientNum == snapshot->entities[i].otherEntityNum)
		{
			xmod.dmg.type = DmgShotgun;
			break;
		}
	}

	dmg->total = total;
}

void X_DMG_PushDamageForEntity(refEntity_t *ref)
{
	XModResources *rs = &xmod.rs;

	if (ref->reType != RT_MODEL)
	{
		return;
	}

	if (xmod.dmg.type == DmgMissle
		&& ref->hModel && rs->modelMissle
		&& ref->hModel == rs->modelMissle)
	{
		X_DMG_PushSplashDamageForDirectHit(ref->origin);
	}
	else if (xmod.dmg.type == DmgRail
			 && ref->hModel && rs->modelRail
			 && ref->hModel == rs->modelRail)
	{
		X_DMG_PushSplashDamageForDirectHit(ref->origin);
	}
	else if (xmod.dmg.type == DmgShotgun
			 && ref->hModel && rs->modelBullet
			 && ref->hModel == rs->modelBullet)
	{
		X_DMG_PushSplashDamageForDirectHit(ref->origin);
	}
}

void X_DMG_PushDamageForDirectHit(int clientNum, const vec3_t origin)
{
	Damage *dmg = &xmod.dmg;

	if (dmg->type != DmgDirect || xmod.dmg.printed)
	{
		return;
	}

	if (dmg->target != clientNum)
	{
		return;
	}

	X_DMG_PushSplashDamageForDirectHit(origin);
}

void X_DMG_PushSplashDamageForDirectHit(const vec3_t origin)
{
	Damage *dmg = &xmod.dmg;

	if (dmg->type == DmgNone)
	{
		return;
	}

	if (xmod.dmg.printed)
	{
		return;
	}

	int currentMs = Com_Milliseconds();
	if (dmg->lastRedir + dmg->redirDuration < currentMs)
	{
		dmg->dir = (dmg->dir == 1 ? -1 : 1);
	}

	dmg->lastRedir = currentMs;

	DamageIcon *icon = dmg->icons + (dmg->iconNum++ % countof(dmg->icons));
	icon->value = dmg->damage;
	icon->start = Com_Milliseconds();
	icon->dir = dmg->dir;
	icon->params[0] = 10 + rand() % 10;
	icon->params[1] = ((rand() % 2) == 1 ? 1.0f : -1.0f);
	icon->params[2] = 0.1 * (1 + (rand() % 12));
	VectorCopy(origin, icon->origin);

	ChooseDamageColor(dmg->damage, icon->color);

	xmod.dmg.printed = qtrue;
}

static void PushUnclassifiedDamage(const refdef_t *fd)
{
	if (xmod.dmg.printed)
	{
		return;
	}

	vec3_t start, end;
	trace_t trace;

	VectorCopy(fd->vieworg, start);
	VectorMA(start, 131072, fd->viewaxis[0], end);

	CM_BoxTrace(&trace, start, end, vec3_origin, vec3_origin, 0, CONTENTS_SOLID | CONTENTS_BODY, qfalse);

	X_DMG_PushSplashDamageForDirectHit(trace.endpos);
}

static void ChooseDamageColor(int damage, byte *rgba)
{
	if (damage <= 25)
	{
		MAKERGBA(rgba, 80, 255, 10, 255);
	}
	else if (damage <= 50)
	{
		MAKERGBA(rgba, 250, 250, 10, 255);
	}
	else if (damage <= 75)
	{
		MAKERGBA(rgba, 250, 170, 10, 255);
	}
	else if (damage <= 100)
	{
		MAKERGBA(rgba, 250, 25, 10, 255);
	}
	else if (damage <= 150)
	{
		MAKERGBA(rgba, 250, 15, 150, 255);
	}
	else if (damage <= 200)
	{
		MAKERGBA(rgba, 200, 15, 254, 255);
	}
	else
	{
		MAKERGBA(rgba, 128, 128, 255, 255);
	}
}

static int GetClientNumByEntityNum(int entityNum)
{
	snapshot_t *snapshot = &xmod.snap;

	for (int i = 0; i < snapshot->numEntities; i++)
	{
		if (snapshot->entities[i].eType == ET_PLAYER && snapshot->entities[i].number == entityNum)
		{
			return snapshot->entities[i].clientNum;
		}
	}

	return -1;
}

void X_DMG_DrawDamage(const refdef_t *fd)
{
	Damage *dmg = &xmod.dmg;

	PushUnclassifiedDamage(fd);

	int peak = (int) (dmg->iconNum % countof(dmg->icons));
	for (int i = peak; i < countof(dmg->icons); i++)
	{
		DrawDamageIcon((refdef_t *) fd, i);
	}

	for (int i = 0; i < peak; i++)
	{
		DrawDamageIcon((refdef_t *) fd, i);
	}
}

static void SetDamageIconPosition(DamageIcon *icon, vec3_t origin, const vec3_t viewaxis, int delataMs)
{
	Damage *dmg = &xmod.dmg;

	float step = 2.0f / dmg->duration;
	float x = (delataMs * step);
	float y = -(x * x) + (2 * x);
	
	switch (x_hck_dmg_draw->integer)
	{
		case 1:
			x /= (step * 15) * icon->dir;
			y /= (step * 8);
			break;
		case 2:
			
			x /= (step * 15) * icon->dir;
			y /= (step * icon->params[0]);
			break;
		case 3:
			step = (2.0f - icon->params[2]) / dmg->duration;
			x = (delataMs * step);
			y = -(x * x) + (2 * x);
			x /= (step * 15) * icon->params[1];
			y /= (step * icon->params[0]);
			break;
		case 4:
			step = (2.0f - icon->params[2]) / dmg->duration;
			x = (delataMs * step);
			y = -(x * x) + (2 * x);
			x /= (step * 40) * icon->params[1];
			y /= (step * icon->params[0]);
			break;
		default:
			x /= (step * 15) * icon->dir;
			y /= (step * 20);
			break;
	}

	VectorMA(origin, x, viewaxis, origin);
	origin[2] += 50 + y;

	int end = dmg->duration - 0xFF;
	if (delataMs > end)
	{
		icon->color[3] = 0xFF - (delataMs - end);
	}

	if (icon->value >= 100)
	{
		icon->value = icon->value;
	}
}

static void DrawDamageIcon(refdef_t *fd, int inx)
{
	Damage *dmg = &xmod.dmg;
	DamageIcon *icon = dmg->icons + inx;

	if (!icon->value)
	{
		return;
	}

	int currentMs = Com_Milliseconds();
	if (icon->start + dmg->duration <= currentMs)
	{
		icon->value = 0;
		return;
	}

	float radius = ((Distance(fd->vieworg, icon->origin) / 100.f) / 2) + 5;

	vec3_t origin;
	VectorCopy(icon->origin, origin);

	SetDamageIconPosition(icon, origin, fd->viewaxis[1], (currentMs - icon->start));

	if (icon->value == 1)
	{
		AddDamageHitToScene(origin, radius);
	}
	else
	{
		AddDamageNumberToScene(origin, icon->color, icon->value, fd->viewaxis[1], radius);
	}
}

static void AddDamageNumberToScene(const vec3_t origin, const byte *color, int value, const vec3_t axis, float radius)
{
	for (int i = 0; i < 10; i++)
	{
		if (!value)
		{
			break;
		}

		int num = value % 10;
		value /= 10;

		refEntity_t ent;
		memset(&ent, 0, sizeof(ent));
		ent.reType = RT_SPRITE;
		ent.customShader = xmod.rs.shaderNumbers[num];
		ent.radius = radius;
		ent.renderfx = RF_DEPTHHACK;
		ent.shader.rgba[0] = color[0];
		ent.shader.rgba[1] = color[1];
		ent.shader.rgba[2] = color[2];
		ent.shader.rgba[3] = color[3];

		float mult = radius * 1.5;

		VectorCopy(origin, ent.origin);
		VectorMA(ent.origin, i * mult, axis, ent.origin);

		Original_AddRefEntityToScene(&ent, qfalse);
	}
}

static void AddDamageHitToScene(const vec3_t origin, float radius)
{
	refEntity_t ent;
	memset(&ent, 0, sizeof(ent));
	ent.reType = RT_SPRITE;
	ent.customShader = xmod.rs.shaderOneHPHit;
	ent.radius = radius;
	ent.renderfx = RF_DEPTHHACK;
	ent.shader.rgba[0] = 255;
	ent.shader.rgba[1] = 255;
	ent.shader.rgba[2] = 255;
	ent.shader.rgba[3] = 255;

	VectorCopy(origin, ent.origin);

	Original_AddRefEntityToScene(&ent, qfalse);
}
