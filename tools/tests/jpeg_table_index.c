/* Table-index contract for the actual vendor routine; compiled as C. */
#include "../../code/libjpeg/jdmarker.c"
#include <assert.h>
#include <setjmp.h>

static jmp_buf table_error;

static void table_error_exit( j_common_ptr cinfo )
{
	int index = *(const int *)cinfo->client_data;
	if ( index & 0x10 ) index -= 0x10;
	assert( index >= NUM_HUFF_TBLS );
	assert( cinfo->err->msg_code == JERR_DHT_INDEX );
	assert( cinfo->err->msg_parm.i[0] == index );
	longjmp( table_error, 1 );
}

static void table_message( j_common_ptr cinfo, int level )
{
	(void)cinfo;
	(void)level;
}

JHUFF_TBL *jpeg_alloc_huff_table( j_common_ptr cinfo )
{
	(void)cinfo;
	assert( 0 ); /* Every legal slot already has a table. */
	return NULL;
}

static void check_table_index( int index )
{
	struct jpeg_decompress_struct cinfo = { 0 };
	struct jpeg_error_mgr error = { 0 };
	struct jpeg_source_mgr source = { 0 };
	JHUFF_TBL tables[2][NUM_HUFF_TBLS];
	JOCTET bytes[20] = { 0, 20, 0, 1 };
	int ac = (index & 0x10) != 0, slot = ac ? index - 0x10 : index;
	int i;
	memset( tables, 0x7a, sizeof( tables ) );
	bytes[2] = (JOCTET)index;
	bytes[19] = 42;
	source.next_input_byte = bytes;
	source.bytes_in_buffer = sizeof( bytes );
	error.error_exit = table_error_exit;
	error.emit_message = table_message;
	cinfo.err = &error;
	cinfo.src = &source;
	cinfo.client_data = &index;
	for ( i = 0; i < NUM_HUFF_TBLS; i++ ) {
		cinfo.dc_huff_tbl_ptrs[i] = &tables[0][i];
		cinfo.ac_huff_tbl_ptrs[i] = &tables[1][i];
	}
	if ( setjmp( table_error ) ) {
		assert( slot >= NUM_HUFF_TBLS );
		return;
	}
	assert( get_dht( &cinfo ) );
	assert( slot < NUM_HUFF_TBLS );
	assert( source.bytes_in_buffer == 0 );
	for ( i = 0; i < NUM_HUFF_TBLS; i++ ) {
		assert( tables[0][i].bits[1] == ((!ac && i == slot) ? 1 : 0x7a) );
		assert( tables[1][i].bits[1] == ((ac && i == slot) ? 1 : 0x7a) );
	}
	assert( tables[ac][slot].huffval[0] == 42 );
}

void TestJpegTables( void )
{
	int i;
	const int invalid[] = { 4, 5, 0x14, 0x15, 255 };
	for ( i = 0; i < NUM_HUFF_TBLS; i++ ) {
		check_table_index( i );
		check_table_index( i | 0x10 );
	}
	for ( i = 0; i < (int)(sizeof( invalid ) / sizeof( invalid[0] )); i++ )
		check_table_index( invalid[i] );
}

int main( void )
{
	TestJpegTables();
	return 0;
}
