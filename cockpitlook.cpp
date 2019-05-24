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

const float PI = 3.141592f;
const float RAD_TO_DEG = 180.0f / PI;

typedef enum {
	TRACKER_NONE,
	TRACKER_FREEPIE,
	TRACKER_STEAMVR
} TrackerType;
TrackerType g_TrackerType = TRACKER_NONE;

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
	FreePIE definitions and functions.
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
void InitFreePIE() {
	hFreePIE = LoadLibraryA("freepie_io.dll");

	if (hFreePIE != NULL) {
		log_debug("[DBG] FreePIE loaded");
		freepie_io_6dof_slots = (freepie_io_6dof_slots_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_slots");
		freepie_io_6dof_read = (freepie_io_6dof_read_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_read");
		g_bFreePIELoaded = true;

		UINT32 num_slots = freepie_io_6dof_slots();
		log_debug("[DBG] num_slots: %d", num_slots);
	}
	else {
		log_debug("[DBG] Could not load FreePIE");
	}
	g_bFreePIEInitialized = true;
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

// SteamVR
#include <headers/openvr.h>
bool g_bEnableSteamVR = true; // The user sets this flag to request support for SteamVR
bool g_bSteamVRInitialized = false; // The system sets this to true when SteamVR has been initialized
bool g_bUseSteamVR = false; // The system sets this to true if SteamVR is loaded and working

vr::IVRSystem *g_pHMD = NULL;

bool InitSteamVR()
{
	vr::EVRInitError eError = vr::VRInitError_None;
	g_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);

	if (eError != vr::VRInitError_None)
	{
		g_pHMD = NULL;
		log_debug("[DBG] [MouseLook] Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
		return false;
	}
	return true;
}

void ShutDownSteamVR() {
	if (g_pHMD)
	{
		vr::VR_Shutdown();
		g_pHMD = NULL;
	}
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
	GetKeyboardDeviceState();
	int playerIndex = params[-6];
	if (!g_bFreePIEInitialized && !g_bEnableSteamVR)
		InitFreePIE();

	bool bUseFreePIE = false;
	if (!g_bEnableSteamVR && g_bFreePIELoaded)
		bUseFreePIE = readFreePIE();

	if (g_bEnableSteamVR && !g_bSteamVRInitialized)
	{
		g_bUseSteamVR = InitSteamVR();
		g_bSteamVRInitialized = true;
		log_debug("[DBG] [MouseLook] g_bUseSteamVR: %d", g_bUseSteamVR);
	}

	if (!PlayerDataTable[playerIndex].externalCamera
		&& !PlayerDataTable[playerIndex].hyperspacePhase
		&& PlayerDataTable[playerIndex].cockpitDisplayed
		&& !PlayerDataTable[playerIndex].gunnerTurretActive
		&& PlayerDataTable[playerIndex].cockpitDisplayed2
		&& *numberOfPlayersInGame == 1)
	{
		// Keyboard code for moving the cockpit camera angle
		__int16 keycodePressed = *keyPressedAfterLocaleAfterMapping;

		if (bUseFreePIE) {
			float yaw = g_FreePIEData.yaw;
			float pitch = g_FreePIEData.pitch;

			if (yaw < 0.0f) yaw += 360.0f;
			PlayerDataTable[playerIndex].cockpitCameraYaw = (short)(yaw / 360.0f * 65535.0f);
			
			if (pitch < 0.0f) pitch += 360.0f;
			PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(-pitch / 360.0f * 65535.0f);
		}

		// SteamVR code to read yaw and pitch
		if (g_bUseSteamVR) {
			float yaw, pitch;
			GetSteamVRPositionalData(&yaw, &pitch);
			yaw *= RAD_TO_DEG;
			pitch *= RAD_TO_DEG;
			if (yaw < 0.0f) yaw += 360.0f;
			if (pitch < 0.0f) pitch += 360.0f;
			//log_debug("[DBG] [MouseLook] Steam yaw, pitch: %0.3f, %0.3f", yaw, pitch);
			PlayerDataTable[playerIndex].cockpitCameraYaw = (short)(-yaw / 360.0f * 65535.0f);
			PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(pitch / 360.0f * 65535.0f);
		}

		if (*win32NumPad4Pressed || keycodePressed == KeyCode_ARROWLEFT)
			PlayerDataTable[playerIndex].cockpitCameraYaw -= 1200;

		if (*win32NumPad6Pressed || keycodePressed == KeyCode_ARROWRIGHT)
			PlayerDataTable[playerIndex].cockpitCameraYaw += 1200;

		if (*win32NumPad8Pressed || keycodePressed == KeyCode_ARROWDOWN)
			PlayerDataTable[playerIndex].cockpitCameraPitch += 1200;

		if (*win32NumPad2Pressed || keycodePressed == KeyCode_ARROWUP)
			PlayerDataTable[playerIndex].cockpitCameraPitch -= 1200;

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD uReason, LPVOID lpReserved)
{
	switch (uReason)
	{
	case DLL_PROCESS_ATTACH:
		log_debug("[DBG] [MouseLook] Mouse Hook Loaded");
		// Load vrparams.cfg here and enable either FreePIE or SteamVR
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		break;
	}

	return TRUE;
}