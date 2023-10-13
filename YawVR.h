#pragma once

namespace YawVR
{
	extern bool  bEnabled;
	extern char  sServerIP[80];
	extern int   udpPort;
	extern int   tcpPort;

	extern float yawScale;
	extern float pitchScale;
	extern float rollScale;
	extern float distScale;

	extern int yawLimit;
	extern int pitchForwardLimit;
	extern int pitchBackwardLimit;
	extern int rollLimit;
	extern float maxPitchFromAccel;
	extern float minPitchFromAccel;
	extern bool enableHyperAccel;

	bool InitializeSockets();
	void Shutdown();
	void Initialize();
	void ApplyInertia(float yawInertia, float pitchInertia, float rollInertia, float distInertia);

	void debug(const char* format, ...);
};