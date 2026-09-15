/* gcc -O2 -fno-strict-aliasing -fsanitize=address,pointer-compare
 * -ffunction-sections -fdata-sections tools/tests/extension_validation.c
 * code/qcommon/files.c code/qcommon/q_shared.c -Wl,--gc-sections
 * -o /tmp/extension-test
 * ASAN_OPTIONS=detect_invalid_pointer_pairs=2 /tmp/extension-test
 */
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
	const char *names[] = { "no_extension", "a", "", ".", ".x" };
	const char *ext = NULL;
	unsigned int i;

	for ( i = 0; i < sizeof( names ) / sizeof( names[0] ); i++ )
		assert( FS_AllowedExtension( names[i], qfalse, NULL ) == qtrue );
	assert( FS_AllowedExtension( "module.so.1", qfalse, &ext ) == qfalse );
	assert( strcmp( ext, "so.1" ) == 0 );
	assert( FS_AllowedExtension( ".SO.9", qfalse, NULL ) == qfalse );
	assert( FS_AllowedExtension( "a.1", qfalse, NULL ) == qtrue );
	assert( FS_AllowedExtension( "data.pk3", qtrue, NULL ) == qtrue );
	assert( FS_AllowedExtension( "data.pk3", qfalse, NULL ) == qfalse );
	return 0;
}
