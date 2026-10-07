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
	DWORD	Key;		// hash of Path: block cache key (survives close/reopen)
	char	Path[PSP_PATH_MAX];
};

CORE_API INT GPspPhase[PSPPH_Max];
CORE_API INT GPspIoBytes = 0, GPspIoReads = 0, GPspIoSeeks = 0, GPspIoReopens = 0;
CORE_API INT GPspSeekHist[6];
CORE_API INT GPspIoOpenUs = 0, GPspIoReadUs = 0;
CORE_API INT GPspNearPass = 0;
CORE_API DOUBLE GPspLoadChildTime = 0.0;
CORE_API DOUBLE GPspLoadPhase[4];
struct FPspClassTime { UClass* Class; DOUBLE Seconds; INT Count; INT Bytes; };
static FPspClassTime GPspClassTimes[64];
static INT GPspClassTimeNum = 0;
CORE_API void appPspLoadClassTime( UClass* Class, DOUBLE Seconds, INT Bytes )
{
	INT i;
	for( i = 0; i < GPspClassTimeNum && GPspClassTimes[i].Class != Class; i++ );
	if( i == GPspClassTimeNum )
	{
		if( GPspClassTimeNum == ARRAY_COUNT(GPspClassTimes) )
			return;
		GPspClassTimes[GPspClassTimeNum++] = { Class, 0.0, 0, 0 };
	}
	GPspClassTimes[i].Seconds += Seconds; GPspClassTimes[i].Count++; GPspClassTimes[i].Bytes += Bytes;
}
static FPspClassTime GPspPostTimes[64];
static INT GPspPostTimeNum = 0;
CORE_API void appPspPostLoadClassTime( UClass* Class, DOUBLE Seconds )
{
	INT i;
	for( i = 0; i < GPspPostTimeNum && GPspPostTimes[i].Class != Class; i++ );
	if( i == GPspPostTimeNum )
	{
		if( GPspPostTimeNum == ARRAY_COUNT(GPspPostTimes) )
			return;
		GPspPostTimes[GPspPostTimeNum++] = { Class, 0.0, 0, 0 };
	}
	GPspPostTimes[i].Seconds += Seconds; GPspPostTimes[i].Count++;
}
CORE_API void appPspLoadClassReport( INT Top )
{
	for( INT n = 0; n < 6; n++ )
	{
		INT Best = -1;
		for( INT i = 0; i < GPspPostTimeNum; i++ )
			if( GPspPostTimes[i].Count && ( Best < 0 || GPspPostTimes[i].Seconds > GPspPostTimes[Best].Seconds ) )
				Best = i;
		if( Best < 0 )
			break;
		debugf( NAME_Log, TEXT("PSPLOAD:   postload %-16s %5.2f s in %5i objects"), GPspPostTimes[Best].Class->GetName(), (FLOAT)GPspPostTimes[Best].Seconds, GPspPostTimes[Best].Count );
		GPspPostTimes[Best].Count = 0;
	}
	GPspPostTimeNum = 0;
	for( INT n = 0; n < Top; n++ )
	{
		INT Best = -1;
		for( INT i = 0; i < GPspClassTimeNum; i++ )
			if( GPspClassTimes[i].Count && ( Best < 0 || GPspClassTimes[i].Seconds > GPspClassTimes[Best].Seconds ) )
				Best = i;
		if( Best < 0 )
			break;
		debugf( NAME_Log, TEXT("PSPLOAD:   class %-16s %5.2f s in %5i objects, %6i KB"), GPspClassTimes[Best].Class->GetName(), (FLOAT)GPspClassTimes[Best].Seconds, GPspClassTimes[Best].Count, GPspClassTimes[Best].Bytes / 1024 );
		GPspClassTimes[Best].Count = 0;
	}
	GPspClassTimeNum = 0;
}   // microseconds in sceIoOpen / sceIoLseek+sceIoRead
#include <pspthreadman.h>
static inline DWORD PspUs() { return sceKernelGetSystemTimeLow(); }

CORE_API INT appPspPrecacheCap()
{
	static INT Bytes = -1;
	if( Bytes < 0 )
	{
		INT KB = 0;   // measured: ~2% fewer reads for 50% more bytes -- the block cache already covers it
		if( GConfig ) GConfig->GetInt( TEXT("PSP"), TEXT("PrecacheKB"), KB );
		Parse( appCmdLine(), TEXT("PRECACHEKB="), KB );
		Bytes = Clamp( KB, 0, 16 ) * 1024;
	}
	return Bytes;
}

CORE_API INT appPspFirstRefill()
{
	static INT Bytes = -1;
	if( Bytes < 0 )
	{
		INT KB = 1;
		if( GConfig ) GConfig->GetInt( TEXT("PSP"), TEXT("RefillKB"), KB );
		Parse( appCmdLine(), TEXT("REFILLKB="), KB );
		Bytes = Clamp( KB, 1, 16 ) * 1024;
	}
	return Bytes;
}
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

// [PSP] FileHandles / -PSPHANDLES=N: live kernel handles the pool may hold
// (default PSP_MAX_KERNEL_HANDLES). Everything else that opens files -- the
// log, config writes -- comes on top, and the hardware hangs on the 10th.
static INT PspMaxHandles()
{
	static INT Max = -1;
	if( Max < 0 )
	{
		Max = PSP_MAX_KERNEL_HANDLES;
		if( GConfig ) GConfig->GetInt( TEXT("PSP"), TEXT("FileHandles"), Max );
		Parse( appCmdLine(), TEXT("PSPHANDLES="), Max );
		Max = Clamp( Max, 1, 8 );
	}
	return Max;
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
	while( PspLiveHandles() >= PspMaxHandles() )
		PspEvictOne( F );
	const DWORD T0 = PspUs();
	F->Fd = sceIoOpen( F->Path, PSP_O_RDONLY, 0777 );
	GPspIoOpenUs += (INT)( PspUs() - T0 );
	F->FdPos = 0;
	++GPspIoReopens;
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
	F->Key = 2166136261u;
	for( const char* c = F->Path; *c; ++c )
		F->Key = ( F->Key ^ (BYTE)( *c >= 'A' && *c <= 'Z' ? *c + 32 : *c ) ) * 16777619u;
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

static INT PspRawRead( FPspFile* F, INT Pos, void* Dest, INT Count )
{
	if( !PspEnsureOpen( F ) )
		return -1;
	struct FT { DWORD T0; ~FT() { GPspIoReadUs += (INT)( PspUs() - T0 ); } } Timer = { PspUs() };
	if( F->FdPos != Pos )
	{
		if( sceIoLseek32( F->Fd, Pos, PSP_SEEK_SET ) != Pos )
			return -1;
		F->FdPos = Pos;
		++GPspIoSeeks;
	}
	INT Got = 0;
	while( Got < Count )
	{
		INT N = sceIoRead( F->Fd, (BYTE*)Dest + Got, Count - Got );
		++GPspIoReads;
		if( N <= 0 )
			break;
		Got += N;
		GPspIoBytes += N;
	}
	F->FdPos += Got;
	return Got;
}

/*-----------------------------------------------------------------------------
	Block cache.

	On a Memory Stick every read costs ~1.8 ms whatever its size, and UT's
	package loader keeps jumping back to regions it read moments ago (Entry:
	14,896 of 18,836 window leaves were backwards). A shared LRU cache of 2 KB
	blocks keyed by file and offset turns those returns into memory copies.
	A miss reads exactly the uncached span the request needs. Requests over
	16 KB (lazy texture/sound data) go straight to the file.
	[PSP] ReadCacheKB (default 1024; 0 disables).
-----------------------------------------------------------------------------*/

enum { PSP_BLK = 2048, PSP_BLK_SHIFT = 11 };
static BYTE*  GPspBlkData  = NULL;
static DWORD* GPspBlkKey   = NULL;   // file key
static INT*   GPspBlkNum   = NULL;   // block number in the file, -1 = free
static INT*   GPspBlkLen   = NULL;   // valid bytes (short at end of file)
static DWORD* GPspBlkStamp = NULL;
static INT*   GPspBlkNext  = NULL;   // hash chain
static INT*   GPspBlkHash  = NULL;
static INT    GPspBlkCount = -1, GPspBlkHashSize = 0;
static DWORD  GPspBlkClock = 0;
CORE_API INT  GPspCacheHits = 0, GPspCacheMisses = 0;

static UBOOL PspCacheInit()
{
	if( GPspBlkCount >= 0 )
		return GPspBlkCount > 0;
	INT KB = 1024;
	if( GConfig ) GConfig->GetInt( TEXT("PSP"), TEXT("ReadCacheKB"), KB );
	Parse( appCmdLine(), TEXT("READCACHEKB="), KB );
	GPspBlkCount = Clamp( KB, 0, 8192 ) * 1024 / PSP_BLK;
	if( !GPspBlkCount )
		return 0;
	GPspBlkHashSize = 1; while( GPspBlkHashSize < GPspBlkCount * 2 ) GPspBlkHashSize <<= 1;
	GPspBlkData  = (BYTE*) malloc( GPspBlkCount * PSP_BLK );
	GPspBlkKey   = (DWORD*)malloc( GPspBlkCount * sizeof(DWORD) );
	GPspBlkNum   = (INT*)  malloc( GPspBlkCount * sizeof(INT) );
	GPspBlkLen   = (INT*)  malloc( GPspBlkCount * sizeof(INT) );
	GPspBlkStamp = (DWORD*)malloc( GPspBlkCount * sizeof(DWORD) );
	GPspBlkNext  = (INT*)  malloc( GPspBlkCount * sizeof(INT) );
	GPspBlkHash  = (INT*)  malloc( GPspBlkHashSize * sizeof(INT) );
	if( !GPspBlkData || !GPspBlkKey || !GPspBlkNum || !GPspBlkLen || !GPspBlkStamp || !GPspBlkNext || !GPspBlkHash )
	{
		GPspBlkCount = 0;
		return 0;
	}
	for( INT i = 0; i < GPspBlkCount; i++ ) { GPspBlkNum[i] = -1; GPspBlkStamp[i] = 0; GPspBlkNext[i] = -1; }
	for( INT i = 0; i < GPspBlkHashSize; i++ ) GPspBlkHash[i] = -1;
	debugf( NAME_Init, TEXT("PSPFILE: read cache %i KB in %i blocks"), GPspBlkCount * PSP_BLK / 1024, GPspBlkCount );
	return 1;
}
static inline INT PspBlkBucket( DWORD Key, INT Num ) { return (INT)( ( Key ^ ( (DWORD)Num * 2654435761u ) ) & ( GPspBlkHashSize - 1 ) ); }
static INT PspBlkFind( DWORD Key, INT Num )
{
	for( INT i = GPspBlkHash[ PspBlkBucket( Key, Num ) ]; i >= 0; i = GPspBlkNext[i] )
		if( GPspBlkNum[i] == Num && GPspBlkKey[i] == Key )
			return i;
	return -1;
}
static void PspBlkUnlink( INT Slot )
{
	INT* Link = &GPspBlkHash[ PspBlkBucket( GPspBlkKey[Slot], GPspBlkNum[Slot] ) ];
	while( *Link >= 0 && *Link != Slot ) Link = &GPspBlkNext[*Link];
	if( *Link == Slot ) *Link = GPspBlkNext[Slot];
	GPspBlkNum[Slot] = -1;
}
static INT PspBlkAlloc( DWORD Key, INT Num )
{
	INT Victim = 0;
	for( INT i = 0; i < GPspBlkCount; i++ )
	{
		if( GPspBlkNum[i] < 0 ) { Victim = i; break; }
		if( GPspBlkStamp[i] < GPspBlkStamp[Victim] ) Victim = i;
	}
	if( GPspBlkNum[Victim] >= 0 )
		PspBlkUnlink( Victim );
	GPspBlkKey[Victim] = Key; GPspBlkNum[Victim] = Num;
	INT& Head = GPspBlkHash[ PspBlkBucket( Key, Num ) ];
	GPspBlkNext[Victim] = Head; Head = Victim;
	return Victim;
}

CORE_API INT appPspRead( FPspFile* F, INT Pos, void* Dest, INT Count )
{
	if( Count > 16384 || Pos < 0 || !PspCacheInit() )
		return PspRawRead( F, Pos, Dest, Count );
	if( Pos + Count > F->Size )
		Count = F->Size - Pos;
	BYTE* Out = (BYTE*)Dest;
	INT Done = 0;
	while( Done < Count )
	{
		const INT At  = Pos + Done;
		const INT Blk = At >> PSP_BLK_SHIFT;
		INT Slot = PspBlkFind( F->Key, Blk );
		if( Slot < 0 )
		{
			// Read the run of uncached blocks this request still needs.
			const INT LastBlk = ( Pos + Count - 1 ) >> PSP_BLK_SHIFT;
			INT Run = 1;
			while( Blk + Run <= LastBlk && Run < 8 && PspBlkFind( F->Key, Blk + Run ) < 0 )
				Run++;
			static BYTE Tmp[8 * PSP_BLK];
			const INT Start = Blk << PSP_BLK_SHIFT;
			const INT Want  = Min( Run * PSP_BLK, F->Size - Start );
			const INT Got   = PspRawRead( F, Start, Tmp, Want );
			if( Got <= 0 )
				return Done ? Done : -1;
			++GPspCacheMisses;
			for( INT r = 0; r * PSP_BLK < Got; r++ )
			{
				const INT S = PspBlkAlloc( F->Key, Blk + r );
				GPspBlkLen[S] = Min( (INT)PSP_BLK, Got - r * PSP_BLK );
				appMemcpy( GPspBlkData + S * PSP_BLK, Tmp + r * PSP_BLK, GPspBlkLen[S] );
				GPspBlkStamp[S] = ++GPspBlkClock;
			}
			Slot = PspBlkFind( F->Key, Blk );
			if( Slot < 0 )
				return Done ? Done : -1;
		}
		else
			++GPspCacheHits;
		GPspBlkStamp[Slot] = ++GPspBlkClock;
		const INT Off = At - ( Blk << PSP_BLK_SHIFT );
		const INT N   = Min( Count - Done, GPspBlkLen[Slot] - Off );
		if( N <= 0 )
			break;
		appMemcpy( Out + Done, GPspBlkData + Slot * PSP_BLK + Off, N );
		Done += N;
	}
	return Done;
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
