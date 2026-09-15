/* Run from the repository root:
 * make -j8 BUILD_CLIENT=0 USE_SDL=0 USE_CURL=0 BUILD_DIR=/tmp/native-dispatch \
 *   CC=clang LDFLAGS='tools/tests/native_dispatch.c -Wl,--wrap=main -lm -ldl'
 * /tmp/native-dispatch/release-linux-x86_64/quake3e.ded.x64
 */
#include "../../code/qcommon/q_shared.h"
#include "../../code/qcommon/qcommon.h"
#include "../../code/qcommon/vm_local.h"

static int expected[3];

static intptr_t QDECL entry( int command, int a, int b, int c ) {
	if ( command != 42 || a != expected[0] || b != expected[1] || c != expected[2] ) {
		fprintf( stderr, "FAIL: native entry received stale arguments\n" );
		return -1;
	}
	return 17;
}

int __wrap_main( void ) {
	vm_t vm = {0};
	vm.entryPoint = entry;
	const int counts[] = { 3, 0, 1, 2 };
	for ( unsigned index = 0; index < sizeof(counts)/sizeof(counts[0]); index++ ) {
		int count = counts[index];
		for ( int i = 0; i < 3; i++ ) expected[i] = i < count ? 11 + i : 0;
		intptr_t result;
		switch ( count ) {
		case 0: result = VM_Call( &vm, 0, 42 ); break;
		case 1: result = VM_Call( &vm, 1, 42, 11 ); break;
		case 2: result = VM_Call( &vm, 2, 42, 11, 12 ); break;
		default: result = VM_Call( &vm, 3, 42, 11, 12, 13 ); break;
		}
		if ( result != 17 || vm.callLevel != 0 ) return 1;
	}
	puts( "PASS: native dispatch initializes unused slots for counts 0 through 3" );
	return 0;
}
