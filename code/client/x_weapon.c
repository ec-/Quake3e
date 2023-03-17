#include "client.h"
#include "x_local.h"

// ====================
//   CVars

static cvar_t *x_wp_mod_lightning = 0;
static cvar_t *x_wp_mod_lightning_width = 0;

static cvar_t *x_wp_autoswitch = 0;

// ====================
//   Const vars

static char X_HELP_WP_MOD_LIGHTNING[] = "\n ^fx_wp_mod_lightning^5 0|1^7\n\n"
										"   Enable alternative display of the LG (thin shaft).\n";
static char X_HELP_WP_MOD_LIGHTNING_WIDTH[] = "\n ^fx_wp_mod_lightning_width^5 0|...|50^7\n\n"
											  "   Adjust the width of the LG lightning ray.\n";

static char X_HELP_WP_AUTOSWITCH[] = "\n ^fx_wp_autoswitch^5 0|...|9^7\n\n"
									 "   Autoswitch to specified weapon after respawn.\n"
									 "   ^50^7 - disable autoswitch,^5 1|...|9^7 - weapon number.\n";
// ====================
//   Static routines

// ====================
//   Implementation

void X_WP_Init()
{
	X_Main_RegisterXCommand(x_wp_mod_lightning, "0", "0", "1", X_HELP_WP_MOD_LIGHTNING);

	X_Main_RegisterXCommand(x_wp_mod_lightning_width, "0", "0", "50", X_HELP_WP_MOD_LIGHTNING_WIDTH);

	X_Main_RegisterXCommand(x_wp_autoswitch, "0", "0", "9", X_HELP_WP_AUTOSWITCH);
}

/* =====================
	Purpose:
	Autoswitch weapon after respawn,
	if specified weapon is able to be selected.
   ===================== */
void X_WP_CheckAutoswitchRequirement()
{
	static qboolean isPlayerDead = qfalse;
	static int deathTime = 0;

	// is autoswitching enabled?
	if (x_wp_autoswitch->integer == WP_NONE)
		return;

	// checking if player is dead
	if (xmod.snap.ps.stats[STAT_HEALTH] <= 0)
	{
		isPlayerDead = qtrue;
		deathTime = xmod.snap.serverTime;
		return;
	}
	else if (isPlayerDead // player was dead and has been respawned
			 && xmod.snap.ps.stats[STAT_HEALTH] > 0 // autoswitch is not working without a little delaying
			 && deathTime + 150 < xmod.snap.serverTime)
	{

		isPlayerDead = qfalse;

		// checking if specified weapon is able to be selected
		weapon_t pendingWeapon = x_wp_autoswitch->integer;

		// we are does not have this weapon
		if (!(xmod.snap.ps.stats[STAT_WEAPONS] & (1 << pendingWeapon)))
			return;

		// send weapon switch command
		CL_SendConsoleCommand(va("weapon %d", pendingWeapon));

		return;
	}
}