/*=============================================================================
	FFileManagerLinux.h: Unreal Linux based file manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Brandon Reinhart
=============================================================================*/

#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>
#include "FFileManagerGeneric.h"

/*-----------------------------------------------------------------------------
	File Manager.
-----------------------------------------------------------------------------*/

// File manager.
class FArchiveFileReader : public FArchive
{
public:
	FArchiveFileReader( FILE* InFile, FOutputDevice* InError, INT InSize )
	:	File			( InFile )
	,	Error			( InError )
	,	Size			( InSize )
	,	Pos				( 0 )
	,	BufferBase		( 0 )
	,	BufferCount		( 0 )
	{
		guard(FArchiveFileReader::FArchiveFileReader);
		fseek( File, 0, SEEK_SET );
		ArIsLoading = ArIsPersistent = 1;
		unguard;
	}
	~FArchiveFileReader()
	{
		guard(FArchiveFileReader::~FArchiveFileReader);
		if( File )
			Close();
		unguard;
	}
	void Precache( INT HintCount )
	{
		guardSlow(FArchiveFileReader::Precache);
		checkSlow(Pos==BufferBase+BufferCount);
		BufferBase = Pos;
		BufferCount = Min( Min( HintCount, (INT)(ARRAY_COUNT(Buffer) - (Pos&(ARRAY_COUNT(Buffer)-1))) ), Size-Pos );
		if( fread( Buffer, BufferCount, 1, File )!=1 && BufferCount!=0 )
		{
			ArIsError = 1;
			Error->Logf( TEXT("fread failed: BufferCount=%i Error=%i"), BufferCount, ferror(File) );
			return;
		}
		unguardSlow;
	}
	void Seek( INT InPos )
	{
		guard(FArchiveFileReader::Seek);
		check(InPos>=0);
		check(InPos<=Size);
		if( fseek(File,InPos,SEEK_SET) )
		{
			ArIsError = 1;
			Error->Logf( TEXT("seek Failed %i/%i: %i %i"), InPos, Size, Pos, ferror(File) );
		}
		Pos         = InPos;
		BufferBase  = Pos;
		BufferCount = 0;
		unguard;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Size;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileReader::Close);
		if( File )
			fclose( File );
		File = NULL;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		guardSlow(FArchiveFileReader::Serialize);
		while( Length>0 )
		{
			INT Copy = Min( Length, BufferBase+BufferCount-Pos );
			if( Copy==0 )
			{
				if( Length >= ARRAY_COUNT(Buffer) )
				{
					if( fread( V, Length, 1, File )!=1 )
					{
						ArIsError = 1;
						Error->Logf( TEXT("fread failed: Length=%i Error=%i"), Length, ferror(File) );
					}
					Pos += Length;
					BufferBase += Length;
					return;
				}
				Precache( MAXINT );
				Copy = Min( Length, BufferBase+BufferCount-Pos );
				if( Copy<=0 )
				{
					ArIsError = 1;
					Error->Logf( TEXT("ReadFile beyond EOF %i+%i/%i"), Pos, Length, Size );
				}
				if( ArIsError )
					return;
			}
			appMemcpy( V, Buffer+Pos-BufferBase, Copy );
			Pos       += Copy;
			Length    -= Copy;
			V          = (BYTE*)V + Copy;
		}
		unguardSlow;
	}
protected:
	FILE*			File;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	BYTE			Buffer[1024];
};
class FArchiveFileWriter : public FArchive
{
public:
	FArchiveFileWriter( FILE* InFile, FOutputDevice* InError )
	:	File		(InFile)
	,	Error		( InError )
	,	Pos			(0)
	,	BufferCount	(0)
	{}
	~FArchiveFileWriter()
	{
		guard(FArchiveFileWriter::~FArchiveFileWriter);
		if( File )
			Close();
		File = NULL;
		unguard;
	}
	void Seek( INT InPos )
	{
		Flush();
		if( fseek(File,InPos,SEEK_SET) )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("SeekFailed",TEXT("Core")) );
		}
		Pos = InPos;
	}
	INT Tell()
	{
		return Pos;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileWriter::Close);
		Flush();
		if( File && fclose( File ) )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		File = NULL;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		Pos += Length;
		INT Copy;
		while( Length > (Copy=ARRAY_COUNT(Buffer)-BufferCount) )
		{
			appMemcpy( Buffer+BufferCount, V, Copy );
			BufferCount += Copy;
			Length      -= Copy;
			V            = (BYTE*)V + Copy;
			Flush();
		}
		if( Length )
		{
			appMemcpy( Buffer+BufferCount, V, Length );
			BufferCount += Length;
		}
	}
	void Flush()
	{
		if( BufferCount && fwrite( Buffer, BufferCount, 1, File )!=1 )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		BufferCount=0;
	}
protected:
	FILE*			File;
	FOutputDevice*	Error;
	INT				Pos;
	INT				BufferCount;
	BYTE			Buffer[4096];
};

#ifdef PLATFORM_PSP
#include "UnPsp.h"
// Reader on the pooled sceIo layer (UnPspFile.cpp) with a 16KB read window:
// UT deserializes a few bytes at a time and every sceIoRead is a syscall into
// the Memory Stick driver.
class FArchivePspReader : public FArchive
{
public:
	FArchivePspReader( FPspFile* InFile, FOutputDevice* InError )
	:	File( InFile ), Error( InError ), Size( appPspSize(InFile) ), Pos( 0 ), BufferBase( 0 ), BufferCount( 0 ), NextRefill( appPspFirstRefill() )
	{
		ArIsLoading = ArIsPersistent = 1;
	}
	~FArchivePspReader()
	{
		if( File )
			Close();
	}
	// Memory Stick time follows bytes moved, not the number of reads (the
	// Unreal port measured a level load at 14 s with 1 KB first reads and 24 s
	// with 16 KB). So the first refill after a seek is small and doubles
	// while the reads stay sequential.
	void Precache( INT HintCount )
	{
		// The linker announces each object's size before deserialising it
		// (ULinkerLoad::Preload): fetch that much in one go, up to
		// [PSP] PrecacheKB, unless the window already holds it.
		if( HintCount > 0 && HintCount < MAXINT )
		{
			const INT Want = Min( Min( HintCount, appPspPrecacheCap() ), Size-Pos );
			if( Pos >= BufferBase && Pos + Want <= BufferBase + BufferCount )
				return;
			BufferBase  = Pos;
			BufferCount = Max( Want, Min( NextRefill, Size-Pos ) );
			if( BufferCount > 0 && appPspRead( File, Pos, Buffer, BufferCount ) != BufferCount )
			{
				ArIsError = 1;
				Error->Logf( TEXT("PSP read failed: Pos=%i Count=%i"), Pos, BufferCount );
			}
			return;
		}
		const UBOOL Sequential = ( Pos == BufferBase + BufferCount );
		if( Sequential && BufferCount )
			NextRefill = Min( NextRefill * 2, (INT)ARRAY_COUNT(Buffer) );
		BufferBase  = Pos;
		BufferCount = Min( NextRefill, Size-Pos );
		if( BufferCount > 0 && appPspRead( File, Pos, Buffer, BufferCount ) != BufferCount )
		{
			ArIsError = 1;
			Error->Logf( TEXT("PSP read failed: Pos=%i Count=%i"), Pos, BufferCount );
		}
	}
	void Seek( INT InPos )
	{
		check(InPos>=0);
		check(InPos<=Size);
		Pos = InPos;
		if( Pos < BufferBase || Pos > BufferBase+BufferCount )
		{
			// Seek profile (PSPLOAD line): how far reads jump outside the window.
			const INT Jump = Pos - ( BufferBase + BufferCount );
			GPspSeekHist[ Jump < 0 ? 0 : Jump < 1024 ? 1 : Jump < 4096 ? 2 : Jump < 16384 ? 3 : Jump < 65536 ? 4 : 5 ]++;
			BufferBase  = Pos;
			BufferCount = 0;
			NextRefill  = appPspFirstRefill();
		}
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Size;
	}
	UBOOL Close()
	{
		if( File )
			appPspClose( File );
		File = NULL;
		return !ArIsError;
	}
	void Serialize( void* V, INT Length )
	{
		while( Length>0 )
		{
			INT Copy = Min( Length, BufferBase+BufferCount-Pos );
			if( Copy<=0 )
			{
				if( Length >= ARRAY_COUNT(Buffer) )
				{
					if( appPspRead( File, Pos, V, Length ) != Length )
					{
						ArIsError = 1;
						Error->Logf( TEXT("PSP read failed: Pos=%i Length=%i"), Pos, Length );
					}
					Pos += Length;
					BufferBase  = Pos;
					BufferCount = 0;
					return;
				}
				Precache( MAXINT );
				Copy = Min( Length, BufferBase+BufferCount-Pos );
				if( Copy<=0 )
				{
					ArIsError = 1;
					Error->Logf( TEXT("ReadFile beyond EOF %i+%i/%i"), Pos, Length, Size );
				}
				if( ArIsError )
					return;
			}
			appMemcpy( V, Buffer+Pos-BufferBase, Copy );
			Pos       += Copy;
			Length    -= Copy;
			V          = (BYTE*)V + Copy;
		}
	}
protected:
	FPspFile*		File;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	INT				NextRefill;
	BYTE			Buffer[16384];
};
#endif

class FFileManagerLinux : public FFileManagerGeneric
{
public:
#ifdef PLATFORM_PSP
	// Never open a file just to measure it: that spends a kernel handle.
	INT FileSize( const TCHAR* Filename )
	{
		return appPspStatSize( Filename );
	}
#endif
	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		guard(FFileManagerLinux::CreateFileReader);
#ifdef PLATFORM_PSP
		{
			FPspFile* PspFile = appPspOpen( Filename );
			if( !PspFile )
			{
				if( Flags & FILEREAD_NoFail )
					appErrorf(TEXT("Failed to read file: %s"),Filename);
				return NULL;
			}
			return new(TEXT("PspFileReader"))FArchivePspReader(PspFile,Error);
		}
#endif
		FILE* File = fopen(TCHAR_TO_ANSI(Filename), TCHAR_TO_ANSI(TEXT("rb")));
		if( !File )
		{
			if( Flags & FILEREAD_NoFail )
				appErrorf(TEXT("Failed to read file: %s"),Filename);
			return NULL;
		}
		fseek( File, 0, SEEK_END );
		return new(TEXT("LinuxFileReader"))FArchiveFileReader(File,Error,ftell(File));
		unguard;
	}
	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		guard(FFileManagerLinux::CreateFileWriter);
		if( Flags & FILEWRITE_EvenIfReadOnly )
		{
#if !defined(PLATFORM_DREAMCAST) && !defined(PLATFORM_PSP)
			chmod(TCHAR_TO_ANSI(Filename), S_IRUSR | S_IWUSR);
#endif
		}
		if( (Flags & FILEWRITE_NoReplaceExisting) && FileSize(Filename)>=0 )
			return NULL;
		const TCHAR* Mode = (Flags & FILEWRITE_Append) ? TEXT("ab") : TEXT("wb"); 
#ifdef PLATFORM_PSP
		FILE* File = fopen(appPspFullPath(Filename),Mode);
		// Every writer is a kernel handle outside the read pool: log them.
		printf( "PSPFILE: writer %s %s\n", appPspFullPath(Filename), File ? "open" : "FAILED" );
#else
		FILE* File = fopen(TCHAR_TO_ANSI(Filename),TCHAR_TO_ANSI(Mode));
#endif
		if( !File )
		{
			if( Flags & FILEWRITE_NoFail )
				appErrorf( TEXT("Failed to write: %s"), Filename );
			return NULL;
		}
		if( Flags & FILEWRITE_Unbuffered )
			setvbuf( File, 0, _IONBF, 0 );
		return new(TEXT("LinuxFileWriter"))FArchiveFileWriter(File,Error);
		unguard;
	}
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
	{
		guard(FFileManagerLinux::Delete);
		if( EvenReadOnly )
		{
#if !defined(PLATFORM_DREAMCAST) && !defined(PLATFORM_PSP)
			chmod(TCHAR_TO_ANSI(Filename), S_IRUSR | S_IWUSR);
#endif
		}
#ifdef PLATFORM_PSP
		return unlink(appPspFullPath(Filename))==0 || (errno==ENOENT && !RequireExists);
#endif
		return unlink(TCHAR_TO_ANSI(Filename))==0 || (errno==ENOENT && !RequireExists);
		unguard;
	}
	SQWORD GetGlobalTime( const TCHAR* Filename )
	{
		guard(FFileManagerLinux::GetGlobalTime);

		return 0;
		
		unguard;
	}
	UBOOL SetGlobalTime( const TCHAR* Filename )
	{
		guard(FFileManagerLinux::SetGlobalTime);

		return 0;
		
		unguard;
	}
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
	{
		guard(FFileManagerLinux::MakeDirectory);
		if( Tree )
			return FFileManagerGeneric::MakeDirectory( Path, Tree );
		return mkdir(TCHAR_TO_ANSI(Path), S_IRUSR | S_IWUSR | S_IXUSR)==0 || errno==EEXIST;
		unguard;
	}
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
	{
		guard(FFileManagerLinux::DeleteDirectory);
		if( Tree )
			return FFileManagerGeneric::DeleteDirectory( Path, RequireExists, Tree );
		return rmdir(TCHAR_TO_ANSI(Path))==0 || (errno==ENOENT && !RequireExists);
		unguard;
	}
	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
	{
		guard(FFileManagerLinux::FindFiles);
		TArray<FString> Result;
	
		DIR *Dirp;
		struct dirent* Direntp;
		char Path[256];
		char File[256];
		char *Filestart;
		char *Cur;
		UBOOL Match;

		// Initialize Path to Filename.
		appStrcpy( Path, Filename );

		// Convert MS "\" to Unix "/".
		for( Cur = Path; *Cur != '\0'; Cur++ )
			if( *Cur == '\\' )
				*Cur = '/';
	
		// Separate path and filename.
		Filestart = Path;
		for( Cur = Path; *Cur != '\0'; Cur++ )
			if( *Cur == '/' )
				Filestart = Cur + 1;

		// Store filename and remove it from Path.
		appStrcpy( File, Filestart );
		*Filestart = '\0';

		// Check for empty path.
		if (appStrlen( Path ) == 0)
			appSprintf( Path, "./" );

		// Open directory, get first entry.
		Dirp = opendir( Path );
		if (Dirp == NULL)
				return Result;
		Direntp = readdir( Dirp );

		// Check each entry.
		while( Direntp != NULL )
		{
			Match = false;

			if( appStrcmp( File, "*" ) == 0 )
			{
				// Any filename.
				Match = true;
			}
			else if( appStrcmp( File, "*.*" ) == 0 )
			{
				// Any filename with a '.'.
				if( appStrchr( Direntp->d_name, '.' ) != NULL )
					Match = true;
			}
			else if( File[0] == '*' )
			{
				// "*.ext" filename.
				if( appStrstr( Direntp->d_name, (File + 1) ) != NULL )
					Match = true;
			}
			else if( File[appStrlen( File ) - 1] == '*' )
			{
				// "name.*" filename.
				if( appStrncmp( Direntp->d_name, File, appStrlen( File ) - 1 ) == 
					0 )
					Match = true;
			}
			else if( appStrstr( File, "*" ) != NULL )
			{
				// single str.*.str match.
				TCHAR* star = appStrstr( File, "*" );
				INT filelen = appStrlen( File );
				INT starlen = appStrlen( star );
				INT starpos = filelen - (starlen - 1);
				TCHAR prefix[256];
				appStrncpy( prefix, File, starpos );
				star++;
				if( appStrncmp( Direntp->d_name, prefix, starpos - 1 ) == 0 )
				{
					// part before * matches
					TCHAR* postfix = Direntp->d_name + (appStrlen(Direntp->d_name) - starlen) + 1;
					if ( appStrcmp( postfix, star ) == 0 )
						Match = true;
				}
			}
			else
			{
				// Literal filename.
				if( appStrcmp( Direntp->d_name, File ) == 0 )
					Match = true;
			}

			// Does this entry match the Filename?
			if( Match )
			{
				// Yes, add the file name to Result.
				new(Result)FString(Direntp->d_name);
			}
		
			// Get next entry.
			Direntp = readdir( Dirp );
		}

		// Close directory.
		closedir( Dirp );

		return Result;
		unguard;
	}
	UBOOL SetDefaultDirectory( const TCHAR* Filename )
	{
		guard(FFileManagerLinux::SetDefaultDirectory);
		return chdir(TCHAR_TO_ANSI(Filename))==0;
		unguard;
	}
	FString GetDefaultDirectory()
	{
		guard(FFileManagerLinux::GetDefaultDirectory);
		{
			ANSICHAR Buffer[1024]="";
			if( getcwd( Buffer, ARRAY_COUNT(Buffer) ) == NULL )
			{
				// Fallback to current directory string if getcwd fails.
				appStrcpy( Buffer, "." );
			}
			return appFromAnsi( Buffer );
		}
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
