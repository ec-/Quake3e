#include <string.h>
#include "server.h"

#define MAX_FILTER_MESSAGE 1000

typedef enum
{
	// actions
	FOP_DROP,
	// pure string comparison
	FOP_MATCH,
	// integer/string comparisons
	FOP_EQ,
	FOP_NEQ,
	FOP_LT,
	FOP_LTE,
	FOP_GT,
	FOP_GTE,
	FOP_MAX,
} filter_op;

static const char *opstr[ FOP_MAX ] =
{
	"drop",
	"* ",
	"== ",
	"!= ",
	"< ",
	"<= ",
	"> ",
	">= ",
};

typedef union 
{
	char *string;
	int integer;
} node_parm_t;

typedef struct filter_node_s
{
	struct filter_node_s *next;		// next node for current scope
	struct filter_node_s *child;	// action/child node
	char *p1;						// userinfo key or action message
	node_parm_t p2;
	int fop:4;
	unsigned is_date:1;				// p1 is a virtual date key
	unsigned is_fname:1;			// p1 is a filtered name field
	unsigned is_string:1;			// p2 contains string, not integer
	unsigned is_quoted:1;			// use atoi( p2.string ) for comparison
	unsigned is_cvar:1;				// p2 contains cvar name, should be dereferenced

	unsigned tagged:1;

} filter_node_t;

static filter_node_t *nodes;

static char filterMessage[ MAX_FILTER_MESSAGE ];
static char filterDate[ 64 ];  // current date string in "YYYY-MM-DD HH:mm" format
static char filterName[ 256 ]; // filtered "name" userinfo key
static int  filterDateMsec;
static int  filterCurrMsec;
static int	nodeCount; // total count
static int	tempCount; // nodes that can expire
static int  expiredCount;


static void CleanStr( char *dst, int dst_size, const char *src )
{
	const char *max = dst + dst_size - 1;
	int	c;

	while ( (c = *src) != '\0' ) {
		if ( Q_IsColorString( src ) ) {
			src += 2;
			continue;
		} else if ( c >= ' ' && c <= '~' ) {
			*dst++ = c;
			if ( dst >= max )
				break;
		}
		src++;
	}

	*dst = '\0';
}


static const char *op2str( filter_op op )
{
	if ( (unsigned) op >= FOP_MAX )
		return "? ";
	else
		return opstr[ op ];

}


static void free_nodes( filter_node_t *node )
{
	filter_node_t *next;
	while ( node != NULL )
	{
		next = node->next;
		if ( node->child != NULL )
		{
			free_nodes( node->child );
		}
		Z_Free( node );
		node = next;
	}
}


static int eval_node( const filter_node_t *node )
{
	if ( node->fop == FOP_DROP )
	{
		Q_strncpyz( filterMessage, node->p1, sizeof( filterMessage ) );
		return -1; // will break *->next node walk in parent
	}
	else
	{
		const char *value, *value2;
		int res = 0, v1, v2;

		if ( node->is_date )
		{
			if ( filterCurrMsec != filterDateMsec ) // update date string
			{
				qtime_t t;
				Com_RealTime( &t );
				sprintf( node->p1, "%04i-%02i-%02i %02i:%02i",
					t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
					t.tm_hour, t.tm_min );
				filterDateMsec = filterCurrMsec;
			}
			value = node->p1;
		}
		else
		if ( node->is_fname )
		{
			if ( filterName[0] == '\0' )
			{
				CleanStr( filterName, sizeof( filterName ), Info_ValueForKeyToken( "name" ) );
			}
			//value = node->p1; // p1 points on filterName
			value = filterName;
		}
		else
		{
			value = Info_ValueForKeyToken( node->p1 ); 
		}

		if ( node->is_string )
		{
			value2 = node->p2.string;
			if ( node->is_cvar ) // dereference value2 
			{
				value2 = Cvar_VariableString( value2 + 1 );
			}

			if ( node->fop == FOP_MATCH )
			{
				res = Com_FilterExt( value2, value );
				return res; // early exit, just to silent compiler warnings about uninitialized v1 & v2
			}
			else
			{
				if ( node->is_quoted ) // forced string comparison
				{
					v1 = Q_stricmp( value, value2 );
					v2 = 0;
				}
				else // integer comparison
				{
					v1 = atoi( value );
					v2 = atoi( value2 );
				}
			}
		}
		else
		{
			v1 = atoi( value );
			v2 = node->p2.integer;
		}

		switch ( node->fop )
		{
			//case FOP_MATCH:res = Com_FilterExt( value2, value ); break;
			case FOP_EQ:   res = (v1 == v2); break;
			case FOP_NEQ:  res = (v1 != v2); break;
			case FOP_LT:   res = (v1 <  v2); break;
			case FOP_LTE:  res = (v1 <= v2); break;
			case FOP_GT:   res = (v1 >  v2); break;
			case FOP_GTE:  res = (v1 >= v2); break;
		}
		return res;
	}
}


static void dump_nodes( const filter_node_t *node, int level, int skip_tagged, FILE *f )
{
	char buf[ MAX_TOKEN_CHARS + 32 ];
	int i, n;

	while ( node )
	{
		if ( node->tagged && skip_tagged )
		{
			node = node->next;
			continue;
		}

		for ( i = 0; i < level ; i++ )
			fwrite( "\t", 1, 1, f );

		if ( node->fop == FOP_DROP ) // final action
		{
			if ( *node->p1 )
			{
				n = sprintf( buf, "drop \"%s\"", node->p1 );
				fwrite( buf, n, 1, f );
			} else
				fwrite( "drop", 4, 1, f );
		}
		else
		{
			const char *s = op2str( node->fop );

			if ( node->is_date )
			{
				if ( node->fop == FOP_LT ) // do not print default action for dates
					s = "";
				n = sprintf( buf, "date %s\"%s\"", s, node->p2.string );
			}
			else
			{
				if ( node->fop == FOP_EQ ) // do not print default action for strings
					s = "";

				if ( node->is_string )
				{
					if ( node->is_quoted )
						n = sprintf( buf, "%s %s\"%s\"", node->p1, s, node->p2.string );
					else
						n = sprintf( buf, "%s %s%s", node->p1, s, node->p2.string );
				}
				else
				{
					n = sprintf( buf, "%s %s%i", node->p1, s, node->p2.integer );
				}
			}

			fwrite( buf, n, 1, f );

			if ( node->child )
			{
				fwrite( " {\n", 3, 1, f );

				dump_nodes( node->child, level + 1, skip_tagged, f );

				fwrite( "\n", 1, 1, f );

				for ( i = 0; i < level; i++ )
					fwrite( "\t", 1, 1, f );

				fwrite( "}", 1, 1, f );

				if ( node->next ) 
					fwrite( "\n", 1, 1, f );
			}

		}
		node = node->next;
//		if ( node && level == 0 )
//			fwrite( "\n", 1, 1, f );
	}
}


static int walk_nodes( const filter_node_t *node )
{
	while ( node != NULL )
	{
		int res;
		if ( ( res = eval_node( node ) ) != 0 ) // evaluate current node
		{
			if ( res < 0 || ( res = walk_nodes( node->child ) ) < 0 )
			{
				return res;
			}
		}
		node = node->next;
	}

	return 0;
}


// marks specified node and its kids as expired
static void tag_from( filter_node_t *node )
{
	if ( node == NULL )
		return;

	// current node
	node->tagged = 1;

	// child nodes
	node = node->child;

	while ( node != NULL )
	{
		if ( node->child )
		{
			tag_from( node->child );
		}

		node->tagged = 1;
		node = node->next;
	}
}


static void clear_tags( filter_node_t *node )
{
	while ( node != NULL )
	{
		if ( node->child )
		{
			clear_tags( node->child );
		}
		node->tagged = 0;
		node = node->next;
	}
}


// try to find single expired date nodes
static int tag_expired( filter_node_t *node )
{
	while ( node != NULL )
	{
		int res;

		if ( node->is_date && node->fop == FOP_LT ) // it can expire
		{
			res = eval_node( node );
			if ( res == 0 )
			{
				tag_from( node );	// tag current and all descending nodes
				res = -2;			// skip any further evaluation
				expiredCount++;
			}
		}
		else
		{
			res = 1;
		}

		if ( res < 0 || ( res = tag_expired( node->child ) ) < 0 )
		{
			if ( res == -1 ) // action node
			{
				return res;
			}
		}

		node = node->next;
	}

	return 0;
}


// starting from root node
static unsigned tag_parents( filter_node_t *node )
{
	unsigned r = 1; // masked value for all child nodes
	unsigned v;
	while ( node != NULL )
	{
		if ( node->child ) 
			v = tag_parents( node->child );
		else 
			v = node->tagged;
		r &= v;
		node->tagged = v;
		node = node->next;
	}
	return r;
}


static int is_integer( const char *s )
{
	int n = 0;

	if ( *s == '-' )
		++s;

	while ( *s >= '0' && *s <= '9' )
	{
		++s; ++n;
	}

	if ( n == 0 || n > 24 || *s != '\0' )
		return 0;
	else
		return 1;
}


static filter_node_t *new_node( const char *p1, const char *p2, filter_op fop, int quoted )
{
	filter_node_t *node;
	int len, len1, len2;
	unsigned is_date = 0;
	unsigned is_fname = 0;

	// handle "date" key specially
	if ( Q_stricmp( p1, "date" ) == 0 && fop != FOP_DROP )
	{
		is_date = 1;
		len1 = 0; // p1 will point on a static date buffer
		if ( !quoted )
		{
			COM_ParseError( "expecting quoted string with 'date' key" );
			return NULL;
		}
	}
	else if ( Q_stricmp( p1, "fname" ) == 0 && fop != FOP_DROP )
	{
		is_fname = 1;
		len1 = 0; // p1 will point on a static filtered name buffer
		if ( !quoted )
		{
			COM_ParseError( "expecting quoted string with 'fname' key" );
			return NULL;
		}
	}
	else
	{
		len1 = strlen( p1 ) + 1; // key name or action message
		if ( len1 > MAX_FILTER_MESSAGE ) 
			len1 = MAX_FILTER_MESSAGE;
	}

	// right value
	if ( quoted || is_fname || is_date || is_integer( p2 ) == 0 )
		len2 = strlen( p2 ) + 1; // string value
	else
		len2 = 0; // integer or null value

	len = len1 + len2 + sizeof( *node );
	node = (filter_node_t *) Z_Malloc( len );
	memset( node, 0, len );

	node->fop = fop;
	node->is_date = is_date;
	node->is_fname = is_fname;

	if ( is_date )
	{
		// point on static date buffer
		node->p1 = filterDate; 
		if ( fop == FOP_LT )
			tempCount++; // check for potential expire
	}
	else if ( is_fname )
	{
		// point on static filtered name buffer
		node->p1 = filterName; 
	}
	else
	{
		// left value (key/action message)
		node->p1 = (char*) (node + 1);
		memcpy( node->p1, p1, len1 );
		node->p1[len1-1] = '\0';

		// if not action - convert key name to lowercase
		if ( fop != FOP_DROP )
			Q_strlwr( node->p1 );
	}

	if ( len2 ) // quoted or non-integer value
	{
		node->is_string = 1;
		node->is_quoted = quoted;

		if ( p2[0] == '$' && p2[1] != '$' && p2[1] != '\0' )
			node->is_cvar = 1; // needs to be defererenced at runtime

		if ( len1 )
			node->p2.string = (char*)( node->p1 + len1 );
		else
			node->p2.string = (char*)( node + 1 );

		strcpy( node->p2.string, p2 );
	}
	else // integer/action parameter
	{
		node->p2.integer = atoi( p2 );
	}

	nodeCount++; // statistics

	return node;
}


static const char *parse_section( const char *text, int level, filter_node_t **root, qboolean in_scope )
{
	filter_node_t *curr, *ch;
	filter_op fop;
	char lvalue[256];
	const char *v0, *back;
	tokenType_t op;

	curr = NULL;

	for ( ;; )
	{
		// expecting new key/action
		v0 = COM_ParseComplex( &text, in_scope );
		if ( com_tokentype == TK_EOF ) 
			break;

		// we are in child inline node
		if ( com_tokentype == TK_NEWLINE )
		{
			if ( curr == NULL ) 
			{
				COM_ParseError( "unexpected newline" );
				return NULL;
			}
			break; // exit from child node
		}

		 // leave current section
		if ( *v0 == '}' && curr && in_scope && level )
			break;

		if ( com_tokentype != TK_STRING /*&& com_tokentype != TK_QUOTED*/ )
		{
			COM_ParseError( "unexpected key/action '%s'", v0 );
			return NULL;
		}

		if ( Q_stricmp( v0, "drop" ) == 0 )
		{
			fop = FOP_DROP;
			back = text; // backup
			v0 = COM_ParseComplex( &text, qfalse );
			if ( com_tokentype != TK_QUOTED )
			{
				// "drop" action can have empty message (defaults to "Banned.")
				if ( /*fop == FOP_DROP && */ ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF || *v0 == '}' ) )
				{
					if ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF )
						v0 = "";
					text = back; // restore backup
				}
				else
				{
					COM_ParseError( "unexpected '%s'", v0 );
					return NULL;
				}
			}
			ch = new_node( v0, "0", fop, 0 ); // action node, p2 = "0", quoted = 0
		}
		else
		{
			// save key
			Q_strncpyz( lvalue, v0, sizeof( lvalue ) );
			// expect operator or value
			v0 = COM_ParseComplex( &text, qfalse );
			// override default op
			if ( com_tokentype >= TK_EQ && com_tokentype <= TK_MATCH )
			{
				op = com_tokentype;
				v0 = COM_ParseComplex( &text, qfalse ); // get rvalue
			}
			else
			{
				if ( Q_stricmp( lvalue, "date" ) == 0 )
					op = TK_LT; // default OP is LESS for dates
				else
					op = TK_EQ; // default OP is EQUAL for strings/integers
			}

			//  value must be sting or quoted string, `~` must be used with quoted strings only
			if ( (com_tokentype != TK_STRING && com_tokentype != TK_QUOTED) || (op == TK_MATCH && com_tokentype != TK_QUOTED ) )
			{
				COM_ParseError( "expecting value for key '%s' instead of '%s'", lvalue, v0 );
				return NULL;
			}

			switch ( op )
			{
				case TK_EQ:    fop = FOP_EQ;    break;
				case TK_NEQ:   fop = FOP_NEQ;   break;
				case TK_LT:    fop = FOP_LT;    break;
				case TK_LTE:   fop = FOP_LTE;   break;
				case TK_GT:    fop = FOP_GT;    break;
				case TK_GTE:   fop = FOP_GTE;   break;
				case TK_MATCH: fop = FOP_MATCH; break;
				default:
					COM_ParseError( "bad operator #%i", op );
					return NULL;
			}

			//Com_Printf( "%i: KEY %s <%i> %s\n", level, lvalue, fop, v0 ); // debug

			// allocate new filter node
			ch = new_node( lvalue, v0, fop, (com_tokentype == TK_QUOTED) ); // quoted = x
			if ( ch == NULL )
				return NULL;

			back = text;
			v0 = COM_ParseComplex( &text, qfalse ); // check current line
			if ( *v0 == '{' ) // open new section
			{
				text = parse_section( text, level + 1, &ch->child, qtrue );
			}
			else if ( com_tokentype == TK_STRING ) // new key/action on the same line, open new section
			{
				text = parse_section( back, level + 1, &ch->child, qfalse );
			} 
			else if ( com_tokentype == TK_NEWLINE || com_tokentype == TK_EOF )  // expect new section
			{
				v0 = COM_ParseComplex( &text, qtrue );
				if ( *v0 == '{' )
				{ 
					text = parse_section( text, level + 1, &ch->child, qtrue );
				}
				else
				{
					COM_ParseError( "expecting new section/action node" );
					text = NULL;
				}
			} // else parse new key/action
		}

		// update node pointers
		if ( curr == NULL )
			*root = ch;
		else 
			curr->next = ch;

		curr = ch;

		if ( text == NULL )
			break;
	}

	return text;
}


static qboolean parse_file( const char *filename )
{
	const char *text;
	char *data;
	qtime_t t;
	FILE *f;
	int size;
	
	// unconditionally release old filters
	free_nodes( nodes );
	nodes = NULL;

	nodeCount = 0;
	tempCount = 0;

	if ( !filename || !*filename )
		return qfalse;

	f = fopen( filename, "rb" );
	if ( f == NULL )
		return qfalse;

	//Com_Printf( "...loading userinfo filters form '%s'\n", filename );

	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );

	data = (char*) Z_Malloc( size + 1 );
	if ( fread( data, size, 1, f ) != 1 )
	{
		Z_Free( data );
		fclose( f );
		return qfalse;
	}

	data[ size ] = '\0';
	fclose( f );

	COM_BeginParseSession( filename );

	text = parse_section( data, 0, &nodes, qtrue );

	if ( text == NULL ) // error
	{
		free_nodes( nodes );
		nodes = NULL;
	}

	Z_Free( data );

	if ( text == NULL )
		return qfalse;

	// initialize date string
	Com_RealTime( &t );
	sprintf( filterDate, "%04i-%02i-%02i %02i:%02i",
		t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
		t.tm_hour, t.tm_min );

	filterDateMsec = Sys_Milliseconds();

	return qtrue;
}


static void SV_ReloadFilters( const char *filename, filter_node_t *new_node )
{
	static char loaded_name[ MAX_OSPATH * 3 ];
	static fileOffset_t loaded_fsize;
	static fileTime_t loaded_ctime;
	static fileTime_t loaded_mtime;
	const char *ospath;

	if ( *filename && ( tempCount || new_node ) )
	{
		fileTime_t curr_ctime, curr_mtime;
		fileOffset_t curr_fsize;
		qboolean reload;
		qboolean dump;

		ospath = FS_BuildOSPath( FS_GetHomePath(), FS_GetCurrentGameDir(), filename );
		if ( strcmp( ospath, loaded_name ) )
			reload = qtrue;
		else if ( !Sys_GetFileStats( loaded_name, &curr_fsize, &curr_mtime, &curr_ctime ) )
			reload = qtrue;
		else if ( curr_fsize != loaded_fsize || curr_mtime != loaded_mtime || curr_ctime != loaded_ctime )
			reload = qtrue;
		else
			reload = qfalse;

		if ( reload )
		{
			//Com_Printf( "...reloading filter nodes from %s\n", ospath );
			if ( parse_file( ospath ) )
			{
				Q_strncpyz( loaded_name, ospath, sizeof( loaded_name ) );
				Sys_GetFileStats( loaded_name, &loaded_fsize, &loaded_mtime, &loaded_ctime );
			}
		}

		dump = qfalse;

		// add new nodes(s)
		if ( new_node )
		{
			clear_tags( nodes );
			// link new new node
			new_node->next = nodes;
			nodes = new_node;
			dump = qtrue;
		}

		// tag expired nodes
		if ( tempCount )
		{
			expiredCount = 0;
			// find single expired nodes
			tag_expired( nodes );
			if ( expiredCount ) 
			{
				tag_parents( nodes );
				dump = qtrue;
			}
		}

		if ( dump )
		{
			FILE *f;
			f = Sys_FOpen( ospath, "w" );
			if ( f ) 
			{
				dump_nodes( nodes, 0, 1, f ); // skip tagged
				fclose( f );
			}
		}
	}

	ospath = FS_BuildOSPath( FS_GetHomePath(), FS_GetCurrentGameDir(), filename );
	if ( *filename && strcmp( ospath, loaded_name ) == 0 )
	{
		fileTime_t curr_ctime, curr_mtime;
		fileOffset_t curr_fsize;
		if ( Sys_GetFileStats( ospath, &curr_fsize, &curr_mtime, &curr_ctime ) )
		{
			if ( curr_fsize == loaded_fsize && curr_mtime == loaded_mtime && curr_ctime == loaded_ctime )
			{
				return; // filter file not changed
			}
		}
	}

	loaded_name[ 0 ] = '\0';

	if ( parse_file( ospath ) )
	{
		Com_Printf( "...%i filter nodes loaded from '%s'\n", nodeCount, filename );
		// save file metadata
		Q_strncpyz( loaded_name, ospath, sizeof( loaded_name ) );
		Sys_GetFileStats( loaded_name, &loaded_fsize, &loaded_mtime, &loaded_ctime );
	}
}


void SV_LoadFilters( const char *filename )
{
	SV_ReloadFilters( filename, NULL );
}


const char *SV_RunFilters( const char *userinfo, const netadr_t *addr )
{
	if ( addr->type <= NA_LOOPBACK ) // cannot kick host player/bot
		return "";

	Info_Tokenize( userinfo );

	filterName[0] = '\0';
	filterMessage[0] = '\0';
	filterCurrMsec = Sys_Milliseconds();

	if ( walk_nodes( nodes ) != 0 )
	{
		if ( filterMessage[0] )
			return filterMessage;
		else
			return "Banned.";
	}
	else
		return "";
}


#define IS_LEAP(year) ( ( ( (year) % 4 == 0 ) && ( (year) % 100 != 0 ) ) || ( (year) % 400 == 0 ) )

/* Add hours to specified date */
static void Q_AddTime( qtime_t *qtime, unsigned int n )
{
	unsigned int md[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };	
	unsigned int year, month, day, min, hour;

	year = qtime->tm_year + 1900;
	month = qtime->tm_mon;
	day = qtime->tm_mday;
	hour = qtime->tm_hour;
	min = qtime->tm_min;

	// add minutes
	n += min;  min  = n % 60; n -= min;  n /= 60; // hours

	// add hours
	n += hour; hour = n % 24; n -= hour; n /= 24; // days

	// add days
	if ( IS_LEAP( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	// add days-months-years
	while ( 1 )
	{
		if ( day + n > md[month] )
		{
			n -= ( md[month] - day + 1 );
			month++;
			day = 1;
			if ( month > 11 )
			{
				year++;
				month = 0;
				if ( IS_LEAP( year ) )
					md[1] = 29;
				else
					md[1] = 28;
			}
		}
		else
		{
			day += n;
			break;
		}
	}

	qtime->tm_year = year - 1900;
	qtime->tm_mon = month;
	qtime->tm_mday = day;
	qtime->tm_hour = hour;
	qtime->tm_min = min;
}


/* Add months to specified date */
static void Q_AddDate( qtime_t *qtime, int n )
{
	unsigned int md[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	unsigned int year, month, day, last;

	year = qtime->tm_year + 1900;
	month = qtime->tm_mon;
	day = qtime->tm_mday;

	last = (day == md[month]);

	if ( IS_LEAP( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	n += month; month = n % 12;	n -= month;	n /= 12; year += n;

	if ( IS_LEAP( year ) )
		md[1] = 29;
	else
		md[1] = 28;

	if ( last )
		day = md[ month ];
	else
		if ( day > md[ month ] )
			day = md[ month ];

	qtime->tm_year = year - 1900;
	qtime->tm_mon = month;
	qtime->tm_mday = day;
}


/*
===============
SV_AddFilter_f
===============
*/
void SV_AddFilter_f( void )
{
	filter_node_t *node;
	client_t *cl;
	char cmd[ 4096 ], buf[MAX_CMD_LINE], date[32];
	const char *v, *s;
	const char *reason;
	qtime_t t;
	int i, n, keys;

	if ( !sv_filter->string[0] )
	{
		Com_Printf( "Filter system is not enabled.\n" );
		SV_ReloadFilters( "", NULL );
		return;
	}

	if ( Cmd_Argc() < 2 )
	{
		Com_Printf( "Usage: %s <id> [key1] [key2] ... [keyN] [date +<duration[h|d|w|m]>|<date> ] [reason <text>]\nDefault key is \"ip\"\nDefault duration unit is minutes, h(ours), d(ays), w(eeks), m(onths) suffixes can also be specified.\n", Cmd_Argv( 0 ) );
		return;
	}

	cl = SV_GetPlayerByHandle();
	if ( cl == NULL )
	{
		Com_Printf( "Unknown client '%s'\n", Cmd_Argv( 1 ) );
		return;
	}

	Com_RealTime( &t );
	date[0] = '\0';
	cmd[0] = '\0';
	keys = 0;
	reason = "";

	Info_Tokenize( cl->userinfo );

	// attach userinfo keys
	for ( i = 2; i < Cmd_Argc(); i++ )
	{
		v = Cmd_Argv( i );

		// special case: ban reason
		if ( Q_stricmp( v, "reason" ) == 0 )
		{
			if ( i >= Cmd_Argc() - 1 )
			{
				Com_Printf( S_COLOR_YELLOW "missing reason value\n" );
				return;
			}
			reason = Cmd_Argv( i + 1 );
			i++;
			continue;
		}

		// special case: duration (date) field
		if ( Q_stricmp( v, "date" ) == 0 )
		{
			if ( i >= Cmd_Argc() - 1 )
			{
				Com_Printf( S_COLOR_YELLOW "missing date value\n" );
				return;
			}
			i++;
			v = Cmd_Argv( i );
			if ( v[0] == '+' )
			{
				v++;
				if ( *v < '1' || *v > '9' )
				{
					Com_Printf( "expecting integer value for duration\n" );
					return;
				}
				n = 0;
				while ( *v >= '0' && *v <= '9' ) {
					n = n * 10 + ( *v++ - '0' );
				}
				switch ( *v ) {
					case '\0': Q_AddTime( &t, n ); break;
					case 'H': case 'h': Q_AddTime( &t, n * 60 ); break;
					case 'D': case 'd': Q_AddTime( &t, n * 24 * 60 ); break;
					case 'W': case 'w': Q_AddTime( &t, n * 24 * 7 * 60 ); break;
					case 'M': case 'm': Q_AddDate( &t, n ); break;
					default:
						Com_Printf( S_COLOR_YELLOW "unsupported date suffix '%c'\n", *v );
						return;
				}
				Com_sprintf( date, sizeof( date ), " date \"%04i-%02i-%02i %02i:%02i\"", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min );
			}
			else
			{
				Com_sprintf( date, sizeof( date ), " date \"%s\"", v );
			}
			continue;
		}

		s = Info_ValueForKeyToken( v );
		if ( *s == '\0' ) // skip empty keys
			continue;

		Com_sprintf( buf, sizeof( buf ), " %s \"%s\"", v, s );
		Q_strcat( cmd, sizeof( cmd ), buf );
		keys++;
	}

	if ( !keys ) // add default key(s)
	{
		Com_sprintf( buf, sizeof( cmd ), " ip \"%s\"", Info_ValueForKeyToken( "ip" ) );
		Q_strcat( cmd, sizeof( cmd ), buf );
	}

	if ( date[0] )
		Q_strcat( cmd, sizeof( cmd ), date );

	if ( *reason )
		Com_sprintf( buf, sizeof( buf ), " drop \"%s\"", reason );
	else
		strcpy( buf, " drop" );

	Q_strcat( cmd, sizeof( cmd ), buf );

	Com_DPrintf( "bancmd: `%s`\n", cmd );

	node = NULL;
	COM_BeginParseSession( "command" );
	s = parse_section( cmd, 0, &node, qtrue ); // level=0,in_scope=qtrue
	if ( s == NULL ) // syntax error
	{
		free_nodes( node );
		return;
	}

	if ( node && node->fop == FOP_DROP )
	{
		Com_Printf( S_COLOR_YELLOW "Standalone \"drop\" nodes is not allowed!\n" );
		free_nodes( node );
		return;
	}

	if ( node ) // should always success
	{
		SV_ReloadFilters( sv_filter->string, node );
		SV_DropClient( cl, *reason ? reason : "Banned." );
	}
}


/*
===============
SV_AddFilterCmd_f

Parses raw filter command string
===============
*/
void SV_AddFilterCmd_f( void ) 
{
	filter_node_t *node;
	const char *cmd, *s;

	if ( !sv_filter->string[0] ) 
	{
		Com_Printf( "Filter system is not enabled.\n" );
		SV_ReloadFilters( "", NULL );
		return;
	}

	if ( Cmd_Argc() < 2 )
	{
		Com_Printf( "Usage: %s <filter format string>\n", Cmd_Argv( 0 ) );
		return;
	}

	cmd = Cmd_Cmd() + strlen( Cmd_Argv( 0 ) ) + 1;

	node = NULL;
	COM_BeginParseSession( "command" );
	s = parse_section( cmd, 0, &node, qtrue ); // level=0,in_scope=qtrue
	if ( s == NULL ) // syntax error
	{
		free_nodes( node );
		return;
	}

	if ( node && node->fop == FOP_DROP )
	{
		Com_Printf( S_COLOR_YELLOW "Standalone \"drop\" nodes is not allowed!\n" );
		free_nodes( node );
		return;
	}

	if ( node )
	{
		SV_ReloadFilters( sv_filter->string, node );
	}
}
