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

HWND g_hWnd = NULL;

// cockpitlook.cfg parameters
const char *TRACKER_TYPE = "tracker_type"; // Defines which tracker to use
const char *TRACKER_TYPE_FREEPIE = "FreePIE"; // Use FreePIE as the tracker
const char *TRACKER_TYPE_STEAMVR = "SteamVR"; // User SteamVR as the tracker
const char *YAW_MULTIPLIER = "yaw_multiplier";
const char *PITCH_MULTIPLIER = "pitch_multiplier";

// General constants
const float PI = 3.141592f;
const float RAD_TO_DEG = 180.0f / PI;

// Tracker-specific constants
// Some people might want to use the regular (non-VR) game with a tracker. In that case
// they usually want to be able to look around the cockpit while still having the screen
// in front of them, so we need a multiplier for the yaw and pitch.
const float DEFAULT_YAW_MULTIPLIER = 1.0f;
const float DEFAULT_PITCH_MULTIPLIER = 1.0f;

// General types and globals
typedef enum {
	TRACKER_NONE,
	TRACKER_FREEPIE,
	TRACKER_STEAMVR
} TrackerType;
TrackerType g_TrackerType = TRACKER_NONE;

float g_fYawMultiplier = DEFAULT_YAW_MULTIPLIER;
float g_fPitchMultiplier = DEFAULT_PITCH_MULTIPLIER;

void log_debug(const char *format, ...)
{
	static char buf[120];

	va_list args;
	va_start(args, format);

	vsprintf_s(buf, 120, format, args);
	OutputDebugString(buf);

	va_end(args);
}

/*
 *	FreePIE definitions and functions.
 */
// An attempt to access the shared store of data failed.
const INT32 FREEPIE_IO_ERROR_SHARED_DATA = -1;
// An attempt to access out of bounds data was made.
const INT32 FREEPIE_IO_ERROR_OUT_OF_BOUNDS = -2;

typedef struct freepie_io_6dof_data
{
	float yaw;
	float pitch;
	float roll;

	float x;
	float y;
	float z;
} freepie_io_6dof_data;

typedef UINT32(__cdecl *freepie_io_6dof_slots_fun_type)();
freepie_io_6dof_slots_fun_type freepie_io_6dof_slots = NULL;
typedef INT32(__cdecl *freepie_io_6dof_read_fun_type)(UINT32 index, UINT32 length, freepie_io_6dof_data *output);
freepie_io_6dof_read_fun_type freepie_io_6dof_read = NULL;

bool g_bFreePIELoaded = false, g_bFreePIEInitialized = false;
freepie_io_6dof_data g_FreePIEData;

HMODULE hFreePIE = NULL;
bool InitFreePIE() {
	LONG lRes = ERROR_SUCCESS;
	char regvalue[512];
	DWORD size = 512;
	log_debug("[DBG] [Cockpitlook] Initializing FreePIE");
	
	lRes = RegGetValue(HKEY_CURRENT_USER, "Software\\FreePIE", "path", RRF_RT_ANY, NULL, regvalue, &size);
	if (lRes != ERROR_SUCCESS) {
		log_debug("[DBG] Registry key for FreePIE was not found, error: 0x%x", lRes);
		return false;
	}

	if (size > 0) {
		log_debug("[DBG] FreePIE path: %s", regvalue);
		SetDllDirectory(regvalue);
	}
	else {
		log_debug("[DBG] Cannot load FreePIE, registry path is empty!");
		return false;
	}

	hFreePIE = LoadLibraryA("freepie_io.dll");

	if (hFreePIE != NULL) {
		log_debug("[DBG] FreePIE loaded");
		freepie_io_6dof_slots = (freepie_io_6dof_slots_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_slots");
		freepie_io_6dof_read = (freepie_io_6dof_read_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_read");
		g_bFreePIELoaded = true;

		UINT32 num_slots = freepie_io_6dof_slots();
		log_debug("[DBG] num_slots: %d", num_slots);
		return true;
	}
	else {
		log_debug("[DBG] Could not load FreePIE");
	}
	g_bFreePIEInitialized = true;
	return true;
}

void ShutdownFreePIE() {
	log_debug("[DBG] [Cockpitlook] Shutting down FreePIE");
	if (hFreePIE != NULL)
		FreeLibrary(hFreePIE);
}

bool readFreePIE() {
	// Check how many slots (values) the current FreePIE implementation provides.
	int error = freepie_io_6dof_read(0, 3, &g_FreePIEData);
	if (error < 0) {
		log_debug("[DBG] FreePIE error: %d", error);
		return false;
	}
	return error;
}

/************************************************
  SteamVR
*************************************************/
#include <headers/openvr.h>

vr::IVRSystem *g_pHMD = NULL;

bool InitSteamVR()
{
	log_debug("[DBG] [Cockpitlook] Loading SteamVR");
	vr::EVRInitError eError = vr::VRInitError_None;
	g_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

	if (eError != vr::VRInitError_None)
	{
		g_pHMD = NULL;
		log_debug("[DBG] [Cockpitlook] Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
		return false;
	}
	return true;
}

void ShutDownSteamVR() {
	log_debug("[DBG] [Cockpitlook] Shutting down SteamVR...");
	vr::VR_Shutdown();
	g_pHMD = NULL;
	log_debug("[DBG] [Cockpitlook] SteamVR shut down");
}

/*
 * Convert a rotation matrix to a normalized quaternion.
 * From: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
 */
vr::HmdQuaternionf_t rotationToQuaternion(vr::HmdMatrix34_t m) {
	float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
	vr::HmdQuaternionf_t q;

	if (tr > 0) {
		float S = sqrt(tr + 1.0f) * 2.0f; // S=4*qw 
		q.w = 0.25f * S;
		q.x = (m.m[2][1] - m.m[1][2]) / S;
		q.y = (m.m[0][2] - m.m[2][0]) / S;
		q.z = (m.m[1][0] - m.m[0][1]) / S;
	}
	else if ((m.m[0][0] > m.m[1][1]) && (m.m[0][0] > m.m[2][2])) {
		float S = sqrt(1.0f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.0f; // S=4*qx 
		q.w = (m.m[2][1] - m.m[1][2]) / S;
		q.x = 0.25f * S;
		q.y = (m.m[0][1] + m.m[1][0]) / S;
		q.z = (m.m[0][2] + m.m[2][0]) / S;
	}
	else if (m.m[1][1] > m.m[2][2]) {
		float S = sqrt(1.0f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.0f; // S=4*qy
		q.w = (m.m[0][2] - m.m[2][0]) / S;
		q.x = (m.m[0][1] + m.m[1][0]) / S;
		q.y = 0.25f * S;
		q.z = (m.m[1][2] + m.m[2][1]) / S;
	}
	else {
		float S = sqrt(1.0f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.0f; // S=4*qz
		q.w = (m.m[1][0] - m.m[0][1]) / S;
		q.x = (m.m[0][2] + m.m[2][0]) / S;
		q.y = (m.m[1][2] + m.m[2][1]) / S;
		q.z = 0.25f * S;
	}
	float Q = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
	q.x /= Q;
	q.y /= Q;
	q.z /= Q;
	q.w /= Q;
	return q;
}

/*
   From: http://www.euclideanspace.com/maths/geometry/rotations/conversions/quaternionToEuler/index.htm
   yaw: left = +90, right = -90
   pitch: up = +90, down = -90
   roll: left = +90, right = -90

   if roll > 90, the axis will swap pitch and roll; but why would anyone do that?
*/
void quatToEuler(vr::HmdQuaternionf_t q, float *yaw, float *roll, float *pitch) {
	float test = q.x*q.y + q.z*q.w;

	if (test > 0.499f) { // singularity at north pole
		*yaw = 2 * atan2(q.x, q.w);
		*pitch = PI / 2.0f;
		*roll = 0;
		return;
	}
	if (test < -0.499f) { // singularity at south pole
		*yaw = -2 * atan2(q.x, q.w);
		*pitch = -PI / 2.0f;
		*roll = 0;
		return;
	}
	float sqx = q.x*q.x;
	float sqy = q.y*q.y;
	float sqz = q.z*q.z;
	*yaw = atan2(2.0f * q.y*q.w - 2.0f * q.x*q.z, 1.0f - 2.0f * sqy - 2.0f * sqz);
	*pitch = asin(2.0f * test);
	*roll = atan2(2.0f * q.x*q.w - 2.0f * q.y*q.z, 1.0f - 2.0f * sqx - 2.0f * sqz);
}

void GetSteamVRPositionalData(float *yaw, float *pitch)
{
	float roll;
	vr::TrackedDeviceIndex_t unDevice = vr::k_unTrackedDeviceIndex_Hmd;
	if (!g_pHMD->IsTrackedDeviceConnected(unDevice))
		return;

	vr::VRControllerState_t state;
	if (g_pHMD->GetControllerState(unDevice, &state, sizeof(state)))
	{
		vr::TrackedDevicePose_t trackedDevicePose;
		vr::HmdMatrix34_t poseMatrix;
		vr::HmdQuaternionf_t q;
		vr::ETrackedDeviceClass trackedDeviceClass = vr::VRSystem()->GetTrackedDeviceClass(unDevice);

		vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0, &trackedDevicePose, 1);
		poseMatrix = trackedDevicePose.mDeviceToAbsoluteTracking; // This matrix contains all positional and rotational data.
		q = rotationToQuaternion(trackedDevicePose.mDeviceToAbsoluteTracking);
		quatToEuler(q, yaw, pitch, &roll);
	}
}

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
		switch (g_TrackerType) {
			case TRACKER_FREEPIE:
			{
				if (readFreePIE()) {
					yaw = g_FreePIEData.yaw * g_fYawMultiplier;
					pitch = g_FreePIEData.pitch * g_fPitchMultiplier;

					while (yaw < 0.0f) yaw += 360.0f;
					while (pitch < 0.0f) pitch += 360.0f;

					PlayerDataTable[playerIndex].cockpitCameraYaw = (short)(yaw / 360.0f * 65535.0f);
					PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(-pitch / 360.0f * 65535.0f);
				}
				break;
			}
			case TRACKER_STEAMVR: 
			{
				GetSteamVRPositionalData(&yaw, &pitch);
				yaw *= RAD_TO_DEG * g_fYawMultiplier;
				pitch *= RAD_TO_DEG * g_fPitchMultiplier;

				while (yaw < 0.0f) yaw += 360.0f;
				while (pitch < 0.0f) pitch += 360.0f;

				PlayerDataTable[playerIndex].cockpitCameraYaw = (short)(-yaw / 360.0f * 65535.0f);
				PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(pitch / 360.0f * 65535.0f);
			}
			break;
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
		log_debug("[DBG] Could not load cockpitlook.cfg");
	}

	if (error != 0) {
		log_debug("[DBG] Error %d when loading cockpitlook.cfg", error);
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
					log_debug("[DBG] Using FreePIE for tracking");
					g_TrackerType = TRACKER_FREEPIE;
				}
				else if (_stricmp(value, TRACKER_TYPE_STEAMVR) == 0) {
					log_debug("[DBG] Using SteamVR for tracking");
					g_TrackerType = TRACKER_STEAMVR;
				}
			}
			else if (_stricmp(param, YAW_MULTIPLIER) == 0) {
				g_fYawMultiplier = (float)atof(value);
				if (g_fYawMultiplier == 0.0f)
					g_fYawMultiplier = DEFAULT_YAW_MULTIPLIER;
				log_debug("[DBG] Yaw multiplier: %0.3f", g_fYawMultiplier);
			}
			else if (_stricmp(param, PITCH_MULTIPLIER) == 0) {
				g_fPitchMultiplier = (float)atof(value);
				if (g_fPitchMultiplier == 0.0f)
					g_fPitchMultiplier = DEFAULT_PITCH_MULTIPLIER;
				log_debug("[DBG] Pitch multiplier: %0.3f", g_fPitchMultiplier);
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
		log_debug("[DBG] [Cockpitlook] Cockpit Hook Loaded");
		g_hWnd = GetForegroundWindow();
		log_debug("[DBG] [Cockpitlook] g_hWnd: 0x%x", g_hWnd);
		// Load cockpitlook.cfg here and enable either FreePIE or SteamVR
		LoadParams();
		switch (g_TrackerType)
		{
		case TRACKER_FREEPIE:
			InitFreePIE();
			break;
		case TRACKER_STEAMVR:
			InitSteamVR();
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
			ShutDownSteamVR();
			break;
		}
		break;
	}

	return TRUE;
}