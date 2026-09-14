/* Standalone FS_AllowedExtension output contract regression. */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include <assert.h>

void QDECL Com_Error( errorParm_t level, const char *fmt, ... )
{
	(void)level;
	(void)fmt;
	abort();
}

int main( void )
{
	const struct { const char *name, *extension; qboolean allowed; } cases[] = {
		{ "no_extension", "", qtrue },
		{ "", "", qtrue },
		{ ".", "", qtrue },
		{ "a.txt", "txt", qtrue },
		{ ".SO.9", "SO.9", qfalse },
		{ "module.so.1", "so.1", qfalse },
		{ "data.pk3", "pk3", qfalse }
	};
	const char *extension = NULL;
	unsigned int i;
	for ( i = 0; i < ARRAY_LEN( cases ); i++ ) {
		extension = NULL;
		assert( FS_AllowedExtension( cases[i].name, qfalse, &extension ) == cases[i].allowed );
		assert( extension && !strcmp( extension, cases[i].extension ) );
	}
	extension = NULL;
	assert( FS_AllowedExtension( "data.pk3", qtrue, &extension ) == qtrue );
	assert( extension && !strcmp( extension, "pk3" ) );
	return 0;
}
