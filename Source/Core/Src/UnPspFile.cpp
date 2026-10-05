/*=============================================================================
	UnPspFile.cpp: sceIo file pool for the PSP.

	Ported from the Unreal (v200) PSP port, where this was the single hardest
	bug: UE1 keeps every package linker's file open for the linker's lifetime,
	and on real hardware the tenth concurrently open file makes sceIoOpen never
	return. Nothing is logged; the console just goes black and powers off.
=============================================================================*/

#include "CorePrivate.h"

#ifdef PLATFORM_PSP

#include <pspiofilemgr.h>
#include <unistd.h>
#include <string.h>

#include "UnPsp.h"

enum
{
	PSP_MAX_KERNEL_HANDLES = 6,	// live sceIo handles we allow ourselves
	PSP_MAX_FILES          = 96,	// logical files (packages are few dozen at most)
	PSP_PATH_MAX           = 256,
};

struct FPspFile
{
	UBOOL	InUse;
	SceUID	Fd;			// -1 while evicted
	INT		Size;
	INT		FdPos;		// where the kernel handle's file pointer is
	DWORD	LastUse;
	char	Path[PSP_PATH_MAX];
};

CORE_API INT GPspPhase[PSPPH_Max];
static FPspFile GPspFiles[PSP_MAX_FILES];
static DWORD GPspUseClock = 0;

CORE_API const char* appPspFullPath( const char* In )
{
	static char Out[PSP_PATH_MAX];
	char Tmp[PSP_PATH_MAX];
	if( strchr( In, ':' ) )
		appStrncpy( Tmp, In, sizeof(Tmp) );
	else
	{
		if( !getcwd( Tmp, sizeof(Tmp) ) )
			Tmp[0] = 0;
		INT L = strlen( Tmp );
		if( L && Tmp[L-1] != '/' && Tmp[L-1] != '\\' && L < (INT)sizeof(Tmp) - 1 )
			Tmp[L++] = '/', Tmp[L] = 0;
		strncat( Tmp, In, sizeof(Tmp) - L - 1 );
	}
	for( char* c = Tmp; *c; ++c )
		if( *c == '\\' )
			*c = '/';
	// Split at the drive prefix ("ms0:/", "host0:/") and normalise the rest.
	const char* Rest = strchr( Tmp, ':' );
	Rest = Rest ? Rest + 1 : Tmp;
	if( *Rest == '/' ) ++Rest;
	INT Prefix = Rest - Tmp;
	memcpy( Out, Tmp, Prefix ); Out[Prefix] = 0;
	const char* Seg[64]; INT SegLen[64]; INT N = 0;
	for( const char* p = Rest; *p; )
	{
		const char* e = strchr( p, '/' ); if( !e ) e = p + strlen( p );
		INT Len = e - p;
		if( Len == 0 || ( Len == 1 && p[0] == '.' ) ) {}
		else if( Len == 2 && p[0] == '.' && p[1] == '.' ) { if( N ) --N; }
		else if( N < 64 ) { Seg[N] = p; SegLen[N] = Len; ++N; }
		p = *e ? e + 1 : e;
	}
	INT O = Prefix;
	for( INT i = 0; i < N; ++i )
	{
		if( O + SegLen[i] + 2 >= (INT)sizeof(Out) ) break;
		memcpy( Out + O, Seg[i], SegLen[i] ); O += SegLen[i];
		if( i < N - 1 ) Out[O++] = '/';
	}
	Out[O] = 0;
	return Out;
}

static INT PspLiveHandles()
{
	INT Live = 0;
	for( INT i = 0; i < PSP_MAX_FILES; i++ )
		if( GPspFiles[i].InUse && GPspFiles[i].Fd >= 0 )
			Live++;
	return Live;
}

// Close the least recently used live handle.
static void PspEvictOne( FPspFile* Keep )
{
	FPspFile* Victim = NULL;
	for( INT i = 0; i < PSP_MAX_FILES; i++ )
	{
		FPspFile* F = &GPspFiles[i];
		if( F != Keep && F->InUse && F->Fd >= 0 && ( !Victim || F->LastUse < Victim->LastUse ) )
			Victim = F;
	}
	if( Victim )
	{
		sceIoClose( Victim->Fd );
		Victim->Fd = -1;
	}
}

static UBOOL PspEnsureOpen( FPspFile* F )
{
	F->LastUse = ++GPspUseClock;
	if( F->Fd >= 0 )
		return 1;
	while( PspLiveHandles() >= PSP_MAX_KERNEL_HANDLES )
		PspEvictOne( F );
	F->Fd = sceIoOpen( F->Path, PSP_O_RDONLY, 0777 );
	F->FdPos = 0;
	return F->Fd >= 0;
}

CORE_API INT appPspStatSize( const char* Path )
{
	SceIoStat St;
	if( sceIoGetstat( appPspFullPath( Path ), &St ) < 0 || FIO_S_ISDIR( St.st_mode ) )
		return -1;
	return (INT)St.st_size;
}

CORE_API FPspFile* appPspOpen( const char* InPath )
{
	const char* Path = appPspFullPath( InPath );
	FPspFile* F = NULL;
	for( INT i = 0; i < PSP_MAX_FILES && !F; i++ )
		if( !GPspFiles[i].InUse )
			F = &GPspFiles[i];
	if( !F )
	{
		debugf( NAME_Warning, TEXT("PSPFILE: out of logical file slots opening %s"), Path );
		return NULL;
	}
	appStrncpy( F->Path, Path, sizeof(F->Path) );
	F->Fd = -1;
	F->InUse = 1;
	if( !PspEnsureOpen( F ) )
	{
		F->InUse = 0;
		return NULL;
	}
	F->Size = (INT)sceIoLseek32( F->Fd, 0, PSP_SEEK_END );
	sceIoLseek32( F->Fd, 0, PSP_SEEK_SET );
	F->FdPos = 0;
	return F;
}

CORE_API INT appPspSize( FPspFile* F )
{
	return F->Size;
}

CORE_API INT appPspRead( FPspFile* F, INT Pos, void* Dest, INT Count )
{
	if( !PspEnsureOpen( F ) )
		return -1;
	if( F->FdPos != Pos )
	{
		if( sceIoLseek32( F->Fd, Pos, PSP_SEEK_SET ) != Pos )
			return -1;
		F->FdPos = Pos;
	}
	INT Got = 0;
	while( Got < Count )
	{
		INT N = sceIoRead( F->Fd, (BYTE*)Dest + Got, Count - Got );
		if( N <= 0 )
			break;
		Got += N;
	}
	F->FdPos += Got;
	return Got;
}

CORE_API void appPspClose( FPspFile* F )
{
	if( !F )
		return;
	if( F->Fd >= 0 )
		sceIoClose( F->Fd );
	F->Fd = -1;
	F->InUse = 0;
}

#endif
