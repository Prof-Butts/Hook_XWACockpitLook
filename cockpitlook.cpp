/*
 * Copyright 2019, Justagai.
 * Extended for VR by Leo Reyes, 2019.
 * Justagai coded the whole mouse hook, I (blue_max) just added support for FreePIE and SteamVR tracking.
 * December 2019: Added support for positional tracking by hijacking the cockpit shake variables.

 The function that process keyboard input is L004FBA80. The pressed key is stored in the variable at 
 address 0x08053C0. You may want to hook this function and filter the pressed key.

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

#include "Vectors.h"
#include "Matrices.h"

// TrackIR requires an HWND to register, so let's keep track of one.
HWND g_hWnd = NULL;

extern bool g_bSteamVRInitialized;

#define DEBUG_TO_FILE 1
//#undef DEBUG_TO_FILE

#ifdef DEBUG_TO_FILE
FILE *g_DebugFile = NULL;
#endif

void LoadParams();

void log_debug(const char *format, ...)
{
	static char buf[300];
	static char out[300];

#ifdef DEBUG_TO_FILE
	if (g_DebugFile == NULL) {
		try {
			errno_t error = fopen_s(&g_DebugFile, "./CockpitLook.log", "wt");
		}
		catch (...) {
			OutputDebugString("[DBG] [Cockpitlook] Could not open CockpitLook.log");
		}
	}
#endif

	va_list args;
	va_start(args, format);

	vsprintf_s(buf, 300, format, args);
	sprintf_s(out, 300, "[DBG] [Cockpitlook] %s", buf);
	OutputDebugString(out);
#ifdef DEBUG_TO_FILE
	if (g_DebugFile != NULL) {
		fprintf(g_DebugFile, "%s\n", buf);
		fflush(g_DebugFile);
	}
#endif

	va_end(args);
}

// cockpitlook.cfg parameter names
const char *TRACKER_TYPE				= "tracker_type"; // Defines which tracker to use
const char *TRACKER_TYPE_FREEPIE		= "FreePIE"; // Use FreePIE as the tracker
const char *TRACKER_TYPE_STEAMVR		= "SteamVR"; // Use SteamVR as the tracker
const char *TRACKER_TYPE_TRACKIR		= "TrackIR"; // Use TrackIR (or OpenTrack) as the tracker
const char *TRACKER_TYPE_NONE		= "None";
const char *YAW_MULTIPLIER			= "yaw_multiplier";
const char *PITCH_MULTIPLIER			= "pitch_multiplier";
const char *YAW_OFFSET				= "yaw_offset";
const char *PITCH_OFFSET				= "pitch_offset";
const char *FREEPIE_SLOT				= "freepie_slot";

const char *POS_X_MULTIPLIER_VRPARAM = "positional_x_multiplier";
const char *POS_Y_MULTIPLIER_VRPARAM = "positional_y_multiplier";
const char *POS_Z_MULTIPLIER_VRPARAM = "positional_z_multiplier";
const char *MIN_POSITIONAL_X_VRPARAM = "min_positional_track_x";
const char *MAX_POSITIONAL_X_VRPARAM = "max_positional_track_x";
const char *MIN_POSITIONAL_Y_VRPARAM = "min_positional_track_y";
const char *MAX_POSITIONAL_Y_VRPARAM = "max_positional_track_y";
const char *MIN_POSITIONAL_Z_VRPARAM = "min_positional_track_z";
const char *MAX_POSITIONAL_Z_VRPARAM = "max_positional_track_z";

// Tracker-specific constants
// Some people might want to use the regular (non-VR) game with a tracker. In that case
// they usually want to be able to look around the cockpit while still having the screen
// in front of them, so we need a multiplier for the yaw and pitch. Or, you know, just
// give users the option to invert the axis if needed.
const float DEFAULT_YAW_MULTIPLIER = 1.0f;
const float DEFAULT_PITCH_MULTIPLIER = 1.0f;
//const float DEFAULT_ROLL_MULTIPLIER = 1.0f;
const float DEFAULT_YAW_OFFSET = 0.0f;
const float DEFAULT_PITCH_OFFSET = 0.0f;
const int   DEFAULT_FREEPIE_SLOT = 0;
const int   VK_L_KEY = 0x4c;

// General types and globals
typedef enum {
	TRACKER_NONE,
	TRACKER_FREEPIE,
	TRACKER_STEAMVR,
	TRACKER_TRACKIR
} TrackerType;
TrackerType g_TrackerType = TRACKER_NONE;

float g_fYawMultiplier   = DEFAULT_YAW_MULTIPLIER;
float g_fPitchMultiplier = DEFAULT_PITCH_MULTIPLIER;
//float g_fRollMultiplier  = DEFAULT_ROLL_MULTIPLIER;
float g_fYawOffset		 = DEFAULT_YAW_OFFSET;
float g_fPitchOffset		 = DEFAULT_PITCH_OFFSET;
int   g_iFreePIESlot		 = DEFAULT_FREEPIE_SLOT;
bool	  g_bYawPitchFromMouseOverride = false;
bool  g_bKeyboardLean = false;
Vector4 g_headCenter(0, 0, 0, 0), g_headPos(0, 0, 0, 0);
Vector3 g_headPosFromKeyboard(0, 0, 0);
int g_FreePIEOutputSlot = -1;

/*********************************************************************/
/*	Code used to enable leaning in the cockpit with the arrow keys   */
/*********************************************************************/

/*
 * Compute the current ship's orientation. Returns:
 * Rs: The "Right" vector in global coordinates
 * Us: The "Up" vector in global coordinates
 * Fs: The "Forward" vector in global coordinates
 * A viewMatrix that maps [Rs, Us, Fs] to the major [X, Y, Z] axes
 */
Matrix4 GetCurrentHeadingMatrix(Vector4 &Rs, Vector4 &Us, Vector4 &Fs, bool invert = false)
{
	const float DEG2RAD = 3.141593f / 180;
	float yaw, pitch, roll;
	Matrix4 rotMatrixFull, rotMatrixYaw, rotMatrixPitch, rotMatrixRoll;
	Vector4 T, B, N;
	// Compute the full rotation
	yaw   = PlayerDataTable[0].yaw   / 65536.0f * 360.0f;
	pitch = PlayerDataTable[0].pitch / 65536.0f * 360.0f;
	roll  = PlayerDataTable[0].roll  / 65536.0f * 360.0f;

	// To test how (x,y,z) is aligned with either the Y+ or Z+ axis, just multiply rotMatrixPitch * rotMatrixYaw * (x,y,z)
	//Matrix4 rotMatrixFull, rotMatrixYaw, rotMatrixPitch, rotMatrixRoll;
	rotMatrixFull.identity();
	rotMatrixYaw.identity();   rotMatrixYaw.rotateY(-yaw);
	rotMatrixPitch.identity(); rotMatrixPitch.rotateX(-pitch);
	rotMatrixRoll.identity();  rotMatrixRoll.rotateY(roll);

	// rotMatrixYaw aligns the orientation with the y-z plane (x --> 0)
	// rotMatrixPitch * rotMatrixYaw aligns the orientation with y+ (x --> 0 && z --> 0)
	// so the remaining rotation must be around the y axis (?)
	// DEBUG, x = z, y = x, z = y;
	// The yaw is indeed the y-axis rotation, it goes from -180 to 0 to 180.
	// When pitch == 90, the craft is actually seeing the horizon
	// When pitch == 0, the craft is looking towards the sun
	// New approach: let's build a TBN system here to avoid the gimbal lock problem
	float cosTheta, cosPhi, sinTheta, sinPhi;
	cosTheta = cos(yaw * DEG2RAD), sinTheta = sin(yaw * DEG2RAD);
	cosPhi = cos(pitch * DEG2RAD), sinPhi = sin(pitch * DEG2RAD);
	N.z = cosTheta * sinPhi;
	N.x = sinTheta * sinPhi;
	N.y = cosPhi;
	N.w = 0;

	// This transform chain will always transform (N.x,N.y,N.z) into (0, 1, 0)
	// To make an orthonormal basis, we need x+ and z+
	N = rotMatrixPitch * rotMatrixYaw * N;
	//log_debug("[DBG] N(DEBUG): %0.3f, %0.3f, %0.3f", N.x, N.y, N.z); // --> displays (0,1,0)
	B.x = 0; B.y = 0; B.z = -1; B.w = 0;
	T.x = 1; T.y = 0; T.z = 0; T.w = 0;
	B = rotMatrixRoll * B;
	T = rotMatrixRoll * T;
	// Our new basis is T,B,N; but we need to invert the yaw/pitch rotation we applied
	rotMatrixFull = rotMatrixPitch * rotMatrixYaw;
	rotMatrixFull.invert();
	T = rotMatrixFull * T;
	B = rotMatrixFull * B;
	N = rotMatrixFull * N;
	// Our TBN basis is now in absolute coordinates
	Matrix4 rotX, refl;
	rotX.identity();
	rotX.rotateX(90.0f);
	refl.set(
		1,  0,  0,  0,
		0, -1,  0,  0,
		0,  0,  1,  0,
		0,  0,  0,  1
	);
	Fs = refl * rotX * N;
	Us = refl * rotX * B;
	Rs = refl * rotX * T;
	Fs.w = 0; Rs.w = 0; Us.w = 0;
	// This transform chain gets us the orientation of the craft in XWA's coord system:
	// [1,0,0] is right, [0,1,0] is forward, [0,0,1] is up

	Matrix4 viewMatrix;
	if (!invert) { // Transform current ship's heading to Global Coordinates (Major Axes)
		viewMatrix = Matrix4(
			Rs.x, Us.x, Fs.x, 0,
			Rs.y, Us.y, Fs.y, 0,
			Rs.z, Us.z, Fs.z, 0,
			0, 0, 0, 1
		);
		// Rs, Us, Fs is an orthonormal basis
	}
	else { // Transform Global Coordinates to the Ship's Coordinate System
		viewMatrix = Matrix4(
			Rs.x, Rs.y, Rs.z, 0,
			Us.x, Us.y, Us.z, 0,
			Fs.x, Fs.y, Fs.z, 0,
			0, 0, 0, 1
		);
		// Rs, Us, Fs is an orthonormal basis
	}
	return viewMatrix;
}

typedef struct HeadPosStruct {
	float x, y, z;
} HeadPos;

/* Maps (-6, 6) to (-0.5, 0.5) using a sigmoid function */
float centeredSigmoid(float x) {
	return 1.0f / (1.0f + exp(-x)) - 0.5f;
}

// TODO: Remove all these variables from ddraw once the migration is complete.
//float g_fXWAUnitsToMetersScale = 655.36f; // This is technically correct; but it seems too much for me
float g_fXWAUnitsToMetersScale = 400.0f; // This value feels better
float g_fPosXMultiplier = -1.0f, g_fPosYMultiplier = -1.0f, g_fPosZMultiplier = -1.0f;
float g_fMinPositionX = -2.50f, g_fMaxPositionX = 2.50f;
float g_fMinPositionY = -2.50f, g_fMaxPositionY = 2.50f;
float g_fMinPositionZ = -2.50f, g_fMaxPositionZ = 2.50f;
HeadPos g_HeadPosAnim = { 0 }, g_HeadPos = { 0 };
bool g_bLeftKeyDown = false, g_bRightKeyDown = false, g_bUpKeyDown = false, g_bDownKeyDown = false;
bool g_bUpKeyDownShift = false, g_bDownKeyDownShift = false, g_bStickyArrowKeys = true, g_bLimitCockpitLean = true;
bool g_bInvertCockpitLeanY = false;
bool g_bResetHeadCenter = false, g_bSteamVRPosFromFreePIE = false;
bool g_bFlipYZAxes = false;
// if true then the arrow keys will modify the cockpit camera's yaw/pitch
// if false, then the arrow keys will perform lean right/left up/down
bool g_bToggleKeyboardCaps = false;
const float ANIM_INCR = 0.01f;
float MAX_LEAN_X = 25.0f, MAX_LEAN_Y = 25.0f, MAX_LEAN_Z = 25.0f;
const float RESET_ANIM_INCR = 2.0f * ANIM_INCR;
// The MAX_LEAN values will be clamped by the limits from vrparams.cfg

void animTickX(Vector3 *headPos) {
	if (g_bRightKeyDown)
		g_HeadPosAnim.x -= ANIM_INCR;
	else if (g_bLeftKeyDown)
		g_HeadPosAnim.x += ANIM_INCR;
	else if (!g_bRightKeyDown && !g_bLeftKeyDown && !g_bStickyArrowKeys) {
		if (g_HeadPosAnim.x < 0.0)
			g_HeadPosAnim.x += RESET_ANIM_INCR;
		if (g_HeadPosAnim.x > 0.0)
			g_HeadPosAnim.x -= RESET_ANIM_INCR;
	}

	// Range clamping
	if (g_bLimitCockpitLean) {
		if (g_HeadPosAnim.x >  MAX_LEAN_X)  g_HeadPosAnim.x =  MAX_LEAN_X;
		if (g_HeadPosAnim.x < -MAX_LEAN_X)  g_HeadPosAnim.x = -MAX_LEAN_X;
	}

	//headPos->x = centeredSigmoid(g_HeadPosAnim.x) * MAX_LEAN_X;
	headPos->x = g_HeadPosAnim.x;
}

void animTickY(Vector3 *headPos) {
	float sign = g_bInvertCockpitLeanY ? -1.0f : 1.0f;
	if (g_bDownKeyDown)
		g_HeadPosAnim.y +=  sign * ANIM_INCR;
	else if (g_bUpKeyDown)
		g_HeadPosAnim.y += -sign * ANIM_INCR;
	else if (!g_bDownKeyDown && !g_bUpKeyDown && !g_bStickyArrowKeys) {
		if (g_HeadPosAnim.y < 0.0)
			g_HeadPosAnim.y += RESET_ANIM_INCR;
		if (g_HeadPosAnim.y > 0.0)
			g_HeadPosAnim.y -= RESET_ANIM_INCR;
	}

	// Range clamping
	if (g_bLimitCockpitLean) {
		if (g_HeadPosAnim.y >  MAX_LEAN_Y)  g_HeadPosAnim.y =  MAX_LEAN_Y;
		if (g_HeadPosAnim.y < -MAX_LEAN_Y)  g_HeadPosAnim.y = -MAX_LEAN_Y;
	}

	//headPos->y = centeredSigmoid(g_HeadPosAnim.y) * MAX_LEAN_Y;
	headPos->y = g_HeadPosAnim.y;
}

void animTickZ(Vector3 *headPos) {
	if (g_bDownKeyDownShift)
		g_HeadPosAnim.z -= ANIM_INCR;
	else if (g_bUpKeyDownShift)
		g_HeadPosAnim.z += ANIM_INCR;
	else if (!g_bDownKeyDownShift && !g_bUpKeyDownShift && !g_bStickyArrowKeys) {
		if (g_HeadPosAnim.z < 0.0)
			g_HeadPosAnim.z += RESET_ANIM_INCR;
		if (g_HeadPosAnim.z > 0.0 /* 0.0001 */)
			g_HeadPosAnim.z -= RESET_ANIM_INCR;
	}

	// Range clamping
	if (g_bLimitCockpitLean) {
		if (g_HeadPosAnim.z >  MAX_LEAN_Z)  g_HeadPosAnim.z =  MAX_LEAN_Z;
		if (g_HeadPosAnim.z < -MAX_LEAN_Z)  g_HeadPosAnim.z = -MAX_LEAN_Z;
	}

	//headPos->z = centeredSigmoid(g_HeadPosAnim.z) * MAX_LEAN_Z;
	headPos->z = g_HeadPosAnim.z;
	headPos->z = -headPos->z; // The z-axis is inverted in XWA w.r.t. the original view-centric definition
}

void ProcessKeyboard(__int16 keycodePressed) {
	static bool bLastLKeyState = false, bCurLKeyState = false;

	//bool bControl = *s_XwaIsControlKeyPressed;
	//bool bShift = *s_XwaIsShiftKeyPressed;
	//bool bAlt = *s_XwaIsAltKeyPressed;
	bool bCtrl		= GetAsyncKeyState(VK_CONTROL);
	bool bShift		= GetAsyncKeyState(VK_SHIFT);
	bool bAlt		= GetAsyncKeyState(VK_MENU);
	bool bRightAlt	= GetAsyncKeyState(VK_RMENU);
	bool bLeftAlt	= GetAsyncKeyState(VK_LMENU);
	// L Key:
	bLastLKeyState = bCurLKeyState;
	bCurLKeyState = GetAsyncKeyState(VK_L_KEY);
	//log_debug("L: %d, ACS: %d,%d,%d", bLKey, bAlt, bCtrl, bShift);

	if (bCtrl && bLastLKeyState && !bCurLKeyState)
	{
		log_debug("*********** RELOADING CockpitLookHook.cfg ***********");
		LoadParams();
	}

	if (bAlt || bCtrl) {
		g_bLeftKeyDown = false;
		g_bRightKeyDown = false;
		g_bUpKeyDown = false;
		g_bDownKeyDown = false;
		g_bUpKeyDownShift = false;
		g_bDownKeyDownShift = false;
		return;
	}
	
	if (bShift) {
		// No Alt, No Ctrl, Shift Pressed
		//g_bUpKeyDownShift = (keycodePressed == KeyCode_ARROWUP);
		g_bUpKeyDownShift = GetAsyncKeyState(VK_UP);
		//g_bDownKeyDownShift = (keycodePressed == KeyCode_ARROWDOWN);
		g_bDownKeyDownShift = GetAsyncKeyState(VK_DOWN);
		g_bUpKeyDown = false;
		g_bDownKeyDown = false;
		return;
	}

	// No Alt, No Ctrl, No Shift
	//g_bLeftKeyDown = (keycodePressed == KeyCode_ARROWLEFT);
	g_bLeftKeyDown = GetAsyncKeyState(VK_LEFT);
	//g_bRightKeyDown = (keycodePressed == KeyCode_ARROWRIGHT);
	g_bRightKeyDown = GetAsyncKeyState(VK_RIGHT);
	//g_bUpKeyDown = (keycodePressed == KeyCode_ARROWUP);
	g_bUpKeyDown = GetAsyncKeyState(VK_UP);
	//g_bDownKeyDown = (keycodePressed == KeyCode_ARROWDOWN);
	g_bDownKeyDown = GetAsyncKeyState(VK_DOWN);
	g_bUpKeyDownShift = false;
	g_bDownKeyDownShift = false;
	g_bResetHeadCenter = (keycodePressed == KeyCode_PERIOD);
	if (keycodePressed == KeyCode_CAPSLOCK)
		g_bToggleKeyboardCaps = !g_bToggleKeyboardCaps;
	
	//log_debug("keycode: 0x%X, ACS: %d,%d,%d", keycodePressed, *s_XwaIsAltKeyPressed, *s_XwaIsControlKeyPressed, *s_XwaIsShiftKeyPressed);
}

/*
 * Update headPos using the keyboard
 */
void ComputeCockpitLean(Vector3 *headPos)
{
	if (g_bResetHeadCenter) {
		g_HeadPosAnim = { 0 };
	}
	
	// Perform the lean left/right etc animations
	animTickX(headPos);
	animTickY(headPos);
	animTickZ(headPos);
}

/*******************************************************************/

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
	int playerIndex = params[-10]; // Using -10 instead of -6, prevents this hook from crashing in Multiplayer
	float yaw = 0.0f, pitch = 0.0f;
	float yawSign = 1.0f, pitchSign = 1.0f;
	bool dataReady = false;
	bool bExternalCamera = PlayerDataTable[playerIndex].externalCamera;

	//XwaDIKeyboardUpdateShiftControlAltKeysPressedState();
	__int16 keycodePressed = *keyPressedAfterLocaleAfterMapping;	
	ProcessKeyboard(keycodePressed);

	if (//!PlayerDataTable[playerIndex].hyperspacePhase && // Enable mouse-look during hyperspace
		PlayerDataTable[playerIndex].cockpitDisplayed
		&& !PlayerDataTable[playerIndex].gunnerTurretActive
		&& PlayerDataTable[playerIndex].cockpitDisplayed2
		&& *numberOfPlayersInGame == 1)
	{
		// Keyboard code for moving the cockpit camera angle
		//__int16 keycodePressed = *keyPressedAfterLocaleAfterMapping;

		// Read tracking data.
		switch (g_TrackerType) 
		{
			case TRACKER_NONE:
			{
				Vector3 headPos;
				static float fake_yaw = 0.0f, fake_pitch = 0.0f;

				if (g_bResetHeadCenter) 
				{
					fake_yaw = fake_pitch = 0.0f;
					g_headCenter[0] = 0.0f;
					g_headCenter[1] = 0.0f;
					g_headCenter[2] = 0.0f;
					g_HeadPosAnim = { 0 };
				}

				if (!g_bToggleKeyboardCaps) {
					ComputeCockpitLean(&headPos);
					headPos = -headPos;
					
					//Vector4 pos(headPos.x, headPos.y, headPos.z, 1.0f);
					//g_headPos = (pos - g_headCenter);
					g_headCenter.x = headPos.x;
					g_headCenter.y = headPos.y;
					g_headCenter.z = headPos.z;
				} else {
					if (g_bLeftKeyDown)  fake_yaw -= 1.0f;
					if (g_bRightKeyDown) fake_yaw += 1.0f;
					if (g_bDownKeyDown)  fake_pitch -= 1.0f;
					if (g_bUpKeyDown)	 fake_pitch += 1.0f;
				}
				yaw   = fake_yaw;
				pitch = fake_pitch;
				g_headPos = g_headCenter;
				// Apply the head's position directly if mouse look is enabled
				if (*mouseLook && !*inMissionFilmState && !*viewingFilmState && g_bKeyboardLean) {
					Vector4 Rs, Us, Fs;
					Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(Rs, Us, Fs, true);
					g_headPos = HeadingMatrix * g_headPos;
					PlayerDataTable->cockpitXReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[0]);
					PlayerDataTable->cockpitYReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[1]);
					PlayerDataTable->cockpitZReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[2]);
					dataReady = false;
				} 
				else {
					// If mouse look is disabled, then change the orientation and position of the camera
					dataReady = true;
				}

				// Debug: Write the fake yaw/pitch and cockpit lean to FreePIE to fake a headset
				if (g_FreePIEOutputSlot > -1) {
					g_FreePIEData.yaw   = fake_yaw;
					g_FreePIEData.pitch = fake_pitch;
					g_FreePIEData.roll  = 0.0f;
					g_FreePIEData.x =  g_headPos.x;
					g_FreePIEData.y =  g_headPos.y;
					g_FreePIEData.z = -g_headPos.z;
					WriteFreePIE(g_FreePIEOutputSlot);
				}
			}
			break;

			case TRACKER_FREEPIE:
			{
				//Vector3 headPosFromKeyboard(0,0,0);
				//if (!g_bToggleKeyboardCaps) {
				if (g_bKeyboardLean) {
					ComputeCockpitLean(&g_headPosFromKeyboard);
					g_headPosFromKeyboard = -g_headPosFromKeyboard;
				}

				// The Z-axis should now be inverted because of XWA's coord system
				pitchSign = -1.0f;
				if (ReadFreePIE(g_iFreePIESlot)) {
					if (g_bResetHeadCenter) {
						g_headCenter[0] =  g_FreePIEData.x;
						g_headCenter[1] =  g_FreePIEData.y;
						g_headCenter[2] = -g_FreePIEData.z;
					}
					yaw    = g_FreePIEData.yaw   * g_fYawMultiplier;
					pitch  = g_FreePIEData.pitch * g_fPitchMultiplier;

					if (g_bYawPitchFromMouseOverride) {
						// If FreePIE could not be read, then get the yaw/pitch from the mouse:
						yaw   =  (float)PlayerDataTable[0].cockpitCameraYaw   / 32768.0f * 180.0f;
						pitch = -(float)PlayerDataTable[0].cockpitCameraPitch / 32768.0f * 180.0f;
					}

					Vector4 pos(g_FreePIEData.x, g_FreePIEData.y, -g_FreePIEData.z, 1.0f);
					g_headPos = (pos - g_headCenter);
					
					// Old code:
					//yaw   = g_FreePIEData.yaw   * g_fYawMultiplier;
					//pitch = g_FreePIEData.pitch * g_fPitchMultiplier;
					dataReady = true;
				}
			}
			break;

			case TRACKER_STEAMVR: 
			{
				float x, y, z;
				
				if (g_bKeyboardLean) {
					ComputeCockpitLean(&g_headPosFromKeyboard);
					g_headPosFromKeyboard = -g_headPosFromKeyboard;
				}

				dataReady = GetSteamVRPositionalData(&yaw, &pitch, &x, &y, &z);
				// We need to invert the Z-axis because of XWA's coordinate system.
				z = -z;

				// HACK ALERT: I'm reading the positional tracking data from FreePIE when
				// running SteamVR because setting up the PSMoveServiceSteamVRBridge is kind
				// of... tricky; and I'm not going to bother right now since PSMoveService
				// already works very well for me.
				// Read the positional data from FreePIE if the right flag is set
				if (g_bSteamVRPosFromFreePIE) {
					ReadFreePIE(g_iFreePIESlot);
					x =  g_FreePIEData.x;
					y =  g_FreePIEData.y;
					z = -g_FreePIEData.z;
				}

				yaw      *= RAD_TO_DEG * g_fYawMultiplier;
				pitch    *= RAD_TO_DEG * g_fPitchMultiplier;
				yawSign   = -1.0f;
				if (g_bResetHeadCenter) {
					g_headCenter[0] = x;
					g_headCenter[1] = y;
					g_headCenter[2] = z;
				}
				Vector4 pos(x, y, z, 1.0f);
				g_headPos = (pos - g_headCenter);
			}
			break;

			case TRACKER_TRACKIR:
			{
				float x, y, z;
				// These numbers were determined empirically by ual002:
				const float scale_x = -0.0002f;
				const float scale_y =  0.0002f;
				const float scale_z = -0.0002f;
				
				if (g_bKeyboardLean) {
					ComputeCockpitLean(&g_headPosFromKeyboard);
					g_headPosFromKeyboard = -g_headPosFromKeyboard;
				}

				if (ReadTrackIRData(&yaw, &pitch, &x, &y, &z)) {
					x		 *= scale_x;
					y		 *= scale_y;
					z		 *= scale_z;
					yaw		 *= g_fYawMultiplier;
					pitch	 *= g_fPitchMultiplier;
					yawSign   = -1.0f; 
					pitchSign = -1.0f;

					if (g_bFlipYZAxes) {
						float temp = y; y = z; z = temp;
					}

					if (g_bResetHeadCenter) {
						g_headCenter[0] = x;
						g_headCenter[1] = y;
						g_headCenter[2] = z;
					}
					Vector4 pos(x, y, z, 1.0f);
					g_headPos = (pos - g_headCenter);

					dataReady = true;
				}
			}
			break;
		}

		// The offset is applied after the tracking data is read, regardless of the tracker.
		if (dataReady) {
			yaw   += g_fYawOffset;
			pitch += g_fPitchOffset;
			while (yaw   < 0.0f) yaw   += 360.0f;
			while (pitch < 0.0f) pitch += 360.0f;

			//if (!bExternalCamera) {
				PlayerDataTable[playerIndex].cockpitCameraYaw   = (short)(yawSign   * yaw   / 360.0f * 65535.0f);
				PlayerDataTable[playerIndex].cockpitCameraPitch = (short)(pitchSign * pitch / 360.0f * 65535.0f);

				g_headPos[0] = g_headPos[0] * g_fPosXMultiplier + g_headPosFromKeyboard[0];
				g_headPos[1] = g_headPos[1] * g_fPosYMultiplier + g_headPosFromKeyboard[1];
				g_headPos[2] = g_headPos[2] * g_fPosZMultiplier + g_headPosFromKeyboard[2];

				// Limits clamping
				if (g_headPos[0] < g_fMinPositionX) g_headPos[0] = g_fMinPositionX;
				if (g_headPos[1] < g_fMinPositionY) g_headPos[1] = g_fMinPositionY;
				if (g_headPos[2] < g_fMinPositionZ) g_headPos[2] = g_fMinPositionZ;

				if (g_headPos[0] > g_fMaxPositionX) g_headPos[0] = g_fMaxPositionX;
				if (g_headPos[1] > g_fMaxPositionY) g_headPos[1] = g_fMaxPositionY;
				if (g_headPos[2] > g_fMaxPositionZ) g_headPos[2] = g_fMaxPositionZ;

				// For some reason it looks like we don't need to compensate for yaw/pitch
				// here (as opposed to doing it in ddraw), applying the translation directly seems 
				// to work fine... Maybe because the frame's perspective is computed after this 
				// point (i.e. we're at the beginning of the frame), whereas in ddraw we're at the
				// end of the frame (?)

				Vector4 Rs, Us, Fs;
				Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(Rs, Us, Fs, true);
				g_headPos = HeadingMatrix * g_headPos;
				PlayerDataTable->cockpitXReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[0]);
				PlayerDataTable->cockpitYReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[1]);
				PlayerDataTable->cockpitZReference = (int)(g_fXWAUnitsToMetersScale * g_headPos[2]);
			/*} else {
				PlayerDataTable[playerIndex].cameraYaw   = (short)(yawSign   * yaw   / 360.0f * 65535.0f);
				PlayerDataTable[playerIndex].cameraPitch = (short)(pitchSign * pitch / 360.0f * 65535.0f);
			}*/
		}

		// TODO: I changed the arrow keys for numpad keys because the keys would move the
		//       view and I want to use them for something else. I need to validate that this
		//		 change didn't actually break the code.
		//if (*win32NumPad4Pressed || keycodePressed == KeyCode_ARROWLEFT)
		if (*win32NumPad4Pressed || keycodePressed == KeyCode_NUMPAD3)
			PlayerDataTable[playerIndex].cockpitCameraYaw -= 1200;

		//if (*win32NumPad6Pressed || keycodePressed == KeyCode_ARROWRIGHT)
		if (*win32NumPad6Pressed || keycodePressed == KeyCode_NUMPAD6)
			PlayerDataTable[playerIndex].cockpitCameraYaw += 1200;

		//if (*win32NumPad8Pressed || keycodePressed == KeyCode_ARROWDOWN)
		if (*win32NumPad8Pressed || keycodePressed == KeyCode_NUMPAD8)
			PlayerDataTable[playerIndex].cockpitCameraPitch += 1200;

		//if (*win32NumPad2Pressed || keycodePressed == KeyCode_ARROWUP)
		if (*win32NumPad2Pressed || keycodePressed == KeyCode_NUMPAD2)
			PlayerDataTable[playerIndex].cockpitCameraPitch -= 1200;

		if (*win32NumPad5Pressed || keycodePressed == KeyCode_NUMPAD5 || keycodePressed == KeyCode_PERIOD)
		{
			if (!bExternalCamera) {
				// Reset cockpit camera
				PlayerDataTable[playerIndex].cockpitCameraYaw = 0;
				PlayerDataTable[playerIndex].cockpitCameraPitch = 0;
			} else {
				// Reset external camera
				PlayerDataTable[playerIndex].cameraYaw   = 0;
				PlayerDataTable[playerIndex].cameraPitch = 0;
			}
			//cockpitRefX = 0;
			//cockpitRefY = 0;
			//cockpitRefZ = 0;
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
					//if (PlayerDataTable->hyperspacePhase)
					//	log_debug("[DBG] [Mouse] [Hyper]: %d, %d", *mouseLook_X, *mouseLook_Y);

					if (abs(*mouseLook_X) > 85 || abs(*mouseLook_Y) > 85)
					{
						char _mouseLookInverted = *mouseLookInverted;

						if (!bExternalCamera) {
							PlayerDataTable[playerIndex].cockpitCameraYaw += 40 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].cockpitCameraPitch +=  40 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].cockpitCameraPitch += -40 * _mouseLook_Y;
						}
						else {
							PlayerDataTable[playerIndex].cameraYaw += 40 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].cameraPitch +=  40 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].cameraPitch += -40 * _mouseLook_Y;
						}
					}
					else
					{
						char _mouseLookInverted = *mouseLookInverted;

						if (!bExternalCamera) {
							PlayerDataTable[playerIndex].cockpitCameraYaw += 15 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].cockpitCameraPitch += 15 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].cockpitCameraPitch += -15 * _mouseLook_Y;
						}
						else {
							PlayerDataTable[playerIndex].cameraYaw += 15 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].cameraPitch +=  15 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].cameraPitch += -15 * _mouseLook_Y;
						}
					}
				}
			}

			if (*mouseLookResetPosition)
			{
				PlayerDataTable[playerIndex].cockpitCameraYaw = 0;
				PlayerDataTable[playerIndex].cockpitCameraPitch = 0;
				PlayerDataTable[playerIndex].cameraYaw = 0;
				PlayerDataTable[playerIndex].cameraPitch = 0;
			}
			if (*mouseLookWasNotEnabled)
				*mouseLookWasNotEnabled = 0;
		}
	}
	
//out:
	g_bResetHeadCenter = false;

	params[-1] = 0x4F9C33;
	return 0;
}

/* Load the cockpitlook.cfg file */
void LoadParams() {
	FILE *file;
	int error = 0;

	try {
		error = fopen_s(&file, "./CockpitLook.cfg", "rt");
	}
	catch (...) {
		log_debug("Could not load cockpitlook.cfg");
	}

	if (error != 0) {
		log_debug("Error %d when loading cockpitlook.cfg", error);
		return;
	}

	char buf[160], param[80], svalue[80];
	float fValue;
	while (fgets(buf, 160, file) != NULL) {
		// Skip comments and blank lines
		if (buf[0] == ';' || buf[0] == '#')
			continue;
		if (strlen(buf) == 0)
			continue;

		if (sscanf_s(buf, "%s = %s", param, 80, svalue, 80) > 0) {
			fValue = (float )atof(svalue);
			if (_stricmp(param, TRACKER_TYPE) == 0) {
				if (_stricmp(svalue, TRACKER_TYPE_FREEPIE) == 0) {
					log_debug("Using FreePIE for tracking");
					g_TrackerType = TRACKER_FREEPIE;
				}
				else if (_stricmp(svalue, TRACKER_TYPE_STEAMVR) == 0) {
					log_debug("Using SteamVR for tracking");
					g_TrackerType = TRACKER_STEAMVR;
				}
				else if (_stricmp(svalue, TRACKER_TYPE_TRACKIR) == 0) {
					log_debug("Using TrackIR for tracking");
					g_TrackerType = TRACKER_TRACKIR;
				}
				else if (_stricmp(svalue, TRACKER_TYPE_NONE) == 0) {
					log_debug("Tracking disabled");
					g_TrackerType = TRACKER_NONE;
				}
			}
			else if (_stricmp(param, YAW_MULTIPLIER) == 0) {
				g_fYawMultiplier = fValue;
				log_debug("Yaw multiplier: %0.3f", g_fYawMultiplier);
			}
			else if (_stricmp(param, PITCH_MULTIPLIER) == 0) {
				g_fPitchMultiplier = fValue;
				log_debug("Pitch multiplier: %0.3f", g_fPitchMultiplier);
			}
			else if (_stricmp(param, YAW_OFFSET) == 0) {
				g_fYawOffset = fValue;
				log_debug("Yaw offset: %0.3f", g_fYawOffset);
			}
			else if (_stricmp(param, PITCH_OFFSET) == 0) {
				g_fPitchOffset = fValue;
				log_debug("Pitch offset: %0.3f", g_fPitchOffset);
			}
			else if (_stricmp(param, FREEPIE_SLOT) == 0) {
				g_iFreePIESlot = (int )fValue;
				log_debug("FreePIE slot: %d", g_iFreePIESlot);
			}

			// Extra parameters to enable positional tracking
			else if (_stricmp(param, "yaw_pitch_from_mouse_override") == 0) {
				g_bYawPitchFromMouseOverride = (bool)fValue;
			}
			else if (_stricmp(param, POS_X_MULTIPLIER_VRPARAM) == 0) {
				g_fPosXMultiplier = fValue;
			}
			else if (_stricmp(param, POS_Y_MULTIPLIER_VRPARAM) == 0) {
				g_fPosYMultiplier = fValue;
			}
			else if (_stricmp(param, POS_Z_MULTIPLIER_VRPARAM) == 0) {
				g_fPosZMultiplier = fValue;
			}

			else if (_stricmp(param, MIN_POSITIONAL_X_VRPARAM) == 0) {
				g_fMinPositionX = fValue;
			}
			else if (_stricmp(param, MAX_POSITIONAL_X_VRPARAM) == 0) {
				g_fMaxPositionX = fValue;
			}
			else if (_stricmp(param, MIN_POSITIONAL_Y_VRPARAM) == 0) {
				g_fMinPositionY = fValue;
			}
			else if (_stricmp(param, MAX_POSITIONAL_Y_VRPARAM) == 0) {
				g_fMaxPositionY = fValue;
			}
			else if (_stricmp(param, MIN_POSITIONAL_Z_VRPARAM) == 0) {
				g_fMinPositionZ = fValue;
			}
			else if (_stricmp(param, MAX_POSITIONAL_Z_VRPARAM) == 0) {
				g_fMaxPositionZ = fValue;
			}
			else if (_stricmp(param, "steamvr_pos_from_freepie") == 0) {
				g_bSteamVRPosFromFreePIE = (bool)fValue;
			}
			else if (_stricmp(param, "xwa_units_to_meters_scale") == 0) {
				g_fXWAUnitsToMetersScale = fValue;
			}
			else if (_stricmp(param, "flip_yz_axes") == 0) {
				g_bFlipYZAxes = (bool)fValue;
			}
			// Cockpit Lean
			else if (_stricmp(param, "keyboard_lean") == 0) {
				g_bKeyboardLean = (bool)fValue;
			}
			else if (_stricmp(param, "sticky_lean") == 0) {
				g_bStickyArrowKeys = (bool)fValue;
			}
			else if (_stricmp(param, "limit_cockpit_lean") == 0) {
				g_bLimitCockpitLean = (bool)fValue;
			}

			else if (_stricmp(param, "cockpit_lean_x_limit") == 0) {
				MAX_LEAN_X = fValue;
			}
			else if (_stricmp(param, "cockpit_lean_y_limit") == 0) {
				MAX_LEAN_Y = fValue;
			}
			else if (_stricmp(param, "cockpit_lean_z_limit") == 0) {
				MAX_LEAN_Z = fValue;
			}
			else if (_stricmp(param, "invert_cockpit_lean_y") == 0) {
				g_bInvertCockpitLeanY = (bool)fValue;
			}

			else if (_stricmp(param, "write_5dof_to_freepie_slot") == 0) {
				g_FreePIEOutputSlot = (int)fValue;
				if (g_FreePIEOutputSlot != -1) {
					log_debug("Writing 5dof to slot %d", g_FreePIEOutputSlot);
					InitFreePIE();
				}
			}
			

		}
	} // while ... read file
	fclose(file);
}

void InitKeyboard()
{
	GetAsyncKeyState(VK_LEFT);
	GetAsyncKeyState(VK_RIGHT);
	GetAsyncKeyState(VK_UP);
	GetAsyncKeyState(VK_DOWN);
	GetAsyncKeyState(VK_SHIFT);
	GetAsyncKeyState(VK_CONTROL);
	GetAsyncKeyState(VK_MENU); // Alt Key
	GetAsyncKeyState(VK_RMENU);
	GetAsyncKeyState(VK_LMENU);
	GetAsyncKeyState(VK_L_KEY);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD uReason, LPVOID lpReserved)
{
	switch (uReason)
	{
	case DLL_PROCESS_ATTACH:
		log_debug("Cockpit Hook Loaded");
		g_hWnd = GetForegroundWindow();
		// Load cockpitlook.cfg here and enable FreePIE, SteamVR or TrackIR
		LoadParams();
		InitKeyboard();
		log_debug("Parameters loaded");
		switch (g_TrackerType)
		{
		case TRACKER_FREEPIE:
			InitFreePIE();
			break;
		case TRACKER_STEAMVR:
			InitSteamVR();
			if (g_bSteamVRPosFromFreePIE)
				InitFreePIE();
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
		log_debug("Unloading Cockpitlook hook");
		switch (g_TrackerType) {
		case TRACKER_FREEPIE:
			ShutdownFreePIE();
			break;
		case TRACKER_STEAMVR:
			// We can't shutdown SteamVR twice: we either shut it down here, or in ddraw.dll.
			// It looks like the right order is to shut it down here.
			ShutdownSteamVR();
			if (g_bSteamVRPosFromFreePIE)
				ShutdownFreePIE();
			break;
		case TRACKER_TRACKIR:
			ShutdownTrackIR();
			break;
		case TRACKER_NONE:
			if (g_FreePIEOutputSlot != -1)
				ShutdownFreePIE();
			break;
		}
		log_debug("Exiting Cockpitlook hook");
		break;
	}

	return TRUE;
}