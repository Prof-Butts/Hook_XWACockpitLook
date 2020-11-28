#include <windows.h>
#include <stdio.h>
#include "TrackIR.h"

extern HWND g_hWnd;

void log_debug(const char *format, ...);

enum NPRESULT
{
	NP_OK,
	NP_ERR_DEVICE_NOT_PRESENT,
	NP_ERR_UNSUPPORTED_OS,
	NP_ERR_INVALID_ARG,
	NP_ERR_DLL_NOT_FOUND,
	NP_ERR_NO_DATA,
	NP_ERR_INTERNAL_DATA,
};

typedef struct TRACKIRSIGNATUREDATA_STRUCT
{
	char DllSignature[200];
	char AppSignature[200];
} TRACKIRSIGNATUREDATA;

typedef struct TRACKIRDATA_STRUCT
{
	WORD wNPStatus;
	WORD wPFrameSignature;
	DWORD dwNPIOData;
	float fNPRoll;
	float fNPPitch;
	float fNPYaw;
	float fNPX;
	float fNPY;
	float fNPZ;
	float fNPRawX;
	float fNPRawY;
	float fNPRawZ;
	float fNPDeltaX;
	float fNPDeltaY;
	float fNPDeltaZ;
	float fNPSmoothX;
	float fNPSmoothY;
	float fNPSmoothZ;
} TRACKIRDATA;

typedef int(__stdcall *NP_GetSignature_fun_type)(TRACKIRSIGNATUREDATA *);
typedef int(__stdcall *NP_RegisterWindowHandle_fun_type)(unsigned int hwnd);
typedef int(__stdcall *NP_UnregisterWindowHandle_fun_type)();
typedef int(__stdcall *NP_RegisterProgramProfileID_fun_type)(short id);
typedef int(__stdcall *NP_QueryVersion_fun_type)(unsigned short *version);
typedef int(__stdcall *NP_RequestData_fun_type)(unsigned short flags);
typedef int(__stdcall *NP_StartDataTransmission_fun_type)();
typedef int(__stdcall *NP_StopDataTransmission_fun_type)();
typedef int(__stdcall *NP_StopCursor_fun_type)();
typedef int(__stdcall *NP_StartCursor_fun_type)();
typedef int(__stdcall *NP_GetData_fun_type)(TRACKIRDATA *pData);
typedef int(__stdcall *NP_ReCenter_fun_type)();

NP_GetSignature_fun_type NP_GetSignature = NULL;
NP_RegisterWindowHandle_fun_type NP_RegisterWindowHandle = NULL;
NP_UnregisterWindowHandle_fun_type NP_UnregisterWindowHandle = NULL;
NP_QueryVersion_fun_type NP_QueryVersion = NULL;
NP_RequestData_fun_type NP_RequestData = NULL;
NP_RegisterProgramProfileID_fun_type NP_RegisterProgramProfileID = NULL;
NP_StartDataTransmission_fun_type NP_StartDataTransmission = NULL;
NP_StopDataTransmission_fun_type NP_StopDataTransmission = NULL;
NP_StopCursor_fun_type NP_StopCursor = NULL;
NP_StartCursor_fun_type NP_StartCursor = NULL;
NP_GetData_fun_type NP_GetData = NULL;
NP_ReCenter_fun_type NP_ReCenter = NULL;

HMODULE hTrackIR = NULL;
unsigned short g_TrackIRRequestData = 0x77;
short g_FreePIEProfileID = 13302; // Borrowing FreePIE's ID since I don't think I'm ever going to get one
TRACKIRDATA data;
extern bool g_bGlobalDebug;

bool InitTrackIR() {
	LONG lRes = ERROR_SUCCESS;
	char regvalue[1024], npclientPath[1024];
	DWORD size = 1024;
	unsigned short version = 0;
	int error;
	bool success = true;

	if (g_bGlobalDebug) log_debug("[TrackIR] InitTrackIR()");
	lRes = RegGetValueA(HKEY_CURRENT_USER, "Software\\NaturalPoint\\NaturalPoint\\NPClient Location\\",
		"Path", RRF_RT_ANY, NULL, regvalue, &size);
	if (lRes != ERROR_SUCCESS) {
		log_debug("Registry key for TrackIR was not found, error: 0x%x", lRes);
		success = false;
		goto out;
	}
	log_debug("Registry key: [%s]", regvalue);
	sprintf_s(npclientPath, 512, "%s\\NPClient.dll", regvalue);

	hTrackIR = LoadLibraryA(npclientPath);
	if (hTrackIR != NULL)
		log_debug("Loaded NPClient.dll");
	else {
		log_debug("Could not load NPClient.dll");
		success = false;
		goto out;
	}

	NP_GetSignature = (NP_GetSignature_fun_type)GetProcAddress(hTrackIR, "NP_GetSignature");
	NP_RegisterWindowHandle = (NP_RegisterWindowHandle_fun_type)GetProcAddress(hTrackIR, "NP_RegisterWindowHandle");
	NP_UnregisterWindowHandle = (NP_UnregisterWindowHandle_fun_type)GetProcAddress(hTrackIR, "NP_UnregisterWindowHandle");
	NP_QueryVersion = (NP_QueryVersion_fun_type)GetProcAddress(hTrackIR, "NP_QueryVersion");
	NP_RequestData = (NP_RequestData_fun_type)GetProcAddress(hTrackIR, "NP_RequestData");
	NP_RegisterProgramProfileID = (NP_RegisterProgramProfileID_fun_type)GetProcAddress(hTrackIR, "NP_RegisterProgramProfileID");
	NP_StartDataTransmission = (NP_StartDataTransmission_fun_type)GetProcAddress(hTrackIR, "NP_StartDataTransmission");
	NP_StopDataTransmission = (NP_StopDataTransmission_fun_type)GetProcAddress(hTrackIR, "NP_StopDataTransmission");
	NP_StopCursor = (NP_StopCursor_fun_type)GetProcAddress(hTrackIR, "NP_StopCursor");
	NP_StartCursor = (NP_StartCursor_fun_type)GetProcAddress(hTrackIR, "NP_StartCursor");
	NP_GetData = (NP_GetData_fun_type)GetProcAddress(hTrackIR, "NP_GetData");
	NP_ReCenter = (NP_ReCenter_fun_type)GetProcAddress(hTrackIR, "NP_ReCenter");

	if (NP_GetSignature == NULL) { log_debug("NP_GetSignature could not be loaded"); goto out; }
	if (NP_RegisterWindowHandle == NULL) { log_debug("NP_RegisterWindowHandle could not be loaded"); goto out; }
	if (NP_UnregisterWindowHandle == NULL) { log_debug("NP_UnregisterWindowHandle could not be loaded"); goto out; }
	if (NP_QueryVersion == NULL) { log_debug("NP_QueryVersion could not be loaded"); goto out; }
	if (NP_RequestData == NULL) { log_debug("NP_RequestData could not be loaded"); goto out; }
	if (NP_RegisterProgramProfileID == NULL) { log_debug("NP_RegisterProgramProfileID could not be loaded"); goto out; }
	if (NP_StartDataTransmission == NULL) { log_debug("NP_StartDataTransmission could not be loaded"); goto out; }
	if (NP_StopCursor == NULL) { log_debug("NP_StopCursor could not be loaded"); goto out; }
	if (NP_StartCursor == NULL) { log_debug("NP_StartCursor could not be loaded"); goto out; }
	if (NP_GetData == NULL) { log_debug("NP_GetData could not be loaded"); goto out; }
	if (NP_ReCenter == NULL) { log_debug("NP_ReCenter could not be loaded"); goto out; }

	TRACKIRSIGNATUREDATA signature;
	error = NP_GetSignature(&signature);
	if (error != 0) {
		log_debug("error %d when getting TrackIR's signature", error);
		success = false;
		goto out;
	}
	if (g_bGlobalDebug) log_debug("Signature:\n%s\n%s", signature.AppSignature, signature.DllSignature);

	error = NP_QueryVersion(&version);
	if (error != 0) {
		log_debug("error %d when querying version", error);
		success = false;
		goto out;
	}
	if (g_bGlobalDebug) log_debug("NPClient version: 0x%x", version);

	error = NP_RegisterWindowHandle((unsigned int )g_hWnd);
	if (error != 0) {
		log_debug("error %d when registering window handle 0x%x", error, g_hWnd);
		success = false;
		goto out;
	}
	log_debug("Registered window handle: 0x%x", g_hWnd);

	error = NP_RequestData(g_TrackIRRequestData);
	if (error != 0) {
		log_debug("error %d when requesting data (0x%x)", error, g_TrackIRRequestData);
		success = false;
		goto out;
	}
	if (g_bGlobalDebug) log_debug("NP_RequestData(0x%x)", g_TrackIRRequestData);

	error = NP_RegisterProgramProfileID(g_FreePIEProfileID);
	if (error != 0) {
		log_debug("error %d when registering program profile ID: %d", error, g_FreePIEProfileID);
		success = false;
		goto out;
	}
	log_debug("Profile ID %d registered", g_FreePIEProfileID);

	error = NP_StopCursor();
	if (error != 0) {
		log_debug("error %d when calling NP_StopCursor()", error);
		success = false;
		goto out;
	}
	if (g_bGlobalDebug) log_debug("NP_StopCursor()");

	error = NP_StartDataTransmission();
	if (error != 0) {
		log_debug("error %d when calling NP_StartDataTransmission", error);
		success = false;
		goto out;
	}
	if (g_bGlobalDebug) log_debug("NP_StartDataTransmission()");

out:
	if (!success)
		ShutdownTrackIR();
	else
		if (g_bGlobalDebug) log_debug("TrackIR Initialized successfully");
	return success;
}

void ShutdownTrackIR()
{
	int error = 0;
	error = NP_StopDataTransmission();
	if (error != NP_OK) {
		log_debug("error %d when calling NP_StopDataTransmission()", error);
	}
	if (g_bGlobalDebug) log_debug("NP_StopDataTransmission()");

	error = NP_StartCursor();
	if (error != NP_OK) {
		log_debug("error %d when calling NP_StartCursor()", error);
	}
	if (g_bGlobalDebug) log_debug("NP_StartCursor()");


	error = NP_UnregisterWindowHandle();
	if (error != NP_OK) {
		log_debug("error %d when calling NP_UnregisterWindowHandle()", error);
	}
	if (g_bGlobalDebug) log_debug("NP_UnregisterWindowHandle()");
	FreeLibrary(hTrackIR);
}

bool ReadTrackIRData(float *yaw, float *pitch, float *x, float *y, float *z) {
	int error;
	error = NP_GetData(&data);
	if (error != NP_OK || data.wNPStatus != 0) {
		log_debug("error: %d, wNPStatus: %d", error, data.wNPStatus);
		return false;
	}
	*yaw   = data.fNPYaw   / 100.0f;
	*pitch = data.fNPPitch / 100.0f;
	*x     = data.fNPX;
	*y	   = data.fNPY;
	*z	   = data.fNPZ;
	return true;
}