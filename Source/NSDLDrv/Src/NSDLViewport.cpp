#include <string.h>
#include <ctype.h>

#include "NSDLDrv.h"
#include "UnRender.h"

IMPLEMENT_CLASS( UNSDLViewport );

/*-----------------------------------------------------------------------------
	UNSDLViewport implementation.
-----------------------------------------------------------------------------*/

//
// SDL_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::MouseButtonMap[6] =
{
	/* invalid           */ IK_None,
	/* SDL_BUTTON_LEFT   */ IK_LeftMouse,
	/* SDL_BUTTON_MIDDLE */ IK_MiddleMouse,
	/* SDL_BUTTON_RIGHT  */ IK_RightMouse,
	/* SDL_BUTTON_X1     */ IK_None,
	/* SDL_BUTTON_X2     */ IK_None
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::JoyButtonMap[SDL_CONTROLLER_BUTTON_MAX] =
{
	/* BUTTON_A             */ IK_Joy1,
	/* BUTTON_B             */ IK_Joy2,
	/* BUTTON_X             */ IK_Joy3,
	/* BUTTON_Y             */ IK_Joy4,
	/* BUTTON_BACK          */ IK_Joy5,
	/* BUTTON_GUIDE         */ IK_Joy6,
#ifdef PLATFORM_PSP
	// UT's console opens its menu on the raw Escape key, before bindings.
	/* BUTTON_START         */ IK_Escape,
#else
	/* BUTTON_START         */ IK_Joy7,
#endif
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_JoyPovUp,
	/* BUTTON_DPAD_DOWN     */ IK_JoyPovDown,
	/* BUTTON_DPAD_LEFT     */ IK_JoyPovLeft,
	/* BUTTON_DPAD_RIGHT    */ IK_JoyPovRight,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map for UI controls.
//
const BYTE UNSDLViewport::JoyButtonMapUI[SDL_CONTROLLER_BUTTON_MAX] =
{
#ifdef PLATFORM_PSP
	// UWindow is a mouse UI: the stick moves the cursor (see below).
	/* BUTTON_A (Cross)     */ IK_LeftMouse,
	/* BUTTON_B (Circle)    */ IK_Escape,
	/* BUTTON_X (Square)    */ IK_RightMouse,
	/* BUTTON_Y (Triangle)  */ IK_Enter,
#else
	/* BUTTON_A             */ IK_Enter,
	/* BUTTON_B             */ IK_Escape,
	/* BUTTON_X             */ IK_N,
	/* BUTTON_Y             */ IK_Y,
#endif
	/* BUTTON_BACK          */ IK_Escape,
	/* BUTTON_GUIDE         */ IK_Escape,
	/* BUTTON_START         */ IK_Escape,
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_Up,
	/* BUTTON_DPAD_DOWN     */ IK_Down,
	/* BUTTON_DPAD_LEFT     */ IK_Left,
	/* BUTTON_DPAD_RIGHT    */ IK_Right,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
// UT's menus are UWindow, which the console runs while bUWindowActive is set.
static UBOOL PspInUWindow( UObject* Console )
{
	if( !Console )
		return 0;
	static UClass* CachedClass = NULL;
	static UBoolProperty* Prop = NULL;
	if( Console->GetClass() != CachedClass )
	{
		CachedClass = Console->GetClass();
		Prop = FindField<UBoolProperty>( CachedClass, TEXT("bUWindowActive") );
	}
	return Prop && ( *(DWORD*)( (BYTE*)Console + Prop->Offset ) & Prop->BitMask );
}

const BYTE UNSDLViewport::JoyAxisMap[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ IK_JoyX,
	/* AXIS_LEFT_Y          */ IK_JoyY,
	/* AXIS_RIGHT_X         */ IK_JoyU,
	/* AXIS_RIGHT_Y         */ IK_JoyV,
	/* AXIS_LTRIGGER        */ IK_Joy12,
	/* AXIS_RTRIGGER        */ IK_Joy13,
};

//
// Additional scale to apply per SDL axis.
//
const FLOAT UNSDLViewport::JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ +60.f,
	/* AXIS_LEFT_Y          */ -60.f,
	/* AXIS_RIGHT_X         */ +60.f,
	/* AXIS_RIGHT_Y         */ +60.f,
	/* AXIS_LTRIGGER        */ +60.f,
	/* AXIS_RTRIGGER        */ +60.f,
};

//
// SDL_Scancode -> EInputKey translation map.
//
BYTE UNSDLViewport::KeyMap[512];
void UNSDLViewport::InitKeyMap()
{
	#define INIT_KEY_RANGE( AStart, AEnd, BStart, BEnd ) \
		for( DWORD Key = AStart; Key <= AEnd; ++Key ) KeyMap[Key] = BStart + ( Key - AStart )

	appMemset( KeyMap, 0, sizeof( KeyMap ) );

	// TODO: IK_LControl, IK_LShift, etc exist, what are they for?
	KeyMap[SDL_SCANCODE_LSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_RSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_LCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_RCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_LALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_RALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_GRAVE] = IK_Tilde;
	KeyMap[SDL_SCANCODE_ESCAPE] = IK_Escape;
	KeyMap[SDL_SCANCODE_SPACE] = IK_Space;
	KeyMap[SDL_SCANCODE_RETURN] = IK_Enter;
	KeyMap[SDL_SCANCODE_BACKSPACE] = IK_Backspace;
	KeyMap[SDL_SCANCODE_CAPSLOCK] = IK_CapsLock;
	KeyMap[SDL_SCANCODE_TAB] = IK_Tab;
	KeyMap[SDL_SCANCODE_DELETE] = IK_Delete;
	KeyMap[SDL_SCANCODE_INSERT] = IK_Insert;
	KeyMap[SDL_SCANCODE_HOME] = IK_Home;
	KeyMap[SDL_SCANCODE_END] = IK_End;
	KeyMap[SDL_SCANCODE_PAGEUP] = IK_PageUp;
	KeyMap[SDL_SCANCODE_PAGEDOWN] = IK_PageDown;
	KeyMap[SDL_SCANCODE_PRINTSCREEN] = IK_PrintScrn;
	KeyMap[SDL_SCANCODE_EQUALS] = IK_Equals;
	KeyMap[SDL_SCANCODE_SEMICOLON] = IK_Semicolon;
	KeyMap[SDL_SCANCODE_BACKSLASH] = IK_Backslash;
	KeyMap[SDL_SCANCODE_SLASH] = IK_Slash;
	KeyMap[SDL_SCANCODE_LEFTBRACKET] = IK_LeftBracket;
	KeyMap[SDL_SCANCODE_RIGHTBRACKET] = IK_RightBracket;
	KeyMap[SDL_SCANCODE_COMMA] = IK_Comma;
	KeyMap[SDL_SCANCODE_PERIOD] = IK_Period;
	KeyMap[SDL_SCANCODE_LEFT] = IK_Left;
	KeyMap[SDL_SCANCODE_UP] = IK_Up;
	KeyMap[SDL_SCANCODE_RIGHT] = IK_Right;
	KeyMap[SDL_SCANCODE_DOWN] = IK_Down;
	KeyMap[SDL_SCANCODE_0] = IK_0;
	KeyMap[SDL_SCANCODE_KP_0] = IK_NumPad0;
	KeyMap[SDL_SCANCODE_KP_PERIOD] = IK_NumPadPeriod;

	INIT_KEY_RANGE( SDL_SCANCODE_1,    SDL_SCANCODE_9,    IK_1,       IK_9 );
	INIT_KEY_RANGE( SDL_SCANCODE_A,    SDL_SCANCODE_Z,    IK_A,       IK_Z );
	INIT_KEY_RANGE( SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_9, IK_NumPad1, IK_NumPad9 );
	INIT_KEY_RANGE( SDL_SCANCODE_F1,   SDL_SCANCODE_F12,  IK_F1,      IK_F12 );
	INIT_KEY_RANGE( SDL_SCANCODE_F13,  SDL_SCANCODE_F24,  IK_F13,     IK_F24 );

	#undef INIT_KEY_RANGE
}

//
// Static init.
//
void UNSDLViewport::InternalClassInitializer( UClass* Class )
{
	guard(UNSDLViewport::InternalClassInitializer);
	// Fill in keymap.
	InitKeyMap();
	unguard;
}

//
// Constructor.
//
UNSDLViewport::UNSDLViewport( ULevel* InLevel, UNSDLClient* InClient )
:	UViewport()
,	Client( InClient )
{
	guard(UNSDLViewport::UNSDLViewport);

	// Set color bytes based on screen resolution.
	SDL_DisplayMode Mode;
	SDL_GetDesktopDisplayMode( InClient->DefaultDisplay, &Mode );
	ColorBytes = SDL_BYTESPERPIXEL( Mode.format );
	Caps = 0;
	if( ColorBytes == 2 && SDL_PIXELLAYOUT( Mode.format ) == SDL_PACKEDLAYOUT_565 )
	{
		Caps |= CC_RGB565;
	}

	// Inherit default display until we have a window.
	DisplayIndex = InClient->DefaultDisplay;
	DisplaySize.w = InClient->GetDefaultDisplayMode().w;
	DisplaySize.h = InClient->GetDefaultDisplayMode().h;

	// Init input.
	if( GIsEditor )
		Input->Init( this );

	Destroyed = false;
	QuitRequested = false;
	HoldCount = 0;

	unguard;
}

// UObject interface.
void UNSDLViewport::Destroy()
{
	guard(UNSDLViewport::Destroy);
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	UViewport::Destroy();
	unguard;
}

//
// Set the mouse cursor according to Unreal or UnrealEd's mode, or to
// an hourglass if a slow task is active. Not implemented.
//
void UNSDLViewport::SetModeCursor()
{
	guard(UNSDLViewport::SetModeCursor);
	unguard;
}

//
// Update user viewport interface.
//
void UNSDLViewport::UpdateWindowFrame()
{
	guard(UNSDLViewport::UpdateWindowFrame);

	// If not a window, exit.
	if( hWnd==NULL || HoldCount > 0 )
		return;

	// Set viewport window's name to show resolution.
	char WindowName[80];
	if( !GIsEditor || (Actor->ShowFlags&SHOW_PlayerCtrl) )
	{
		appSprintf( WindowName, LocalizeGeneral("Product","Core") );
	}
	else switch( Actor->RendMap )
	{
		case REN_Wire:		strcpy(WindowName,LocalizeGeneral("ViewPersp")); break;
		case REN_OrthXY:	strcpy(WindowName,LocalizeGeneral("ViewXY")); break;
		case REN_OrthXZ:	strcpy(WindowName,LocalizeGeneral("ViewXZ")); break;
		case REN_OrthYZ:	strcpy(WindowName,LocalizeGeneral("ViewYZ")); break;
		default:			strcpy(WindowName,LocalizeGeneral("ViewOther")); break;
	}

	// Set window title.
	if( SizeX && SizeY )
	{
		appSprintf(WindowName+strlen(WindowName)," (%i x %i)",SizeX,SizeY);
		if( this == Client->CurrentViewport() )
			strcat( WindowName, " *" );
	}
	SDL_SetWindowTitle( hWnd, WindowName );

	unguard;
}

//
// Open a viewport window.
//
void UNSDLViewport::OpenWindow( DWORD InParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UNSDLViewport::OpenWindow);
	check(Actor);
	check(HoldCount == 0);
	UBOOL DoRepaint=0, DoSetActive=0;
	UBOOL DoOpenGL=0;
	UBOOL NoHard=ParseParam( appCmdLine(), "nohard" );
	SDL_GLprofile GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;

	// Clamp invalid sizes – SDL textures can't be 0x0
	// If no size specified, try to read from config
	if( NewX <= 0 || NewY <= 0 )
	{
		if( !Temporary && !GIsEditor )
		{
			// Try to get windowed viewport size from config
			INT ConfigX = 0, ConfigY = 0;
			if( GConfig->GetInt( TEXT("NSDLDrv.NSDLClient"), TEXT("WindowedViewportX"), ConfigX ) && ConfigX > 0 )
				NewX = ConfigX;
			if( GConfig->GetInt( TEXT("NSDLDrv.NSDLClient"), TEXT("WindowedViewportY"), ConfigY ) && ConfigY > 0 )
				NewY = ConfigY;
		}
		// Final fallback
		if( NewX <= 0 ) NewX = 320;
		if( NewY <= 0 ) NewY = 240;
	}
	NewX = Align(NewX,4);
	debugf( NAME_Log, TEXT("OpenWindow: NewX=%d, NewY=%d"), NewX, NewY );

	if( !Temporary && !GIsEditor && !NoHard )
	{
		// HACK: Just check if we're about to load OpenGLDrv. Not sure how else you would know to add the GL flag.
		FString Temp;
		Parse( appCmdLine(), TEXT("GAMERENDERDEVICE="), Temp );
		if( Temp.Len() == 0 )
			GConfig->GetString( TEXT("Engine.Engine"), TEXT("GameRenderDevice"), Temp );
		appStrupr( (TCHAR*)*Temp );
		if( Temp.InStr(TEXT("OPENGL")) == INDEX_NONE )
		{
			Parse( appCmdLine(), TEXT("WINDOWEDRENDERDEVICE="), Temp );
			if( Temp.Len() == 0 )
				GConfig->GetString( TEXT("Engine.Engine"), TEXT("WindowedRenderDevice"), Temp );
			appStrupr( (TCHAR*)*Temp );
			if( Temp.InStr(TEXT("OPENGL")) != INDEX_NONE )
				DoOpenGL = 1;
		}
		else
		{
			DoOpenGL = 1;
		}
		if( DoOpenGL && Temp.InStr(TEXT("GLES")) != INDEX_NONE )
			GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
	}

	// User window of launcher if no parent window was specified.
	if( !InParentWindow )
	{
		QWORD ParentPtr;
		Parse( appCmdLine(), TEXT("HWND="), ParentPtr );
		InParentWindow = (DWORD)ParentPtr;
	}

	if( Temporary )
	{
		// Create in-memory data.
		ColorBytes = 2;
		ScreenPointer = (BYTE*)appMalloc( 2 * NewX * NewY, "TemporaryViewportData" );	
		hWnd = NULL;
		debugf( NAME_Log, "Opened temporary viewport" );
	}
	else
	{
		// Get flags.
		DWORD Flags = 0;
		if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
		{
			Flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
		}
		else
		{
			Flags = SDL_WINDOW_HIDDEN;
		}
		if( DoOpenGL )
		{
			Flags |= SDL_WINDOW_OPENGL;
		}

		// Set OpenGL attributes if needed.
		if( DoOpenGL )
		{
			if( GLProfile == SDL_GL_CONTEXT_PROFILE_ES )
			{
				// Request GLES2.
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			}
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, GLProfile );
		}

		// Set position and size.
		if( OpenX==-1 )
			OpenX = SDL_WINDOWPOS_UNDEFINED;
		if( OpenY==-1 )
			OpenY = SDL_WINDOWPOS_UNDEFINED;

		// If switching renderers, destroy the old window.
		if( hWnd && ( DoOpenGL != !!( SDL_GetWindowFlags( hWnd ) & SDL_WINDOW_OPENGL ) ) )
		{
			CloseWindow();
		}

		// Create or update the window.
		if( !hWnd )
		{
			// Creating new viewport.
			hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
			if( !hWnd && DoOpenGL )
			{
				// Try without GL.
				debugf( NAME_Warning, "Could not create OpenGL window: %s. Trying without OpenGL.", SDL_GetError() );
				Flags &= ~SDL_WINDOW_OPENGL;
				DoOpenGL = 0;
				hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
			}
			if( !hWnd )
			{
				appErrorf( "Could not create SDL window: %s", SDL_GetError() );
			}

			// Set parent window.
			if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
			{
				SDL_SetWindowModalFor( hWnd, (SDL_Window*)InParentWindow );
			}

			debugf( NAME_Log, "Opened viewport" );
			DoSetActive = DoRepaint = 1;
		}
		else
		{
			// Resizing existing viewport.
			SetClientSize( NewX, NewY, false );
		}

		// Create GL context or SDL renderer if needed.
		if( DoOpenGL )
		{
			if( !GLCtx )
			{
				GLCtx = SDL_GL_CreateContext( hWnd );
				if( !GLCtx )
				{
					appErrorf( "Could not create GL context: %s", SDL_GetError() );
				}
			}
			SDL_GL_MakeCurrent( hWnd, GLCtx );
		}
		else
		{
			SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "nearest" );
			SDLRen = SDL_CreateRenderer( hWnd, -1, 0 );
			if( !SDLRen )
			{
				// Fallback to software.
				debugf( NAME_Warning, "Could not create SDL renderer: %s. Trying software.", SDL_GetError() );
				SDLRen = SDL_CreateRenderer( hWnd, -1, SDL_RENDERER_SOFTWARE );
				if( !SDLRen )
				{
					appErrorf( "Could not create SDL renderer: %s", SDL_GetError() );
				}
			}
			// Create framebuffer texture.
			SDLTexFormat = SDL_PIXELFORMAT_ARGB8888;
			ColorBytes = SDL_BYTESPERPIXEL( SDLTexFormat );
			Caps = ( SDL_PIXELLAYOUT( SDLTexFormat ) == SDL_PACKEDLAYOUT_565 ) ? CC_RGB565 : 0;
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}

		SDL_ShowWindow( hWnd );

		// Get this window's display parameters.
		SDL_DisplayMode DisplayMode;
		DisplayIndex = SDL_GetWindowDisplayIndex( hWnd );
		if( SDL_GetWindowDisplayMode( hWnd, &DisplayMode ) == 0 )
		{
			DisplaySize.w = DisplayMode.w;
			DisplaySize.h = DisplayMode.h;
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	if( !RenDev && Temporary )
		Client->TryRenderDevice( this, "SoftDrv.SoftwareRenderDevice", 0 );
	if( !RenDev && !GIsEditor && !NoHard )
		Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", Client->StartupFullscreen );
	if( !RenDev )
		Client->TryRenderDevice( this, "ini:Engine.Engine.WindowedRenderDevice", 0 );
	check(RenDev);

	if( !Temporary )
		UpdateWindowFrame();
	if( DoRepaint )
		Repaint( 1 );

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been opened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void UNSDLViewport::CloseWindow()
{
	guard(UNSDLViewport::CloseWindow);

	if( hWnd )
	{
		if( SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = NULL;
		}
		if( SDLRen )
		{
			SDL_DestroyRenderer( SDLRen );
			SDLRen = NULL;
		}
		if( GLCtx )
		{
			SDL_GL_DeleteContext( GLCtx );
			GLCtx = NULL;
		}
		SDL_DestroyWindow( hWnd );
		hWnd = NULL;
	}

	unguard;
}

//
// Lock the viewport window and set the approprite Screen and RealScreen fields
// of Viewport.  Returns 1 if locked successfully, 0 if failed.  Note that a
// lock failing is not a critical error; it's a sign that a DirectDraw mode
// has ended or the user has closed a viewport window.
//
UBOOL UNSDLViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
	guard(UNSDLViewport::LockWindow);
	clock(Client->DrawCycles);

	// Make sure window is lockable.
	if( !hWnd )
	{
		return 0;
	}

	if( HoldCount > 0 || !SizeX || !SizeY )
	{
		appErrorf( "Failed locking viewport" );
		return 0;
	}

	if( SDLRen && SDLTex )
	{
		// Obtain pointer to screen.
		Stride = SizeX;
		ScreenPointer = NULL;
		SDL_LockTexture( SDLTex, NULL, (void **)&ScreenPointer, &Stride );
		Stride /= ColorBytes;
		check(ScreenPointer);
	}

	// Success.
	unclock(Client->DrawCycles);

	return UViewport::Lock( FlashScale, FlashFog, ScreenClear, RenderLockFlags, HitData, HitSize );

	unguard;
}

//
// Return whether fullscreen.
//
UBOOL UNSDLViewport::IsFullscreen()
{
	guard(UNSDLViewport::IsFullscreen);
	if( !hWnd )
		return 0;
	Uint32 Flags = SDL_GetWindowFlags( hWnd );
	return (Flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
	unguard;
}

//
// Resize the viewport.
//
UBOOL UNSDLViewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, INT InNewColorBytes )
{
	guard(UNSDLViewport::ResizeViewport);

	// Remember viewport for audio (mimic X/Win behaviour).
	UViewport* SavedViewport = NULL;
	if( Client->Engine->Audio && !GIsEditor && !(GetFlags() & RF_Destroyed) )
		SavedViewport = Client->Engine->Audio->GetViewport();

	// Accept default parameters.
	INT NewX          = InNewX         == INDEX_NONE ? SizeX      : InNewX;
	INT NewY          = InNewY         == INDEX_NONE ? SizeY      : InNewY;
	INT NewColorBytes = InNewColorBytes== INDEX_NONE ? ColorBytes : InNewColorBytes;

	// Default resolution handling: use client defaults when not explicitly specified.
	if( InNewX == INDEX_NONE || InNewY == INDEX_NONE )
	{
		const SDL_DisplayMode& DefaultMode = Client->GetDefaultDisplayMode();
		if( NewBlitFlags & BLIT_Fullscreen )
		{
			// In fullscreen, prefer desktop resolution if nothing is specified.
			NewX = InNewX != INDEX_NONE ? InNewX : DefaultMode.w;
			NewY = InNewY != INDEX_NONE ? InNewY : DefaultMode.h;
		}
	}

	// Apply new size / mode.
	if( NewBlitFlags & BLIT_Fullscreen )
	{
		// Switching to fullscreen.
		MakeFullscreen( NewX, NewY, 1 );
	}
	else
	{
		// Windowed resize.
		SetClientSize( NewX, NewY, 1 );
		EndFullscreen();
	}

	// Update color depth if requested (render devices read this).
	ColorBytes = NewColorBytes;

	// Update audio.
	if( SavedViewport && SavedViewport!=Client->Engine->Audio->GetViewport() )
		Client->Engine->Audio->SetViewport( SavedViewport );

	// Update window frame.
	UpdateWindowFrame();

	return 1;

	unguard;
}

//
// Unlock the viewport window.  If Blit=1, blits the viewport's frame buffer.
//
void UNSDLViewport::Unlock( UBOOL Blit )
{
	guard(UNSDLViewport::Unlock);

	Client->DrawCycles=0;
	clock(Client->DrawCycles);

	// Unlock base.
	UViewport::Unlock( Blit );

	// Blit, if desired.
	if( Blit && hWnd && HoldCount == 0 )
	{
		if( GLCtx )
		{

			SDL_GL_SwapWindow( hWnd );
		}
		else if( SDLRen && SDLTex )
		{
			// Blitting with SDLRenderer.
			SDL_UnlockTexture( SDLTex );
			SDL_RenderCopy( SDLRen, SDLTex, NULL, NULL );
			SDL_RenderPresent( SDLRen );
		}
	}

	unclock(Client->DrawCycles);

	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void UNSDLViewport::MakeCurrent()
{
	guard(UNSDLViewport::MakeCurrent);
	Current = 1;
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Client->Viewports(i);
		if( OldViewport->Current && OldViewport != this )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindowFrame();
		}
	}
	if( GLCtx )
	{
		SDL_GL_MakeCurrent( hWnd, GLCtx );
	}
	UpdateWindowFrame();
	unguard;
}

//
// Repaint the viewport.
//
void UNSDLViewport::Repaint( UBOOL Blit )
{
	guard(UNSDLViewport::Repaint);
	if( HoldCount == 0 && RenDev && SizeX && SizeY )
		Client->Engine->Draw( this, Blit );
	unguard;
}

//
// Set the client size (viewport view size) of a viewport.
//
void UNSDLViewport::SetClientSize( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::SetClientSize);

	// Guard against zero/negative sizes.
	if( NewX <= 0 ) NewX = 320;
	if( NewY <= 0 ) NewY = 240;

	if( hWnd )
	{
		SDL_SetWindowSize( hWnd, NewX, NewY );
		// Resize output texture if required.
		if( SDLRen && SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	// Optionally save this size in the profile.
	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
// Return the viewport's window.
//
void* UNSDLViewport::GetWindow()
{
	return (void*)hWnd;
}

//
// Try to make this viewport fullscreen, matching the fullscreen
// mode of the nearest x-size to the current window. If already in
// fullscreen, returns to non-fullscreen.
//
void UNSDLViewport::MakeFullscreen( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::MakeFullscreen);

	// If someone else is fullscreen, stop them.
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	Client->EndFullscreen();

	// Save this window.
	SavedX = SizeX;
	SavedY = SizeY;

	// Fullscreen rendering. For now no borderless.
	// Note: FullscreenViewport tracking removed - Unreal 1 specific
	SetClientSize( NewX, NewY, false );
	SDL_SetWindowFullscreen( hWnd, SDL_WINDOW_FULLSCREEN );

	if( UpdateProfile )
	{
		Client->FullscreenViewportX = NewX;
		Client->FullscreenViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

//
//
//
void UNSDLViewport::EndFullscreen()
{
	guard(UNSDLViewport::EndFullscreen);

	SDL_SetWindowFullscreen( hWnd, 0 );
	SetClientSize( SavedX, SavedY, false );

	unguard;
}

//
// Update input for viewport.
//
void UNSDLViewport::UpdateInput( UBOOL Reset )
{
	guard(UNSDLViewport::UpdateInput);

	if( Reset )
		appMemset( (void*)JoyAxis, 0, sizeof(JoyAxis) );

	unguard;
}

//
// If the cursor is currently being captured, stop capturing, clipping, and 
// hiding it, and move its position back to where it was when it was initially
// captured.
//
void UNSDLViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(UNSDLViewport::SetMouseCapture);

	// If only focus, reject.
	if( OnlyFocus )
		if( hWnd != SDL_GetMouseFocus() )
			return;

	// If capturing, windows requires clipping in order to keep focus.
	Clip |= Capture;

	// Handle capturing.
	SDL_SetRelativeMouseMode( (SDL_bool)Capture );

	unguard;
}

UBOOL UNSDLViewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(UWindowsViewport::CauseInputEvent);

	// Route to engine if a valid key
	if( iKey > 0 )
		return Client->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	else
		return 0;

	unguard;
}

UBOOL UNSDLViewport::TickInput()
{
	guard(UNSDLViewport::TickInput);

	SDL_Event Ev;
	INT Tmp;
	const DOUBLE CurTime = appSeconds();
	const FLOAT DeltaTime = (FLOAT)( CurTime - InputUpdateTime );

	while( SDL_PollEvent( &Ev ) )
	{
		switch( Ev.type )
		{
			case SDL_QUIT:
				// signal to client and remember set a flag just in case
				QuitRequested = true;
				return true;
			case SDL_TEXTINPUT:
				for( const char *p = Ev.text.text; *p && p < Ev.text.text + sizeof( Ev.text.text ); ++p )
				{
					if( *p < 0 )
						break;
					if( isprint( *p ) || *p == '\r' )
						Client->Engine->Key( this, (EInputKey)*p );
				}
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				CauseInputEvent( KeyMap[Ev.key.keysym.scancode], ( Ev.type == SDL_KEYDOWN ) ? IST_Press : IST_Release );
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
				CauseInputEvent( MouseButtonMap[Ev.button.button], ( Ev.type == SDL_MOUSEBUTTONDOWN ) ? IST_Press : IST_Release );
				break;
			case SDL_MOUSEWHEEL:
				if( Ev.wheel.y )
				{
					CauseInputEvent( IK_MouseW, IST_Axis, Ev.wheel.y );
					if( Ev.wheel.y < 0 )
					{
						CauseInputEvent( IK_MouseWheelDown, IST_Press );
						CauseInputEvent( IK_MouseWheelDown, IST_Release );
					}
					else if( Ev.wheel.y > 0 )
					{
						CauseInputEvent( IK_MouseWheelUp, IST_Press );
						CauseInputEvent( IK_MouseWheelUp, IST_Release );
					}
				}
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				{
					// HACK: Swap to alternate bindings when in menus, but not when waiting for keypress in the keybind menu.
					// Note: GetMainFrame() is Unreal 1 specific, disabled for UT99
					const UBOOL bIsInUI = PspInUWindow( (UObject*)Console );
					const BYTE* JoyMap = bIsInUI ? JoyButtonMapUI : JoyButtonMap;
#ifdef PLATFORM_PSP
					// R shift layer (in game only): R itself still fires its own
					// binding (alt-fire, held to charge), and while it is down
					// L, D-pad up, left and right report as other keys:
					//   R+L -> Joy12, R+Up -> Joy13, R+Left -> Joy14, R+Right -> Joy15
					// A button released after R remembers it was shifted, so the
					// release reaches the same key as the press.
					if( !bIsInUI )
					{
						static UBOOL RHeld = 0;
						static DWORD ShiftedDown = 0;   // bit per SDL button pressed while shifted
						const UBOOL Down = ( Ev.type == SDL_CONTROLLERBUTTONDOWN );
						const INT   B    = Ev.cbutton.button;
						if( B == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER )
							RHeld = Down;
						BYTE Shifted = 0;
						switch( B )
						{
							case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: Shifted = IK_Joy12; break;
							case SDL_CONTROLLER_BUTTON_DPAD_UP:      Shifted = IK_Joy13; break;
							case SDL_CONTROLLER_BUTTON_DPAD_LEFT:    Shifted = IK_Joy14; break;
							case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:   Shifted = IK_Joy15; break;
						}
						if( Shifted && ( Down ? RHeld : ( ShiftedDown & (1u<<B) ) ) )
						{
							if( Down ) ShiftedDown |= (1u<<B); else ShiftedDown &= ~(1u<<B);
							CauseInputEvent( Shifted, Down ? IST_Press : IST_Release );
							break;
						}
					}
#endif
					CauseInputEvent( JoyMap[Ev.cbutton.button], ( Ev.type == SDL_CONTROLLERBUTTONDOWN ) ? IST_Press : IST_Release );
				}
				break;
			case SDL_CONTROLLERAXISMOTION:
				{
					const BYTE Key = JoyAxisMap[Ev.caxis.axis];
					const INT PrevValue = JoyAxis[Ev.caxis.axis];
					INT NewValue = Ev.caxis.value;
					INT DeadZone = 0;
					if ( Key < IK_JoyX )
					{
						// Treat the axis like a trigger.
						if ( PrevValue < JoyAxisPressThreshold && NewValue >= JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Press );
						else if ( PrevValue >= JoyAxisPressThreshold && NewValue < JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Release );
					}
					else
					{
						// Apply deadzone.
						if ( Key >= IK_JoyX && Key <= IK_JoyZ )
							DeadZone = Client->DeadZoneXYZ * 32767.f;
						else if ( Key == IK_JoyR || Key == IK_JoyU || Key == IK_JoyV )
							DeadZone = Client->DeadZoneRUV * 32767.f;
						if ( Abs(NewValue) < DeadZone )
							NewValue = 0;
					}
					JoyAxis[Ev.caxis.axis] = NewValue;
				}
				break;
			case SDL_MOUSEMOTION:
				if( !SDL_GetRelativeMouseMode() )
				{
					// If cursor isn't captured, just do MousePosition.
					Client->Engine->MousePosition( this, 0, Ev.motion.x, Ev.motion.y );
				}
				else
				{
					DWORD ViewportButtonFlags = 0;
					if( Ev.motion.state & SDL_BUTTON_LMASK ) ViewportButtonFlags |= MOUSE_Left;
					if( Ev.motion.state & SDL_BUTTON_RMASK ) ViewportButtonFlags |= MOUSE_Right;
					if( Ev.motion.state & SDL_BUTTON_MMASK ) ViewportButtonFlags |= MOUSE_Middle;
					if( Ev.motion.xrel || Ev.motion.yrel )
					{
						Client->Engine->MouseDelta( this, ViewportButtonFlags, Ev.motion.xrel, -Ev.motion.yrel );
						if( Ev.motion.xrel ) CauseInputEvent( IK_MouseX, IST_Axis, Ev.motion.xrel );
						if( Ev.motion.yrel ) CauseInputEvent( IK_MouseY, IST_Axis, -Ev.motion.yrel );
					}
				}
				break;
			default:
				break;
		}
	}

	// In the menus the left stick is the mouse: UWindow's console moves its
	// cursor by IK_MouseX/IK_MouseY axis deltas (times its MouseScale, 0.6).
	if( PspInUWindow( (UObject*)Console ) )
	{
		const FLOAT Speed = 700.f * DeltaTime;   // full tilt crosses the 480px screen in ~1s
		const FLOAT X = JoyAxis[SDL_CONTROLLER_AXIS_LEFTX] / 32767.f;
		const FLOAT Y = JoyAxis[SDL_CONTROLLER_AXIS_LEFTY] / 32767.f;
		// Square response: fine control near the centre, speed at the rim.
		if( X ) CauseInputEvent( IK_MouseX, IST_Axis,  X * Abs(X) * Speed );
		if( Y ) CauseInputEvent( IK_MouseY, IST_Axis, -Y * Abs(Y) * Speed );
		InputUpdateTime = CurTime;
		return QuitRequested;
	}

	// Constantly hammer the input system with axis events for axes that are not zero.
	for ( INT i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i )
	{
		const BYTE Key = JoyAxisMap[i];
		const SWORD Value = JoyAxis[i];
		if ( Value && Key && Key >= IK_JoyX )
		{
			const FLOAT FltValue = Clamp( Value / 32767.f, -1.f, 1.f );
			FLOAT Scale = ( Key >= IK_JoyX && Key <= IK_JoyZ ) ? Client->ScaleXYZ : Client->ScaleRUV;
			Scale *= JoyAxisDefaultScale[i] * DeltaTime;
			if ( ( Client->InvertV && Key == IK_JoyV ) || ( Client->InvertY && Key == IK_JoyY ) )
				Scale = -Scale;
			CauseInputEvent( Key, IST_Axis, FltValue * Scale );
		}
	}

	InputUpdateTime = CurTime;

	return QuitRequested;

	unguard;
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

UBOOL UNSDLViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNSDLViewport::Exec);
	if( UViewport::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("ToggleFullscreen")) )
	{
		// Toggle fullscreen.
		// Note: FullscreenViewport tracking removed - Unreal 1 specific
		Client->EndFullscreen();
		if( !(Actor->ShowFlags & SHOW_ChildWindow) )
			Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", 1 );
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("GetCurrentRes")) )
	{
		Ar.Logf( TEXT("%ix%i"), SizeX, SizeY );
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("SetRes")) )
	{
		INT X=appAtoi(Cmd), Y=appAtoi(appStrchr(Cmd,TEXT('x')) ? appStrchr(Cmd,TEXT('x'))+1 : appStrchr(Cmd,TEXT('X')) ? appStrchr(Cmd,TEXT('X'))+1 : TEXT(""));
		if( X && Y )
		{
			// Note: FullscreenViewport tracking removed - Unreal 1 specific
			MakeFullscreen( X, Y, 1 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd, TEXT("Preferences")) )
	{
		// Note: FullscreenViewport tracking removed - Unreal 1 specific
		Client->EndFullscreen();
		return 1;
	}
	else return 0;
	unguard;
}
