/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#	if WINVER < 0x501
#		ifdef __MINGW32__
			// wspiapi.h isn't available on MinGW, so if it's
			// present it's because the end user has added it
			// and we should look for it in our tree
#			include "wspiapi.h"
#		else
#			include <wspiapi.h>
#		endif
#	else // WINVER >= 0x501

#if	1	// Windows2000 compatibility 
#		include <ws2tcpip.h>
#		include <wspiapi.h>
#else
#		include <ws2spi.h>
#endif	

#endif // WINVER >= 0x501

typedef int socklen_t;
#	ifdef ADDRESS_FAMILY
#		define sa_family_t	ADDRESS_FAMILY
#	else
typedef unsigned short sa_family_t;
#	endif

#	undef EAGAIN
#	undef EADDRNOTAVAIL
#	undef EAFNOSUPPORT
#	undef ECONNRESET

#	define EAGAIN			WSAEWOULDBLOCK
#	define EADDRNOTAVAIL	WSAEADDRNOTAVAIL
#	define EAFNOSUPPORT		WSAEAFNOSUPPORT
#	define ECONNRESET		WSAECONNRESET
typedef u_long	ioctlarg_t;
#	define socketError		WSAGetLastError( )

static WSADATA	winsockdata;
static qboolean	winsockInitialized = qfalse;

#else // !_WIN32

#	if MAC_OS_X_VERSION_MIN_REQUIRED == 1020
		// needed for socklen_t on OSX 10.2
#		define _BSD_SOCKLEN_T_
#	endif

#	include <sys/socket.h>
#	include <errno.h>
#	include <netdb.h>
#	include <netinet/in.h>
#	include <arpa/inet.h>
#	include <net/if.h>
#	include <sys/ioctl.h>
#	include <sys/types.h>
#	include <sys/time.h>
#	include <unistd.h>
#	if !defined(__sun) && !defined(__sgi)
#		include <ifaddrs.h>
#	endif

#	ifdef __sun
#		include <sys/filio.h>
#	endif

typedef int SOCKET;
#	define INVALID_SOCKET		-1
#	define SOCKET_ERROR			-1
#	define closesocket			close
#	define ioctlsocket			ioctl
typedef int	ioctlarg_t;
#	define socketError			errno

#endif

typedef union {
	struct sockaddr_in v4;
	struct sockaddr_in6 v6;
	struct sockaddr_storage ss;
} sockaddr_t;

#pragma pack(push,1)
typedef struct socks5_request_s {
	uint8_t version;
	uint8_t command;
	uint8_t reserved;
	uint8_t addrtype;
	union {
		struct {
			struct in_addr addr;
			uint16_t port;
		} v4;
#ifdef USE_IPV6
		struct {
			struct in6_addr addr;
			uint16_t port;
		} v6;
#endif
		byte buffer[64];
	} u;
} socks5_request_t;

typedef union socks5_udp_request_s {
	struct {
		uint8_t reserved[2];
		uint8_t fragnum;
		uint8_t addrtype;
		union {
			struct {
				struct in_addr addr;
				uint16_t port;
				char data[2000];
			} v4;
#ifdef USE_IPV6
			struct {
				struct in6_addr addr;
				uint16_t port;
				char data[2000];
			} v6;
#endif
		} u;
	} s;
	char buf[1];
} socks5_udp_request_t;
#pragma pack(pop)


static qboolean usingSocks = qfalse;
static int networkingEnabled = 0;

static cvar_t	*net_enabled;

static cvar_t	*net_socksEnabled;
static cvar_t	*net_socksServer;
static cvar_t	*net_socksPort;
static cvar_t	*net_socksUsername;
static cvar_t	*net_socksPassword;

static cvar_t	*net_ip;
static cvar_t	*net_port;
#ifdef USE_IPV6
static cvar_t	*net_ip6;
static cvar_t	*net_port6;
static cvar_t	*net_mcast6addr;
static cvar_t	*net_mcast6iface;
#endif
static cvar_t	*net_dropsim;

static sockaddr_t socksRelayAddr;

static SOCKET	ip_socket = INVALID_SOCKET;
static SOCKET	socks_socket = INVALID_SOCKET;

#ifdef USE_IPV6
static SOCKET	ip6_socket = INVALID_SOCKET;
static SOCKET	multicast6_socket = INVALID_SOCKET;

// Keep track of currently joined multicast group.
static struct ipv6_mreq curgroup;
// And the currently bound address.
static struct sockaddr_in6 boundto;
#endif

#ifndef IF_NAMESIZE
  #define IF_NAMESIZE 16
#endif

// use an admin local address per default so that network admins can decide on how to handle quake3 traffic.
#define NET_MULTICAST_IP6 "ff04::696f:7175:616b:6533"

#define	MAX_IPS		32

typedef struct
{
	char ifname[IF_NAMESIZE];
	
	netadrtype_t type;
	sa_family_t family;
	sockaddr_t addr;
	sockaddr_t netmask;
} nip_localaddr_t;

static nip_localaddr_t localIP[MAX_IPS];
static int numIP;

static void	NET_Restart_f( void );

//=============================================================================


/*
====================
NET_ErrorString
====================
*/
static char *NET_ErrorString( void ) {
#ifdef _WIN32
	//FIXME: replace with FormatMessage?
	switch( socketError ) {
		case WSAEINTR: return "WSAEINTR";
		case WSAEBADF: return "WSAEBADF";
		case WSAEACCES: return "WSAEACCES";
		case WSAEDISCON: return "WSAEDISCON";
		case WSAEFAULT: return "WSAEFAULT";
		case WSAEINVAL: return "WSAEINVAL";
		case WSAEMFILE: return "WSAEMFILE";
		case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
		case WSAEINPROGRESS: return "WSAEINPROGRESS";
		case WSAEALREADY: return "WSAEALREADY";
		case WSAENOTSOCK: return "WSAENOTSOCK";
		case WSAEDESTADDRREQ: return "WSAEDESTADDRREQ";
		case WSAEMSGSIZE: return "WSAEMSGSIZE";
		case WSAEPROTOTYPE: return "WSAEPROTOTYPE";
		case WSAENOPROTOOPT: return "WSAENOPROTOOPT";
		case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
		case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
		case WSAEOPNOTSUPP: return "WSAEOPNOTSUPP";
		case WSAEPFNOSUPPORT: return "WSAEPFNOSUPPORT";
		case WSAEAFNOSUPPORT: return "WSAEAFNOSUPPORT";
		case WSAEADDRINUSE: return "WSAEADDRINUSE";
		case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
		case WSAENETDOWN: return "WSAENETDOWN";
		case WSAENETUNREACH: return "WSAENETUNREACH";
		case WSAENETRESET: return "WSAENETRESET";
		case WSAECONNABORTED: return "WSWSAECONNABORTEDAEINTR";
		case WSAECONNRESET: return "WSAECONNRESET";
		case WSAENOBUFS: return "WSAENOBUFS";
		case WSAEISCONN: return "WSAEISCONN";
		case WSAENOTCONN: return "WSAENOTCONN";
		case WSAESHUTDOWN: return "WSAESHUTDOWN";
		case WSAETOOMANYREFS: return "WSAETOOMANYREFS";
		case WSAETIMEDOUT: return "WSAETIMEDOUT";
		case WSAECONNREFUSED: return "WSAECONNREFUSED";
		case WSAELOOP: return "WSAELOOP";
		case WSAENAMETOOLONG: return "WSAENAMETOOLONG";
		case WSAEHOSTDOWN: return "WSAEHOSTDOWN";
		case WSASYSNOTREADY: return "WSASYSNOTREADY";
		case WSAVERNOTSUPPORTED: return "WSAVERNOTSUPPORTED";
		case WSANOTINITIALISED: return "WSANOTINITIALISED";
		case WSAHOST_NOT_FOUND: return "WSAHOST_NOT_FOUND";
		case WSATRY_AGAIN: return "WSATRY_AGAIN";
		case WSANO_RECOVERY: return "WSANO_RECOVERY";
		case WSANO_DATA: return "WSANO_DATA";
		default: return "NO ERROR";
	}
#else
	return strerror(socketError);
#endif
}


static void NetadrToSockadr( const netadr_t *a, sockaddr_t *s ) {
	switch ( a->type ) {
		case NA_BROADCAST:
			s->v4.sin_family = AF_INET;
			s->v4.sin_port = a->port;
			s->v4.sin_addr.s_addr = INADDR_BROADCAST;
			break;
		case NA_IP:
			s->v4.sin_family = AF_INET;
			memcpy( &s->v4.sin_addr.s_addr, a->ipv._4, sizeof( s->v4.sin_addr.s_addr ) );
			s->v4.sin_port = a->port;
			break;
#ifdef USE_IPV6
		case NA_IP6:
			s->v6.sin6_family = AF_INET6;
			memcpy( &s->v6.sin6_addr, a->ipv._6, sizeof( s->v6.sin6_addr ) );
			s->v6.sin6_port = a->port;
			s->v6.sin6_scope_id = a->scope_id;
			break;
		case NA_MULTICAST6:
			s->v6.sin6_family = AF_INET6;
			s->v6.sin6_addr = curgroup.ipv6mr_multiaddr;
			s->v6.sin6_port = a->port;
			break;
#endif
		default:
			s->v4.sin_family = AF_UNSPEC;
			s->v4.sin_port = 0;
			s->v4.sin_addr.s_addr = INADDR_ANY;
			break;
	}
}


static void SockadrToNetadr( const sockaddr_t *s, netadr_t *a ) {
	if ( s->ss.ss_family == AF_INET ) {
		a->type = NA_IP;
		memcpy( a->ipv._4, &s->v4.sin_addr.s_addr, sizeof( a->ipv._4 ) );
		a->port = s->v4.sin_port;
	}
#ifdef USE_IPV6
	else if ( s->ss.ss_family == AF_INET6 )
	{
		a->type = NA_IP6;
		memcpy( a->ipv._6, &s->v6.sin6_addr, sizeof( a->ipv._6 ) );
		a->port = s->v6.sin6_port;
		a->scope_id = s->v6.sin6_scope_id;
	}
#endif
}


static const struct addrinfo *SearchAddrInfo( const struct addrinfo *hints, sa_family_t family )
{
	while ( hints )
	{
		if ( hints->ai_family == family )
			return hints;

		hints = hints->ai_next;
	}
	
	return NULL;
}


/*
=============
gai_error_str

wrapper over gai_strerror() to describe common error code(s)
because in-game console can't properly render non-ascii characters
on systems with locales other than US/UK
=============
*/
static const char *gai_error_str( int ecode )
{
	switch ( ecode )
	{
		case EAI_NONAME:
			return "Unknown host.";
		default:
			return gai_strerror( ecode );
	}
}


/*
=============
Sys_StringToSockaddr
=============
*/
static qboolean Sys_StringToSockaddr( const char *s, sockaddr_t *sadr, int sadr_len, sa_family_t family, int type )
{
	struct addrinfo hint;
	struct addrinfo *res = NULL;
	int retval;

	memset( sadr, 0x0, sadr_len );
	memset( &hint, 0x0, sizeof( hint ) );

	hint.ai_family = family;
	hint.ai_socktype = type;

	retval = getaddrinfo( s, NULL, &hint, &res );

	if ( retval == 0 )
	{
		const struct addrinfo *search = NULL;

		if ( family == AF_UNSPEC )
		{
			// Decide here and now which protocol family to use
#ifdef USE_IPV6
			if ( net_enabled->integer & NET_PRIOV6 )
			{
				if ( net_enabled->integer & NET_ENABLEV6 )
					search = SearchAddrInfo( res, AF_INET6 );
				
				if ( !search && ( net_enabled->integer & NET_ENABLEV4 ) )
					search = SearchAddrInfo( res, AF_INET );
			}
			else
#endif
			{
				if ( net_enabled->integer & NET_ENABLEV4 )
					search = SearchAddrInfo( res, AF_INET );
#ifdef USE_IPV6
				if ( !search && ( net_enabled->integer & NET_ENABLEV6 ) )
					search = SearchAddrInfo( res, AF_INET6 );
#endif
			}
		}
		else
			search = SearchAddrInfo( res, family );

		if ( search )
		{
			size_t addrlen = MIN( search->ai_addrlen, sadr_len );

			memcpy ( sadr, search->ai_addr, addrlen );
			freeaddrinfo( res );

			return qtrue;
		}
		else
			Com_Printf( "%s: Error resolving %s: No address of required type found.\n", __func__, s );
	}
	else
		Com_Printf( "%s: Error resolving %s: %s\n", __func__, s, gai_error_str( retval ) );

	if ( res )
		freeaddrinfo( res );

	return qfalse;
}


/*
=============
Sys_SockaddrToString
=============
*/
static void Sys_SockaddrToString( char *dest, int destlen, const sockaddr_t *input )
{
	socklen_t inputlen;

#ifdef USE_IPV6
	if ( input->ss.ss_family == AF_INET6 )
		inputlen = sizeof(struct sockaddr_in6);
	else
#endif
		inputlen = sizeof(struct sockaddr_in);

	if ( getnameinfo( (const struct sockaddr *)input, inputlen, dest, destlen, NULL, 0, NI_NUMERICHOST ) && destlen > 0 )
		*dest = '\0';
}


/*
=============
Sys_StringToAdr
=============
*/
qboolean Sys_StringToAdr( const char *s, netadr_t *a, netadrtype_t family ) {
	sockaddr_t sadr;
	sa_family_t fam;
	
	switch(family)
	{
		case NA_IP:
			fam = AF_INET;
		break;
#ifdef USE_IPV6
		case NA_IP6:
			fam = AF_INET6;
		break;
#endif
		default:
			fam = AF_UNSPEC;
		break;
	}

	if ( !Sys_StringToSockaddr( s, &sadr, sizeof( sadr ), fam, SOCK_DGRAM ) ) {
		return qfalse;
	}

	SockadrToNetadr( &sadr, a );
	return qtrue;
}


/*
===================
NET_CompareBaseAdrMask

Compare without port, and up to the bit number given in netmask.
===================
*/
qboolean NET_CompareBaseAdrMask( const netadr_t *a, const netadr_t *b, unsigned int netmask )
{
	byte cmpmask, *addra, *addrb;
	int curbyte;

	if (a->type != b->type)
		return qfalse;

	if (a->type == NA_LOOPBACK)
		return qtrue;

	if (a->type == NA_IP)
	{
		addra = (byte *) &a->ipv._4;
		addrb = (byte *) &b->ipv._4;
		
		if (netmask > 32)
			netmask = 32;
	}
#ifdef USE_IPV6
	else if (a->type == NA_IP6)
	{
		addra = (byte *) &a->ipv._6;
		addrb = (byte *) &b->ipv._6;
		
		if (netmask > 128)
			netmask = 128;
	}
#endif
	else
	{
		Com_Printf ("%s: bad address type\n", __func__);
		return qfalse;
	}

	curbyte = netmask >> 3;

	if(curbyte && memcmp(addra, addrb, curbyte))
		return qfalse;

	netmask &= 0x07;
	if(netmask)
	{
		cmpmask = (1 << netmask) - 1;
		cmpmask <<= 8 - netmask;

		if((addra[curbyte] & cmpmask) == (addrb[curbyte] & cmpmask))
			return qtrue;
	}
	else
		return qtrue;
	
	return qfalse;
}


/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr( const netadr_t *a, const netadr_t *b )
{
	return NET_CompareBaseAdrMask( a, b, ~0U );
}


const char *NET_AdrToString( const netadr_t *a )
{
	static char s[NET_ADDRSTRMAXLEN];

	if (a->type == NA_LOOPBACK)
		strcpy( s, "loopback" );
	else if (a->type == NA_BOT)
		strcpy( s, "bot" );
#ifdef USE_IPV6
	else if (a->type == NA_IP || a->type == NA_IP6)
#else
	else if (a->type == NA_IP)
#endif
	{
		sockaddr_t sadr;
		NetadrToSockadr( a, &sadr );
		Sys_SockaddrToString( s, sizeof(s), &sadr );
	}

	return s;
}


const char *NET_AdrToStringwPort( const netadr_t *a )
{
	static char s[NET_ADDRSTRMAXLEN];

	if (a->type == NA_LOOPBACK)
		strcpy( s, "loopback" );
	else if (a->type == NA_BOT)
		strcpy( s, "bot" );
	else if(a->type == NA_IP)
		Com_sprintf(s, sizeof(s), "%s:%hu", NET_AdrToString(a), ntohs(a->port));
#ifdef USE_IPV6
	else if(a->type == NA_IP6)
		Com_sprintf(s, sizeof(s), "[%s]:%hu", NET_AdrToString(a), ntohs(a->port));
#endif

	return s;
}


qboolean NET_CompareAdr( const netadr_t *a, const netadr_t *b )
{
	if ( !NET_CompareBaseAdr( a, b ) )
		return qfalse;

#ifdef USE_IPV6
	if (a->type == NA_IP || a->type == NA_IP6)
#else
	if (a->type == NA_IP)
#endif
	{
		if (a->port == b->port)
			return qtrue;
	}
	else
		return qtrue;
		
	return qfalse;
}


qboolean NET_IsLocalAddress( const netadr_t *adr ) 
{
	return adr->type == NA_LOOPBACK;
}

//=============================================================================

/*
==================
NET_GetPacket

Receive one packet
==================
*/
static qboolean NET_GetPacket( netadr_t *net_from, msg_t *net_message, const fd_set *fdr )
{
	int 	ret;
	sockaddr_t	from;
	socklen_t	fromlen;
	int		err;

	if(ip_socket != INVALID_SOCKET && FD_ISSET(ip_socket, fdr))
	{
		fromlen = sizeof(from);
		ret = recvfrom( ip_socket, (void *)net_message->data, net_message->maxsize, 0, (struct sockaddr *) &from, &fromlen );

		if (ret == SOCKET_ERROR)
		{
			err = socketError;

			if( err != EAGAIN && err != ECONNRESET )
				Com_Printf( "NET_GetPacket: %s\n", NET_ErrorString() );
		}
		else
		{
			memset( &from.v4.sin_zero, 0, sizeof( from.v4.sin_zero ) );

			if ( usingSocks && memcmp( &from, &socksRelayAddr, fromlen ) == 0 ) {
				if ( ret < 10 || net_message->data[0] != 0 || net_message->data[1] != 0 || net_message->data[2] != 0 || net_message->data[3] != 1 ) {
					return qfalse;
				}
				net_from->type = NA_IP;
				net_from->ipv._4[0] = net_message->data[4];
				net_from->ipv._4[1] = net_message->data[5];
				net_from->ipv._4[2] = net_message->data[6];
				net_from->ipv._4[3] = net_message->data[7];
				net_from->port = *(uint16_t *)&net_message->data[8];
				net_message->readcount = 10;
			}
			else {
				net_from->type = NA_BAD;
				SockadrToNetadr( &from, net_from );
				net_message->readcount = 0;
			}

			if( ret >= net_message->maxsize ) {
				Com_Printf( "Oversize packet from %s\n", NET_AdrToString( net_from ) );
				return qfalse;
			}

			net_message->cursize = ret;
			return qtrue;
		}
	}

#ifdef USE_IPV6
	if(ip6_socket != INVALID_SOCKET && FD_ISSET(ip6_socket, fdr))
	{
		fromlen = sizeof(from);
		ret = recvfrom(ip6_socket, (void *)net_message->data, net_message->maxsize, 0, (struct sockaddr *) &from, &fromlen);

		if (ret == SOCKET_ERROR)
		{
			err = socketError;

			if( err != EAGAIN && err != ECONNRESET )
				Com_Printf( "NET_GetPacket: %s\n", NET_ErrorString() );
		}
		else
		{
			net_from->type = NA_BAD;
			SockadrToNetadr( &from, net_from );
			net_message->readcount = 0;

			if(ret >= net_message->maxsize)
			{
				Com_Printf( "Oversize packet from %s\n", NET_AdrToString( net_from ) );
				return qfalse;
			}
			
			net_message->cursize = ret;
			return qtrue;
		}
	}

	if(multicast6_socket != INVALID_SOCKET && multicast6_socket != ip6_socket && FD_ISSET(multicast6_socket, fdr))
	{
		fromlen = sizeof(from);
		ret = recvfrom(multicast6_socket, (void *)net_message->data, net_message->maxsize, 0, (struct sockaddr *) &from, &fromlen);

		if (ret == SOCKET_ERROR)
		{
			err = socketError;

			if( err != EAGAIN && err != ECONNRESET )
				Com_Printf( "NET_GetPacket: %s\n", NET_ErrorString() );
		}
		else
		{
			net_from->type = NA_BAD;
			SockadrToNetadr( &from, net_from );
			net_message->readcount = 0;

			if(ret >= net_message->maxsize)
			{
				Com_Printf( "Oversize packet from %s\n", NET_AdrToString( net_from ) );
				return qfalse;
			}

			net_message->cursize = ret;
			return qtrue;
		}
	}
#endif // USE_IPV6

	return qfalse;
}

//=============================================================================


/*
==================
Sys_SendPacket
==================
*/
void Sys_SendPacket( int length, const void *data, const netadr_t *to ) {
	int ret = SOCKET_ERROR;
	sockaddr_t addr;

	switch ( to->type ) {
		case NA_BROADCAST:
		case NA_IP:
#ifdef USE_IPV6
		case NA_IP6:
		case NA_MULTICAST6:
#endif
			break;
		default:
			Com_Error( ERR_FATAL, "Sys_SendPacket: bad address type %i", to->type );
			return;
	}

#ifdef USE_IPV6
	if( (ip_socket == INVALID_SOCKET && to->type == NA_IP) ||
		(ip_socket == INVALID_SOCKET && to->type == NA_BROADCAST) ||
		(ip6_socket == INVALID_SOCKET && to->type == NA_IP6) ||
		(ip6_socket == INVALID_SOCKET && to->type == NA_MULTICAST6) )
		return;

	if (to->type == NA_MULTICAST6 && (net_enabled->integer & NET_DISABLEMCAST))
		return;
#else
	if ( ip_socket == INVALID_SOCKET && ( to->type == NA_IP || to->type == NA_BROADCAST ) )
		return;
#endif

	NetadrToSockadr( to, &addr );

	if ( usingSocks && to->type == NA_IP ) {
		socks5_udp_request_t cmd;

		if ( length <= sizeof( cmd.s.u.v4.data ) ) {
			cmd.s.reserved[0] = 0;
			cmd.s.reserved[1] = 0;
			cmd.s.fragnum = 0;  // not fragmented
			cmd.s.addrtype = 1; // address type: IPV4
			cmd.s.u.v4.addr.s_addr = addr.v4.sin_addr.s_addr;
			cmd.s.u.v4.port = addr.v4.sin_port;
			memcpy( cmd.s.u.v4.data, data, length );
			ret = sendto( ip_socket, cmd.buf, length + 10, 0, ( struct sockaddr * ) &socksRelayAddr.v4, sizeof( socksRelayAddr.v4 ) );
		}
	}
	else {
		if ( addr.ss.ss_family == AF_INET )
			ret = sendto( ip_socket, data, length, 0, (struct sockaddr *) &addr, sizeof(struct sockaddr_in) );
#ifdef USE_IPV6
		else if ( addr.ss.ss_family == AF_INET6 )
			ret = sendto( ip6_socket, data, length, 0, (struct sockaddr *) &addr, sizeof(struct sockaddr_in6) );
#endif
	}

	if( ret == SOCKET_ERROR ) {
		int err = socketError;

		// wouldblock is silent
		if( err == EAGAIN ) {
			return;
		}

		// some PPP links do not allow broadcasts and return an error
		if( ( err == EADDRNOTAVAIL ) && ( to->type == NA_BROADCAST ) ) {
			return;
		}

		Com_Printf( "Sys_SendPacket: %s\n", NET_ErrorString() );
	}
}


//=============================================================================

/*
==================
Sys_IsLANAddress

LAN clients will have their rate var ignored
==================
*/
qboolean Sys_IsLANAddress( const netadr_t *adr ) {
	int		index, run, addrsize;
	qboolean differed;
	const byte *compareadr, *comparemask, *compareip;

	if( adr->type == NA_LOOPBACK ) {
		return qtrue;
	}

	if( adr->type == NA_IP )
	{
		// RFC1918:
		// 10.0.0.0        -   10.255.255.255  (10/8 prefix)
		// 172.16.0.0      -   172.31.255.255  (172.16/12 prefix)
		// 192.168.0.0     -   192.168.255.255 (192.168/16 prefix)
		if(adr->ipv._4[0] == 10)
			return qtrue;
		if(adr->ipv._4[0] == 172 && (adr->ipv._4[1]&0xf0) == 16)
			return qtrue;
		if(adr->ipv._4[0] == 192 && adr->ipv._4[1] == 168)
			return qtrue;

		if(adr->ipv._4[0] == 127)
			return qtrue;
	}
#ifdef USE_IPV6
	else if(adr->type == NA_IP6)
	{
		if(adr->ipv._6[0] == 0xfe && (adr->ipv._6[1] & 0xc0) == 0x80)
			return qtrue;
		if((adr->ipv._6[0] & 0xfe) == 0xfc)
			return qtrue;
	}
#endif

	// Now compare against the networks this computer is member of.
	for ( index = 0; index < numIP; index++ )
	{
		if ( localIP[index].type == adr->type )
		{
			if ( adr->type == NA_IP )
			{
				compareip = (byte *) &((struct sockaddr_in *) &localIP[index].addr)->sin_addr.s_addr;
				comparemask = (byte *) &((struct sockaddr_in *) &localIP[index].netmask)->sin_addr.s_addr;
				compareadr = adr->ipv._4;
				
				addrsize = sizeof(adr->ipv._4);
			}
#ifdef USE_IPV6
			else if ( adr->type == NA_IP6 || adr->type == NA_MULTICAST6 )
			{
				// TODO? should we check the scope_id here?

				compareip = (byte *) &((struct sockaddr_in6 *) &localIP[index].addr)->sin6_addr;
				comparemask = (byte *) &((struct sockaddr_in6 *) &localIP[index].netmask)->sin6_addr;
				compareadr = adr->ipv._6;
				
				addrsize = sizeof(adr->ipv._6);
			}
#endif
			else
				continue;

			differed = qfalse;
			for ( run = 0; run < addrsize; run++ )
			{
				if ((compareip[run] & comparemask[run]) != (compareadr[run] & comparemask[run]))
				{
					differed = qtrue;
					break;
				}
			}
			
			if ( !differed )
				return qtrue;
		}
	}
	
	return qfalse;
}


/*
==================
Sys_ShowIP
==================
*/
void Sys_ShowIP( void ) {
	int i;
	char addrbuf[NET_ADDRSTRMAXLEN];

	for(i = 0; i < numIP; i++)
	{
		Sys_SockaddrToString( addrbuf, sizeof(addrbuf), &localIP[i].addr );

		if(localIP[i].type == NA_IP)
			Com_Printf( "IP: %s\n", addrbuf);
#ifdef USE_IPV6
		else if(localIP[i].type == NA_IP6)
			Com_Printf( "IP6: %s\n", addrbuf);
#endif
	}
}


//=============================================================================


/*
====================
NET_IPSocket
====================
*/
static SOCKET NET_IPSocket( const char *net_interface, int port, int *err ) {
	SOCKET				newsocket;
	struct sockaddr_in	address;
	ioctlarg_t			_true = 1;
	int					i = 1;

	*err = 0;

	if( net_interface ) {
		Com_Printf( "Opening IP socket: %s:%i\n", net_interface, port );
	}
	else {
		Com_Printf( "Opening IP socket: 0.0.0.0:%i\n", port );
	}

	if( ( newsocket = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET ) {
		*err = socketError;
		Com_Printf( "WARNING: NET_IPSocket: socket: %s\n", NET_ErrorString() );
		return newsocket;
	}
	// make it non-blocking
	if( ioctlsocket( newsocket, FIONBIO, &_true ) == SOCKET_ERROR ) {
		Com_Printf( "WARNING: NET_IPSocket: ioctl FIONBIO: %s\n", NET_ErrorString() );
		*err = socketError;
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

	// make it broadcast capable
	if( setsockopt( newsocket, SOL_SOCKET, SO_BROADCAST, (char *) &i, sizeof(i) ) == SOCKET_ERROR ) {
		Com_Printf( "WARNING: NET_IPSocket: setsockopt SO_BROADCAST: %s\n", NET_ErrorString() );
	}

	if( !net_interface || !net_interface[0]) {
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		if ( !Sys_StringToSockaddr( net_interface, (sockaddr_t *)&address, sizeof( address ), AF_INET, SOCK_DGRAM ) )
		{
			closesocket( newsocket );
			return INVALID_SOCKET;
		}
	}

	if( port == PORT_ANY ) {
		address.sin_port = 0;
	}
	else {
		address.sin_port = htons( (short)port );
	}

	if( bind( newsocket, (void *)&address, sizeof(address) ) == SOCKET_ERROR ) {
		Com_Printf( "WARNING: NET_IPSocket: bind: %s\n", NET_ErrorString() );
		*err = socketError;
		closesocket( newsocket );
		return INVALID_SOCKET;
	}

	return newsocket;
}


/*
====================
NET_IP6Socket
====================
*/
#ifdef USE_IPV6
static SOCKET NET_IP6Socket( const char *net_interface, int port, struct sockaddr_in6 *bindto, int *err ) {
	SOCKET				newsocket;
	struct sockaddr_in6	address;
	ioctlarg_t			_true = 1;

	*err = 0;

	if( net_interface )
	{
		// Print the name in brackets if there is a colon:
		if(Q_CountChar(net_interface, ':'))
			Com_Printf( "Opening IP6 socket: [%s]:%i\n", net_interface, port );
		else
			Com_Printf( "Opening IP6 socket: %s:%i\n", net_interface, port );
	}
	else
		Com_Printf( "Opening IP6 socket: [::]:%i\n", port );

	if( ( newsocket = socket( PF_INET6, SOCK_DGRAM, IPPROTO_UDP ) ) == INVALID_SOCKET ) {
		*err = socketError;
		Com_Printf( "WARNING: NET_IP6Socket: socket: %s\n", NET_ErrorString() );
		return newsocket;
	}

	// make it non-blocking
	if( ioctlsocket( newsocket, FIONBIO, &_true ) == SOCKET_ERROR ) {
		Com_Printf( "WARNING: NET_IP6Socket: ioctl FIONBIO: %s\n", NET_ErrorString() );
		*err = socketError;
		closesocket(newsocket);
		return INVALID_SOCKET;
	}

#ifdef IPV6_V6ONLY
	{
		int i = 1;

		// ipv4 addresses should not be allowed to connect via this socket.
		if(setsockopt(newsocket, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &i, sizeof(i)) == SOCKET_ERROR)
		{
			// win32 systems don't seem to support this anyways.
			Com_DPrintf("WARNING: NET_IP6Socket: setsockopt IPV6_V6ONLY: %s\n", NET_ErrorString());
		}
	}
#endif

	if( !net_interface || !net_interface[0]) {
		address.sin6_family = AF_INET6;
		address.sin6_addr = in6addr_any;
	}
	else
	{
		if ( !Sys_StringToSockaddr( net_interface, (sockaddr_t *)&address, sizeof(address), AF_INET6, SOCK_DGRAM ) )
		{
			closesocket(newsocket);
			return INVALID_SOCKET;
		}
	}

	if( port == PORT_ANY ) {
		address.sin6_port = 0;
	}
	else {
		address.sin6_port = htons( (short)port );
	}

	if( bind( newsocket, (void *)&address, sizeof(address) ) == SOCKET_ERROR ) {
		Com_Printf( "WARNING: NET_IP6Socket: bind: %s\n", NET_ErrorString() );
		*err = socketError;
		closesocket( newsocket );
		return INVALID_SOCKET;
	}
	
	if(bindto)
		*bindto = address;

	return newsocket;
}


/*
====================
NET_SetMulticast6
Set the current multicast group
====================
*/
static void NET_SetMulticast6( void )
{
	struct sockaddr_in6 addr;

	if ( !*net_mcast6addr->string || !Sys_StringToSockaddr( net_mcast6addr->string, (sockaddr_t *) &addr, sizeof( addr ), AF_INET6, SOCK_DGRAM ) )
	{
		Com_Printf("WARNING: NET_JoinMulticast6: Incorrect multicast address given, "
			   "please set cvar %s to a sane value.\n", net_mcast6addr->name);
		
		Cvar_SetIntegerValue( net_enabled->name, net_enabled->integer | NET_DISABLEMCAST );
		
		return;
	}
	
	memcpy(&curgroup.ipv6mr_multiaddr, &addr.sin6_addr, sizeof(curgroup.ipv6mr_multiaddr));

	if(*net_mcast6iface->string)
	{
#ifdef _WIN32
		curgroup.ipv6mr_interface = net_mcast6iface->integer;
#else
		curgroup.ipv6mr_interface = if_nametoindex(net_mcast6iface->string);
#endif
	}
	else
		curgroup.ipv6mr_interface = 0;
}


/*
====================
NET_JoinMulticast
Join an ipv6 multicast group
====================
*/
void NET_JoinMulticast6( void )
{
	int err;
	
	if(ip6_socket == INVALID_SOCKET || multicast6_socket != INVALID_SOCKET || (net_enabled->integer & NET_DISABLEMCAST))
		return;
	
	if(IN6_IS_ADDR_MULTICAST(&boundto.sin6_addr) || IN6_IS_ADDR_UNSPECIFIED(&boundto.sin6_addr))
	{
		// The way the socket was bound does not prohibit receiving multi-cast packets. So we don't need to open a new one.
		multicast6_socket = ip6_socket;
	}
	else
	{
		if((multicast6_socket = NET_IP6Socket(net_mcast6addr->string, ntohs(boundto.sin6_port), NULL, &err)) == INVALID_SOCKET)
		{
			// If the OS does not support binding to multicast addresses, like WinXP, at least try with the normal file descriptor.
			multicast6_socket = ip6_socket;
		}
	}
	
	if(curgroup.ipv6mr_interface)
	{
		if (setsockopt(multicast6_socket, IPPROTO_IPV6, IPV6_MULTICAST_IF,
					(char *) &curgroup.ipv6mr_interface, sizeof(curgroup.ipv6mr_interface)) < 0)
		{
			Com_Printf("NET_JoinMulticast6: Couldn't set scope on multicast socket: %s\n", NET_ErrorString());

			if(multicast6_socket != ip6_socket)
			{
				closesocket(multicast6_socket);
				multicast6_socket = INVALID_SOCKET;
				return;
			}
		}
	}

	if (setsockopt(multicast6_socket, IPPROTO_IPV6, IPV6_JOIN_GROUP, (char *) &curgroup, sizeof(curgroup)))
	{
		Com_Printf("NET_JoinMulticast6: Couldn't join multicast group: %s\n", NET_ErrorString());

		if(multicast6_socket != ip6_socket)
		{
			closesocket(multicast6_socket);
			multicast6_socket = INVALID_SOCKET;
			return;
		}
	}
}


void NET_LeaveMulticast6( void )
{
	if(multicast6_socket != INVALID_SOCKET)
	{
		if(multicast6_socket != ip6_socket)
			closesocket(multicast6_socket);
		else
			setsockopt(multicast6_socket, IPPROTO_IPV6, IPV6_LEAVE_GROUP, (char *) &curgroup, sizeof(curgroup));

		multicast6_socket = INVALID_SOCKET;
	}
}
#endif // USE_IPV6


/*
====================
NET_OpenSocks
====================
*/
static void NET_OpenSocks( int port ) {
	struct sockaddr_in	address;
	int					len;
	unsigned char		buf[4 + 255 * 2];
	socks5_request_t	cmd;

	usingSocks = qfalse;

	Com_Printf( "Opening connection to SOCKS server.\n" );

	if ( ( socks_socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP ) ) == INVALID_SOCKET ) {
		Com_Printf( "WARNING: NET_OpenSocks: socket: %s\n", NET_ErrorString() );
		return;
	}

	if ( !Sys_StringToSockaddr( net_socksServer->string, (sockaddr_t*)&address, sizeof( address ), AF_INET, SOCK_STREAM ) ) {
		Com_Printf( "WARNING: %s failed\n", __func__ );
		return;
	}

	address.sin_port = htons( net_socksPort->integer );

	if ( connect( socks_socket, ( struct sockaddr * )&address, sizeof( struct sockaddr_in ) ) == SOCKET_ERROR ) {
		Com_Printf( "%s: connect: %s\n", __func__, NET_ErrorString() );
		return;
	}

	buf[0] = 5;	// SOCKS version

	if ( *net_socksUsername->string || *net_socksPassword->string ) {
		// rfc1929 - send socks authentication handshake
		buf[1] = 2; // method count
		buf[2] = 0; // method id #00: no authentication
		buf[3] = 2; // method id #02: username/password
		len = 4;
	} else {
		buf[1] = 1; // method count
		buf[2] = 0; // method id #00: no authentication
		len = 3;
	}

	if ( send( socks_socket, (void *)buf, len, 0 ) == SOCKET_ERROR ) {
		Com_Printf( "%s: send: %s\n", __func__, NET_ErrorString() );
		return;
	}

	// get the response
	len = recv( socks_socket, (void *)buf, 32, 0 );
	if ( len == SOCKET_ERROR ) {
		Com_Printf( "%s: recv: %s\n", __func__, NET_ErrorString() );
		return;
	}
	if ( len != 2 || buf[0] != 5 ) {
		Com_Printf( "%s: bad auth.method response\n", __func__ );
		return;
	}

	switch ( buf[1] ) {
		case 0: // no authentication
		case 2: // username/password authentication
			break;
		default:
			Com_Printf( "%s: unsupported auth.method\n", __func__ );
			return;
	}

	// do username/password authentication if needed
	if ( buf[1] == 2 ) {
		int		ulen;
		int		plen;

		// build the request
		ulen = strlen( net_socksUsername->string );
		plen = strlen( net_socksPassword->string );
		if ( ulen > 255 ) {
			ulen = 255;
		}
		if ( plen > 255 ) {
			plen = 255;
		}
		buf[0] = 1;		// username/password authentication version
		buf[1] = ulen;
		if ( ulen ) {
			memcpy( &buf[2], net_socksUsername->string, ulen );
		}
		buf[2 + ulen] = plen;
		if ( plen ) {
			memcpy( &buf[3 + ulen], net_socksPassword->string, plen );
		}

		// send it
		if ( send( socks_socket, (void *)buf, 3 + ulen + plen, 0 ) == SOCKET_ERROR ) {
			Com_Printf( "%s: send: %s\n", __func__, NET_ErrorString() );
			return;
		}

		// get the response
		len = recv( socks_socket, (void *)buf, 64, 0 );
		if ( len == SOCKET_ERROR ) {
			Com_Printf( "%s: recv: %s\n", __func__, NET_ErrorString() );
			return;
		}
		if ( len != 2 || buf[0] != 1 ) {
			Com_Printf( "%s: bad auth response\n", __func__ );
			return;
		}
	}

	// send the UDP associate request
	cmd.version = 5;  // SOCKS version
	cmd.command = 3;  // UDP associate
	cmd.reserved = 0; // reserved
	cmd.addrtype = 1; // address type: IPV4
	cmd.u.v4.addr.s_addr = INADDR_ANY;
	cmd.u.v4.port = htons( port );
	if ( send( socks_socket, (void *)&cmd, 10, 0 ) == SOCKET_ERROR ) {
		Com_Printf( "%s: send: %s\n", __func__, NET_ErrorString() );
		return;
	}

	// get the response
	len = recv( socks_socket, (void *)&cmd, sizeof( cmd ), 0 );
	if ( len == SOCKET_ERROR ) {
		Com_Printf( "%s: recv: %s\n", __func__, NET_ErrorString() );
		return;
	}
	if ( len < 10 || cmd.version != 5 ) {
		Com_Printf( "%s: bad response\n", __func__ );
		return;
	}

	// check completion code
	if ( cmd.command != 0 ) {
		Com_Printf( "%s: request denied: %i\n", __func__, cmd.command );
		return;
	}
	if ( cmd.addrtype != 1 ) {
		Com_Printf( "%s: relay address is not IPV4: %i\n", __func__, cmd.addrtype );
		return;
	}

	memset( &socksRelayAddr, 0, sizeof( socksRelayAddr ) );

	socksRelayAddr.v4.sin_family = AF_INET;
	socksRelayAddr.v4.sin_addr.s_addr = cmd.u.v4.addr.s_addr;
	socksRelayAddr.v4.sin_port = cmd.u.v4.port;

	usingSocks = qtrue;
}


/*
=====================
NET_AddLocalAddress
=====================
*/
static void NET_AddLocalAddress( const char *ifname, const struct sockaddr *addr, const struct sockaddr *netmask )
{
	int addrlen;
	sa_family_t family;
	
	// only add addresses that have all required info.
	if (!addr || !netmask || !ifname)
		return;
	
	family = addr->sa_family;

	if(numIP < MAX_IPS)
	{
		if(family == AF_INET)
		{
			addrlen = sizeof(struct sockaddr_in);
			localIP[numIP].type = NA_IP;
		}
#ifdef USE_IPV6
		else if(family == AF_INET6)
		{
			addrlen = sizeof(struct sockaddr_in6);
			localIP[numIP].type = NA_IP6;
		}
#endif
		else
			return;
		
		Q_strncpyz(localIP[numIP].ifname, ifname, sizeof(localIP[numIP].ifname));
	
		localIP[numIP].family = family;

		memcpy(&localIP[numIP].addr, addr, addrlen);
		memcpy(&localIP[numIP].netmask, netmask, addrlen);
		
		numIP++;
	}
}


#ifndef _WIN32
static void NET_GetLocalAddress( void )
{
	char	hostname[256];
	struct ifaddrs *ifap, *search;

	if ( gethostname( hostname, sizeof( hostname ) ) )
		return;

	Com_Printf( "Hostname: %s\n", hostname );

	numIP = 0;

	if ( getifaddrs( &ifap ) )
		Com_Printf( "NET_GetLocalAddress: Unable to get list of network interfaces: %s\n", NET_ErrorString() );
	else
	{
		for( search = ifap; search; search = search->ifa_next )
		{
			// Only add interfaces that are up.
			if ( ifap->ifa_flags & IFF_UP )
				NET_AddLocalAddress( search->ifa_name, search->ifa_addr, search->ifa_netmask );
		}
	
		freeifaddrs( ifap );
		
		Sys_ShowIP();
	}
}
#else // _WIN32
static void NET_GetLocalAddress( void ) {
	char	hostname[256];
	struct addrinfo	hint;
	struct addrinfo	*res = NULL;

	numIP = 0;

	if ( gethostname( hostname, sizeof( hostname ) ) == SOCKET_ERROR )
		return;

	Com_Printf( "Hostname: %s\n", hostname );
	
	memset(&hint, 0, sizeof(hint));
	
	hint.ai_family = AF_UNSPEC;
	hint.ai_socktype = SOCK_DGRAM;
	
	if ( !getaddrinfo( hostname, NULL, &hint, &res ) )
	{
		struct addrinfo *search;
		struct sockaddr_in mask4;
#ifdef USE_IPV6
		struct sockaddr_in6 mask6;
#endif
	
		/* On operating systems where it's more difficult to find out the configured interfaces, we'll just assume a
		 * netmask with all bits set. */
	
		memset(&mask4, 0, sizeof(mask4));
		mask4.sin_family = AF_INET;
		memset(&mask4.sin_addr.s_addr, 0xFF, sizeof(mask4.sin_addr.s_addr));

#ifdef USE_IPV6
		memset(&mask6, 0, sizeof(mask6));
		mask6.sin6_family = AF_INET6;
		memset(&mask6.sin6_addr, 0xFF, sizeof(mask6.sin6_addr));
#endif

		// add all IPs from returned list.
		for ( search = res; search; search = search->ai_next )
		{
			if ( search->ai_family == AF_INET )
				NET_AddLocalAddress( "", search->ai_addr, (struct sockaddr *) &mask4 );
#ifdef USE_IPV6
			else if ( search->ai_family == AF_INET6 )
				NET_AddLocalAddress( "", search->ai_addr, (struct sockaddr *) &mask6 );
#endif
		}
	
		Sys_ShowIP();
	}
	
	if ( res )
		freeaddrinfo( res );
}
#endif // _WIN32


/*
====================
NET_OpenIP
====================
*/
static void NET_OpenIP( void ) {
	int		i;
	int		err;
	int		port;
#ifdef USE_IPV6
	int		port6;
#endif

	port = net_port->integer;
#ifdef USE_IPV6
	port6 = net_port6->integer;
#endif

	NET_GetLocalAddress();

	// automatically scan for a valid port, so multiple
	// dedicated servers can be started without requiring
	// a different net_port for each one
#ifdef USE_IPV6
	if ( net_enabled->integer & NET_ENABLEV6 )
	{
		for( i = 0 ; i < 10 ; i++ )
		{
			ip6_socket = NET_IP6Socket(net_ip6->string, port6 + i, &boundto, &err);
			if (ip6_socket != INVALID_SOCKET)
			{
				Cvar_SetIntegerValue( "net_port6", port6 + i );
				break;
			}
			else
			{
				if(err == EAFNOSUPPORT)
					break;
			}
		}
		if(ip6_socket == INVALID_SOCKET)
			Com_Printf( "WARNING: Couldn't bind to a v6 ip address.\n");
	}
#endif

	if(net_enabled->integer & NET_ENABLEV4)
	{
		for( i = 0 ; i < 10 ; i++ ) {
			ip_socket = NET_IPSocket( net_ip->string, port + i, &err );
			if (ip_socket != INVALID_SOCKET) {
				Cvar_SetIntegerValue( "net_port", port + i );

				if (net_socksEnabled->integer)
					NET_OpenSocks( port + i );

				break;
			}
			else
			{
				if(err == EAFNOSUPPORT)
					break;
			}
		}
		
		if(ip_socket == INVALID_SOCKET)
			Com_Printf( "WARNING: Couldn't bind to a v4 ip address.\n");
	}
}


//===================================================================


/*
====================
NET_GetCvars
====================
*/
static qboolean NET_GetCvars( void ) {
	int modified;

#if defined (DEDICATED) || !defined (USE_IPV6)
	// I want server owners to explicitly turn on ipv6 support.
	net_enabled = Cvar_Get( "net_enabled", "1", CVAR_LATCH | CVAR_ARCHIVE_ND | CVAR_NORESTART );
#else
	/* End users have it enabled so they can connect to ipv6-only hosts, but ipv4 will be
	 * used if available due to ping */
	net_enabled = Cvar_Get( "net_enabled", "3", CVAR_LATCH | CVAR_ARCHIVE_ND | CVAR_NORESTART );
#endif

	Cvar_SetDescription( net_enabled, "Networking options, bitmask:\n"
		" 1 - enable IPv4\n"
#ifdef USE_IPV6
		" 2 - enable IPv6\n"
		" 4 - prioritize IPv6 connections over IPv4\n"
		" 8 - disable IPv6 multicast"
#endif
		);

	Cvar_CheckRange( net_enabled, NULL, NULL, CV_INTEGER );
	modified = net_enabled->modified;
	net_enabled->modified = qfalse;

	net_ip = Cvar_Get( "net_ip", "0.0.0.0", CVAR_LATCH );
	modified += net_ip->modified;
	net_ip->modified = qfalse;

	net_port = Cvar_Get( "net_port", va( "%i", PORT_SERVER ), CVAR_LATCH | CVAR_NORESTART );
	Cvar_CheckRange( net_port, "0", "65535", CV_INTEGER );
	modified += net_port->modified;
	net_port->modified = qfalse;
	
#ifdef USE_IPV6
	net_ip6 = Cvar_Get( "net_ip6", "::", CVAR_LATCH );
	modified += net_ip6->modified;
	net_ip6->modified = qfalse;

	net_port6 = Cvar_Get( "net_port6", va( "%i", PORT_SERVER ), CVAR_LATCH | CVAR_NORESTART );
	Cvar_CheckRange( net_port6, "0", "65535", CV_INTEGER );
	modified += net_port6->modified;
	net_port6->modified = qfalse;

	// Some cvars for configuring multicast options which facilitates scanning for servers on local subnets.
	net_mcast6addr = Cvar_Get( "net_mcast6addr", NET_MULTICAST_IP6, CVAR_LATCH | CVAR_ARCHIVE_ND );
	modified += net_mcast6addr->modified;
	net_mcast6addr->modified = qfalse;

#ifdef _WIN32
	net_mcast6iface = Cvar_Get( "net_mcast6iface", "0", CVAR_LATCH | CVAR_ARCHIVE_ND );
#else
	net_mcast6iface = Cvar_Get( "net_mcast6iface", "", CVAR_LATCH | CVAR_ARCHIVE_ND );
#endif
	modified += net_mcast6iface->modified;
	net_mcast6iface->modified = qfalse;
#endif // USE_IPV6

	net_socksEnabled = Cvar_Get( "net_socksEnabled", "0", CVAR_LATCH | CVAR_ARCHIVE_ND );
	Cvar_CheckRange( net_socksEnabled, "0", "1", CV_INTEGER );
	modified += net_socksEnabled->modified;
	net_socksEnabled->modified = qfalse;

	net_socksServer = Cvar_Get( "net_socksServer", "", CVAR_LATCH | CVAR_ARCHIVE_ND );
	modified += net_socksServer->modified;
	net_socksServer->modified = qfalse;

	net_socksPort = Cvar_Get( "net_socksPort", "1080", CVAR_LATCH | CVAR_ARCHIVE_ND );
	Cvar_CheckRange( net_socksPort, "0", "65535", CV_INTEGER );
	modified += net_socksPort->modified;
	net_socksPort->modified = qfalse;

	net_socksUsername = Cvar_Get( "net_socksUsername", "", CVAR_LATCH | CVAR_ARCHIVE_ND );
	modified += net_socksUsername->modified;
	net_socksUsername->modified = qfalse;

	net_socksPassword = Cvar_Get( "net_socksPassword", "", CVAR_LATCH | CVAR_ARCHIVE_ND );
	modified += net_socksPassword->modified;
	net_socksPassword->modified = qfalse;

	net_dropsim = Cvar_Get( "net_dropsim", "", CVAR_TEMP );

	return modified ? qtrue : qfalse;
}


/*
====================
NET_Config
====================
*/
static void NET_Config( qboolean enableNetworking ) {
	qboolean	modified;
	qboolean	stop;
	qboolean	start;

	// get any latched changes to cvars
	modified = NET_GetCvars();

	if( !net_enabled->integer ) {
		enableNetworking = qfalse;
	}

	// if enable state is the same and no cvars were modified, we have nothing to do
	if( enableNetworking == networkingEnabled && !modified ) {
		return;
	}

	if( enableNetworking == networkingEnabled ) {
		if( enableNetworking ) {
			stop = qtrue;
			start = qtrue;
		}
		else {
			stop = qfalse;
			start = qfalse;
		}
	}
	else {
		if( enableNetworking ) {
			stop = qfalse;
			start = qtrue;
		}
		else {
			stop = qtrue;
			start = qfalse;
		}
		networkingEnabled = enableNetworking;
	}

	if( stop ) {
		if ( ip_socket != INVALID_SOCKET ) {
			closesocket( ip_socket );
			ip_socket = INVALID_SOCKET;
		}
#ifdef USE_IPV6
		if(multicast6_socket != INVALID_SOCKET)
		{
			if(multicast6_socket != ip6_socket)
				closesocket(multicast6_socket);
				
			multicast6_socket = INVALID_SOCKET;
		}

		if ( ip6_socket != INVALID_SOCKET ) {
			closesocket( ip6_socket );
			ip6_socket = INVALID_SOCKET;
		}
#endif
		if ( socks_socket != INVALID_SOCKET ) {
			closesocket( socks_socket );
			socks_socket = INVALID_SOCKET;
		}
		
	}

	if( start )
	{
		if ( net_enabled->integer )
		{
			NET_OpenIP();
#ifdef USE_IPV6
			NET_SetMulticast6();
#endif
		}
	}
}


/*
====================
NET_Init
====================
*/
void NET_Init( void ) {
#ifdef _WIN32
	int		r;

	r = WSAStartup( MAKEWORD( 2, 0 ), &winsockdata );
	if( r ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Winsock initialization failed, returned %d\n", r );
		return;
	}

	winsockInitialized = qtrue;
	Com_DPrintf( "Winsock Initialized\n" );
#endif

	NET_Config( qtrue );
	
	Cmd_AddCommand( "net_restart", NET_Restart_f );
}


/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown( void ) {
	if ( !networkingEnabled ) {
		return;
	}

	NET_Config( qfalse );

#ifdef _WIN32
	WSACleanup();
	winsockInitialized = qfalse;
#endif
}


/*
====================
NET_Event

Called from NET_Sleep which uses select() to determine which sockets have seen action.
====================
*/
static void NET_Event( const fd_set *fdr )
{
	byte bufData[ MAX_MSGLEN_BUF ];
	netadr_t from;
	msg_t netmsg;
	
	while( 1 )
	{
		MSG_Init( &netmsg, bufData, MAX_MSGLEN );

		if ( NET_GetPacket( &from, &netmsg, fdr ) )
		{
			if ( net_dropsim->value > 0.0f && net_dropsim->value <= 100.0f )
			{
				// com_dropsim->value percent of incoming packets get dropped.
				if ( rand() < (int) (((double) RAND_MAX) / 100.0 * (double) net_dropsim->value) )
					continue; // drop this packet
			}

#ifdef DEDICATED
			Com_RunAndTimeServerPacket( &from, &netmsg );
#else
			if ( com_sv_running->integer || com_dedicated->integer )
				Com_RunAndTimeServerPacket( &from, &netmsg );
			else
				CL_PacketEvent( &from, &netmsg );
#endif
		}
		else
			break;
	}
}


/*
====================
NET_Sleep

Sleeps msec or until something happens on the network

Returns qfalse on network event or qtrue in all other cases
====================
*/
qboolean NET_Sleep( int timeout )
{
	struct timeval tv;
	fd_set fdr;
	int retval;
	SOCKET highestfd = INVALID_SOCKET;

	if ( timeout < 0 )
		timeout = 0;

	FD_ZERO( &fdr );

	if ( ip_socket != INVALID_SOCKET )
	{
		FD_SET( ip_socket, &fdr );

		highestfd = ip_socket;
	}

#ifdef USE_IPV6
	if ( ip6_socket != INVALID_SOCKET )
	{
		FD_SET( ip6_socket, &fdr );

		if ( highestfd == INVALID_SOCKET || ip6_socket > highestfd )
			highestfd = ip6_socket;
	}
#endif

	if ( highestfd == INVALID_SOCKET )
	{
#ifdef _WIN32
		// windows ain't happy when select is called without valid FDs
		Sleep( timeout / 1000 );
		return qtrue;
#else
		usleep( timeout );
		return qtrue;
#endif
	}

	tv.tv_sec = timeout / 1000000;
	tv.tv_usec = timeout - tv.tv_sec * 1000000;

	retval = select( highestfd + 1, &fdr, NULL, NULL, &tv );

	if ( retval > 0 ) {
		NET_Event( &fdr );
		return qfalse;
	}

	if ( retval == SOCKET_ERROR ) {
#ifndef _WIN32
		if ( socketError != EINTR )
#endif
		Com_Printf( S_COLOR_YELLOW "Warning: select() syscall failed: %s\n", 
			NET_ErrorString() );
	}

	return qtrue;
}


/*
====================
NET_Restart_f
====================
*/
static void NET_Restart_f( void )
{
	NET_Config( qtrue );
}
