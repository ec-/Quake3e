#include "x_local.h"

// ====================
//   CVars

static cvar_t* x_wp_mod_lightning = 0;
static cvar_t* x_wp_mod_lightning_width = 0;

// ====================
//   Const vars

static char X_HELP_WP_MOD_LIGHTNING[]		= "\n ^fx_wp_mod_lightning^5 0|1^7\n\n"
											  "   Enable alternative display of the LG (thin shaft).\n";
static char X_HELP_WP_MOD_LIGHTNING_WIDTH[] = "\n ^fx_wp_mod_lightning_width^5 0|...|50^7\n\n"
											  "   Adjust the width of the LG lightning ray.\n";
// ====================
//   Static routines


// ====================
//   Implementation

void X_WP_Init()
{
	RegisterXCommand(x_wp_mod_lightning, "0", "0", "1", X_HELP_WP_MOD_LIGHTNING);

	RegisterXCommand(x_wp_mod_lightning_width, "0", "0", "50", X_HELP_WP_MOD_LIGHTNING_WIDTH);
}
