/* Standalone callback ABI regression: no content input. */
#include "../../code/qcommon/unzip.c"
#include <assert.h>

void *S_Malloc( size_t size )
{
	void *p = calloc( 1, size );
	assert( p );
	return p;
}

void Z_Free( void *p )
{
	free( p );
}

int main( void )
{
	z_stream stream = { 0 };
	assert( inflateInit2( &stream, 15 ) == Z_OK );
	assert( stream.state != NULL );
	assert( inflateEnd( &stream ) == Z_OK );
	assert( stream.state == NULL );
	return 0;
}
