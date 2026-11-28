/* gcc -O2 -ffunction-sections -fdata-sections \
 * tools/tests/vulkan_light_grid.c -Wl,--gc-sections -lm -o /tmp/vulkan-light-grid
 * /tmp/vulkan-light-grid
 */
#include "../../code/renderervk/tr_bsp.c"
#include <assert.h>

refimport_t ri;
trGlobals_t tr;
static cvar_t mapOverBrightBits;
cvar_t *r_mapOverBrightBits = &mapOverBrightBits;
static cvar_t mapGreyScale;
cvar_t *r_mapGreyScale = &mapGreyScale;

static byte samples[30 * 3 * 3 * 8];
static byte lightGrid[sizeof( samples )];

static void QDECL Print( printParm_t level, const char *fmt, ... ) {
	(void)level;
	fputs( fmt, stderr );
}

static void *Alloc( size_t size, ha_pref pref ) {
	assert( size <= sizeof( lightGrid ) );
	assert( pref == h_low );
	return lightGrid;
}

static void CheckLightGrid( float gridSize, float min, float max, int count, float origin ) {
	lump_t lump = { 0, count * 3 * 3 * 8 };
	bmodel_t worldModel = { 0 };

	worldModel.bounds[0][0] = min;
	worldModel.bounds[1][0] = max;
	worldModel.bounds[0][1] = -64.0f;
	worldModel.bounds[1][1] = 64.0f;
	worldModel.bounds[0][2] = -128.0f;
	worldModel.bounds[1][2] = 128.0f;
	s_worldData.bmodels = &worldModel;
	VectorSet( s_worldData.lightGridSize, gridSize, 64.0f, 128.0f );
	fileBase = samples;

	R_LoadLightGrid( &lump );

	assert( s_worldData.lightGridData == lightGrid );
	assert( s_worldData.lightGridBounds[0] == count );
	assert( s_worldData.lightGridBounds[1] == 3 );
	assert( s_worldData.lightGridBounds[2] == 3 );
	assert( s_worldData.lightGridOrigin[0] == origin );
}

int main( void ) {
	ri.Printf = Print;
	ri.Hunk_Alloc = Alloc;

	CheckLightGrid( 64.0f, -64.0f, 64.0f, 3, -64.0f );
	CheckLightGrid( 48.1f, -299.0f, 1121.0f, 30, -6.0f * 48.1f );
	puts( "PASS: Vulkan light grid with standard and fractional spacing" );
	return 0;
}
