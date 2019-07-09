/*
===========================================================================
Copyright (C) 2005-2006 Tim Angus

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

#include "client.h"
#include "snd_local.h"

#define INDEX_FILE_EXTENSION ".index.dat"

#define MAX_RIFF_CHUNKS 16

typedef struct audioFormat_s
{
  int rate;
  int format;
  int channels;
  int bits;

  int sampleSize;
  unsigned int totalBytes;
} audioFormat_t;

typedef struct aviFileData_s
{
  qboolean      fileOpen;
  qboolean      pipe;
  fileHandle_t  f;
  char          fileName[ MAX_QPATH ];
  unsigned int  fileSize;
  unsigned int  moviOffset;
  unsigned int  moviSize;

  fileHandle_t  idxF;
  int           numIndices;

  int           frameRate;
  int           framePeriod;
  int           width, height;
  int           numVideoFrames;
  int           maxRecordSize;
  qboolean      motionJpeg;

  qboolean      audio;
  audioFormat_t a;
  int           numAudioFrames;
  int           audioFrameSize;

  int           chunkStack[ MAX_RIFF_CHUNKS ];
  int           chunkStackTop;

  byte          *cBuffer, *eBuffer;
} aviFileData_t;

static aviFileData_t afd;

#define MAX_AVI_BUFFER 2048

static byte buffer[ MAX_AVI_BUFFER ];
static int  bufIndex;


/*
===============
SafeFS_Write
===============
*/
static ID_INLINE void SafeFS_Write( const void *buf, int len, fileHandle_t f )
{
  if ( FS_Write( buf, len, f ) < len )
		Com_Error( ERR_DROP, "Failed to write avi file" );
}


/*
===============
WRITE_STRING
===============
*/
static ID_INLINE void WRITE_STRING( const char *s )
{
  Com_Memcpy( &buffer[ bufIndex ], s, strlen( s ) );
  bufIndex += strlen( s );
}


/*
===============
WRITE_4BYTES
===============
*/
static ID_INLINE void WRITE_4BYTES( int x )
{
  buffer[ bufIndex + 0 ] = (byte)( ( x >>  0 ) & 0xFF );
  buffer[ bufIndex + 1 ] = (byte)( ( x >>  8 ) & 0xFF );
  buffer[ bufIndex + 2 ] = (byte)( ( x >> 16 ) & 0xFF );
  buffer[ bufIndex + 3 ] = (byte)( ( x >> 24 ) & 0xFF );
  bufIndex += 4;
}


/*
===============
WRITE_2BYTES
===============
*/
static ID_INLINE void WRITE_2BYTES( int x )
{
  buffer[ bufIndex + 0 ] = (byte)( ( x >>  0 ) & 0xFF );
  buffer[ bufIndex + 1 ] = (byte)( ( x >>  8 ) & 0xFF );
  bufIndex += 2;
}


/*
===============
START_CHUNK
===============
*/
static ID_INLINE void START_CHUNK( const char *s )
{
	if( afd.chunkStackTop >= MAX_RIFF_CHUNKS )
	{
		Com_Error( ERR_DROP, "ERROR: Top of chunkstack breached" );
	} 
	else 
	{
		afd.chunkStack[ afd.chunkStackTop ] = bufIndex;
		afd.chunkStackTop++;
		WRITE_STRING( s );
		WRITE_4BYTES( 0 );
	}
}


/*
===============
END_CHUNK
===============
*/
static ID_INLINE void END_CHUNK( void )
{
	int endIndex = bufIndex;

	if( afd.chunkStackTop <= 0 )
	{
		Com_Error( ERR_DROP, "ERROR: Bottom of chunkstack breached" );
	} 
	else
	{
		afd.chunkStackTop--;
		bufIndex = afd.chunkStack[ afd.chunkStackTop ];
		bufIndex += 4;
		WRITE_4BYTES( endIndex - bufIndex - 4 );
		bufIndex = endIndex;
		bufIndex = PAD( bufIndex, 2 );
	}
}


/*
===============
CL_WriteAVIHeader
===============
*/
static void CL_WriteAVIHeader( void )
{
  bufIndex = 0;
  afd.chunkStackTop = 0;

  START_CHUNK( "RIFF" );
  {
    WRITE_STRING( "AVI " );
    {
      START_CHUNK( "LIST" );
      {
        WRITE_STRING( "hdrl" );
        WRITE_STRING( "avih" );
        WRITE_4BYTES( 56 );                     //"avih" "chunk" size
        WRITE_4BYTES( afd.framePeriod );        //dwMicroSecPerFrame
        WRITE_4BYTES( afd.maxRecordSize * afd.frameRate ); //dwMaxBytesPerSec
        WRITE_4BYTES( 0 );                      //dwReserved1
        if ( afd.pipe )
          WRITE_4BYTES( 0x100 );                //dwFlags bits IS_INTERLEAVED=0x100
        else
          WRITE_4BYTES( 0x110 );                //dwFlags bits HAS_INDEX=0x10 and IS_INTERLEAVED=0x100
        WRITE_4BYTES( afd.numVideoFrames );     //dwTotalFrames
        WRITE_4BYTES( 0 );                      //dwInitialFrame

        if( afd.audio )                         //dwStreams
          WRITE_4BYTES( 2 );
        else
          WRITE_4BYTES( 1 );

        WRITE_4BYTES( afd.maxRecordSize );      //dwSuggestedBufferSize
        WRITE_4BYTES( afd.width );              //dwWidth
        WRITE_4BYTES( afd.height );             //dwHeight
        WRITE_4BYTES( 0 );                      //dwReserved[ 0 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 1 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 2 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 3 ]

        START_CHUNK( "LIST" );
        {
          WRITE_STRING( "strl" );
          WRITE_STRING( "strh" );
          WRITE_4BYTES( 56 );                   //"strh" "chunk" size
          WRITE_STRING( "vids" );

          if ( afd.motionJpeg && !afd.pipe )
            WRITE_STRING( "MJPG" );
          else
            WRITE_4BYTES( 0 );                  // BI_RGB

          WRITE_4BYTES( 0 );                    //dwFlags
          WRITE_4BYTES( 0 );                    //dwPriority
          WRITE_4BYTES( 0 );                    //dwInitialFrame

          WRITE_4BYTES( 1 );                    //dwTimescale
          WRITE_4BYTES( afd.frameRate );        //dwDataRate
          WRITE_4BYTES( 0 );                    //dwStartTime
          WRITE_4BYTES( afd.numVideoFrames );   //dwDataLength

          WRITE_4BYTES( afd.maxRecordSize );    //dwSuggestedBufferSize
          WRITE_4BYTES( -1 );                   //dwQuality
          WRITE_4BYTES( 0 );                    //dwSampleSize
          WRITE_2BYTES( 0 );                    //rcFrame
          WRITE_2BYTES( 0 );                    //rcFrame
          WRITE_2BYTES( afd.width );            //rcFrame
          WRITE_2BYTES( afd.height );           //rcFrame

          WRITE_STRING( "strf" );
          WRITE_4BYTES( 40 );                   //"strf" "chunk" size
          WRITE_4BYTES( 40 );                   //biSize
          WRITE_4BYTES( afd.width );            //biWidth
          WRITE_4BYTES( afd.height );           //biHeight
          WRITE_2BYTES( 1 );                    //biPlanes
          WRITE_2BYTES( 24 );                   //biBitCount

          if( afd.motionJpeg && !afd.pipe )     //biCompression
          {
            WRITE_STRING( "MJPG" );
            WRITE_4BYTES( afd.width *
              afd.height );                     //biSizeImage
          }
          else
          {
            WRITE_4BYTES( 0 );                  // BI_RGB
            WRITE_4BYTES( afd.width *
                afd.height * 3 );               //biSizeImage
          }

          WRITE_4BYTES( 0 );                    //biXPelsPetMeter
          WRITE_4BYTES( 0 );                    //biYPelsPetMeter
          WRITE_4BYTES( 0 );                    //biClrUsed
          WRITE_4BYTES( 0 );                    //biClrImportant
        }
        END_CHUNK( );

        if( afd.audio )
        {
          START_CHUNK( "LIST" );
          {
            WRITE_STRING( "strl" );
            WRITE_STRING( "strh" );
            WRITE_4BYTES( 56 );                 //"strh" "chunk" size
            WRITE_STRING( "auds" );
            WRITE_4BYTES( 0 );                  //FCC
            WRITE_4BYTES( 0 );                  //dwFlags
            WRITE_4BYTES( 0 );                  //dwPriority
            WRITE_4BYTES( 0 );                  //dwInitialFrame

            WRITE_4BYTES( afd.a.sampleSize );   //dwTimescale
            WRITE_4BYTES( afd.a.sampleSize *
                afd.a.rate );                   //dwDataRate
            WRITE_4BYTES( 0 );                  //dwStartTime
            WRITE_4BYTES( afd.a.totalBytes /
                afd.a.sampleSize );             //dwDataLength

            WRITE_4BYTES( 0 );                  //dwSuggestedBufferSize
            WRITE_4BYTES( -1 );                 //dwQuality
            WRITE_4BYTES( afd.a.sampleSize );   //dwSampleSize
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame

            WRITE_STRING( "strf" );
            WRITE_4BYTES( 18 );                 //"strf" "chunk" size
            WRITE_2BYTES( afd.a.format );       //wFormatTag
            WRITE_2BYTES( afd.a.channels );     //nChannels
            WRITE_4BYTES( afd.a.rate );         //nSamplesPerSec
            WRITE_4BYTES( afd.a.sampleSize *
                afd.a.rate );                   //nAvgBytesPerSec
            WRITE_2BYTES( afd.a.sampleSize );   //nBlockAlign
            WRITE_2BYTES( afd.a.bits );         //wBitsPerSample
            WRITE_2BYTES( 0 );                  //cbSize
          }
          END_CHUNK( );
        }
      }
      END_CHUNK( );

      afd.moviOffset = bufIndex;

      START_CHUNK( "LIST" );
      {
        WRITE_STRING( "movi" );
      }
    }
  }
}


static qboolean CL_ValidatePipeFormat( const char *s )
{
	while ( *s != '\0' ) 
	{
		if ( *s == '.' && *(s+1) == '.' && ( *(s+2) == '/' || *(s+2) == '\\' ) )
			return qfalse;
		if ( *s == ':' && *(s+1) == ':' )
			return qfalse;
		if ( *s == '>' || *s == '|' || *s == '&' )
			return qfalse;
		s++;
	}
	return qtrue;
}


/*
===============
CL_OpenAVIForWriting

Creates an AVI file and gets it into a state where
writing the actual data can begin
===============
*/
qboolean CL_OpenAVIForWriting( const char *fileName, qboolean pipe )
{
  if ( afd.fileOpen )
    return qfalse;

  Com_Memset( &afd, 0, sizeof( aviFileData_t ) );

  if ( pipe )
  {
    char cmd[ MAX_OSPATH * 4 ];
    const char *cmd_fmt = "ffmpeg -f avi -i - -threads 0 -y %s \"%s\" 2> \"%s-log.txt\"";
    const char *ospath;

    if ( !CL_ValidatePipeFormat( cl_aviPipeFormat->string ) ) {
        Com_Printf( S_COLOR_YELLOW "Invalid pipe format: %s\n", cl_aviPipeFormat->string );
        return qfalse;
    }

    ospath = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "", fileName );
    Com_sprintf( cmd, sizeof( cmd ), cmd_fmt, cl_aviPipeFormat->string, ospath, ospath );
    if( ( afd.f = FS_PipeOpenWrite( cmd, fileName ) ) == FS_INVALID_HANDLE )
      return qfalse;
  }
  else
  {
    if( ( afd.f = FS_FOpenFileWrite( fileName ) ) == FS_INVALID_HANDLE )
      return qfalse;

    if( ( afd.idxF = FS_FOpenFileWrite( va( "%s" INDEX_FILE_EXTENSION, fileName ) ) ) == FS_INVALID_HANDLE )
    {
      FS_FCloseFile( afd.f );
      return qfalse;
    }
  }

  Q_strncpyz( afd.fileName, fileName, sizeof( afd.fileName ) );

  afd.frameRate = cl_aviFrameRate->integer;
  afd.framePeriod = (int)( 1000000.0 / afd.frameRate );

  afd.width = cls.captureWidth;
  afd.height = cls.captureHeight;

  if ( cl_aviMotionJpeg->integer && !pipe )
    afd.motionJpeg = qtrue;
  else
    afd.motionJpeg = qfalse;

  // Buffers only need to store RGB pixels.
  // Allocate a bit more space for the capture buffer to account for possible
  // padding at the end of pixel lines, and padding for alignment
  #define MAX_PACK_LEN 16
  //afd.cBuffer = Z_Malloc((afd.width * 3 + MAX_PACK_LEN - 1) * afd.height + MAX_PACK_LEN - 1);
  afd.cBuffer = Z_Malloc((afd.width * afd.height * 4) + MAX_PACK_LEN - 1); // allocate for RGBA storage
  // raw avi files have pixel lines start on 4-byte boundaries
  afd.eBuffer = Z_Malloc(PAD(afd.width * 3, AVI_LINE_PADDING) * afd.height);

  afd.a.rate = dma.speed;
  afd.a.format = WAV_FORMAT_PCM;
  afd.a.channels = dma.channels;
  afd.a.bits = dma.samplebits;
  afd.a.sampleSize = ( afd.a.bits * afd.a.channels ) / 8;

  afd.audioFrameSize = ceil( (float)(afd.a.rate * afd.a.sampleSize) / (float)afd.frameRate );

  if ( !Cvar_VariableIntegerValue( "s_initsound" ) )
  {
    afd.audio = qfalse;
  }
  else
  {
    if ( afd.a.bits != 16 || afd.a.channels != 2 )
    {
      Com_Printf( S_COLOR_YELLOW "WARNING: Audio format of %d bit/%d channels not supported",
          afd.a.bits, afd.a.channels );
      afd.audio = qfalse;
    }
    else
      afd.audio = qtrue;
  }

  // This doesn't write a real header, but allocates the
  // correct amount of space at the beginning of the file
  CL_WriteAVIHeader();

  if ( pipe )
  {
    afd.pipe = qtrue;
    SafeFS_Write( buffer, bufIndex, afd.f );
    bufIndex = 0;
  }
  else
  {
    SafeFS_Write( buffer, bufIndex, afd.f );
    afd.fileSize = bufIndex;

    bufIndex = 0;
    START_CHUNK( "idx1" );
    SafeFS_Write( buffer, bufIndex, afd.idxF );

    afd.moviSize = 4; // For the "movi"
  }
  afd.fileOpen = qtrue;

  return qtrue;
}


/*
===============
CL_CheckFileSize
===============
*/
static qboolean CL_CheckFileSize( int bytesToAdd )
{
	unsigned int newFileSize;

	if ( afd.pipe )
	{
		return qfalse;
	}

	newFileSize =
		afd.fileSize +                // Current file size
		bytesToAdd +                  // What we want to add
		( afd.numIndices * 16 ) +     // The index
		4;                            // The index size

	// I assume all the operating systems
	// we target can handle a 2Gb file
	//if( newFileSize > INT_MAX )
	if( newFileSize > UINT_MAX || newFileSize < afd.fileSize )
	{
		// Close the current file...
		CL_CloseAVI();

		// ...And open a new one
		CL_OpenAVIForWriting( va( "%s-%02d.avi", clc.videoName, ++clc.videoIndex ), qfalse );

		return qtrue;
	}

	return qfalse;
}


/*
===============
CL_WriteAVIVideoFrame
===============
*/
void CL_WriteAVIVideoFrame( const byte *imageBuffer, int size )
{
  unsigned int chunkOffset = afd.fileSize - afd.moviOffset - 8;
  int   chunkSize = 8 + size;
  int   paddingSize = PADLEN(size, 2);
  byte  padding[ 4 ] = { 0 };

  if( !afd.fileOpen )
    return;

  // Chunk header + contents + padding
  if ( CL_CheckFileSize( 8 + size + 2 ) )
    return;

  bufIndex = 0;
  WRITE_STRING( "00dc" );
  WRITE_4BYTES( size );
  afd.numVideoFrames++;

  SafeFS_Write( buffer, 8, afd.f );
  SafeFS_Write( imageBuffer, size, afd.f );
  SafeFS_Write( padding, paddingSize, afd.f );

  if ( afd.pipe )
    return;

  afd.fileSize += ( chunkSize + paddingSize );
  afd.moviSize += ( chunkSize + paddingSize );

  if ( size > afd.maxRecordSize )
    afd.maxRecordSize = size;

  // Index
  bufIndex = 0;
  WRITE_STRING( "00dc" );           //dwIdentifier
  WRITE_4BYTES( 0x00000010 );       //dwFlags (all frames are KeyFrames)
  WRITE_4BYTES( chunkOffset );      //dwOffset
  WRITE_4BYTES( size );             //dwLength
  SafeFS_Write( buffer, 16, afd.idxF );

  afd.numIndices++;
}


#define PCM_BUFFER_SIZE 44100

static byte pcmCaptureBuffer[ PCM_BUFFER_SIZE ];
static int  bytesInBuffer = 0;

/*
===============
CL_FlushAudioBuffer
===============
*/
static void CL_FlushCaptureBuffer( void ) 
{
    unsigned int chunkOffset = afd.fileSize - afd.moviOffset - 8;
    int   chunkSize = 8 + bytesInBuffer;
    int   paddingSize = PADLEN( bytesInBuffer, 2 );
    byte  padding[ 4 ] = { 0 };

    if ( !bytesInBuffer )
        return;

    bufIndex = 0;
    WRITE_STRING( "01wb" );
    WRITE_4BYTES( bytesInBuffer );
    afd.numAudioFrames++;

    SafeFS_Write( buffer, 8, afd.f );
    SafeFS_Write( pcmCaptureBuffer, bytesInBuffer, afd.f );
    SafeFS_Write( padding, paddingSize, afd.f );

    if ( !afd.pipe )
    {
        afd.fileSize += ( chunkSize + paddingSize );
        afd.moviSize += ( chunkSize + paddingSize );
        afd.a.totalBytes += bytesInBuffer;
        // Index
        bufIndex = 0;
        WRITE_STRING( "01wb" );           //dwIdentifier
        WRITE_4BYTES( 0 );                //dwFlags
        WRITE_4BYTES( chunkOffset );      //dwOffset
        WRITE_4BYTES( bytesInBuffer );    //dwLength
        SafeFS_Write( buffer, 16, afd.idxF );
        afd.numIndices++;
    }

    bytesInBuffer = 0;
}


/*
===============
CL_WriteAVIAudioFrame
===============
*/
void CL_WriteAVIAudioFrame( const byte *pcmBuffer, int size )
{
	if( !afd.audio )
		return;

	if( !afd.fileOpen )
		return;

	// Chunk header + contents + padding
	if( CL_CheckFileSize( 8 + bytesInBuffer + size + 2 ) )
		return;

	if( bytesInBuffer + size > PCM_BUFFER_SIZE )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Audio capture buffer overflow -- truncating\n" );
		size = PCM_BUFFER_SIZE - bytesInBuffer;
	}

	// Only write if we have a frame's worth of audio
	//if( bytesInBuffer >= afd.audioFrameSize )
	if( bytesInBuffer + size > afd.audioFrameSize )
	{
		CL_FlushCaptureBuffer();
	}

	if ( pcmBuffer ) 
	{
		Com_Memcpy( &pcmCaptureBuffer[ bytesInBuffer ], pcmBuffer, size );
		bytesInBuffer += size;
	}
}


/*
===============
CL_TakeVideoFrame
===============
*/
void CL_TakeVideoFrame( void )
{
	// AVI file isn't open
	if( !afd.fileOpen )
		return;

	re.TakeVideoFrame( afd.width, afd.height,
		afd.cBuffer, afd.eBuffer, afd.motionJpeg );
}


/*
===============
CL_CloseAVI

Closes the AVI file and writes an index chunk
===============
*/
qboolean CL_CloseAVI( void )
{
	int indexRemainder;
	int indexSize;
	const char *idxFileName;

	// AVI file isn't open
	if( !afd.fileOpen ) {
		return qfalse;
	}

	CL_FlushCaptureBuffer();

	Z_Free( afd.cBuffer );
	Z_Free( afd.eBuffer );

	if ( afd.pipe )
	{
		Com_Printf( "Wrote %d:%d frames to pipe:%s\n", afd.numVideoFrames, afd.numAudioFrames, afd.fileName );
		FS_FCloseFile( afd.f );
		afd.f = FS_INVALID_HANDLE;
		afd.fileOpen = qfalse;
		afd.pipe = qfalse;
		return qtrue;
	}

	idxFileName = va( "%s" INDEX_FILE_EXTENSION, afd.fileName );
	indexSize = afd.numIndices * 16;

	afd.fileOpen = qfalse;

	FS_Seek( afd.idxF, 4, FS_SEEK_SET );
	bufIndex = 0;
	WRITE_4BYTES( indexSize );
	SafeFS_Write( buffer, bufIndex, afd.idxF );
	FS_FCloseFile( afd.idxF );
	afd.idxF = FS_INVALID_HANDLE;

	// Write index

	// Open the temp index file
	if( ( indexSize = FS_Home_FOpenFileRead( idxFileName, &afd.idxF ) ) <= 0 )
	{
		if ( afd.idxF != FS_INVALID_HANDLE ) 
		{
			FS_FCloseFile( afd.idxF );
		}
		FS_FCloseFile( afd.f );
		return qfalse;
	}

	indexRemainder = indexSize;

	// Append index to end of avi file
	while( indexRemainder > MAX_AVI_BUFFER )
	{
		FS_Read( buffer, MAX_AVI_BUFFER, afd.idxF );
		SafeFS_Write( buffer, MAX_AVI_BUFFER, afd.f );
		afd.fileSize += MAX_AVI_BUFFER;
		indexRemainder -= MAX_AVI_BUFFER;
	}

	FS_Read( buffer, indexRemainder, afd.idxF );
	SafeFS_Write( buffer, indexRemainder, afd.f );
	afd.fileSize += indexRemainder;
	FS_FCloseFile( afd.idxF );

	// Remove temp index file
	FS_HomeRemove( idxFileName );

	// Write the real header
	FS_Seek( afd.f, 0, FS_SEEK_SET );
	CL_WriteAVIHeader();

	bufIndex = 4;
	WRITE_4BYTES( afd.fileSize - 8 ); // "RIFF" size

	bufIndex = afd.moviOffset + 4;    // Skip "LIST"
	WRITE_4BYTES( afd.moviSize );

	SafeFS_Write( buffer, bufIndex, afd.f );

	FS_FCloseFile( afd.f );

	Com_DPrintf( "Wrote %d:%d frames to %s\n", afd.numVideoFrames, afd.numAudioFrames, afd.fileName );

	return qtrue;
}


/*
===============
CL_VideoRecording
===============
*/
qboolean CL_VideoRecording( void )
{
  return afd.fileOpen;
}
