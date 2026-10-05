/*=============================================================================
	UnStaticExports.cpp: Dreamcast-specific routines.
=============================================================================*/

#ifndef UNREAL_STATIC
#error "This file is for static builds only."
#endif

#include "Core.h"

CORE_API FPackageExport* GExportsTable;

CORE_API void* appGetStaticExport( const char* Name )
{
	FPackageExport* Iter = GExportsTable;
	while( Iter )
	{
#ifdef PLATFORM_PSP
		if( !Iter->Name )
		{
			debugf( NAME_Warning, TEXT("PSPEXPORT: node %p has no name (addr %p next %p) looking up %s"), Iter, Iter->Address, Iter->Next, Name );
			for( FPackageExport* E = GExportsTable; E && E != Iter; E = E->Next )
				debugf( NAME_Warning, TEXT("PSPEXPORT:   %p %s"), E, E->Name );
			Iter = Iter->Next;
			continue;
		}
#endif
		if( !appStrcmp( Name, Iter->Name ) )
			return Iter->Address;
		Iter = Iter->Next;
	}
	return nullptr;
}
