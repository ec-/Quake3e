/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cvar.c -- dynamic variable tracking

#include "q_shared.h"
#include "qcommon.h"

static cvar_t	*cvar_vars = NULL;
static cvar_t	*cvar_cheats;
static cvar_t	*cvar_developer;
int			cvar_modifiedFlags;

#define	MAX_CVARS	2048
static cvar_t	cvar_indexes[MAX_CVARS];
static int		cvar_numIndexes;

static int	cvar_group[ CVG_MAX ];

#define FILE_HASH_SIZE		256
static	cvar_t	*hashTable[FILE_HASH_SIZE];
static	qboolean cvar_sort = qfalse;

/*
================
return a hash value for the filename
================
*/
static long generateHashValue( const char *fname ) {
	int		i;
	long	hash;
	char	letter;

	hash = 0;
	i = 0;
	while (fname[i] != '\0') {
		letter = locase[(byte)fname[i]];
		hash+=(long)(letter)*(i+119);
		i++;
	}
	hash &= (FILE_HASH_SIZE-1);
	return hash;
}


/*
============
Cvar_ValidateName
============
*/
static qboolean Cvar_ValidateName( const char *name ) {
	const char *s;
	int c;
	
	if ( !name ) {
		return qfalse;
	}

	s = name;
	while ( (c = *s++) != '\0' ) {
		if ( c == '\\' || c == '\"' || c == ';' || c == '%' || c <= ' ' || c >= '~' )
			return qfalse;
	}

	if ( (s - name) >= MAX_STRING_CHARS ) {
		return qfalse;
	}

	return qtrue;
}


/*
============
Cvar_FindVar
============
*/
static cvar_t *Cvar_FindVar( const char *var_name ) {
	cvar_t	*var;
	long hash;

	if ( !var_name )
		return NULL;

	hash = generateHashValue( var_name );
	
	for ( var = hashTable[ hash ] ; var ; var = var->hashNext ) {
		if ( !Q_stricmp( var_name, var->name ) ) {
			return var;
		}
	}

	return NULL;
}


/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue( const char *var_name ) {
	cvar_t	*var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return var->value;
}


/*
============
Cvar_VariableIntegerValue
============
*/
int Cvar_VariableIntegerValue( const char *var_name ) {
	cvar_t	*var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return 0;
	return var->integer;
}


/*
============
Cvar_VariableString
============
*/
const char *Cvar_VariableString( const char *var_name ) {
	cvar_t *var;
	
	var = Cvar_FindVar (var_name);
	if (!var)
		return "";
	return var->string;
}


/*
============
Cvar_VariableStringBuffer
============
*/
void Cvar_VariableStringBuffer( const char *var_name, char *buffer, int bufsize ) {
	cvar_t *var;
	
	var = Cvar_FindVar (var_name);
	if (!var) {
		*buffer = '\0';
	}
	else {
		Q_strncpyz( buffer, var->string, bufsize );
	}
}


/*
============
Cvar_VariableStringBufferSafe
============
*/
void Cvar_VariableStringBufferSafe( const char *var_name, char *buffer, int bufsize, int flag ) {
	cvar_t *var;
	
	var = Cvar_FindVar( var_name );
	if ( !var || var->flags & flag ) {
		*buffer = '\0';
	}
	else {
		Q_strncpyz( buffer, var->string, bufsize );
	}
}


/*
============
Cvar_Flags
============
*/
unsigned Cvar_Flags( const char *var_name )
{
	const cvar_t *var;
	
	if ( ( var = Cvar_FindVar( var_name ) ) == NULL )
		return CVAR_NONEXISTENT;
	else
	{
		if ( var->modified )
			return var->flags | CVAR_MODIFIED;
		else
			return var->flags;
	}
}


/*
============
Cvar_CommandCompletion
============
*/
void Cvar_CommandCompletion( void (*callback)(const char *s) )
{
	const cvar_t *cvar;

	for ( cvar = cvar_vars; cvar; cvar = cvar->next ) {
		if ( cvar->name && ( cvar->flags & CVAR_NOTABCOMPLETE ) == 0 ) {
			callback( cvar->name );
		}
	}
}


static qboolean Cvar_IsIntegral( const char *s ) {

	if ( *s == '-' && *(s+1) != '\0' )
		s++;

	while ( *s != '\0' ) {
		if ( *s < '0' || *s > '9' ) {
			return qfalse;
		}
		s++;
	}

	return qtrue;
}


/*
============
Cvar_Validate
============
*/
static const char *Cvar_Validate( cvar_t *var, const char *value, qboolean warn )
{
	static char intbuf[ 32 ];
	const char *limit;
	float valuef;
	int	  valuei;

	if ( var->validator == CV_NONE )
		return value;

	if ( !value )
		return value;

	limit = NULL;

	if ( var->validator == CV_INTEGER || var->validator == CV_FLOAT ) {
		if ( !Q_isanumber( value ) ) {
			if ( warn )
				Com_Printf( "WARNING: cvar '%s' must be numeric", var->name );
			limit = var->resetString;
		} else {
			if ( var->validator == CV_INTEGER ) {
				if ( !Cvar_IsIntegral( value ) ) {
					if ( warn )
						Com_Printf( "WARNING: cvar '%s' must be integral", var->name );
					sprintf( intbuf, "%i", atoi( value ) );
					value = intbuf; // new value
				}
				valuei = atoi( value );
				if ( var->mins && valuei < atoi( var->mins ) ) {
					limit = var->mins;
				} else if ( var->maxs && valuei > atoi( var->maxs ) ) {
					limit = var->maxs;
				}
			} else { // CV_FLOAT
				valuef = Q_atof( value );
				if ( var->mins && valuef < Q_atof( var->mins ) ) {
					limit = var->mins;
				} else if ( var->maxs && valuef > Q_atof( var->maxs ) ) {
					limit = var->maxs;
				}
			}

			if ( warn ) {
				if ( limit && ( limit == var->mins || limit == var->maxs ) ) {
					if ( value == intbuf ) { // cast to integer
						Com_Printf( " and" ); 
					} else {
						Com_Printf( "WARNING: cvar '%s'", var->name );
					}
					Com_Printf( " is out of range (%s '%s')", (limit == var->mins) ? "min" : "max", limit );
				}
			}
		} // Q_isanumber
	} // CV_INTEGER || CV_FLOAT
	// TODO: stringlist
	else if ( var->validator == CV_FSPATH ) {
		// check for directory traversal patterns
		if ( FS_InvalidGameDir( value ) ) {
			if ( warn ) {
				Com_Printf( "WARNING: cvar '%s' contains invalid patterns", var->name );
			}
			// try to use current value if it is valid
			if ( !FS_InvalidGameDir( var->string ) ) {
				if ( warn ) {
					Com_Printf( "\n" );
				}
				return var->string;
			}
			limit = var->resetString;
		}
	}

	if ( limit || value == intbuf ) {
		if ( !limit )
			limit = value;
		if ( warn )
			Com_Printf( ", setting to '%s'\n", limit );
		return limit;
	} else {
		return value;
	}
}


/*
============
Cvar_Get

If the variable already exists, the value will not be set unless CVAR_ROM
The flags will be or'ed in if the variable exists.
============
*/
cvar_t *Cvar_Get( const char *var_name, const char *var_value, int flags ) {
	cvar_t	*var;
	long	hash;
	int	index;

	if ( !var_name || !var_value ) {
		Com_Error( ERR_FATAL, "Cvar_Get: NULL parameter" );
	}

	if ( !Cvar_ValidateName( var_name ) ) {
		Com_Printf( "invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

#if 0 // FIXME: values with backslash happen
	if ( !Cvar_ValidateString( var_value ) ) {
		Com_Printf("invalid cvar value string: %s\n", var_value );
		var_value = "BADVALUE";
	}
#endif

	var = Cvar_FindVar (var_name);
	
	if(var)
	{
		int vm_created = (flags & CVAR_VM_CREATED);
		var_value = Cvar_Validate(var, var_value, qfalse);

		// Make sure the game code cannot mark engine-added variables as gamecode vars
		if(var->flags & CVAR_VM_CREATED)
		{
			if ( !vm_created )
				var->flags &= ~CVAR_VM_CREATED;
		}
		else if (!(var->flags & CVAR_USER_CREATED))
		{
			if ( vm_created )
				flags &= ~CVAR_VM_CREATED;
		}

		// if the C code is now specifying a variable that the user already
		// set a value for, take the new value as the reset value
		if(var->flags & CVAR_USER_CREATED)
		{
			var->flags &= ~CVAR_USER_CREATED;
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );

			if ( flags & CVAR_ROM || ( (flags & CVAR_DEVELOPER) && !cvar_developer->integer ) )
			{
				// this variable was set by the user,
				// so force it to value given by the engine.

				if(var->latchedString)
					Z_Free(var->latchedString);
				
				var->latchedString = CopyString(var_value);
			}
		}

		// Make sure servers cannot mark engine-added variables as SERVER_CREATED
		if ( var->flags & CVAR_SERVER_CREATED )
		{
			if ( !( flags & CVAR_SERVER_CREATED ) ) {
				// reset server-created flag
				var->flags &= ~CVAR_SERVER_CREATED;
				if ( vm_created ) {
					// reset to state requested by local VM module
					var->flags &= ~CVAR_ROM;
					Z_Free( var->resetString );
					var->resetString = CopyString( var_value );
					if ( var->latchedString )
						Z_Free( var->latchedString );
					var->latchedString = CopyString( var_value );
				}
			}
		}
		else
		{
			if ( flags & CVAR_SERVER_CREATED )
				flags &= ~CVAR_SERVER_CREATED;
		}

		var->flags |= flags;

		// only allow one non-empty reset string without a warning
		if ( !var->resetString[0] ) {
			// we don't have a reset string yet
			Z_Free( var->resetString );
			var->resetString = CopyString( var_value );
		} else if ( var_value[0] && strcmp( var->resetString, var_value ) ) {
			Com_DPrintf( "Warning: cvar \"%s\" given initial values: \"%s\" and \"%s\"\n",
				var_name, var->resetString, var_value );
		}

		// if we have a latched string, take that value now
		if ( var->latchedString ) {
			char *s;

			s = var->latchedString;
			var->latchedString = NULL;	// otherwise cvar_set2 would free it
			Cvar_Set2( var_name, s, qtrue );
			Z_Free( s );
		}

		// ZOID--needs to be set so that cvars the game sets as 
		// SERVERINFO get sent to clients
		cvar_modifiedFlags |= flags;

		return var;
	}

	//
	// allocate a new cvar
	//

	// find a free cvar
	for(index = 0; index < MAX_CVARS; index++)
	{
		if(!cvar_indexes[index].name)
			break;
	}

	if(index >= MAX_CVARS)
	{
		if(!com_errorEntered)
			Com_Error(ERR_FATAL, "Error: Too many cvars, cannot create a new one!");

		return NULL;
	}
	
	var = &cvar_indexes[index];
	
	if(index >= cvar_numIndexes)
		cvar_numIndexes = index + 1;
		
	var->name = CopyString( var_name );
	var->string = CopyString( var_value );
	var->modified = qtrue;
	var->modificationCount = 1;
	var->value = Q_atof( var->string );
	var->integer = atoi( var->string );
	var->resetString = CopyString( var_value );
	var->validator = CV_NONE;
	var->description = NULL;
	var->group = CVG_NONE;
	cvar_group[ var->group ] = 1;

	// link the variable in
	var->next = cvar_vars;
	if ( cvar_vars )
		cvar_vars->prev = var;

	var->prev = NULL;
	cvar_vars = var;

	var->flags = flags;
	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= var->flags;

	hash = generateHashValue(var_name);
	var->hashIndex = hash;

	var->hashNext = hashTable[hash];
	if ( hashTable[hash] )
		hashTable[hash]->hashPrev = var;

	var->hashPrev = NULL;
	hashTable[hash] = var;

	 // sort on write
	cvar_sort = qtrue;

	return var;
}


static void Cvar_QSortByName( cvar_t **a, int n ) 
{
	cvar_t *temp;
	cvar_t *m;
	int i, j;

	i = 0;
	j = n;
	m = a[ n>>1 ];

	do {
		// sort in descending order
		while ( strcmp( a[i]->name, m->name ) > 0 ) i++;
		while ( strcmp( a[j]->name, m->name ) < 0 ) j--;

		if ( i <= j ) {
			temp = a[i]; 
			a[i] = a[j]; 
			a[j] = temp;
			i++; 
			j--;
		}
	} while ( i <= j );

	if ( j > 0 ) Cvar_QSortByName( a, j );
	if ( n > i ) Cvar_QSortByName( a+i, n-i );
}


static void Cvar_Sort( void ) 
{
	cvar_t *list[ MAX_CVARS ], *var;
	int count;
	int i;

	for ( count = 0, var = cvar_vars; var; var = var->next ) {
		if ( var->name ) {
			list[ count++ ] = var;
		} else {
			Com_Error( ERR_FATAL, "%s: NULL cvar name", __func__ );
		}
	}

	if ( count < 2 ) {
		return; // nothing to sort
	}

	Cvar_QSortByName( &list[0], count-1 );
	
	cvar_vars = NULL;

	// relink cvars
	for ( i = 0; i < count; i++ ) {
		var = list[ i ];
		// link the variable in
		var->next = cvar_vars;
		if ( cvar_vars )
			cvar_vars->prev = var;
		var->prev = NULL;
		cvar_vars = var;
	}
}


/*
============
Cvar_Print

Prints the value, default, and latched string of the given variable
============
*/
static void Cvar_Print( const cvar_t *v ) {

	Com_Printf ("\"%s\" is:\"%s" S_COLOR_WHITE "\"",
		v->name, v->string );

	if ( !( v->flags & CVAR_ROM ) ) {
		Com_Printf (" default:\"%s" S_COLOR_WHITE "\"",
			v->resetString );
	}

	Com_Printf ("\n");

	if ( v->latchedString ) {
		Com_Printf( "latched: \"%s\"\n", v->latchedString );
	}

	if ( v->description ) {
		Com_Printf( "%s\n", v->description );
	}
}


/*
============
Cvar_Set2
============
*/
cvar_t *Cvar_Set2( const char *var_name, const char *value, qboolean force ) {
	cvar_t	*var;

//	Com_DPrintf( "Cvar_Set2: %s %s\n", var_name, value );

	if ( !Cvar_ValidateName( var_name ) )
	{
		Com_Printf( "invalid cvar name string: %s\n", var_name );
		var_name = "BADNAME";
	}

#if 0	// FIXME
	if ( value && !Cvar_ValidateString( value ) ) {
		Com_Printf("invalid cvar value string: %s\n", value );
		var_value = "BADVALUE";
	}
#endif

	var = Cvar_FindVar( var_name );
	if ( !var )
	{
		if ( !value )
			return NULL;
		// create it
		if ( !force )
			return Cvar_Get( var_name, value, CVAR_USER_CREATED );
		else
			return Cvar_Get( var_name, value, 0 );
	}

	if ( var->flags & (CVAR_ROM | CVAR_INIT | CVAR_CHEAT | CVAR_DEVELOPER) && !force )
	{
		if ( var->flags & CVAR_ROM )
		{
			Com_Printf( "%s is read only.\n", var_name );
			return var;
		}

		if ( var->flags & CVAR_INIT )
		{
			Com_Printf( "%s is write protected.\n", var_name );
			return var;
		}

		if ( (var->flags & CVAR_CHEAT) && !cvar_cheats->integer )
		{
			if ( !Q_stricmp(var_name, "cg_gunX") || !Q_stricmp(var_name, "cg_gunY") || !Q_stricmp(var_name, "cg_gunZ")
				|| !Q_stricmp(var_name, "r_flareCoeff") || !Q_stricmp(var_name, "r_flareFade") || !Q_stricmp(var_name, "r_flareSize") )
				/*Com_Printf( "^3%s ^2applied^1.\n", var_name )*/ ;
			else
			{
				Com_Printf( "%s is cheat protected.\n", var_name );
				return var;
			}
		}

		if ( (var->flags & CVAR_DEVELOPER) && !cvar_developer->integer )
		{
			Com_Printf( "%s can be set only in developer mode.\n", var_name );
			return var;
		}
	}

	if ( !value )
		value = var->resetString;

	value = Cvar_Validate( var, value, qtrue );

	if ( (var->flags & CVAR_LATCH) && var->latchedString )
	{
		if ( strcmp( value, var->string ) == 0 )
		{
			Z_Free( var->latchedString );
			var->latchedString = NULL;
			return var;
		}

		if ( strcmp( value, var->latchedString ) == 0 )
			return var;
	}
	else if ( strcmp( value, var->string ) == 0 )
		return var;

	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= var->flags;

	if ( !force )
	{
		if ( var->flags & CVAR_LATCH )
		{
			if ( var->latchedString )
			{
				if ( strcmp( value, var->latchedString ) == 0 )
					return var;
				Z_Free( var->latchedString );
			}
			else
			{
				if ( strcmp( value, var->string ) == 0 )
					return var;
			}

			Com_Printf( "%s will be changed upon restarting.\n", var_name );
			var->latchedString = CopyString( value );
			var->modified = qtrue;
			var->modificationCount++;
			cvar_group[ var->group ] = 1;
			return var;
		}
	}
	else
	{
		if ( var->latchedString )
		{
			Z_Free( var->latchedString );
			var->latchedString = NULL;
		}
	}

	if ( strcmp( value, var->string ) == 0 )
		return var; // not changed

	var->modified = qtrue;
	var->modificationCount++;
	cvar_group[ var->group ] = 1;
	
	Z_Free( var->string ); // free the old value string
	
	var->string = CopyString( value );
	var->value = Q_atof( var->string );
	var->integer = atoi( var->string );

	return var;
}


/*
============
Cvar_Set
============
*/
void Cvar_Set( const char *var_name, const char *value) {

	// prevent cg_gunXYZ & flares from resetting
	if ( !Q_stricmp(var_name, "cg_gunX") || !Q_stricmp(var_name, "cg_gunY") || !Q_stricmp(var_name, "cg_gunZ")
		|| !Q_stricmp(var_name, "r_flareCoeff") || !Q_stricmp(var_name, "r_flareFade") || !Q_stricmp(var_name, "r_flareSize") )
		return;

	Cvar_Set2 (var_name, value, qtrue);
}


/*
============
Cvar_SetSafe
============
*/
void Cvar_SetSafe( const char *var_name, const char *value )
{
	unsigned flags = Cvar_Flags( var_name );
	qboolean force = qtrue;

	if ( flags != CVAR_NONEXISTENT )
	{
		if ( flags & ( CVAR_PROTECTED | CVAR_PRIVATE ) )
		{
			if( value )
				Com_Printf( S_COLOR_YELLOW "Restricted source tried to set "
					"\"%s\" to \"%s\"\n", var_name, value );
			else
				Com_Printf( S_COLOR_YELLOW "Restricted source tried to "
					"modify \"%s\"\n", var_name );
			return;
		}

		// don't let VMs or server change engine latched cvars instantly
		//if ( ( flags & CVAR_LATCH ) && !( flags & CVAR_VM_CREATED ) )
		//{
		//	force = qfalse;
		//}
	}

	// prevent com_maxfps from resetting (OSP)
	if ( !Q_stricmp(var_name, "com_maxfps") && Cvar_VariableIntegerValue("com_maxfps") > 125 )
		return;

	Cvar_Set2( var_name, value, force );
}


/*
============
Cvar_SetLatched
============
*/
void Cvar_SetLatched( const char *var_name, const char *value) {
	Cvar_Set2 (var_name, value, qfalse);
}


/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue( const char *var_name, float value) {
	char	val[32];

	if ( value == (int)value ) {
		Com_sprintf (val, sizeof(val), "%i",(int)value);
	} else {
		Com_sprintf (val, sizeof(val), "%f",value);
	}
	Cvar_Set (var_name, val);
}


/*
============
Cvar_SetIntegerValue
============
*/
void Cvar_SetIntegerValue( const char *var_name, int value ) {
	char	val[32];

	sprintf( val, "%i", value );
	Cvar_Set( var_name, val );
}


/*
============
Cvar_SetValueSafe
============
*/
void Cvar_SetValueSafe( const char *var_name, float value )
{
	char val[32];

	if( Q_isintegral( value ) )
		Com_sprintf( val, sizeof(val), "%i", (int)value );
	else
		Com_sprintf( val, sizeof(val), "%f", value );
	Cvar_SetSafe( var_name, val );
}


/*
============
Cvar_SetModified
============
*/
qboolean Cvar_SetModified( const char *var_name, qboolean modified )
{
	cvar_t	*var;

	var = Cvar_FindVar( var_name );
	if ( var ) 
	{
		var->modified = modified;
		return qtrue;
	}
	else 
	{
		return qfalse;
	}
}


/*
============
Cvar_Reset
============
*/
void Cvar_Reset( const char *var_name ) {
	Cvar_Set2( var_name, NULL, qfalse );
}

/*
============
Cvar_ForceReset
============
*/
void Cvar_ForceReset(const char *var_name)
{
	Cvar_Set2(var_name, NULL, qtrue);
}

/*
============
Cvar_SetCheatState

Any testing variables will be reset to the safe values
============
*/
void Cvar_SetCheatState(void)
{
	cvar_t	*var;

	// set all default vars to the safe value
	for(var = cvar_vars; var ; var = var->next)
	{
		if(var->flags & CVAR_CHEAT)
		{
			// the CVAR_LATCHED|CVAR_CHEAT vars might escape the reset here 
			// because of a different var->latchedString
			if (var->latchedString)
			{
				Z_Free(var->latchedString);
				var->latchedString = NULL;
			}
			if (strcmp(var->resetString,var->string))
				Cvar_Set(var->name, var->resetString);
		}
	}
}

void Cvar_ResetXmodProtection(void)
{
	cvar_t *var;

	for (var = cvar_vars; var; var = var->next)
	{
		if (var->flags & CVAR_XMOD)
		{
			var->flags &= ~(CVAR_ROM | CVAR_XHCK_ON | CVAR_XHCK_OFF);
		}
	}
}

void Cvar_RemoveCheatProtected(const char *var_name)
{
	cvar_t *var;

	var = Cvar_FindVar(var_name);
	if (!var)
	{
		return;
	}

	var->flags &= ~(CVAR_CHEAT);
}


/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command( void ) {
	cvar_t	*v;

	// check variables
	v = Cvar_FindVar (Cmd_Argv(0));
	if (!v) {
		return qfalse;
	}

	// perform a variable print or set
	if ( Cmd_Argc() == 1 ) {
		Cvar_Print( v );
		return qtrue;
	}

	// set the value if forcing isn't required
	Cvar_Set2( v->name, Cmd_ArgsFrom( 1 ), qfalse );
	return qtrue;
}


/*
============
Cvar_Print_f

Prints the contents of a cvar 
(preferred over Cvar_Command where cvar names and commands conflict)
============
*/
static void Cvar_Print_f( void )
{
	const char *name;
	cvar_t *cv;
	
	if(Cmd_Argc() != 2)
	{
		Com_Printf ("usage: print <variable>\n");
		return;
	}

	name = Cmd_Argv(1);

	cv = Cvar_FindVar(name);
	
	if(cv)
		Cvar_Print(cv);
	else
		Com_Printf ("Cvar %s does not exist.\n", name);
}


/*
============
Cvar_Toggle_f

Toggles a cvar for easy single key binding, optionally through a list of
given values
============
*/
static void Cvar_Toggle_f( void ) {
	int		i, c;
	const char	*curval;

	c = Cmd_Argc();
	if ( c < 2 ) {
		Com_Printf( "usage: toggle <variable> [value1, value2, ...]\n" );
		return;
	}

	if ( c == 2 ) {
		Cvar_Set2( Cmd_Argv( 1 ), va( "%d", !Cvar_VariableValue( Cmd_Argv( 1 ) ) ), 
			qfalse );
		return;
	}

	if ( c == 3 ) {
		Com_Printf( "toggle: nothing to toggle to\n" );
		return;
	}

	curval = Cvar_VariableString( Cmd_Argv( 1 ) );

	// don't bother checking the last arg for a match since the desired
	// behaviour is the same as no match (set to the first argument)
	for ( i = 2; i + 1 < c; i++ ) {
		if ( strcmp( curval, Cmd_Argv( i ) ) == 0 ) {
			Cvar_Set2( Cmd_Argv( 1 ), Cmd_Argv(i + 1), qfalse );
			return;
		}
	}

	// fallback
	Cvar_Set2( Cmd_Argv( 1 ), Cmd_Argv( 2 ), qfalse );
}


/*
============
Cvar_Set_f

Allows setting and defining of arbitrary cvars from console, even if they
weren't declared in C code.
============
*/
static void Cvar_Set_f( void ) {
	int		c;
	const char	*cmd;
	cvar_t	*v;

	c = Cmd_Argc();
	cmd = Cmd_Argv(0);

	if ( c < 2 ) {
		Com_Printf ("usage: %s <variable> <value>\n", cmd);
		return;
	}
	if ( c == 2 ) {
		Cvar_Print_f();
		return;
	}

	v = Cvar_Set2 (Cmd_Argv(1), Cmd_ArgsFrom(2), qfalse);
	if( !v ) {
		return;
	}
	switch( cmd[3] ) {
		case 'a':
			if( !( v->flags & CVAR_ARCHIVE ) ) {
				v->flags |= CVAR_ARCHIVE;
				cvar_modifiedFlags |= CVAR_ARCHIVE;
			}
			break;
		case 'u':
			if( !( v->flags & CVAR_USERINFO ) ) {
				v->flags |= CVAR_USERINFO;
				cvar_modifiedFlags |= CVAR_USERINFO;
			}
			break;
		case 's':
			if( !( v->flags & CVAR_SERVERINFO ) ) {
				v->flags |= CVAR_SERVERINFO;
				cvar_modifiedFlags |= CVAR_SERVERINFO;
			}
			break;
	}
}


/*
============
Cvar_Reset_f
============
*/
static void Cvar_Reset_f( void ) {
	if ( Cmd_Argc() != 2 ) {
		Com_Printf ("usage: reset <variable>\n");
		return;
	}
	Cvar_Reset( Cmd_Argv( 1 ) );
}


// returns NULL for non-existent "-" argument
static const char *GetValue( int index, int *ival, float *fval ) 
{
	static char buf[ MAX_CVAR_VALUE_STRING ];
	const char *cmd;
	cvar_t	*var;

	cmd = Cmd_Argv( index );

	if ( ( *cmd == '-' && *(cmd+1) == '\0' ) || *cmd == '\0' ) {
		*ival = 0;
		*fval = 0.0f;
		buf[0] = '\0';
		return NULL;
	}

	var = Cvar_FindVar( cmd );
	if ( !var ) // cvar not found, return string
	{
		*ival = atoi( cmd );
		*fval = Q_atof( cmd );
		Q_strncpyz( buf, cmd, sizeof( buf ) );
		return buf;
	}
	else // found cvar, extract values
	{
		*ival = var->integer;
		*fval = var->value;
		Q_strncpyz( buf, var->string, sizeof( buf ) );
		return buf;
	}
}


typedef enum {
	FT_BAD = 0,
	FT_ADD,
	FT_SUB,
	FT_MUL,
	FT_DIV,
	FT_MOD,
	FT_SIN,
	FT_COS,
	FT_RAND,
} funcType_t;


static funcType_t GetFuncType( void ) 
{
	const char *cmd;
	cmd = Cmd_Argv( 1 );
	if ( !Q_stricmp( cmd, "add" ) )
		return FT_ADD;
	if ( !Q_stricmp( cmd, "sub" ) )
		return FT_SUB;
	if ( !Q_stricmp( cmd, "mul" ) )
		return FT_MUL;
	if ( !Q_stricmp( cmd, "div" ) )
		return FT_DIV;
	if ( !Q_stricmp( cmd, "mod" ) )
		return FT_MOD;
	if ( !Q_stricmp( cmd, "sin" ) )
		return FT_SIN;
	if ( !Q_stricmp( cmd, "cos" ) )
		return FT_COS;
	if ( !Q_stricmp( cmd, "rand" ) )
		return FT_RAND;

	return FT_BAD;
}


static qboolean AllowEmptyCvar( funcType_t ftype ) 
{
	switch ( ftype ) {
		case FT_ADD:
		case FT_SUB:
		case FT_MUL:
		case FT_DIV:
		case FT_MOD:
			return qfalse;
		default:
			return qtrue;
	};
}


static void Cvar_Op( funcType_t ftype, int *ival, float *fval ) 
{
	int icap, imod;
	float fcap, fmod;

	GetValue( 3, &imod, &fmod ); // index 3: value

	switch ( ftype ) {
		case FT_ADD:
			*ival += imod;
			*fval += fmod;
			break;
		case FT_SUB:
			*ival -= imod;
			*fval -= fmod;
			break;
		case FT_MUL:
			*ival *= imod;
			*fval *= fmod;
			break;
		case FT_DIV:
			if ( imod )
				*ival /= imod;
			if ( fmod )
				*fval /= fmod;
			break;
		case FT_MOD:
			if ( imod ) {
				*ival %= imod;
				*fval = (float)( (int)*fval % imod ); // FIXME: use float
			}
			break;

		case FT_SIN:
				*ival = sin( imod );
				*fval = sin( fmod );
				break;

		case FT_COS:
				*ival = cos( imod );
				*fval = cos( fmod );
				break;
		default: 
			break;
	}

	if ( Cmd_Argc() > 4 ) { // low bound
		if ( GetValue( 4, &icap, &fcap ) ) {
			if ( *ival < icap ) *ival = icap;
			if ( *fval < fcap ) *fval = fcap;
		}
	}
	if ( Cmd_Argc() > 5 ) { // high bound
		if ( GetValue( 5, &icap, &fcap ) ) {
			if ( *ival > icap ) *ival = icap;
			if ( *fval > fcap ) *fval = fcap;
		}
	}
}


static void Cvar_Rand( int *ival, float *fval ) 
{
	int icap;
	float fcap;

	*ival = rand();
	*fval = *ival;

	if ( Cmd_Argc() > 3 ) { // base
		if ( GetValue( 3, &icap, &fcap ) ) {
			*ival += icap;
			*fval = *ival;
		}
	}
	if ( Cmd_Argc() > 4 ) { // modulus
		if ( GetValue( 4, &icap, &fcap ) ) {
			if ( icap ) {
				*ival %= icap;
				*fval = *ival;
			}
		}
	}
}


static void Cvar_Func_f( void ) {

	funcType_t	ftype;
	const char	*cvar_name;
	char		value[ 64 ];
	cvar_t		*cvar;
	int			ival;
	float		fval;

	if ( Cmd_Argc() < 3 ) {
		Com_Printf( "usage: \n" \
			"  \\varfunc <add|sub|mul|div|mod|sin|cos> <cvar> <value> [lo.cap] [hi.cap]\n" \
			"  \\varfunc rand <cvar> [base] [modulus]\n" );
		return;
	}

	//     0     1     2      3      4        5
	// \varfunc <op> <cvar> <val> [lo-cap] [hi-cap]
	
	// \varfunc rand <cvar> [base] [modulus]

	ftype = GetFuncType(); // index 1: function type
	if ( ftype == FT_BAD ) {
		Com_Printf( "%s: unknown function %s\n", Cmd_Argv( 0 ), Cmd_Argv( 1 ) );
		return;
	}

	cvar_name = Cmd_Argv( 2 ); // index 2: cvar name
	cvar = Cvar_FindVar( cvar_name );
	if ( !cvar ) {
		if ( !AllowEmptyCvar( ftype ) )	{
			Com_Printf( "Cvar '%s' does not exist.\n", cvar_name );
			return; // FIXME: allow cvar creation for some functions?
		}
	} else if ( cvar->flags & ( CVAR_INIT | CVAR_ROM | CVAR_PROTECTED ) ) {
		Com_Printf( "Cvar '%s' is write-protected.\n", cvar_name );
		return;
	}
	
	if ( cvar ) {
		fval = cvar->value;
		ival = cvar->integer;
	} else {
		fval = 0.0;
		ival = 0;
	}

	if ( ftype == FT_RAND )
		Cvar_Rand( &ival, &fval );
	else
		Cvar_Op( ftype, &ival, &fval ); // apply modification
	
	if ( cvar && cvar->validator == CV_INTEGER ) {
		sprintf( value, "%i", ival );
	} else {
		if ( (int)fval == fval )
			sprintf( value, "%i", (int)fval );
		else
			sprintf( value, "%f", fval );
	}

	Cvar_Set2( cvar_name, value, qfalse );
}


/*
============
Cvar_WriteVariables

Appends lines containing "set variable value" for all variables
with the archive flag set to qtrue.
============
*/
void Cvar_WriteVariables( fileHandle_t f )
{
	cvar_t	*var;
	char	buffer[MAX_CMD_LINE];
	const char	*value;

	if ( cvar_sort ) {
		cvar_sort = qfalse;
		Cvar_Sort();
	}

	for (var = cvar_vars; var; var = var->next)
	{
		if ( !var->name || Q_stricmp( var->name, "cl_cdkey" ) == 0 )
			continue;

		if ( var->flags & CVAR_ARCHIVE ) {
			int len;
			// write the latched value, even if it hasn't taken effect yet
			value = var->latchedString ? var->latchedString : var->string;
			if ( strlen( var->name ) + strlen( value ) + 10 > sizeof( buffer ) ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: %svalue of variable \"%s\" too long to write to file\n", 
					value == var->latchedString ? "latched " : "", var->name );
				continue;
			}
			if ( (var->flags & CVAR_NODEFAULT) && !strcmp( value, var->resetString ) ) {
				continue;
			}
			len = Com_sprintf( buffer, sizeof( buffer ), "seta %s \"%s\"" Q_NEWLINE, var->name, value );

			FS_Write( buffer, len, f );
		}
	}
}


/*
============
Cvar_List_f
============
*/
static void Cvar_List_f( void ) {
	cvar_t	*var;
	int		i;
	const char	*match;

	// sort to get more predictable output
	if ( cvar_sort ) {
		cvar_sort = qfalse;
		Cvar_Sort();
	}

	if ( Cmd_Argc() > 1 ) {
		match = Cmd_Argv( 1 );
	} else {
		match = NULL;
	}

	i = 0;
	for (var = cvar_vars ; var ; var = var->next, i++)
	{
		if(!var->name || (match && !Com_Filter(match, var->name)))
			continue;

		if (var->flags & CVAR_SERVERINFO) {
			Com_Printf("S");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_SYSTEMINFO) {
			Com_Printf("s");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_USERINFO) {
			Com_Printf("U");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ROM) {
			Com_Printf("R");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_INIT) {
			Com_Printf("I");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ARCHIVE) {
			Com_Printf("A");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_LATCH) {
			Com_Printf("L");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_CHEAT) {
			Com_Printf("C");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_USER_CREATED) {
			Com_Printf("?");
		} else {
			Com_Printf(" ");
		}

		Com_Printf (" %s \"%s\"\n", var->name, var->string);
	}

	Com_Printf ("\n%i total cvars\n", i);
	Com_Printf ("%i cvar indexes\n", cvar_numIndexes);
}


/*
============
Cvar_ListModified_f
============
*/
static void Cvar_ListModified_f( void ) {
	cvar_t	*var;
	int		totalModified;
	const char *value;
	const char *match;

	if ( Cmd_Argc() > 1 ) {
		match = Cmd_Argv( 1 );
	} else {
		match = NULL;
	}

	totalModified = 0;
	for (var = cvar_vars ; var ; var = var->next)
	{
		if ( !var->name || !var->modificationCount )
			continue;

		value = var->latchedString ? var->latchedString : var->string;
		if ( !strcmp( value, var->resetString ) )
			continue;

		totalModified++;

		if (match && !Com_Filter(match, var->name))
			continue;

		if (var->flags & CVAR_SERVERINFO) {
			Com_Printf("S");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_SYSTEMINFO) {
			Com_Printf("s");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_USERINFO) {
			Com_Printf("U");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ROM) {
			Com_Printf("R");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_INIT) {
			Com_Printf("I");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_ARCHIVE) {
			Com_Printf("A");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_LATCH) {
			Com_Printf("L");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_CHEAT) {
			Com_Printf("C");
		} else {
			Com_Printf(" ");
		}
		if (var->flags & CVAR_USER_CREATED) {
			Com_Printf("?");
		} else {
			Com_Printf(" ");
		}

		Com_Printf (" %s \"%s\", default \"%s\"\n", var->name, value, var->resetString);
	}

	Com_Printf ("\n%i total modified cvars\n", totalModified);
}


/*
============
Cvar_Unset

Unsets a cvar
============
*/
static cvar_t *Cvar_Unset( cvar_t *cv )
{
	cvar_t *next = cv->next;

	// note what types of cvars have been modified (userinfo, archive, serverinfo, systeminfo)
	cvar_modifiedFlags |= cv->flags;
	
	if ( cv->name )
		Z_Free( cv->name );
	if ( cv->string )
		Z_Free( cv->string );
	if ( cv->latchedString )
		Z_Free( cv->latchedString );
	if ( cv->resetString )
		Z_Free( cv->resetString );
	if ( cv->description )
		Z_Free( cv->description );
	if ( cv->mins )
		Z_Free( cv->mins );
	if ( cv->maxs )
		Z_Free( cv->maxs );

	if ( cv->prev )
		cv->prev->next = cv->next;
	else
		cvar_vars = cv->next;
	if ( cv->next )
		cv->next->prev = cv->prev;

	if ( cv->hashPrev )
		cv->hashPrev->hashNext = cv->hashNext;
	else
		hashTable[cv->hashIndex] = cv->hashNext;
	if ( cv->hashNext )
		cv->hashNext->hashPrev = cv->hashPrev;

	Com_Memset( cv, '\0', sizeof( *cv ) );
	
	return next;
}


/*
============
Cvar_Unset_f

Unsets a userdefined cvar
============
*/
static void Cvar_Unset_f( void )
{
	cvar_t *cv;
	
	if ( Cmd_Argc() != 2 )
	{
		Com_Printf( "Usage: %s <varname>\n", Cmd_Argv( 0 ) );
		return;
	}
	
	cv = Cvar_FindVar( Cmd_Argv( 1 ) );

	if ( !cv )
		return;
	
	if ( cv->flags & CVAR_USER_CREATED )
		Cvar_Unset( cv );
	else
		Com_Printf( "Error: %s: Variable %s is not user created.\n", 
			Cmd_Argv( 0 ), cv->name );
}


/*
============
Cvar_Restart

Resets all cvars to their hardcoded values and removes userdefined variables
and variables added via the VMs if requested.
============
*/

void Cvar_Restart( qboolean unsetVM )
{
	cvar_t *curvar = cvar_vars;

	while(curvar)
	{
		if((curvar->flags & CVAR_USER_CREATED) ||
			(unsetVM && (curvar->flags & CVAR_VM_CREATED)))
		{
			// throw out any variables the user/vm created
			curvar = Cvar_Unset(curvar);
			continue;
		}
		
		if(!(curvar->flags & (CVAR_ROM | CVAR_INIT | CVAR_NORESTART)))
		{
			// Just reset the rest to their default values.
			Cvar_Set2(curvar->name, curvar->resetString, qfalse);
		}
		
		curvar = curvar->next;
	}
}


static void Cvar_Trim( qboolean verbose )
{
	cvar_t *curvar = cvar_vars;
	while ( curvar )
	{
		if ( curvar->flags & CVAR_USER_CREATED )
		{
			// throw out any variables the user created
			if ( verbose )
				Com_Printf( "unset cvar" S_COLOR_YELLOW " %s\n", curvar->name );

			curvar = Cvar_Unset( curvar );
			continue;
		}

		curvar = curvar->next;
	}
}


/*
============
Cvar_Restart_f

Resets all cvars to their hardcoded values
============
*/
static void Cvar_Restart_f( void )
{
	Cvar_Restart( qfalse );
}


/*
============
Cvar_Trim_f

Removes all user-created cvars
This will only accept to run when both the server and client are running unless forced
============
*/
static void Cvar_Trim_f( void )
{
	qboolean forced = qfalse;
	qboolean verbose = qtrue;
	int i;

	for ( i = 1; i < Cmd_Argc(); i++ )
	{
		const char *s = Cmd_Argv( i );
		if ( *s == '-' )
		{
			s++;
			while ( *s != '\0' )
			{
				if ( *s == 'f' ) // force cleanup
					forced = qtrue;
				else if ( *s == 's' ) // silent mode
					verbose = qfalse;
				s++;
			}
		}
	}

#ifdef DEDICATED
	if ( ( com_sv_running && com_sv_running->integer ) || forced )
#else
	if ( ( com_cl_running && com_cl_running->integer && com_sv_running && com_sv_running->integer ) || forced )
#endif
	{
		Cvar_Trim( verbose );
		return;
	}

#ifdef DEDICATED	
	Com_Printf( S_COLOR_YELLOW " You're not running a server, so not all subsystems/VMs are loaded.\n" );
#else
	Com_Printf( S_COLOR_YELLOW " You're not running a listen server, so not all subsystems/VMs are loaded.\n" );
#endif
	Com_Printf( S_COLOR_YELLOW " This means you'd remove cvars that are probably best kept around.\n" );
	Com_Printf( S_COLOR_YELLOW " If you don't care, you can force the call by running '\\%s -f'.\n", Cmd_Argv(0) );
	Com_Printf( S_COLOR_YELLOW " You've been warned.\n" );
}

extern qboolean X_HckActive( void );

/*
=====================
Cvar_InfoString
=====================
*/
const char *Cvar_InfoString( int bit, qboolean *truncated )
{
	static char	info[ MAX_INFO_STRING ];
	const cvar_t *user_vars[ MAX_CVARS ];
	const cvar_t *vm_vars[ MAX_CVARS ];
	const cvar_t *var;
	int user_count;
	int vm_count;
	int i;
	qboolean allSet;

	// sort to get more predictable output
	if ( cvar_sort )
	{
		cvar_sort = qfalse;
		Cvar_Sort();
	}

	info[0] = '\0';
	user_count = 0;
	vm_count = 0;
	allSet = qtrue; // this will be qfalse on overflow

#ifndef DEDICATED
	if ( bit & CVAR_USERINFO )
	{
		allSet &= Info_SetValueForKey(info, "xq3e", XMOD_VERSION);
		allSet &= Info_SetValueForKey(info, "xq3e_hck", X_HckActive() ? "1" : "0");
	}
#endif

	for ( var = cvar_vars; var; var = var->next )
	{
		if ( var->name && ( var->flags & bit ) )
		{
			// put vm/user-created cvars to the end
			if ( var->flags & ( CVAR_USER_CREATED | CVAR_VM_CREATED ) )
			{
				if ( var->flags & CVAR_USER_CREATED )
					user_vars[ user_count++ ] = var;
				else
					vm_vars[ vm_count++ ] = var;
			}
			else
			{
				allSet &= Info_SetValueForKey( info, var->name, var->string );
			}
		}
	}

	// add vm-created cvars
	for ( i = 0; i < vm_count; i++ )
	{
		var = vm_vars[ i ];
		allSet &= Info_SetValueForKey( info, var->name, var->string );
	}

	// add user-created cvars
	for ( i = 0; i < user_count; i++ )
	{
		var = user_vars[ i ];
		allSet &= Info_SetValueForKey( info, var->name, var->string );
	}

	if ( truncated )
	{
		*truncated = !allSet;
	}

	return info;
}


/*
=====================
Cvar_InfoString_Big

  handles large info strings ( CS_SYSTEMINFO )
=====================
*/
const char *Cvar_InfoString_Big( int bit, qboolean *truncated )
{
	static char	info[BIG_INFO_STRING];
	const cvar_t *var;
	qboolean allSet;

	info[0] = '\0';
	allSet = qtrue;

	for ( var = cvar_vars; var; var = var->next )
	{
		if ( var->name && (var->flags & bit) )
			allSet &= Info_SetValueForKey_s( info, sizeof( info ), var->name, var->string );
	}

	if ( truncated )
	{
		*truncated = !allSet;
	}

	return info;
}


/*
=====================
Cvar_InfoStringBuffer
=====================
*/
void Cvar_InfoStringBuffer( int bit, char* buff, int buffsize ) {
	Q_strncpyz( buff, Cvar_InfoString( bit, NULL ), buffsize );
}


/*
=====================
Cvar_CheckRange
=====================
*/
void Cvar_CheckRange( cvar_t *var, const char *mins, const char *maxs, cvarValidator_t type )
{
	if ( type >= CV_MAX ) {
		Com_Printf( S_COLOR_YELLOW "Invalid validation type %i for %s\n", type, var->name );
		return;
	}

	if ( var->mins ) {
		Z_Free( var->mins );
		var->mins = NULL;
	}
	if ( var->maxs ) {
		Z_Free( var->maxs );
		var->maxs = NULL;
	}

	var->validator = type;

	if ( type == CV_NONE )
		return;

	if ( mins )
		var->mins = CopyString( mins );

	if ( maxs )
		var->maxs = CopyString( maxs );

	// Force an initial range check
	Cvar_Set( var->name, var->string );
}


/*
=====================
Cvar_SetDescription
=====================
*/
void Cvar_SetDescription( cvar_t *var, const char *var_description )
{
	if( var_description && var_description[0] != '\0' )
	{
		if( var->description != NULL )
		{
			Z_Free( var->description );
		}
		var->description = CopyString( var_description );
	}
}


/*
=====================
Cvar_SetGroup
=====================
*/
void Cvar_SetGroup( cvar_t *var, cvarGroup_t group ) {
	if ( group < CVG_MAX ) {
		var->group = group;
	} else {
		Com_Error( ERR_DROP, "Bad group index %i for %s", group, var->name );
	}
}


/*
=====================
Cvar_CheckGroup
=====================
*/
int Cvar_CheckGroup( cvarGroup_t group ) {
	if ( group < CVG_MAX ) {
		return cvar_group[ group ];
	} else {
		return 0;
	}
}


/*
=====================
Cvar_ResetGroup
=====================
*/
void Cvar_ResetGroup( cvarGroup_t group, qboolean resetModifiedFlags ) {
	if ( group < CVG_MAX ) {
		cvar_group[ group ] = 0;
		if ( resetModifiedFlags ) {
			int i;
			for ( i = 0; i < cvar_numIndexes; i++ ) {
				if ( cvar_indexes[ i ].group == group && cvar_indexes[ i ].name ) {
					cvar_indexes[ i ].modified = qfalse;
				}
			}
		}
	}
}


/*
=====================
Cvar_Register

basically a slightly modified Cvar_Get for the interpreted modules
=====================
*/
#define INVALID_FLAGS ( CVAR_USER_CREATED | CVAR_SERVER_CREATED | CVAR_PROTECTED | CVAR_PRIVATE | CVAR_MODIFIED | CVAR_NONEXISTENT )
void Cvar_Register( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags, int privateFlag )
{
	cvar_t	*cv;

	// There is code in Cvar_Get to prevent CVAR_ROM cvars being changed by the
	// user. In other words CVAR_ARCHIVE and CVAR_ROM are mutually exclusive
	// flags. Unfortunately some historical game code (including single player
	// baseq3) sets both flags. We unset CVAR_ROM for such cvars.
	if ((flags & (CVAR_ARCHIVE | CVAR_ROM)) == (CVAR_ARCHIVE | CVAR_ROM)) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: Unsetting CVAR_ROM from cvar '%s', "
			"since it is also CVAR_ARCHIVE\n", varName );
		flags &= ~CVAR_ROM;
	}

	// Don't allow VM to specify a different creator or other internal flags.
	if ( flags & INVALID_FLAGS ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: VM tried to set invalid flags 0x%02x on cvar '%s'\n", ( flags & INVALID_FLAGS ), varName );
		flags &= ~INVALID_FLAGS;
	}

	cv = Cvar_FindVar( varName );

	// Don't modify cvar if it's protected.
	if ( cv && ( cv->flags & ( CVAR_PROTECTED | CVAR_PRIVATE ) ) ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: VM tried to register protected cvar '%s' with value '%s'%s\n",
			varName, defaultValue, ( flags & ~cv->flags ) != 0 ? " and new flags" : "" );
		if ( cv->flags & CVAR_PRIVATE ) {
			if ( privateFlag ) {
				return;
			}
		}
	} else {
		cv = Cvar_Get( varName, defaultValue, flags | CVAR_VM_CREATED );
	}

	if (!vmCvar)
		return;

	vmCvar->handle = cv - cvar_indexes;
	vmCvar->modificationCount = -1;

	Cvar_Update( vmCvar, 0 );
}


/*
=====================
Cvar_Update

updates an interpreted modules' version of a cvar
=====================
*/
void Cvar_Update( vmCvar_t *vmCvar, int privateFlag ) {
	size_t	len;
	cvar_t	*cv = NULL;
	assert(vmCvar);

	if ( (unsigned)vmCvar->handle >= cvar_numIndexes ) {
		Com_Error( ERR_DROP, "Cvar_Update: handle out of range" );
	}

	cv = cvar_indexes + vmCvar->handle;

	if ( cv->modificationCount == vmCvar->modificationCount ) {
		return;
	}
	if ( !cv->string ) {
		return;		// variable might have been cleared by a cvar_restart
	} 
	if ( cv->flags & CVAR_PRIVATE ) {
		if ( privateFlag ) {
			return;
		}
	}
	vmCvar->modificationCount = cv->modificationCount;

	len = strlen( cv->string );
	if ( len + 1 > MAX_CVAR_VALUE_STRING ) {
		Com_Printf( S_COLOR_YELLOW "Cvar_Update: src %s length %d exceeds MAX_CVAR_VALUE_STRING - truncate\n",
			cv->string, (int)len );
	}

	Q_strncpyz( vmCvar->string, cv->string, sizeof( vmCvar->string ) ); 

	vmCvar->value = cv->value;
	vmCvar->integer = cv->integer;
}


/*
==================
Cvar_CompleteCvarName
==================
*/
void Cvar_CompleteCvarName( const char *args, int argNum )
{
	if( argNum == 2 )
	{
		// Skip "<cmd> "
		const char *p = Com_SkipTokens( args, 1, " " );

		if( p > args )
			Field_CompleteCommand( p, qfalse, qtrue );
	}
}


/*
============
Cvar_Init

Reads in all archived cvars
============
*/
void Cvar_Init (void)
{
	Com_Memset(cvar_indexes, '\0', sizeof(cvar_indexes));
	Com_Memset(hashTable, '\0', sizeof(hashTable));

	cvar_cheats = Cvar_Get( "sv_cheats", "1", CVAR_ROM | CVAR_SYSTEMINFO );
	Cvar_SetDescription( cvar_cheats, "Enable cheating commands (server side only)." );
	cvar_developer = Cvar_Get( "developer", "0", CVAR_TEMP );
	Cvar_SetDescription( cvar_developer, "Toggles developer mode. Prints more info to console and provides more commands." );

	Cmd_AddCommand ("print", Cvar_Print_f);
	Cmd_AddCommand ("toggle", Cvar_Toggle_f);
	Cmd_SetCommandCompletionFunc( "toggle", Cvar_CompleteCvarName );
	Cmd_AddCommand ("set", Cvar_Set_f);
	Cmd_SetCommandCompletionFunc( "set", Cvar_CompleteCvarName );
	Cmd_AddCommand ("sets", Cvar_Set_f);
	Cmd_SetCommandCompletionFunc( "sets", Cvar_CompleteCvarName );
	Cmd_AddCommand ("setu", Cvar_Set_f);
	Cmd_SetCommandCompletionFunc( "setu", Cvar_CompleteCvarName );
	Cmd_AddCommand ("seta", Cvar_Set_f);
	Cmd_SetCommandCompletionFunc( "seta", Cvar_CompleteCvarName );
	Cmd_AddCommand ("reset", Cvar_Reset_f);
	Cmd_SetCommandCompletionFunc( "reset", Cvar_CompleteCvarName );
	Cmd_AddCommand ("unset", Cvar_Unset_f);
	Cmd_SetCommandCompletionFunc("unset", Cvar_CompleteCvarName);

	Cmd_AddCommand( "varfunc", Cvar_Func_f );

	Cmd_AddCommand ("cvarlist", Cvar_List_f);
	Cmd_AddCommand ("cvar_modified", Cvar_ListModified_f);
	Cmd_AddCommand ("cvar_restart", Cvar_Restart_f);
	Cmd_AddCommand ("cvar_trim", Cvar_Trim_f);
}
