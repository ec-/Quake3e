/* gcc -O2 -fno-strict-aliasing -DUSE_CURL -ffunction-sections -fdata-sections
 * tools/tests/download_url.c code/client/cl_curl.c code/qcommon/q_shared.c
 * -Wl,--gc-sections -lcurl -o /tmp/download-test
 * /tmp/download-test
 */
/* Real URL setup/cleanup with libcurl; no transfer or file I/O. */
#include "../../code/client/client.h"
#include <assert.h>

clientStatic_t cls;
static cvar_t settings;
cvar_t *cl_dlDirectory = &settings;
cvar_t *com_developer = &settings;
void QDECL Com_Printf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_DPrintf( const char *fmt, ... ) { (void)fmt; }
void QDECL Com_Error( errorParm_t level, const char *fmt, ... ) { (void)level; (void)fmt; abort(); }
void Cvar_Set( const char *name, const char *value ) { (void)name; (void)value; }
void Cvar_SetIntegerValue( const char *name, int value ) { (void)name; (void)value; }
void Sys_UnloadLibrary( void *handle ) { assert( !handle ); }
void FS_FCloseFile( fileHandle_t handle ) { (void)handle; assert( 0 ); }
void FS_Remove( const char *path ) { (void)path; }
const char *FS_GetBaseGameDir( void ) { return "baseq3"; }
const char *FS_GetCurrentGameDir( void ) { return "baseq3"; }
qboolean Key_IsDown( int key ) { (void)key; return qfalse; }
qboolean CL_ValidPakSignature( const byte *data, int len ) { (void)data; (void)len; assert( 0 ); return qfalse; }
fileHandle_t FS_SV_FOpenFileWrite( const char *path ) { (void)path; assert( 0 ); return FS_INVALID_HANDLE; }
int FS_Write( const void *buffer, int len, fileHandle_t handle ) { (void)buffer; (void)len; (void)handle; assert( 0 ); return 0; }

qboolean FS_StripExt( char *name, const char *ext ) { (void)name; (void)ext; return qfalse; }

int main( void )
{
	static const struct { const char *base, *url; qboolean header; } cases[] = {
		{ "https://example.invalid/maps", "https://example.invalid/maps/map%20name.pk3", qfalse },
		{ "https://example.invalid/maps/", "https://example.invalid/maps/map%20name.pk3", qfalse },
		{ "https://example.invalid/get?name=%1", "https://example.invalid/get?name=map%20name.pk3", qtrue },
		{ "", "/map%20name.pk3", qfalse }
	};
	unsigned int i;
	for ( i = 0; i < sizeof( cases ) / sizeof( cases[0] ); i++ )
	{
		download_t dl = { 0 };
		assert( Com_DL_Begin( &dl, "map name.pk3", cases[i].base, qfalse ) );
		printf( "%u %s %d\n", i, dl.URL, dl.headerCheck );
		fflush( stdout );
		assert( strcmp( dl.URL, cases[i].url ) == 0 );
		assert( dl.headerCheck == cases[i].header );
		Com_DL_Cleanup( &dl );
	}
	return 0;
}
