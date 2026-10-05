/*============================================================================
	UnSocket.h: Common interface for WinSock and BSD sockets.

	Revision history:
		* Created by Mike Danylchuk
============================================================================*/

/*-----------------------------------------------------------------------------
	Definitions.
-----------------------------------------------------------------------------*/

#if __WINSOCK__
	typedef INT					__SIZE_T__;
	#define GCC_OPT_INT_CAST
#endif

// Provide WinSock definitions for BSD sockets.
#if __BSD_SOCKETS__
	typedef int					SOCKET;
	typedef struct hostent		HOSTENT;
	typedef in_addr				IN_ADDR;
	typedef struct sockaddr		SOCKADDR;
	typedef struct sockaddr_in	SOCKADDR_IN;
	typedef struct linger		LINGER;
	typedef struct timeval		TIMEVAL;
	typedef TCHAR*				LPSTR;

	#define INVALID_SOCKET		-1
	#define SOCKET_ERROR		-1
	//#define WSAEWOULDBLOCK		EWOULDBLOCK
	#define WSAEWOULDBLOCK		EAGAIN
	#define WSAENOTSOCK			ENOTSOCK
	#define WSATRY_AGAIN		TRY_AGAIN
	#define WSAHOST_NOT_FOUND	HOST_NOT_FOUND
	#define WSANO_DATA			NO_DATA
	#define LPSOCKADDR			sockaddr*

	#define closesocket			close
	#define ioctlsocket			ioctl
	#define WSAGetLastError()	errno

	#define GCC_OPT_INT_CAST	(DWORD*)
#endif

// IP address macros.
#if __WINSOCK__
	#define IP(sin_addr,n) sin_addr.S_un.S_un_b.s_b##n
#elif __BSD_SOCKETS__
	#define IP(sin_addr,n) ((BYTE*)&sin_addr.s_addr)[n-1]
#endif

#ifdef PLATFORM_DREAMCAST

#include <kos/net.h>
#include <ppp/ppp.h>
#include <dc/modem/modem.h>

struct linger {
	int l_onoff;
	int l_linger;
};

#define ESOCKTNOSUPPORT 44
#define ESHUTDOWN 58
#define EUSERS 68
#define EREMOTE 71
#define FIONBIO O_NONBLOCK
#undef ioctlsocket

static inline int ioctlsocket( int fd, int opt, void* val )
{
	int flags = fcntl( fd, F_GETFL, 0 );
	return fcntl( fd, F_SETFL, flags | opt );
}

#endif

#ifdef PLATFORM_PSP
//
// PSP: networking is compiled in but deliberately inert (as in the Unreal
// PSP port). The IpDrv classes must register so UT's packages load, but the
// PSP network stack has to be brought up explicitly before ANY socket call:
// libcglue's socket()/bind()/inet_addr() go straight to sceNetInet*, and
// calling them uninitialised is a kernel fault that reboots the console.
// Every operation reports failure instead, which the engine already handles
// as "no network available". These macros come after the system headers, so
// the real declarations are untouched.
//
#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffU
#endif
#ifndef ESOCKTNOSUPPORT
#define ESOCKTNOSUPPORT 44
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN 58
#endif
#ifndef EUSERS
#define EUSERS 68
#endif
#ifndef EREMOTE
#define EREMOTE 71
#endif
#undef  closesocket
#define closesocket( fd )                   ( 0 )
#undef  ioctlsocket
#define ioctlsocket( fd, opt, arg )         ( -1 )
#define socket( af, type, proto )           ( -1 )
#define bind( fd, addr, len )               ( -1 )
#define connect( fd, addr, len )            ( -1 )
#define listen( fd, backlog )               ( -1 )
#define accept( fd, addr, len )             ( -1 )
#define setsockopt( a, b, c, d, e )         ( -1 )
#define getsockopt( a, b, c, d, e )         ( -1 )
#define getsockname( a, b, c )              ( -1 )
#define send( a, b, c, d )                  ( -1 )
#define recv( a, b, c, d )                  ( -1 )
#define sendto( a, b, c, d, e, f )          ( -1 )
#define recvfrom( a, b, c, d, e, f )        ( -1 )
#define select( a, b, c, d, e )             ( -1 )
#define gethostbyname( name )               ( (HOSTENT*)0 )
#define gethostbyaddr( a, b, c )            ( (HOSTENT*)0 )
#define inet_addr( str )                    ( 0xffffffffU )
#define gethostname( name, len )            ( -1 )
#endif

/*----------------------------------------------------------------------------
	Functions.
----------------------------------------------------------------------------*/

UBOOL InitSockets( FString& Error );
TCHAR* SocketError( INT Code=-1 );
UBOOL IpMatches( sockaddr_in& A, sockaddr_in& B );
void IpGetBytes( in_addr Addr, BYTE& Ip1, BYTE& Ip2, BYTE& Ip3, BYTE& Ip4 );
void IpSetBytes( in_addr& Addr, BYTE Ip1, BYTE Ip2, BYTE Ip3, BYTE Ip4 );
void IpGetInt( in_addr Addr, DWORD& Ip );
void IpSetInt( in_addr& Addr, DWORD Ip );
FString IpString( in_addr Addr, INT Port=0 );

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
