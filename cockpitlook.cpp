/*
 * Copyright 2019, Justagai.
 * Extended for VR by Leo Reyes, 2019.
 * Justagai coded the whole mouse hook, I (Leo) just added support for FreePIE and SteamVR tracking.
 */
// TODO: Move the initialization of FreePIE/SteamVR to the dllmain's DLL_PROCESS_ATTACH (assuming this is possible)
#include "cockpitlook.h"
#include "XWAFramework.h"
#include "targetver.h"
#include "Hex.h"

#include <windows.h>
#include <Stdio.h>
#include <stdarg.h>
#include "FreePIE.h"
#include "SteamVR.h"
#include "TrackIR.h"

// TrackIR requires an HWND to register, so let's keep track of one.
HWND g_hWnd = NULL;

void log_debug(const char *format, ...)
{
	static char buf[300];
	static char out[300];

	va_list args;
	va_start(args, format);

	vsprintf_s(buf, 300, format, args);
	sprintf_s(out, 300, "[DBG] [Cockpitlook] %s", buf);
	OutputDebugString(out);

	va_end(args);
}

// cockpitlook.cfg parameter names
const char *TRACKER_TYPE = "tracker_type"; // Defines which tracker to use
const char *TRACKER_TYPE_FREEPIE = "FreePIE"; // Use FreePIE as the tracker
const char *TRACKER_TYPE_STEAMVR = "SteamVR"; // Use SteamVR as the tracker
const char *TRACKER_TYPE_TRACKIR = "TrackIR"; // Use TrackIR (or OpenTrack) as the tracker
const char *YAW_MULTIPLIER = "yaw_multiplier";
const char *PITCH_MULTIPLIER = "pitch_multiplier";

// Tracker-specific constants
// Some people might want to use the regular (non-VR) game with a tracker. In that case
// they usually want to be able to look around the cockpit while still having the screen
// in front of them, so we need a multiplier for the yaw and pitch. Or, you know, just
// give users the option to invert the axis if needed.
const float DEFAULT_YAW_MULTIPLIER = 1.0f;
const float DEFAULT_PITCH_MULTIPLIER = 1.0f;

// General types and globals
typedef enum {
	TRACKER_NONE,
	TRACKER_FREEPIE,
	TRACKER_STEAMVR,
	TRACKER_TRACKIR
} TrackerType;
TrackerType g_TrackerType = TRACKER_NONE;

float g_fYawMultiplier = DEFAULT_YAW_MULTIPLIER;
float g_fPitchMultiplier = DEFAULT_PITCH_MULTIPLIER;

/*
Params[1] = 2nd on stack
Params[0] = 1st on stack
Params[-1] = RETURN
Params[-2] = EAX
Params[-3] = EAX2
Params[-4] = ECX
Params[-5] = EDX
Params[-6] = EBX
Params[-9] = ESI
Params[-10] = EDI
Params[-11] = ESP
Params[-15] = EBP
*/
int CockpitLookHook(int* params)
{
	//GetKeyboardDeviceState();
	int playerIndex = params[-6];
	float yaw = 0.0f, pitch = 0.0f;
	float yawSign = 1.0f, pitchSign = 1.0f;
	bool dataReady = false;

	if (!PlayerDataTable[playerIndex].externalCamera
		&& !PlayerDataTable[playerIndex].hyperspacePhase
		&& PlayerDataTable[playerIndex].cockpitDisplayed
		&& !PlayerDataTable[playerIndex].gunnerTurretActive
		&& PlayerDataTable[playerIndex].cockpitDisplayed2
		&& *numberOfPlayersInGame == 1)
	{
		// Keyboard code for moving the cockpit camera angle
		__int16 keycodePressed = *keyPressedAfterLocaleAfterMapping;

		// Read tracking data.
		switch (g_TrackerType) 
		{
			case TRACKER_FREEPIE:
			{
				pitchSign = -1.0f;
				if (ReadFreePIE()) {
					yaw = g_FreePIEData.yaw * g_fYawMultiplier;
					pitch = g_FreePIEData.pitch * g_fPitchMultiplier;
					dataReady = true;
				}
			}
			break;

			case TRACKER_STEAMVR: 
			{
				GetSteamVRPositionalData(&yaw, &pitch);
				yaw *= RAD_TO_DEG * g_fYawMultiplier;
				pitch *= RAD_TO_DEG * g_fPitchMultiplier;
				yawSign = -1.0f;
				dataReady = true;
			}
			break;

			case TRACKER_TRACKIR:
			{
				if (ReadTrackIRData(&yaw, &pitch)) {
					yaw *= g_fYawMultiplier;
					pitch *= g_fPitchMultiplier;
					yawSign = -1.0f; pitchSign = -1.0f;
					dataReady = true;
				}
			}
			break;
		}

		if (dataReady) {
			while (yaw < 0.0f) yaw += 360.0f;
			while (pitch < 0.0f) pitch += 360.0f;
			PlayerDataTable[playerIndex].cockpitCameraYaw = (short)(yawSign * yaw / 360.0f * 65535.0f);
			PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(pitchSign * pitch / 360.0f * 65535.0f);
		}

		if (*win32NumPad5Pressed || keycodePressed == KeyCode_NUMPAD5)
		{
			// Cockpit camera is reset to center position
			PlayerDataTable[playerIndex].cockpitCameraYaw = 0;
			PlayerDataTable[playerIndex].cockpitCameraPitch = 0;
		}

		// Mouse look code
		if (*mouseLook && !*inMissionFilmState && !*viewingFilmState)
		{
			DirectInputKeyboardReaquire();

			if (!*mouseLookWasNotEnabled)
			{
				__int16 _mouseLook_Y = *mouseLook_Y;
				if (*mouseLook_X || *mouseLook_Y)
				{
					if (abs(*mouseLook_X) > 85 || abs(*mouseLook_Y) > 85)
					{
						char _mouseLookInverted = *mouseLookInverted;

						PlayerDataTable[playerIndex].cockpitCameraYaw += 40 * *mouseLook_X;
						if (_mouseLookInverted)
							PlayerDataTable[playerIndex].cockpitCameraPitch += 40 * _mouseLook_Y;
						else
							PlayerDataTable[playerIndex].cockpitCameraPitch += -40 * _mouseLook_Y;
					}
					else
					{
						char _mouseLookInverted = *mouseLookInverted;
						PlayerDataTable[playerIndex].cockpitCameraYaw += 15 * *mouseLook_X;
						if (_mouseLookInverted)
							PlayerDataTable[playerIndex].cockpitCameraPitch += 15 * _mouseLook_Y;
						else
							PlayerDataTable[playerIndex].cockpitCameraPitch += -15 * _mouseLook_Y;
					}
				}
					
			}
			if (*mouseLookResetPosition)
			{
				PlayerDataTable[playerIndex].cockpitCameraYaw = 0;
				PlayerDataTable[playerIndex].cockpitCameraPitch = 0;
			}
			if (*mouseLookWasNotEnabled)
				*mouseLookWasNotEnabled = 0;
		}
	}
	
	params[-1] = 0x4F9C33;
	return 0;
}

/* Load the cockpitlook.cfg file */
void LoadParams() {
	FILE *file;
	int error = 0;

	try {
		error = fopen_s(&file, "./cockpitlook.cfg", "rt");
	}
	catch (...) {
		log_debug("Could not load cockpitlook.cfg");
	}

	if (error != 0) {
		log_debug("Error %d when loading cockpitlook.cfg", error);
		return;
	}

	char buf[160], param[80], value[80];
	while (fgets(buf, 160, file) != NULL) {
		// Skip comments and blank lines
		if (buf[0] == ';' || buf[0] == '#')
			continue;
		if (strlen(buf) == 0)
			continue;

		if (sscanf_s(buf, "%s = %s", param, 80, value, 80) > 0) {
			if (_stricmp(param, TRACKER_TYPE) == 0) {
				if (_stricmp(value, TRACKER_TYPE_FREEPIE) == 0) {
					log_debug("Using FreePIE for tracking");
					g_TrackerType = TRACKER_FREEPIE;
				}
				else if (_stricmp(value, TRACKER_TYPE_STEAMVR) == 0) {
					log_debug("Using SteamVR for tracking");
					g_TrackerType = TRACKER_STEAMVR;
				}
				else if (_stricmp(value, TRACKER_TYPE_TRACKIR) == 0) {
					log_debug("Using TrackIR for tracking");
					g_TrackerType = TRACKER_TRACKIR;
				}
			}
			else if (_stricmp(param, YAW_MULTIPLIER) == 0) {
				g_fYawMultiplier = (float)atof(value);
				log_debug("Yaw multiplier: %0.3f", g_fYawMultiplier);
			}
			else if (_stricmp(param, PITCH_MULTIPLIER) == 0) {
				g_fPitchMultiplier = (float)atof(value);
				log_debug("Pitch multiplier: %0.3f", g_fPitchMultiplier);
			}
		}
	} // while ... read file
	fclose(file);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD uReason, LPVOID lpReserved)
{
	switch (uReason)
	{
	case DLL_PROCESS_ATTACH:
		log_debug("Cockpit Hook Loaded");
		g_hWnd = GetForegroundWindow();
		log_debug("g_hWnd: 0x%x", g_hWnd);
		// Load cockpitlook.cfg here and enable FreePIE, SteamVR or TrackIR
		LoadParams();
		switch (g_TrackerType)
		{
		case TRACKER_FREEPIE:
			InitFreePIE();
			break;
		case TRACKER_STEAMVR:
			InitSteamVR();
			break;
		case TRACKER_TRACKIR:
			InitTrackIR();
			break;
		}
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		switch (g_TrackerType) {
		case TRACKER_FREEPIE:
			ShutdownFreePIE();
			break;
		case TRACKER_STEAMVR:
			// We can't shutdown SteamVR twice: we either shut it down here, or in ddraw.dll.
			// It looks like the right order is to shut it down here.
			ShutdownSteamVR();
			break;
		case TRACKER_TRACKIR:
			ShutdownTrackIR();
			break;
		}
		break;
	}

	return TRUE;
}