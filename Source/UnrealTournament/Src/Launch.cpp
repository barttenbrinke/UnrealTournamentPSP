/*=============================================================================
	Launch.cpp: Game launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
=============================================================================*/

#ifdef PLATFORM_PSP
// Before the engine headers: UE declares its own operator delete.
#include <exception>
#include <new>
#endif
#include "LaunchPrivate.h"
#ifdef PLATFORM_SDL
#include <SDL2/SDL.h>
#endif
/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

extern "C" {TCHAR THIS_PACKAGE[64]=TEXT("Launch");}

// Memory allocator.
#ifdef PSP_HEAPCHECK
#include "FMallocPspCheck.h"
FMallocPspCheck Malloc;
#else
#include "FMallocAnsi.h"
FMallocAnsi Malloc;
#endif

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#include "FFileManagerLinux.h"
FFileManagerLinux FileManager;

// Config.
#include "FConfigCacheIni.h"

#ifdef PLATFORM_PSP
#include <pspkernel.h>
#include <psppower.h>
#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <unistd.h>

// Main thread stack. UE's package loading and renderer recurse deeply and
// FOutputDevice::Logf puts a 4KB buffer on the stack at every level; the
// Unreal port died with 1MB. pspsdk looks this symbol up by name.
extern "C" { unsigned int sce_newlib_stack_kb_size = 4096; }
// Heap: everything except 1MB for pspgl and the kernel.
extern "C" { int sce_newlib_heap_kb_size = -1024; }

// The engine runs from inside System/, like UnrealTournament.exe on PC: the
// ini search paths are relative to it ("../Maps/*.unr"). The game data lives
// beside the EBOOT, so take the folder from argv[0] ("ms0:/PSP/GAME/<x>/EBOOT.PBP",
// "ef0:/..." on a PSP go). A PSPLink run ("host0:/...") keeps the default.
static char GPspRoot[256] = "ms0:/PSP/GAME/UnrealTournament/System/";
static void PspRootFromLauncher( const char* Launcher )
{
	if( !Launcher || ( strncmp( Launcher, "ms0:/", 5 ) && strncmp( Launcher, "ef0:/", 5 ) ) )
		return;
	const char* Slash = strrchr( Launcher, '/' );
	if( !Slash )
		return;
	const size_t DirLen = (size_t)( Slash + 1 - Launcher );
	if( DirLen + sizeof("System/") > sizeof(GPspRoot) )
		return;
	memcpy( GPspRoot, Launcher, DirLen );
	strcpy( GPspRoot + DirLen, "System/" );
}

// The ways a PSP process can die without writing anything. A user-mode EBOOT
// cannot install a CPU exception handler (that needs kernel imports, and the
// loader rejects the EBOOT with 8002013C), so catch what the C++ runtime offers.
static void PspSyncLog()
{
	sceIoSync( "ms0:", 0 );
}
static void PspOnTerminate()
{
	debugf( TEXT("PSPDEATH: std::terminate -- unhandled exception") );
	PspSyncLog();
	sceKernelExitGame();
}
static void PspOnBadAlloc()
{
	debugf( TEXT("PSPDEATH: operator new failed (out of memory)") );
	PspSyncLog();
	sceKernelExitGame();
}

static void PspPreInit( int argc, char** argv )
{
	const char* Launcher = argc > 0 ? argv[0] : NULL;
	// UE divides by zero and underflows freely; the PSP FPU traps on those
	// instead of producing inf/NaN. Mask the exceptions.
	pspSdkDisableFPUExceptions();
	std::set_terminate( PspOnTerminate );
	std::set_new_handler( PspOnBadAlloc );
	// Homebrew starts at 222MHz unless asked.
	scePowerSetClockFrequency( 333, 333, 166 );
	// Console builds skip the argv[0] module name: name the log and ini.
	appStrcpy( GModule, "UnrealTournament" );
	PspRootFromLauncher( Launcher );
	// -ROOT=<dir>/ (PSPLink runs): take the game data from elsewhere, e.g.
	// "host0:/UnrealTournament/System/" to serve it from the PC over USB.
	for( int i = 1; i < argc; i++ )
		if( !strncmp( argv[i], "-ROOT=", 6 ) && strlen( argv[i] + 6 ) < sizeof(GPspRoot) )
			strcpy( GPspRoot, argv[i] + 6 );
	if( chdir( GPspRoot ) < 0 )
		printf( "Could not chdir to %s\n", GPspRoot );
}
#endif

#ifdef PLATFORM_DREAMCAST
#include <kos.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <kos/thread.h>
#define MAIN_STACK_SIZE (32 * 1024)  
#ifdef DREAMCAST_USE_FATFS
extern "C" {
#include <fatfs.h>
}
#endif
KOS_INIT_FLAGS( INIT_DEFAULT | INIT_CDROM | INIT_CONTROLLER | INIT_KEYBOARD | INIT_MOUSE | INIT_VMU | INIT_NET );
#endif

#ifdef PLATFORM_DREAMCAST
// fix thread stack underrun
static void init_thread_stack(void) {
    kthread_t *current = thd_get_current();
    if (current) {
        void *new_stack = malloc(MAIN_STACK_SIZE);
        if (new_stack) {
            current->stack = new_stack;
            current->stack_size = MAIN_STACK_SIZE;
            current->flags |= THD_OWNS_STACK;
        }
    }
}

// What dbgio device was active at startup
DLL_EXPORT const char* GStartupDbgDev = nullptr;

//
// Display error and lock up.
//
void FatalError( const char* Fmt, ... ) __attribute__((noreturn));
void FatalError( const char* Fmt, ... )
{
	char Msg[2048];

	va_list Args;
	va_start( Args, Fmt );
	vsnprintf( Msg, sizeof( Msg ), Fmt, Args );
	va_end( Args );


	printf( "%s\n\n", Msg );

	arch_stk_trace( 2 );

	while (true)
		thd_sleep( 100 );
}

//
// Handle assertion failure.
//
void HandleAssertFail( const char* File, int Line, const char* Expr, const char* Msg, const char* Func )
{
	FatalError( "ASSERTION FAILED:\nLoc: %s:%d (%s)\nExpr: %s\n%s", File, Line, Func, Expr, Msg);
}

void HandleIrqException( irq_t Code, irq_context_t* Context, void* Data )
{
	bfont_draw_str_vram_fmt( 8, 8, true, "UNHANDLED EXCEPTION 0x%08x", Code );
	bfont_draw_str_vram_fmt( 8, 32, true, "PC: %p PR: %p", (void*)Context->pc, (void*)Context->pr );
	bfont_draw_str_vram_fmt( 8, 56, true, "SR: %p R0: %p", (void*)Context->sr, (void*)Context->r[0] );
	bfont_draw_str_vram_fmt( 8, 80, true, "PROBABLY OUT OF MEMORY");

	arch_stk_trace_at( Context->r[14], 0 );

	volatile INT Dummy = 1;
	while (Dummy);
}

#endif

/*-----------------------------------------------------------------------------
	Initialization
-----------------------------------------------------------------------------*/

//
// Creates a UEngine object.
//
static UEngine* InitEngine()
{
	guard(InitEngine);
	DOUBLE LoadTime = appSeconds();

	// Set exec hook.
	GExec = NULL;

	// Update first-run.
	INT FirstRun=0;
	if (FirstRun<ENGINE_VERSION)
		FirstRun = ENGINE_VERSION;
	GConfig->SetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );

	// Create the global engine object.
	UClass* EngineClass;
	EngineClass = UObject::StaticLoadClass(
		UGameEngine::StaticClass(), NULL, 
		TEXT("ini:Engine.Engine.GameEngine"), 
		NULL, LOAD_NoFail, NULL 
	);
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
	Engine->Init();
#ifdef PLATFORM_DREAMCAST
	malloc_stats();
#endif
	debugf( TEXT("Startup time: %f seconds."), appSeconds()-LoadTime );

	return Engine;
	unguard;
}

//
// Handle an error.
//
void HandleError( const char* Exception )
{
	GIsGuarded=0;
	GIsCriticalError=1;
	debugf( NAME_Exit, "Shutting down after catching exception" );
	debugf( NAME_Exit, "Exiting due to exception" );
	GErrorHist[ARRAY_COUNT(GErrorHist)-1]=0;
#ifdef PLATFORM_PSP
	// SDL's PSP message box cannot run here (the GL context owns the display);
	// the log has the details. Make sure they reach the card.
	PspSyncLog();
#elif defined(PLATFORM_SDL)
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, LocalizeError("Critical"), GErrorHist, SDL_GetKeyboardFocus() );
#elif defined(PLATFORM_DREAMCAST)
	if( Exception )
		FatalError( "FATAL ERROR:\n%s\n\n%s", Exception, GErrorHist );
	else
		FatalError( "FATAL ERROR:\n%s", GErrorHist );
#endif
}

/*-----------------------------------------------------------------------------
	Main Loop
-----------------------------------------------------------------------------*/

//
// X game message loop.
//
static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);

#ifdef PLATFORM_PSP
	// The PSP runs threads strictly by priority: a main loop that never
	// yields starves the mixer and the music thread unless they sit above it.
	// Log every thread's priority once, then lower the main thread by
	// [PSP] MainThreadDrop (default 4; 0 leaves it). Lower number = higher.
	{
		SceUID Ids[64]; int Count = 0;
		if( sceKernelGetThreadmanIdList( SCE_KERNEL_TMID_Thread, Ids, 64, &Count ) >= 0 )
			for( int i = 0; i < Count; ++i )
			{
				SceKernelThreadInfo Info; appMemset( &Info, 0, sizeof(Info) ); Info.size = sizeof(Info);
				if( sceKernelReferThreadStatus( Ids[i], &Info ) >= 0 )
					debugf( NAME_Log, TEXT("PSPTHREAD: %-24s priority %3d stack %6dK"), Info.name, Info.currentPriority, (int)( Info.stackSize / 1024 ) );
			}
		INT Drop = 4;
		GConfig->GetInt( TEXT("PSP"), TEXT("MainThreadDrop"), Drop );
		const int Cur = sceKernelGetThreadCurrentPriority();
		if( Drop > 0 && Cur + Drop < 120 )
		{
			sceKernelChangeThreadPriority( sceKernelGetThreadId(), Cur + Drop );
			debugf( NAME_Log, TEXT("PSPTHREAD: main thread priority %d -> %d so audio threads preempt it"), Cur, sceKernelGetThreadCurrentPriority() );
		}
	}
#endif
	// Loop while running.
	GIsRunning = 1;
	DOUBLE OldTime = appSeconds();
	DOUBLE SecondStartTime = OldTime;
	INT TickCount = 0;
	while( GIsRunning && !GIsRequestingExit )
	{
		// Update the world.
		guard(UpdateWorld);
		DOUBLE NewTime   = appSeconds();
		FLOAT  DeltaTime = NewTime - OldTime;
		Engine->Tick( DeltaTime );
#ifdef PLATFORM_PSP
		// -EXECAT=<secs>:<command>[;<secs>:<command>...]: scripted console
		// commands for unattended test runs ("-EXECAT=30:fire;40:shot").
		{
			static TCHAR Script[256] = TEXT("?");
			static TCHAR* Next = NULL;
			static DOUBLE Start = 0.0;
			if( Script[0] == '?' )
			{
				Script[0] = 0;
				Parse( appCmdLine(), TEXT("EXECAT="), Script, ARRAY_COUNT(Script) );
				Next = Script;
				Start = appSeconds();
				debugf( NAME_Log, TEXT("PSPEXEC: script '%s' from '%s'"), Script, appCmdLine() );
			}
			if( Next && *Next && appSeconds() - Start >= appAtof( Next ) )
			{
				TCHAR* Colon = appStrchr( Next, ':' );
				TCHAR* End = Colon ? appStrchr( Colon, ';' ) : NULL;
				if( End ) *End = 0;
				// Command lines cannot carry spaces: '_' stands in for one.
				for( TCHAR* c = Colon; c && *c; ++c )
					if( *c == '_' ) *c = ' ';
				if( Colon && Engine->Client && Engine->Client->Viewports.Num() )
				{
					debugf( NAME_Log, TEXT("PSPEXEC: %s"), Colon + 1 );
					UViewport* V = Engine->Client->Viewports(0);
					const TCHAR* Cmd = Colon + 1;
					EInputKey Key;
					// "press <KeyName>": a real key press and release, as a pad
					// button would send (aliases only fire on IST_Press).
					if( ParseCommand( &Cmd, TEXT("PRESS") ) && V->Input && V->Input->FindKeyName( Cmd, Key ) )
					{
						Engine->InputEvent( V, Key, IST_Press );
						Engine->InputEvent( V, Key, IST_Release );
					}
					// "dumptex <Package.Texture>": write its top mip (palettised)
					// as System/<Texture>.ppm -- for making the XMB icon.
					else if( ParseCommand( &Cmd, TEXT("DUMPTEX") ) )
					{
						UTexture* T = LoadObject<UTexture>( NULL, Cmd, NULL, LOAD_NoWarn, NULL );
						if( T && T->Mips.Num() && T->Palette )
						{
							FMipmap& M = T->Mips(0);
							M.DataArray.Load();
							FColor* Pal = T->Palette->Colors.GetData();
							TCHAR Name[256]; appSprintf( Name, TEXT("%s.ppm"), T->GetName() );
							if( FILE* F = fopen( Name, "wb" ) )
							{
								fprintf( F, "P6\n%d %d\n255\n", M.USize, M.VSize );
								for( INT i = 0; i < M.USize * M.VSize; i++ )
								{
									FColor C = Pal[ M.DataArray(i) ];
									fputc( C.R, F ); fputc( C.G, F ); fputc( C.B, F );
								}
								fclose( F );
								debugf( NAME_Log, TEXT("PSPEXEC: dumped %s %ix%i to %s"), T->GetPathName(), M.USize, M.VSize, Name );
							}
						}
						else
							debugf( NAME_Log, TEXT("PSPEXEC: no palettised texture %s"), Cmd );
					}
					else
						V->Exec( Colon + 1, *GLog );
				}
				Next = End ? End + 1 : NULL;
			}
		}
#endif
		if( GWindowManager )
			GWindowManager->Tick( DeltaTime );
		OldTime = NewTime;
		TickCount++;
		if( OldTime > SecondStartTime + 1 )
		{
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
			SecondStartTime = OldTime;
			TickCount = 0;
		}
		unguard;

		// Enforce optional maximum tick rate.
		guard(EnforceTickRate);
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
#ifdef PLATFORM_PSP
		// GetMaxTickRate() is 0 offline, so nothing ever sleeps; [PSP] MaxFPS
		// caps the frame rate (steadier frames, and the CPU goes to the
		// audio threads instead of frames nobody sees). 0 disables.
		{
			static INT PspMaxFPS = -1;
			if( PspMaxFPS < 0 )
			{
				PspMaxFPS = 20;
				GConfig->GetInt( TEXT("PSP"), TEXT("MaxFPS"), PspMaxFPS );
				debugf( NAME_Log, TEXT("PSPPERF: frame cap = %i fps"), PspMaxFPS );
			}
			if( PspMaxFPS > 0 && ( MaxTickRate <= 0.f || MaxTickRate > PspMaxFPS ) )
				MaxTickRate = PspMaxFPS;
		}
#endif
		if( MaxTickRate>0.0 )
		{
			FLOAT Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
		}
		unguard;
	}
	GIsRunning = 0;

	unguard;
}

/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

//
// Simple copy.
// 

void SimpleCopy(TCHAR* fromfile, TCHAR* tofile)
{
	INT c;
	FILE* from;
	FILE* to;
	from = fopen(fromfile, "r");
	if (from == NULL)
		return;
	to = fopen(tofile, "w");
	if (to == NULL)
	{
		printf("Can't open or create %s", tofile);
		return;
	}
	while ((c = getc(from)) != EOF)
		putc(c, to);
	fclose(from);
	fclose(to);
}

//
// Exit wound.
//
int CleanUpOnExit(int ErrorLevel)
{
	GFileManager->Delete(TEXT("Running.ini"),0,0);
	debugf( NAME_Title, LocalizeGeneral("Exit") );
	appPreExit();
	GIsGuarded = 0;

	// Shutdown.
	appExit();
	GIsStarted = 0;

	// Restore the user's configuration.
	TCHAR baseconfig[PATH_MAX] = TEXT("");
	if( getcwd(baseconfig, sizeof(baseconfig)) == NULL )
	{
		appStrcpy( baseconfig, TEXT("./User.ini") );
	}
	else
	{
		appStrcat(baseconfig, "/User.ini");
	}

	TCHAR userconfig[PATH_MAX] = TEXT("");
	sprintf(userconfig, "~/.utconf");

	TCHAR exec[PATH_MAX] = TEXT("");
	sprintf(exec, "cp -f %s %s", baseconfig, userconfig);
	//system( exec );

	return ErrorLevel;
}

//
// Entry point.
//
int main( int argc, char* argv[] )
{
#ifdef PLATFORM_PSP
	PspPreInit( argc, argv );
#endif
#ifdef PLATFORM_DREAMCAST
	// fix thread stack underrun
	init_thread_stack();
	// Redirect dbgio to the framebuffer if we're not already using dcload.
	GStartupDbgDev = dbgio_dev_get();
	if( !GStartupDbgDev || !appStrstr( GStartupDbgDev, "dcl" ) )
		dbgio_dev_select( "fb" );
	assert_set_handler( HandleAssertFail );
	irq_set_handler( EXC_UNHANDLED_EXC, HandleIrqException, nullptr );
#ifdef DREAMCAST_USE_FATFS
	if( fs_fat_mount_sd() == 0 )
	{
		printf( "SD card found, will try to load data from there\n" );
	}
	else
	{
		// failed
		printf( "SD card not found, will default to CD\n" );
		sd_shutdown();
		fs_fat_shutdown();
	}
#endif
#endif
	try
	{
	guard(main);
	
	INT ErrorLevel = 0;
	GIsStarted	   = 1;
#if !defined(PLATFORM_DREAMCAST) && !defined(PLATFORM_PSP)
	// Set module name.
	appStrcpy( GModule, argv[0] );

	// Set the package name.
	appStrcpy( THIS_PACKAGE, appPackage() );	
#endif
	// Get the command line.
	TCHAR CmdLine[1024], *CmdLinePtr=CmdLine;
	*CmdLinePtr = 0;
	for( INT i=1; i<argc; i++ )
	{
		if( i>1 )
			appStrcat( CmdLine, " " );
		appStrcat( CmdLine, argv[i] );
	}
#ifdef PLATFORM_PSP
	// No command line from the XMB or PPSSPP: System/cmdline.txt, if present,
	// supplies one (a map URL and switches, e.g. "DM-Deck16][ -NOSOUND").
	if( FILE* F = fopen( "cmdline.txt", "r" ) )
	{
		char Extra[512] = "";
		if( fgets( Extra, sizeof(Extra), F ) )
		{
			for( char* c = Extra; *c; ++c )
				if( *c == '\r' || *c == '\n' ) *c = 0;
			if( Extra[0] && appStrlen(CmdLine) + appStrlen(Extra) + 2 < ARRAY_COUNT(CmdLine) )
			{
				if( CmdLine[0] )
					appStrcat( CmdLine, " " );
				appStrcat( CmdLine, Extra );
			}
		}
		fclose( F );
	}
#endif
#if !defined(PLATFORM_DREAMCAST) && !defined(PLATFORM_PSP)
	// Take care of .ini swapping.
	TCHAR userconfig[PATH_MAX] = TEXT("");
	sprintf(userconfig, "~/.utconf");

	TCHAR baseconfig[PATH_MAX] = TEXT("");
	if( getcwd(baseconfig, sizeof(baseconfig)) == NULL )
	{
		appStrcpy( baseconfig, TEXT("./User.ini") );
	}
	else
	{
		appStrcat(baseconfig, "/User.ini");
	}

	TCHAR exec[PATH_MAX] = TEXT("");
	sprintf(exec, "cp -f %s %s", userconfig, baseconfig);
	//system( exec );
#endif
	//SimpleCopy( userconfig, baseconfig );

	// Init core.
	GIsClient = 1; 
	GIsGuarded = 0;
	appInit( TEXT("UnrealTournament"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
#ifdef PLATFORM_PSP
	{
		extern CORE_API void appPspRecordNativeSizes();
		appPspRecordNativeSizes();
	}
#endif

	// Init mode.
	GIsServer		= 1;
	GIsClient		= !ParseParam(appCmdLine(), TEXT("SERVER"));
	GIsEditor		= 0;
	GIsScriptable	= 1;
#if defined(PLATFORM_DREAMCAST) || defined(PLATFORM_PSP)
	// Load package objects on demand rather than whole packages up front.
	GLazyLoad  		= 1;
#else
	GLazyLoad		= !GIsClient || ParseParam(appCmdLine(), TEXT("LAZY"));
#endif
#ifdef PLATFORM_DREAMCAST
		Warn.AuxOut = GLog;
		GLog		= &Warn;
#else
		// Init console log.
	if (ParseParam(CmdLine, TEXT("LOG")))
	{
			Warn.AuxOut = GLog;
			GLog		= &Warn;
	}
#endif

	// Init engine.
	UEngine* Engine = InitEngine();
	if( Engine )
	{
		debugf( NAME_Title, LocalizeGeneral("Run") );

		// Optionally Exec and exec file.
		FString Temp;
		if( Parse(CmdLine, TEXT("EXEC="), Temp) )
		{
			Temp = FString(TEXT("exec ")) + Temp;
			if( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
				Engine->Client->Viewports(0)->Exec( *Temp, *GLog );
		}

		// Start main engine loop.
		debugf( TEXT("Entering main loop.") );
		if ( !GIsRequestingExit )
			MainLoop( Engine );
	}

	// Finish up.
	return CleanUpOnExit(ErrorLevel);

	unguard;
	}
	catch (...)
	{
		// Chained abort.  Do cleanup.
		HandleError(NULL);
		return CleanUpOnExit(1);
	}
}