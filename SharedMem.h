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
	// XWA units to meters conversion factor. Set by CockpitLook. Used to apply POVOffset
	float povFactor;

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
		this->povFactor = 25.0f;
	}
};

constexpr int TLM_MAX_CARGO  =  80;
constexpr int TLM_MAX_SUBCMP =  80;
constexpr int TLM_MAX_NAME   = 120;
struct SharedMemDataTelemetry
{
	int counter;
	// Player stats
	int shieldsFwd, shieldsBck;
	// Target stats
	int tgtShds, tgtSys, tgtHull;
	float tgtDist;
	char tgtName[TLM_MAX_NAME];
	char tgtCargo[TLM_MAX_CARGO];
	char tgtSubCmp[TLM_MAX_SUBCMP];

	SharedMemDataTelemetry()
	{
		counter = 0;
		shieldsFwd = shieldsBck = -1;

		tgtShds = tgtSys = tgtHull = 0;
		tgtDist = 0;
		tgtName[0] = 0;
		tgtCargo[0] = 0;
		tgtSubCmp[0] = 0;
	};
};

void InitSharedMem();

constexpr auto SHARED_MEM_NAME_COCKPITLOOK = L"Local\\CockpitLookHook";
constexpr auto SHARED_MEM_NAME_TELEMETRY   = L"Local\\XWATelemetry";

extern SharedMem<SharedMemDataCockpitLook> g_SharedMem;
extern SharedMemDataCockpitLook* g_SharedData;

extern SharedMem<SharedMemDataTelemetry> g_SharedMemTelemetry;
extern SharedMemDataTelemetry* g_pSharedDataTelemetry;