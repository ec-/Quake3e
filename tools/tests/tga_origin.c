/* gcc -O2 -ffunction-sections -fdata-sections tools/tests/tga_origin.c \
 * code/qcommon/q_shared.c -Wl,--gc-sections -o /tmp/tga-origin
 * /tmp/tga-origin
 */
#include "../../code/renderercommon/tr_image_tga.c"
#include <assert.h>

refimport_t ri;

enum { WIDTH = 2, HEIGHT = 3, PIXELS = WIDTH * HEIGHT };
static const byte pixels[PIXELS][4] = {
	{ 21, 41, 61, 81 }, { 22, 42, 62, 82 },
	{ 22, 42, 62, 82 }, { 22, 42, 62, 82 },
	{ 23, 43, 63, 83 }, { 24, 44, 64, 84 }
};
static byte fileData[18 + PIXELS * 5];
static int fileSize;

static int ReadFile( const char *name, void **buffer ) {
	(void)name;
	*buffer = fileData;
	return fileSize;
}

static void FreeFile( void *buffer ) {
	assert( buffer == fileData );
}

static void QDECL Print( printParm_t level, const char *format, ... ) {
	va_list args;

	(void)level;
	va_start( args, format );
	vfprintf( stderr, format, args );
	va_end( args );
}

static NORETURN void QDECL ImageError( errorParm_t level, const char *format, ... ) {
	(void)level;
	(void)format;
	abort();
}

static const byte *FilePixel( int index, qboolean topOrigin ) {
	const int row = index / WIDTH;
	const int column = index % WIDTH;
	return pixels[(topOrigin ? row : HEIGHT - 1 - row) * WIDTH + column];
}

static qboolean CheckImage( byte depth, qboolean rle, qboolean topOrigin ) {
	int i, width, height;
	byte *image, *out = fileData + 18;
	byte expected[PIXELS][4];

	fileData[2] = rle ? 10 : (depth == 8 ? 3 : 2);
	fileData[12] = WIDTH;
	fileData[14] = HEIGHT;
	fileData[16] = depth;
	fileData[17] = (topOrigin ? 0x20 : 0) | (depth == 32 ? 8 : 0);

	for ( i = 0; i < PIXELS; i++ ) {
		const byte *pixel = FilePixel( i, topOrigin );

		if ( rle ) {
			const qboolean repeat = i + 1 < PIXELS && pixel[0] == FilePixel( i + 1, topOrigin )[0];
			// Use both raw packets and run-length packets.
			*out++ = repeat ? 0x81 : 0;
			i += repeat;
		}
		if ( depth != 8 ) {
			*out++ = pixel[2];
			*out++ = pixel[1];
		}
		*out++ = pixel[0];
		if ( depth == 32 ) {
			*out++ = pixel[3];
		}
	}
	fileSize = out - fileData;

	for ( i = 0; i < PIXELS; i++ ) {
		expected[i][0] = pixels[i][0];
		expected[i][1] = depth == 8 ? pixels[i][0] : pixels[i][1];
		expected[i][2] = depth == 8 ? pixels[i][0] : pixels[i][2];
		expected[i][3] = depth == 32 ? pixels[i][3] : 255;
	}

	R_LoadTGA( "fixture.tga", &image, &width, &height );
	assert( image && width == WIDTH && height == HEIGHT );
	const qboolean matches = memcmp( image, expected, sizeof( expected ) ) == 0;
	ri.Free( image );
	return matches;
}

int main( void ) {
	static const byte depths[] = { 8, 24, 32 };
	int i, rle, origin, failures = 0;

	ri.FS_ReadFile = ReadFile;
	ri.FS_FreeFile = FreeFile;
	ri.Printf = Print;
	ri.Error = ImageError;
	ri.Malloc = malloc;
	ri.Free = free;

	for ( i = 0; i < ARRAY_LEN( depths ); i++ ) {
		const byte depth = depths[i];

		for ( rle = 0; rle <= (depth != 8); rle++ ) {
			for ( origin = 0; origin <= 1; origin++ ) {
				if ( !CheckImage( depth, rle, origin ) ) {
					fprintf( stderr, "FAIL: depth=%d rle=%d topOrigin=%d\n", depth, rle, origin );
					failures++;
				}
			}
		}
	}

	if ( failures ) {
		return EXIT_FAILURE;
	}
	puts( "PASS: TGA pixel order for both vertical origins" );
	return EXIT_SUCCESS;
}
