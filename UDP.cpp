/*

Simple UDP client. From: https://www.binarytides.com/udp-socket-programming-in-winsock/

Format:

type|key:value

So:

player|speed:100
player|callsign:Rogue 1
player|throttle:66
etc.

More examples: https://github.com/daneedk/TelemetryProxy/blob/master/information.md

*/

#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "UDP.h"

#pragma comment(lib,"ws2_32.lib") // Winsock Library

void log_debug(const char *format, ...);

// Global parameters:
bool g_bUDPEnabled = false;
bool g_bContinuousTelemetry = false;
//#define SERVER "127.0.0.1"	// IP address of the UDP server
char g_sUDPServer[80] = { 0 };
int g_iUDPPort = 1138;		// The port on which to listen for incoming data
int g_UDPFormat = TELEMETRY_FORMAT_SIMPLIFIED;

// Local parameters:
WSADATA g_wsa;
struct sockaddr_in g_si_remote;
int g_socket = -1;

bool InitializeUDP()
{
	// Default to 127.0.0.1 if no server was provided
	if (g_sUDPServer[0] == 0) {
		_snprintf_s(g_sUDPServer, 80, "%s", "127.0.0.1");
		log_debug("[UDP] Using default server: %s", g_sUDPServer);
	}

	// Initialise winsock
	log_debug("[UDP] Initializing Winsock...");
	if (WSAStartup(MAKEWORD(2, 2), &g_wsa) != 0)
	{
		log_debug("[UDP] UDP Init Failed. Error: %d", WSAGetLastError());
		return false;
	}
	log_debug("[UDP] Initialized.");

	return true;
}

bool InitializeUDPSocket() 
{
	log_debug("[UDP] Creating UDP socket");
	if ((g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR)
	{
		log_debug("[UDP] socket() failed with error code : %d", WSAGetLastError());
		return false;
	}

	// setup address structure
	memset((char *)&g_si_remote, 0, sizeof(g_si_remote));
	g_si_remote.sin_family = AF_INET;
	g_si_remote.sin_port = htons(g_iUDPPort);
	inet_pton(AF_INET, g_sUDPServer, &(g_si_remote.sin_addr));

	log_debug("[UDP] UDP socket created successfully");
	return true;
}

bool CloseUDP()
{
	closesocket(g_socket);
	WSACleanup();

	return true;
}

bool SendUDPMessage(char *message)
{
	int slen = sizeof(g_si_remote);

	if (sendto(g_socket, message, strlen(message), 0, (struct sockaddr *)&g_si_remote, slen) == SOCKET_ERROR)
	{
		log_debug("[UDP] sendto() failed with error code : %d", WSAGetLastError());
		return false;
	}
	return true;
}