/* Run from the repository root:
 * make -j8 BUILD_CLIENT=0 USE_SDL=0 USE_CURL=0 BUILD_DIR=/tmp/bot-move-result \
 *   LDFLAGS='tools/tests/bot_move_result.c -Wl,--wrap=main -lm -ldl'
 * /tmp/bot-move-result/release-linux-x86_64/quake3e.ded.x64
 */
#include "../../code/qcommon/q_shared.h"
#include "../../code/botlib/botlib.h"
#include "../../code/botlib/be_ai_goal.h"
#include "../../code/botlib/be_ai_move.h"

extern botlib_import_t botimport;
static int diagnostics;

static void QDECL print( int type, const char *format, ... ) {
	(void)type;
	(void)format;
	diagnostics++;
}

int __wrap_main( void ) {
	bot_moveresult_t result;
	const bot_moveresult_t empty = {0};
	const int poisons[] = { 0x55, 0xA5 };
	botimport.Print = print;
	for ( unsigned i = 0; i < sizeof(poisons)/sizeof(poisons[0]); i++ ) {
		memset( &result, poisons[i], sizeof( result ) );
		BotMoveToGoal( &result, 0, NULL, 0 );
		if ( memcmp( &result, &empty, sizeof( result ) ) ) {
			fprintf( stderr, "FAIL: BotMoveToGoal left stale output fields\n" );
			return 1;
		}
	}
	assert( diagnostics == 2 );
	puts( "PASS: BotMoveToGoal initializes the complete result" );
	return 0;
}
