/*
 * Simple TCP client, based on: https://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedcode1d.html
 */
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdint.h>
#include <time.h>
#include "YawVR.h"

extern HWND g_hWnd;
UINT timerID = 0;

namespace YawVR
{
    bool bEnabled      = true;
    bool bRunThread    = false;

    char sServerIP[80] = "";
    // Default YawVR ports
    int  udpPort      = 50010;
    int  tcpPort      = 50020;

    constexpr uint8_t CHECK_IN        = 0x30;
    constexpr uint8_t CHECK_IN_ANS    = 0x31;
    constexpr uint8_t SET_TILT_LIMITS = 0x40;
    constexpr uint8_t SET_YAW_LIMIT   = 0x70;
    constexpr uint8_t START           = 0xA1;
    constexpr uint8_t STOP            = 0xA2;
    constexpr uint8_t EXIT            = 0xA3;
    constexpr uint8_t ERR             = 0xA5;

    static WSADATA     wsaData;
    static SOCKADDR_IN ServerSocket;
    static SOCKET      tcpSocket;
    static SOCKET      udpSocket;

    constexpr int      BUF_SIZE = 128;
    static    char     buffer[BUF_SIZE];
    static    uint8_t* ubuffer = (uint8_t*)buffer;

    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float yawScale   =    7.0f;
    float pitchScale = -200.0f;
    float rollScale  =    3.0f;

    int yawLimit           = -1;
    int pitchForwardLimit  = -1;
    int pitchBackwardLimit = -1;
    int rollLimit          = -1;

    HANDLE hThread = INVALID_HANDLE_VALUE;
    time_t timestamp = 0;

    static FILE* debugFile = NULL;
    void debug(const char* format, ...)
    {
        static char buf[300];
        static char out[300];

        if (debugFile == NULL) {
            try {
                errno_t error = fopen_s(&debugFile, "./YawVR.log", "wt");
            }
            catch (...) {
                OutputDebugString("[DBG] [Cockpitlook] Could not open YawVR.log");
            }
        }

        va_list args;
        va_start(args, format);

        vsprintf_s(buf, 300, format, args);
        sprintf_s(out, 300, "[DBG] [Cockpitlook] %s\n", buf);
        OutputDebugString(out);
        if (debugFile != NULL) {
            fprintf(debugFile, "%s\n", buf);
            fflush(debugFile);
        }

        va_end(args);
    }

    bool InitializeSockets()
    {
        // Initialize Winsock version 2.2
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            debug("[YVR] Failed on WSAStartup");
            return false;
        }
        debug("[YVR] Winsock status is %s", wsaData.szSystemStatus);

        // Create a new socket to make a client connection.
        // AF_INET = 2, The Internet Protocol version 4 (IPv4) address family, TCP protocol
        tcpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        udpSocket = socket(AF_INET, SOCK_DGRAM,  IPPROTO_UDP);
        u_long mode = 1;  // 1 to enable non-blocking socket
        ioctlsocket(udpSocket, FIONBIO, &mode);

        if (tcpSocket == INVALID_SOCKET)
        {
            debug("[YVR] TCP Socket creation failed. Error code: %ld", WSAGetLastError());
            WSACleanup();
            return false;
        }
        else
            debug("[YVR] TCP Socket created");

        if (udpSocket == INVALID_SOCKET)
        {
            debug("[YVR] UDP Socket creation failed. Error code: %ld", WSAGetLastError());
            WSACleanup();
            return false;
        }
        else
            debug("[YVR] UDP Socket created");

        return true;
    }

    void ShutdownSockets()
    {
        if (closesocket(tcpSocket) != 0)
            debug("[YVR] Cannot close TCP socket. Error code: %ld", WSAGetLastError());

        if (WSACleanup() != 0)
            debug("[YVR] WSACleanup() failed");
        else
            debug("[YVR] closed");
    }

    bool Connect()
    {
        int ret = 0;

        ServerSocket.sin_family = AF_INET; // IPv4
        ServerSocket.sin_port = htons(tcpPort);
        if (inet_pton(AF_INET, sServerIP, &(ServerSocket.sin_addr)) != 1)
        {
            debug("[YVR] inet_pton failed (1): %ld", WSAGetLastError());
            return false;
        }

        debug("[YVR] Connecting TCP socket...");
        ret = connect(tcpSocket, (SOCKADDR*)&ServerSocket, sizeof(ServerSocket));

        if (ret != 0)
        {
            debug("[YVR] connect() failed. Error code: %ld", WSAGetLastError());
            closesocket(tcpSocket);
            WSACleanup();
            return false;
        }
        else
        {
            debug("[YVR] TCP socket is ready");
        }


        ServerSocket.sin_family = AF_INET; // IPv4
        ServerSocket.sin_port = htons(udpPort);
        if (inet_pton(AF_INET, sServerIP, &(ServerSocket.sin_addr)) != 1)
        {
            debug("[YVR] inet_pton failed (2): %ld", WSAGetLastError());
            return false;
        }

        debug("[YVR] Connecting UDP socket...");
        if (connect(udpSocket, (SOCKADDR*)&ServerSocket, sizeof(ServerSocket)) != 0)
        {
            debug("[YVR] connect() failed. Error code: %ld", WSAGetLastError());
            closesocket(tcpSocket);
            WSACleanup();
            return false;
        }
        else
        {
            debug("[YVR] UDP socket is ready");
        }
        return true;
    }

    bool Send(char* data, int data_len)
    {
        int bytesSent = send(tcpSocket, data, data_len, 0);
        if (bytesSent == SOCKET_ERROR)
        {
            debug("[YVR] send() error %ld", WSAGetLastError());
            return false;
        }

        return true;
    }

    int Recv()
    {
        int bytesRecv = recv(tcpSocket, buffer, BUF_SIZE, 0);
        if (bytesRecv == SOCKET_ERROR)
        {
            debug("[YVR] recv() error %ld", WSAGetLastError());
            return -1;
        }
        //log_debug("[YVR] Bytes received: %d", bytesRecv);
        //for (int i = 0; i < bytesRecv; i++)
        //    log_debug("[YVR]    0x%x = %c", (unsigned int)buffer[i], buffer[i]);

        return bytesRecv;
    }

    bool SendUDPMessage(char* message)
    {
        int slen = sizeof(ServerSocket);

        if (sendto(udpSocket, message, strlen(message), 0, (struct sockaddr*)&ServerSocket, slen) == SOCKET_ERROR)
        {
            debug("[YVR] sendto() failed with error code: %d", WSAGetLastError());
            return false;
        }
        return true;
    }

    bool RecvUDPMessage()
    {
        buffer[0] = 0;
        if (recvfrom(udpSocket, buffer, BUF_SIZE, 0, nullptr, nullptr) == SOCKET_ERROR)
        {
            debug("[YVR] recvfrom() failed with error code: %d", WSAGetLastError());
            return false;
        }
        return true;
    }

    bool BroadcastUDPMessage(char* message)
    {
        sockaddr_in send_addr;
        send_addr.sin_family = AF_INET;
        send_addr.sin_port = htons(udpPort);
        if (inet_pton(AF_INET, "255.255.255.255", &(send_addr.sin_addr)) != 1)
        {
            debug("[YVR] inet_pton failed: %ld", WSAGetLastError());
            return false;
        }

        udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        //u_long mode = 1;  // 1 to enable non-blocking socket
        //ioctlsocket(udpSocket, FIONBIO, &mode);

        char broadcast = 1;
        setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        if (sendto(udpSocket, message, strlen(message), 0, (SOCKADDR*)&send_addr, sizeof(send_addr)) == SOCKET_ERROR)
        {
            debug("[YVR] Broadcast sendto() failed with error code: %d", WSAGetLastError());
            return false;
        }
        debug("[YVR] Broadcast sent!");

        sockaddr_in sender_addr;
        int sender_addrLen = sizeof(sender_addr);
        char response[128];
        if (recvfrom(udpSocket, response, 128, 0, (SOCKADDR*)&sender_addr, &sender_addrLen) == SOCKET_ERROR)
        {
            debug("[YVR] Broadcast recvfrom() failed with error code: %d", WSAGetLastError());
            return false;
        }
        debug("[YVR] Broadcast received: %s", response);
        return true;
    }

    void SetPosition(float yaw, float pitch, float roll)
    {
        sprintf_s(buffer, BUF_SIZE, "Y[%3.2f]P[%3.2f]R[%3.2f]", yaw, pitch, roll);
        SendUDPMessage(buffer);
        timestamp = time(NULL);
    }

    bool CheckIn()
    {
        debug("[YVR] Check In");
        buffer[0] = CHECK_IN;
        // Port
        buffer[1] = (&udpPort)[3];
        buffer[2] = (&udpPort)[2];
        buffer[3] = (&udpPort)[1];
        buffer[4] = (&udpPort)[0];
        // Game name
        buffer[5] = 'X';
        buffer[6] = 'W';
        buffer[7] = 'A';
        buffer[8] = 0;
        Send(buffer, 8);

        // The reply should be "1AVAILABLE"
        int len = Recv();
        buffer[len] = 0;

        if (ubuffer[0] != CHECK_IN_ANS)
        {
            debug("[YVR] Expected CHECK_IN_ANS, got 0x%x instead", ubuffer[0]);
            return false;
        }
        if (strstr(buffer + 1, "AVAILABLE") == nullptr) {
            debug("[YVR] YawVR is not available. Got %s", buffer + 1);
            return false;
        }
        
        return true;
    }

    bool Start()
    {
        debug("[YVR] Start");
        buffer[0] = START;
        Send(buffer, 1);
        int len = Recv();
        if (len < 1 || ubuffer[0] != START)
        {
            debug("[YVR] Start failed, got: 0x%x", ubuffer[0]);
            return false;
        }
        return true;
    }

    void Stop()
    {
        debug("[YVR] Stop");
        buffer[0] = STOP;
        Send(buffer, 1);
        (void)Recv();
    }

    void Exit()
    {
        debug("[YVR] Exit");
        buffer[0] = EXIT;
        Send(buffer, 1);
        (void)Recv();
    }

    bool SetYawLimit()
    {
        debug("[YVR] Setting Yaw Limit to: %d", yawLimit);
        buffer[0] = SET_YAW_LIMIT;
        buffer[1] = (&yawLimit)[3];
        buffer[2] = (&yawLimit)[2];
        buffer[3] = (&yawLimit)[1];
        buffer[4] = (&yawLimit)[0];
        Send(buffer, 5);
        int len = Recv();
        if (ubuffer[0] != SET_YAW_LIMIT)
        {
            debug("[YVR} Failed to set Yaw limit");
            return false;
        }
        int newLimit = 0;
        uint8_t* ptr = (uint8_t*)&newLimit;
        //ptr[3] = ubuffer[1];
        //ptr[2] = ubuffer[2];
        ptr[1] = ubuffer[3];
        ptr[0] = ubuffer[4];
        debug("[YVR] new yaw limit: %d", newLimit);
        return true;
    }

    bool SetTiltLimits()
    {
        debug("[YVR] Setting Tilt Limits to: %d,%d, %d",
            pitchForwardLimit, pitchBackwardLimit, rollLimit);
        buffer[0] = SET_TILT_LIMITS;
        
        buffer[1] = (&pitchForwardLimit)[3];
        buffer[2] = (&pitchForwardLimit)[2];
        buffer[3] = (&pitchForwardLimit)[1];
        buffer[4] = (&pitchForwardLimit)[0];

        buffer[5] = (&pitchBackwardLimit)[3];
        buffer[6] = (&pitchBackwardLimit)[2];
        buffer[7] = (&pitchBackwardLimit)[1];
        buffer[8] = (&pitchBackwardLimit)[0];

        buffer[9]  = (&rollLimit)[3];
        buffer[10] = (&rollLimit)[2];
        buffer[11] = (&rollLimit)[1];
        buffer[12] = (&rollLimit)[0];

        Send(buffer, 13);
        int len = Recv();
        if (ubuffer[0] != SET_TILT_LIMITS)
        {
            debug("[YVR} Failed to set Tilt limits");
            return false;
        }

        /*for (int i = 0; i < len; i++)
        {
            debug("[YVR] \t0x%x", ubuffer[i]);
        }*/

        return true;
    }

    DWORD WINAPI ThreadFun(LPVOID lpParam)
    {
        while (bRunThread)
        {
            time_t curTime = time(NULL);
            if (curTime - timestamp > 2)
            {
                yaw   = 0.0f;
                pitch = 0.0f;
                roll  = 0.0f;
                SetPosition(0.0f, 0.0f, 0.0f);
            }
            Sleep(500);
        }

        return 0;
    }

    void Initialize()
    {
        if (!InitializeSockets())
        {
            bEnabled = false;
            debug("[YVR] InitializeSockets() failed. Disabling YawVR");
            return;
        }

        if (!Connect())
        {
            bEnabled = false;
            debug("[YVR] Connect() failed. Disabling YawVR");
            return;
        }

        if (!CheckIn())
        {
            bEnabled = false;
            debug("[YVR] CheckIn() failed. Disabling YawVR");
            return;
        }

        /*
        if (yawLimit != -1)
        {
            SetYawLimit();
        }
        */

        if (pitchForwardLimit != -1 && pitchBackwardLimit != -1 && rollLimit != -1)
        {
            SetTiltLimits();
        }

        if (!Start())
        {
            bEnabled = false;
            debug("[YVR] Start() failed. Disabling YawVR");
            return;
        }

        bRunThread = true;
        hThread = CreateThread(NULL, 0, ThreadFun, NULL, 0, NULL);
    }

    void Shutdown()
    {
        bRunThread = false;
        SetPosition(0, 0, 0);
        Stop();
        Exit();
        ShutdownSockets();
    }

    void ApplyInertia(float yawInertia, float pitchInertia, float rollInertia)
    {
        yaw   += yawScale   * yawInertia;
        pitch  = pitchScale * pitchInertia;
        roll   = rollScale  * rollInertia;
        while (yaw >= 360.0f)
        {
            yaw -= 360.0f;
        }
        while (yaw < 0.0f)
        {
            yaw += 360.0f;
        }

        SetPosition(yaw, pitch, roll);
    }

}