#pragma once

#include <windows.h>
#include "SharedMemTemplate.h"

struct SharedMemDataCockpitLook {
	// Offset added to the current POV when VR is active. This is controlled by ddraw
	float POVOffsetX, POVOffsetY, POVOffsetZ;
	// Euler angles, in degrees, for the current camera matrix coming from SteamVR. This is
	// written by CockpitLookHook:
	float Yaw, Pitch, Roll;
	// Positional tracking data. Written by CockpitLookHook:
	float X, Y, Z;
	// Flag to indicate that the reticle needs setup, to inhibit tracking and avoid roll messing with
	// the pips positions.
	// Set to 0 by ddraw when the game starts a mission (in OnSizeChanged())
	// Set to 1 by a hook in the SetupReticle() XWA function.
	int bIsReticleSetup;

	SharedMemDataCockpitLook() {
		this->POVOffsetX = 0.0f;
		this->POVOffsetY = 0.0f;
		this->POVOffsetZ = 0.0f;
		this->Yaw = 0.0f;
		this->Pitch = 0.0f;
		this->Roll = 0.0f;
		this->X = 0.0f;
		this->Y = 0.0f;
		this->Z = 0.0f;
		this->bIsReticleSetup = 0;
	}
};

void InitSharedMem();

constexpr auto SHARED_MEM_NAME_COCKPITLOOK = L"Local\\CockpitLookHook";

extern SharedMem<SharedMemDataCockpitLook> g_SharedMem;
extern SharedMemDataCockpitLook* g_SharedData;