#pragma once

#include <windows.h>

// This is the actual data that will be shared across DLLs. It contains
// a mixture of custom fields and a generic pointer to share whatever else
// we need later.
struct SharedData {
	// Offset added to the current POV when VR is active. This is controlled by ddraw
	float POVOffsetX, POVOffsetY, POVOffsetZ;
	void *pDataPtr;
	// Euler angles, in degrees, for the current camera matrix coming from SteamVR. This is
	// written by CockpitLookHook:
	float Yaw, Pitch, Roll;
	// Positional tracking data. Written by CockpitLookHook:
	float X, Y, Z;
	// Joystick's position, written by the joystick hook, or by the joystick emulation code.
	// These values are normalized in the range -1..1
	float JoystickYaw, JoystickPitch;
	// Flag to indicate that the reticle needs setup, to inhibit tracking and avoid roll messing with
	// the pips positions.
	// Set to 0 by ddraw when the game starts a mission (in OnSizeChanged())
	// Set to 1 by a hook in the SetupReticle() XWA function.
	int bIsReticleSetup;

	SharedData() {
		this->POVOffsetX = 0.0f;
		this->POVOffsetY = 0.0f;
		this->POVOffsetZ = 0.0f;
		this->pDataPtr = NULL;
		this->Yaw = 0.0f;
		this->Pitch = 0.0f;
		this->Roll = 0.0f;
		this->X = 0.0f;
		this->Y = 0.0f;
		this->Z = 0.0f;
		this->JoystickYaw = 0.0f;
		this->JoystickPitch = 0.0f;
		this->bIsReticleSetup = 0;
	}
};

// This hook loads before ddraw, so it contains the singleton that will be shared.
extern SharedData g_SharedData;

// This is the data that will be shared between different DLLs
// The bDataReady is set to true once the writer has initialized
// the structure.
// pDataPtr points to the actual data to be shared in the regular
// heap space
struct SharedDataProxy {
	bool bDataReady;
	SharedData *pSharedData;
};

constexpr auto SHARED_MEM_NAME = "Local\\CockpitLookHook";
constexpr auto SHARED_MEM_SIZE = sizeof(SharedDataProxy);

// This is the shared memory handler. Call GetMemoryPtr to get a pointer
// to a SharedData structure
class SharedMem {
private:
	HANDLE hMapFile;
	void *pSharedMemPtr;
	bool InitMemory(bool OpenCreate);

public:
	// Specify true in OpenCreate to create the shared memory handle. Set it to false to
	// open an existing shared memory handle.
	SharedMem(bool OpenCreate);
	~SharedMem();
	void *GetMemoryPtr();
};
