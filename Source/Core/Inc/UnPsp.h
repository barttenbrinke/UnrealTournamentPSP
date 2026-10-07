/*=============================================================================
	UnPsp.h: PSP platform helpers shared by Core and the drivers.
=============================================================================*/

#if defined(PLATFORM_PSP) && !defined(_INC_UNPSP)
#define _INC_UNPSP

// Rewrites a path the way the PSP's sceIo driver needs it: forward slashes,
// relative paths anchored on the current directory (newlib emulates chdir,
// sceIoOpen does not), "." and ".." collapsed. Returns a static buffer.
CORE_API const char* appPspFullPath( const char* In );

// Pooled read-only file. The Memory Stick driver on real hardware allows only
// ~9 open files, and the tenth sceIoOpen HANGS instead of failing (PPSSPP never
// reproduces this). UT keeps one handle per loaded package, so readers go
// through a pool that keeps at most PSP_MAX_KERNEL_HANDLES kernel handles alive
// and transparently reopens/re-seeks evicted ones.
struct FPspFile;
CORE_API FPspFile* appPspOpen( const char* Path );
CORE_API INT appPspSize( FPspFile* F );
CORE_API INT appPspRead( FPspFile* F, INT Pos, void* Dest, INT Count );
CORE_API void appPspClose( FPspFile* F );
// sceIoGetstat: size without spending a handle, -1 if missing.
CORE_API INT appPspStatSize( const char* Path );

// First read after a seek, in bytes ([PSP] RefillKB / -REFILLKB=, default 1).
CORE_API INT appPspFirstRefill();
// Largest object prefetch on the linker's size hint ([PSP] PrecacheKB / -PRECACHEKB=, default 0 = off).
CORE_API INT appPspPrecacheCap();
// Memory Stick traffic since start: bytes read, sceIoRead calls, seeks, reopens.
CORE_API extern INT GPspIoBytes, GPspIoReads, GPspIoSeeks, GPspIoReopens;
// Reader seeks that leave the read window: back, <1K, <4K, <16K, <64K, further.
CORE_API extern INT GPspSeekHist[6];
CORE_API extern INT GPspCacheHits, GPspCacheMisses;   // block cache (UnPspFile.cpp)
CORE_API extern INT GPspIoOpenUs, GPspIoReadUs;
// Level load profile: own deserialisation time per class (ULinkerLoad::Preload).
CORE_API extern DOUBLE GPspLoadChildTime;
// Seconds in EndLoad preload, PostLoad, linker construction (tables, imports).
CORE_API extern DOUBLE GPspLoadPhase[4];
CORE_API void appPspLoadClassTime( class UClass* Class, DOUBLE Seconds, INT Bytes );
CORE_API void appPspLoadClassReport( INT Top );
CORE_API void appPspPostLoadClassTime( class UClass* Class, DOUBLE Seconds );

// Set by UCanvas::DrawActor while it draws the first-person weapon: the GL
// driver projects that pass with a 1-unit near plane (the world uses a
// farther one for depth precision) after clearing the depth buffer.
CORE_API extern INT GPspNearPass;

// Profiling build (PSP_KEEP_UCLOCK): per-phase microseconds of the frame,
// summed by UGameEngine::Draw and reported/reset by the GL driver.
enum { PSPPH_World, PSPPH_Hud, PSPPH_Console, PSPPH_Unlock, PSPPH_Audio, PSPPH_Max };
CORE_API extern INT GPspPhase[PSPPH_Max];

#endif
