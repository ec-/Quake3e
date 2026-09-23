/* clang -fsanitize=address,undefined -g tools/tests/huffman_alignment.c
 * code/qcommon/huffman_static.c -o /tmp/huffman-test
 * UBSAN_OPTIONS=halt_on_error=1 /tmp/huffman-test
 */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include <assert.h>

int main( void )
{
	byte buffer[16];
	unsigned int decoded;
	int offset, symbol, written, read;

	for ( offset = 0; offset < 32; offset++ )
	{
		for ( symbol = 0; symbol < 256; symbol++ )
		{
			memset( buffer, 0, sizeof( buffer ) );
			written = HuffmanPutSymbol( buffer, offset, symbol );
			read = HuffmanGetSymbol( &decoded, buffer, offset );
			assert( decoded == (unsigned int)symbol );
			assert( read == written );
		}
	}
	return 0;
}
