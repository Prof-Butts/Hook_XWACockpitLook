#pragma once

#define TELEMETRY_FORMAT_JSON 1
#define TELEMETRY_FORMAT_SIMPLIFIED 0

// UDP Telemetry
extern bool g_bUDPEnabled;
extern int g_iUDPPort;
extern char g_sUDPServer[80];
extern int g_UDPFormat;

bool InitializeUDP();
bool InitializeUDPSocket();
bool CloseUDP();
bool SendUDPMessage(char *message);
