#pragma once

// UDP Telemetry
extern bool g_bUDPEnabled;
extern int g_iUDPPort;
extern char g_sUDPServer[80];

bool InitializeUDP();
bool InitializeUDPSocket();
bool CloseUDP();
bool SendUDPMessage(char *message);
