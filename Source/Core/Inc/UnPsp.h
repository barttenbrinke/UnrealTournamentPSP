/*=============================================================================
	UnPsp.h: PSP platform helpers shared by Core and the drivers.
=============================================================================*/

#ifdef PLATFORM_PSP

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

#endif
