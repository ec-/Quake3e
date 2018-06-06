// console history handling functions

#include "q_shared.h"
#include "qcommon.h"

static      qboolean historyLoaded = qfalse;

#define     COMMAND_HISTORY 32

field_t     historyEditLines[COMMAND_HISTORY];

int         nextHistoryLine; // the last line in the history buffer, not masked
int         historyLine;     // the line being displayed from history buffer
                                 // will be <= nextHistoryLine

#define     MAX_CONSOLE_SAVE_BUFFER ( COMMAND_HISTORY * (MAX_EDIT_LINE + 13) )

static void Con_LoadHistory( void );
static void Con_SaveHistory( void );


/*
================
Con_ResetHistory
================
*/
void Con_ResetHistory( void )
{
	historyLoaded = qfalse;
	nextHistoryLine = 0;
	historyLine = 0;
}


/*
================
Con_SaveField
================
*/
void Con_SaveField( const field_t *field )
{
	const field_t *h;

	if ( !field || field->buffer[0] == '\0' )
		return;

	if ( historyLoaded == qfalse ) {
		historyLoaded = qtrue;
		Con_LoadHistory();
	}

	// try to avoid inserting duplicates
	if ( nextHistoryLine > 0 ) {
		h = &historyEditLines[(nextHistoryLine-1) % COMMAND_HISTORY];
		if ( field->cursor == h->cursor && field->scroll == h->scroll && !strcmp( field->buffer, h->buffer ) ) {
			historyLine = nextHistoryLine;
			return;
		}
	}

	historyEditLines[nextHistoryLine % COMMAND_HISTORY] = *field;
	nextHistoryLine++;
	historyLine = nextHistoryLine;

	Con_SaveHistory();
}


/*
================
Con_HistoryGetPrev

returns qtrue if previously returned edit field needs to be updated
================
*/
qboolean Con_HistoryGetPrev( field_t *field )
{
	qboolean bresult;

	if ( historyLoaded == qfalse ) {
		historyLoaded = qtrue;
		Con_LoadHistory();
	}

	if ( nextHistoryLine - historyLine < COMMAND_HISTORY && historyLine > 0 ) {
		bresult = qtrue;
		historyLine--;
	} else {
		bresult = qfalse;
	}

	*field = historyEditLines[ historyLine % COMMAND_HISTORY ];

	return bresult;
}


/*
================
Con_HistoryGetNext

returns qtrue if previously returned edit field needs to be updated
================
*/
qboolean Con_HistoryGetNext( field_t *field )
{
	qboolean bresult;

	if ( historyLoaded == qfalse ) {
		historyLoaded = qtrue;
		Con_LoadHistory();
	}

	historyLine++;

	if ( historyLine >= nextHistoryLine ) {
		if ( historyLine == nextHistoryLine )
			bresult = qtrue;
		else
			bresult = qfalse;
		historyLine = nextHistoryLine;
		Field_Clear( field );
		return bresult;
	}

	*field = historyEditLines[ historyLine % COMMAND_HISTORY ];

	return qtrue;
}


/*
================
Con_LoadHistory
================
*/
static void Con_LoadHistory( void )
{
	char consoleSaveBuffer[ MAX_CONSOLE_SAVE_BUFFER ];
	int  consoleSaveBufferSize;
	const char *token, *text_p;
	int i, numChars, numLines = 0;
	field_t *edit;
	fileHandle_t f;

	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
	}

	consoleSaveBufferSize = FS_Home_FOpenFileRead( CONSOLE_HISTORY_FILE, &f );
	if ( f == FS_INVALID_HANDLE )
	{
		Com_Printf( "Couldn't read %s.\n", CONSOLE_HISTORY_FILE );
		return;
	}

	if ( consoleSaveBufferSize < MAX_CONSOLE_SAVE_BUFFER &&
			FS_Read( consoleSaveBuffer, consoleSaveBufferSize, f ) == consoleSaveBufferSize )
	{
		consoleSaveBuffer[ consoleSaveBufferSize ] = '\0';
		text_p = consoleSaveBuffer;

		for( i = COMMAND_HISTORY - 1; i >= 0; i-- )
		{
			if ( !*( token = COM_Parse( &text_p ) ) )
				break;

			edit = &historyEditLines[ i ];

			edit->cursor = atoi( token );

			if ( !*( token = COM_Parse( &text_p ) ) )
				break;

			edit->scroll = atoi( token );

			if( !*( token = COM_Parse( &text_p ) ) )
				break;

			numChars = atoi( token );
			text_p++;
			if ( numChars > ( consoleSaveBufferSize - ( text_p - consoleSaveBuffer ) ) || numChars >= sizeof( edit->buffer ) )
			{
				Com_DPrintf( S_COLOR_YELLOW "WARNING: probable corrupt history\n" );
				break;
			}

			if ( edit->cursor > sizeof( edit->buffer ) - 1 )
				edit->cursor = sizeof( edit->buffer ) - 1;
			else if ( edit->cursor < 0 )
				edit->cursor = 0;

			if ( edit->scroll > edit->cursor )
				edit->scroll = edit->cursor;
			else if ( edit->scroll < 0 )
				edit->scroll = 0;

			Com_Memcpy( edit->buffer, text_p, numChars );
			edit->buffer[ numChars ] = '\0';
			text_p += numChars;

			numLines++;
		}

		memmove( &historyEditLines[ 0 ], &historyEditLines[ i + 1 ],
				numLines * sizeof( field_t ) );
		for( i = numLines; i < COMMAND_HISTORY; i++ )
			Field_Clear( &historyEditLines[ i ] );

		historyLine = nextHistoryLine = numLines;
	}
	else
		Com_Printf( "Couldn't read %s.\n", CONSOLE_HISTORY_FILE );

	FS_FCloseFile( f );
}


/*
================
Con_SaveHistory
================
*/
static void Con_SaveHistory( void )
{
	char            consoleSaveBuffer[ MAX_CONSOLE_SAVE_BUFFER ];
	int             consoleSaveBufferSize;
	int             i;
	int             lineLength, saveBufferLength, additionalLength;
	fileHandle_t    f;

	consoleSaveBuffer[ 0 ] = '\0';

	i = ( nextHistoryLine - 1 + COMMAND_HISTORY ) % COMMAND_HISTORY;
	do
	{
		if( historyEditLines[ i ].buffer[ 0 ] )
		{
			lineLength = strlen( historyEditLines[ i ].buffer );
			saveBufferLength = strlen( consoleSaveBuffer );

			//ICK
			additionalLength = lineLength + 13; // strlen( "999 999 999  " )

			if( saveBufferLength + additionalLength < MAX_CONSOLE_SAVE_BUFFER )
			{
				Q_strcat( consoleSaveBuffer, MAX_CONSOLE_SAVE_BUFFER,
						va( "%d %d %d %s ",
						historyEditLines[ i ].cursor,
						historyEditLines[ i ].scroll,
						lineLength,
						historyEditLines[ i ].buffer ) );
			}
			else
				break;
		}
		i = ( i - 1 + COMMAND_HISTORY ) % COMMAND_HISTORY;
	}
	while( i != ( nextHistoryLine - 1 + COMMAND_HISTORY ) % COMMAND_HISTORY );

	consoleSaveBufferSize = strlen( consoleSaveBuffer );

	f = FS_FOpenFileWrite( CONSOLE_HISTORY_FILE );
	if( f == FS_INVALID_HANDLE )
	{
		Com_Printf( "Couldn't write %s.\n", CONSOLE_HISTORY_FILE );
		return;
	}

	if( FS_Write( consoleSaveBuffer, consoleSaveBufferSize, f ) < consoleSaveBufferSize )
		Com_Printf( "Couldn't write %s.\n", CONSOLE_HISTORY_FILE );

	FS_FCloseFile( f );
}
