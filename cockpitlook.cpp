/*
 * Copyright 2019, Justagai.
 * Extended for VR by Leo Reyes, 2019, 2020.
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
#include "UDP.h"
#include "Telemetry.h"
#include "SharedMem.h"

// TrackIR requires an HWND to register, so let's keep track of one.
HWND g_hWnd = NULL;

extern bool g_bSteamVRInitialized;

// The hooks are loaded before ddraw, so we can create the shared memory handle here
SharedMem g_SharedMem(true);
char shared_msg[80] = "message from the CokpitLookHook";
vr::TrackedDevicePose_t g_hmdPose;
float g_fRoll = 0;


#define DEBUG_TO_FILE 1
//#undef DEBUG_TO_FILE

#ifdef DEBUG_TO_FILE
FILE *g_DebugFile = NULL;
#endif

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
	sprintf_s(out, 300, "[DBG] [Cockpitlook] %s\n", buf);
	OutputDebugString(out);
#ifdef DEBUG_TO_FILE
	if (g_DebugFile != NULL) {
		fprintf(g_DebugFile, "%s\n", buf);
		fflush(g_DebugFile);
	}
#endif

	va_end(args);
}

bool g_bGlobalDebug = false;

/*
 * HYPERSPACE variables
 */
enum HyperspacePhaseEnum {
	HS_INIT_ST = 0,				// Initial state, we're not even in Hyperspace
	HS_HYPER_ENTER_ST = 1,		// We're entering hyperspace
	HS_HYPER_TUNNEL_ST = 2,		// Traveling through the blue Hyperspace tunnel
	HS_HYPER_EXIT_ST = 3,		// HyperExit streaks are being rendered
};
HyperspacePhaseEnum g_HyperspacePhaseFSM = HS_INIT_ST;
bool g_bHyperspaceFirstFrame = false, g_bInHyperspace = false, g_bHyperspaceLastFrame = false, g_bHyperspaceTunnelLastFrame = false;
int g_iHyperspaceFrame = -1;
Vector4 g_LastFsBeforeHyperspace;
Matrix4 g_prevHeadingMatrix;
float g_fLastSpeedBeforeHyperspace = 0.0f;

/*
 * Updates the hyperspace FSM. This is a "lightweight" version of the code in ddraw.
 * Here, we mostly care about how to handle cockpit inertia when we jump into hyperspace.
 * The problem is that XWA will snap the camera when entering hyperspace, and that will
 * cause this hook to miscalculate the inertia on that frame. We need to ignore the frame
 * where the head snaps since ddraw will restore the previous camera orientation on the next
 * frame.
 */
void UpdateHyperspaceState(int playerIndex) {
	// Reset the Hyperspace FSM regardless of the previous state. This helps reset the
	// state if we quit on the middle of a movie that is playing back the hyperspace
	// effect. If we do reset the FSM, we need to update the control variables too:
	if (PlayerDataTable[playerIndex].hyperspacePhase == 0) {
		g_bInHyperspace = false;
		g_bHyperspaceLastFrame = (g_HyperspacePhaseFSM == HS_HYPER_EXIT_ST);
		g_iHyperspaceFrame = -1;
		g_HyperspacePhaseFSM = HS_INIT_ST;
		/*if (g_bHyperspaceLastFrame) {
			log_debug("yaw,pitch at hyper exit: %0.3f, %0.3f",
				PlayerDataTable[playerIndex].yaw / 65536.0f * 360.0f,
				PlayerDataTable[playerIndex].pitch / 65536.0f * 360.0f);
		}*/
	}

	switch (g_HyperspacePhaseFSM) {
	case HS_INIT_ST:
		g_bInHyperspace = false;
		g_bHyperspaceFirstFrame = false;
		g_bHyperspaceTunnelLastFrame = false;
		//g_bHyperspaceLastFrame = false; // No need to update this here, we do it at the beginning of this function
		g_iHyperspaceFrame = -1;
		if (PlayerDataTable[playerIndex].hyperspacePhase == 2) {
			// Hyperspace has *just* been engaged. Save the current cockpit camera heading so we can restore it
			g_bHyperspaceFirstFrame = true;
			g_bInHyperspace = true;
			g_iHyperspaceFrame = 0;
			g_HyperspacePhaseFSM = HS_HYPER_ENTER_ST;
		}
		break;
	case HS_HYPER_ENTER_ST:
		g_bInHyperspace = true;
		g_bHyperspaceFirstFrame = false;
		g_bHyperspaceTunnelLastFrame = false;
		g_bHyperspaceLastFrame = false;
		g_iHyperspaceFrame++;
		if (PlayerDataTable[playerIndex].hyperspacePhase == 4)
			g_HyperspacePhaseFSM = HS_HYPER_TUNNEL_ST;
		break;
	case HS_HYPER_TUNNEL_ST:
		g_bInHyperspace = true;
		g_bHyperspaceFirstFrame = false;
		g_bHyperspaceTunnelLastFrame = false;
		g_bHyperspaceLastFrame = false;
		if (PlayerDataTable[playerIndex].hyperspacePhase == 3) {
			//log_debug("[DBG] [FSM] HS_HYPER_TUNNEL_ST --> HS_HYPER_EXIT_ST");
			g_bHyperspaceTunnelLastFrame = true;
			//g_bInHyperspace = true;
			g_HyperspacePhaseFSM = HS_HYPER_EXIT_ST;
			/*log_debug("yaw,pitch at hyper tunnel exit: %0.3f, %0.3f",
				PlayerDataTable[playerIndex].yaw / 65536.0f * 360.0f,
				PlayerDataTable[playerIndex].pitch / 65536.0f * 360.0f);
			*/
		}
		break;
	case HS_HYPER_EXIT_ST:
		g_bInHyperspace = true;
		g_bHyperspaceFirstFrame = false;
		g_bHyperspaceTunnelLastFrame = false;
		g_bHyperspaceLastFrame = false;
		// If we're playing back a film, we may stop the movie while in hyperspace. In
		// that case, we need to "hard-reset" the FSM to its initial state as soon as
		// we see hyperspacePhase == 0 or we'll mess up the state. However, that means
		// that the final transition must also be done at the beginning of this
		// function
		/*
		if (PlayerDataTable[playerIndex].hyperspacePhase == 0) {
			log_debug("[DBG] [FSM] HS_HYPER_EXIT_ST --> HS_INIT_ST");
			g_bInHyperspace = false;
			g_bHyperspaceLastFrame = true;
			log_debug("g_bHyperspaceLastFrame <- true");
			g_iHyperspaceFrame = -1;
			g_HyperspacePhaseFSM = HS_INIT_ST;
		}
		*/
		
		break;
	}
}

void LoadParams();

// cockpitlook.cfg parameter names
const char *TRACKER_TYPE				= "tracker_type"; // Defines which tracker to use
const char *TRACKER_TYPE_FREEPIE		= "FreePIE"; // Use FreePIE as the tracker
const char *TRACKER_TYPE_STEAMVR		= "SteamVR"; // Use SteamVR as the tracker
const char *TRACKER_TYPE_TRACKIR		= "TrackIR"; // Use TrackIR (or OpenTrack) as the tracker
const char *TRACKER_TYPE_NONE			= "None";
const char *POSE_CORRECTED_HEADTRACKING = "pose_corrected_headtracking";
const char *YAW_MULTIPLIER				= "yaw_multiplier";
const char *PITCH_MULTIPLIER			= "pitch_multiplier";
const char *YAW_OFFSET					= "yaw_offset";
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
const float DEFAULT_ROLL_MULTIPLIER = 1.0f;
const float DEFAULT_YAW_OFFSET = 0.0f;
const float DEFAULT_PITCH_OFFSET = 0.0f;
const int   DEFAULT_FREEPIE_SLOT = 0;
// See https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
// for a full list of virtual key codes
const int   VK_I_KEY = 0x49; // Ctrl+I is used to toggle cockpit inertia
const int   VK_J_KEY = 0x4a; // Ctrl+J is used to reload the cockpitlook.cfg params
const int	VK_T_KEY = 0x54;
const int	VK_U_KEY = 0x55;
//const int   VK_K_KEY = 0x4b;
//const int   VK_L_KEY = 0x4c;
const int VK_X_KEY = 0x58; // Ctrl+X is used to dump debug info
bool g_bNumPadAdd = false, g_bNumPadSub = false, g_bCtrl = false;
bool g_bNumPad7 = false, g_bNumPad9 = false;

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
float g_fRollMultiplier = DEFAULT_ROLL_MULTIPLIER;
float g_fYawOffset		 = DEFAULT_YAW_OFFSET;
float g_fPitchOffset	 = DEFAULT_PITCH_OFFSET;
int   g_iFreePIESlot	 = DEFAULT_FREEPIE_SLOT;
bool  g_bYawPitchFromMouseOverride = false;
bool  g_bKeyboardLean = false, g_bKeyboardLook = false;
bool  g_bTestJoystick = true;
Vector4 g_headCenter(0, 0, 0, 0), g_headPos(0, 0, 0, 0), g_headRotationHome(0, 0, 0, 0);
Vector3 g_headPosFromKeyboard(0, 0, 0);
Vector4 g_prevFs(0, 0, 1, 0);
int g_FreePIEOutputSlot = -1;
bool g_bTrackIRLoaded = false;

const auto XwaGetConnectedJoysticksCount = (int(*)())0x00541030;

/*********************************************************************/
/* Cockpit Inertia													 */
/*********************************************************************/
bool g_bCockpitInertiaEnabled = false, g_bExtInertiaEnabled = false;
float g_fCockpitInertia = 0.35f, g_fCockpitSpeedInertia = 0.005f, g_fExtDistInertia = 0.0f;
float g_fCockpitMaxInertia = 0.2f, g_fExtInertia = -16384.0f, g_fExtMaxInertia = 0.025f;
short g_externalTilt = -1820; // -10 degrees
// tilt = degrees * 32768 / 180

/*********************************************************************/
/*	Code used to enable leaning in the cockpit with the arrow keys   */
/*********************************************************************/
// In XWA, the Y+ axis points forward (towards the horizon) and Z+ points up.
// To make this consistent with regular PixelShader coords, we need to swap these
// coordinates using a rotation and reflection. The following matrix stores that
// transformation and is only used in GetCurrentHeadingMatrix().
Matrix4 g_ReflRotX;

void InitHeadingMatrix() {
	/*
	Matrix4 rotX, refl;
	rotX.identity();
	rotX.rotateX(90.0f);
	refl.set(
		1,  0, 0, 0,
		0, -1, 0, 0,
		0,  0, 1, 0,
		0,  0, 0, 1
	);
	g_ReflRotX = refl * rotX;
	*/
	
	g_ReflRotX.set(
		1.0, 0.0, 0.0, 0.0, // 1st column
		0.0, 0.0, 1.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	);
	
}

inline float clamp(float x, const float lowerlimit, const float upperlimit) {
	if (x < lowerlimit) x = lowerlimit; else if (x > upperlimit) x = upperlimit;
	return x;
}

inline float smoothstep(const float min, const float max, float x) {
	// Scale, bias and saturate x to 0..1 range
	x = clamp((x - min) / (max - min), 0.0f, 1.0f);
	// Evaluate polynomial
	return x * x * (3.0f - 2.0f * x);
}

/*
 * Compute the current ship's orientation. Returns:
 * Rs: The "Right" vector in global coordinates
 * Us: The "Up" vector in global coordinates
 * Fs: The "Forward" vector in global coordinates
 * A viewMatrix that maps [Rs, Us, Fs] to the major [X, Y, Z] axes
 */
Matrix4 GetCurrentHeadingMatrix(int playerIndex, Vector4 &Rs, Vector4 &Us, Vector4 &Fs, bool invert = false)
{
	const float DEG2RAD = 3.141593f / 180;
	float yaw, pitch, roll;
	Matrix4 rotMatrixFull, rotMatrixYaw, rotMatrixPitch, rotMatrixRoll;
	Vector4 T, B, N;
	// Compute the full rotation
	yaw   = PlayerDataTable[playerIndex].Camera.CraftYaw / 65536.0f * 360.0f;
	pitch = PlayerDataTable[playerIndex].Camera.CraftPitch / 65536.0f * 360.0f;
	roll  = PlayerDataTable[playerIndex].Camera.CraftRoll  / 65536.0f * 360.0f;

	// yaw-pitch-roll gets reset to: ypr: 0.000, 90.000, 0.000 when entering hyperspace
	/*if (!g_bInHyperspace)
		log_debug("ypr: %0.3f, %0.3f, %0.3f", yaw, pitch, roll);
	else
		log_debug("[H] ypr: %0.3f, %0.3f, %0.3f", yaw, pitch, roll);*/

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
	Fs = g_ReflRotX * N;
	Us = g_ReflRotX * B;
	Rs = g_ReflRotX * T;
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

	// Store data for the next frame if we're not in hyperspace
	if (!g_bInHyperspace || g_HyperspacePhaseFSM == HS_HYPER_EXIT_ST) {
		g_LastFsBeforeHyperspace = Fs;
		g_prevHeadingMatrix = viewMatrix;
		g_fLastSpeedBeforeHyperspace = (float)PlayerDataTable[playerIndex].currentSpeed;
	}
	return viewMatrix;
}

/*
 * Computes cockpit inertia
 * Input: The current Heading matrix H, the current forward vector Fs
 * Output: The X,Y displacement
 */
void ComputeInertia(const Matrix4 &H, Vector4 Fs, float fCurSpeed, int playerIndex, float *XDisp, float *YDisp, float *ZDisp) {
	static bool bFirstFrame = true;
	static float fLastSpeed = 0.0f;
	static time_t prevT = 0;
	time_t curT = time(NULL);
	bool InertiaEnabled = g_bCockpitInertiaEnabled || g_bExtInertiaEnabled;
	// Reset the first frame if the time between successive queries is too big: this
	// implies the game was either paused or a new mission was loaded
	bFirstFrame = curT - prevT > 2; // Reset if +2s have elapsed
	// Skip the very first frame: there's no inertia to compute yet
	if (bFirstFrame || !InertiaEnabled || g_bHyperspaceTunnelLastFrame || g_bHyperspaceLastFrame)
	{
		bFirstFrame = false;
		*XDisp = *YDisp = *ZDisp = 0.0f;
		//log_debug("Resetting X/Y/ZDisp");
		// Update the previous heading vectors
		g_prevFs = Fs;
		prevT = curT;
		fLastSpeed = fCurSpeed;
		return;
	}

	Matrix4 HT = H;
	HT.transpose();
	// Multiplying the current Rs, Us, Fs with H will yield the major axes:
	//Vector4 X = HT * Rs; // --> always returns [1, 0, 0]
	//Vector4 Y = HT * Us; // --> always returns [0, 1, 0]
	//Vector4 Z = HT * Fs; // --> always returns [0, 0, 1]
	//log_debug("[DBG] X: [%0.3f, %0.3f, %0.3f], Y: [%0.3f, %0.3f, %0.3f], Z: [%0.3f, %0.3f, %0.3f]",
	//	X.x, X.y, X.z, Y.x, Y.y, Y.z, Z.x, Z.y, Z.z);

	//Vector4 X = HT * g_prevRs; // --> returns something close to [1, 0, 0]
	//Vector4 Y = HT * g_prevUs; // --> returns something close to [0, 1, 0]
	Vector4 Z = HT * g_prevFs; // --> returns something close to [0, 0, 1]
	//log_debug("[DBG] X: [%0.3f, %0.3f, %0.3f], Y: [%0.3f, %0.3f, %0.3f], Z: [%0.3f, %0.3f, %0.3f]",
	//	X.x, X.y, X.z, Y.x, Y.y, Y.z, Z.x, Z.y, Z.z);
	Vector4 curFs(0, 0, 1, 0);
	Vector4 diffZ = curFs - Z;

	*XDisp = g_fCockpitInertia * diffZ.x;
	*YDisp = g_fCockpitInertia * diffZ.y;
	//ZDisp can be g_fCockpitInertia * diffZ.z to compute roll; but let's do accel/decel instead
	*ZDisp = -g_fCockpitSpeedInertia * (fCurSpeed - fLastSpeed);
	if (*XDisp < -g_fCockpitMaxInertia) *XDisp = -g_fCockpitMaxInertia; else if (*XDisp > g_fCockpitMaxInertia) *XDisp = g_fCockpitMaxInertia;
	if (*YDisp < -g_fCockpitMaxInertia) *YDisp = -g_fCockpitMaxInertia; else if (*YDisp > g_fCockpitMaxInertia) *YDisp = g_fCockpitMaxInertia;
	if (*ZDisp < -g_fCockpitMaxInertia) *ZDisp = -g_fCockpitMaxInertia; else if (*ZDisp > g_fCockpitMaxInertia) *ZDisp = g_fCockpitMaxInertia;

	// Update the previous heading smoothly, otherwise the cockpit may shake a bit
	g_prevFs = 0.1f * Fs + 0.9f * g_prevFs;
	fLastSpeed = 0.1f * fCurSpeed + 0.9f * fLastSpeed;
	prevT = curT;

	if (g_HyperspacePhaseFSM == HS_HYPER_EXIT_ST || g_bHyperspaceLastFrame) 
	{
		*ZDisp = 0.0f;
		fLastSpeed = fCurSpeed;
	}

	//if (g_HyperspacePhaseFSM == HS_HYPER_ENTER_ST || g_HyperspacePhaseFSM == HS_INIT_ST)
	//if (g_bHyperspaceLastFrame || g_bHyperspaceTunnelLastFrame)
	//	log_debug("[%d] X/YDisp: %0.3f, %0.3f",  g_iHyperspaceFrame, *XDisp, *YDisp);
}

/*
 * Takes a [yaw,pitch] "linear" inertia vector and returns a smooth transition between 0 at
 * the origin and 1 near the edges.
 */
void SmoothInertia(float *inout_yawInertia, float *inout_pitchInertia) 
{
	float yawInertia = *inout_yawInertia;
	float pitchInertia = *inout_pitchInertia;

	// First, compute the length of the inertia vector:
	float x, L = sqrt(yawInertia * yawInertia + pitchInertia * pitchInertia);
	// Normalize the [yawInertia, pitchInertia] vector, our vector is now unitary and lies
	// in a circle around the origin
	yawInertia /= L; pitchInertia /= L;
	// Normalize the range of L between 0 and +1. We'll clamp it to 1 using smoothstep below
	x = L / g_fExtMaxInertia;
	// Here, I'm dividing the smoothstep graph and taking the middle point to the right
	// so that we get a curve that starts linear and then tapers off towards 1. Formally,
	// I should multiply x by 0.5 below, but using 0.45 makes a nicer curve. See the following
	// link to visualize the curve we're using:
	// https://www.iquilezles.org/apps/graphtoy/?f1(x)=2.0%20*%20clamp(smoothstep(0,%201,%20x%20*%200.45%20+%200.5)%20-%200.5,%200.0,%201.0)
	L = g_fExtMaxInertia * 2.0f * clamp(smoothstep(0.0f, 1.0f, x * 0.45f + 0.5f) - 0.5f, 0.0, 1.0f);
	// The length of the vector now goes from 0 to g_fExtMaxInertia smoothly and tapers off
	// when approaching g_fExtMaxInertia. Our [yawInertia, pitchInertia] vector is still unitary
	// so we multiply it by the smooth L to extend it back to the right range:
	yawInertia *= L; pitchInertia *= L;

	*inout_yawInertia = yawInertia;
	*inout_pitchInertia = pitchInertia;
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
// float g_fXWAUnitsToMetersScale = 400.0f; // This value feels better
float g_fXWAUnitsToMetersScale = 25.0f; // New value to be applied in CockpitPositionReferenceHook
float g_fPosXMultiplier = 1.666f, g_fPosYMultiplier = 1.666f, g_fPosZMultiplier = 1.666f;
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

void DumpDebugInfo(int playerIndex) {
	static int counter = 0;
	FILE *filePD = NULL, *fileCI = NULL;
	int error = 0;
	char sFileNamePD[128], sFileNameCI[128];
	sprintf_s(sFileNamePD, 128, "./PlayerDataTable%d", counter);
	sprintf_s(sFileNameCI, 128, "./CraftInstance%d", counter);
	counter++;

	int16_t objectIndex = (int16_t)PlayerDataTable[*localPlayerIndex].objectIndex;
	ObjectEntry *object = &((*objects)[objectIndex]);
	MobileObjectEntry *mobileObject = object->MobileObjectPtr;
	CraftInstance *craftInstance = mobileObject->craftInstancePtr;

	log_debug("primarySecondaryArmed: %d, warheadArmed: %d", 
		PlayerDataTable[*localPlayerIndex].primarySecondaryArmed,
		PlayerDataTable[*localPlayerIndex].warheadArmed);

	//log_debug("activeWeapon: %d", PlayerDataTable[*localPlayerIndex].activeWeapon); // This was useless (it's always 1)
	log_debug("WarheadArmed: %d, NumberOfLaserSets: %d, NumberOfLasers: %d, NumWarheadLauncherGroups: %d, Countermeasures: %d", 
		PlayerDataTable[*localPlayerIndex].warheadArmed,
		craftInstance->NumberOfLaserSets,
		craftInstance->NumberOfLasers,
		craftInstance->NumWarheadLauncherGroups,
		craftInstance->CountermeasureAmount);

	//for (int i = 0; i < 2; i++) {
		// For the first warhead group: 
		// WarheadNextHardpoint[0] is 1 when the left tube is ready and 129 (-1 for signed byte?) when the right tube is ready.
		// For the second warhead group:
		// WarheadNextHardpoint[1] is 1 when the left tube is ready, 129 when the right tube is ready.
		//log_debug("WarheadNextHardpoint[%d]: %d", i, craftInstance->WarheadNextHardpoint[i]);
	//}

	/*
	LaserNextHardpoint tells us which laser cannon is ready to fire.
	LaserLinkStatus: 0x1 -- Single fire.
	LaserLinkStatus: 0x2 -- Dual fire. LaserHardpoint will now be either 0x0 or 0x1 and it will skip one laser
	LaserLinkStatus: 0x3 -- Fire all lasers in the current group. LaserHardpoint stays at 0x0 -- all cannons ready to fire
	LaserLinkStatus: 0x4 -- Fire all lasers in all groups. All LaserLinkStatus indices become 0x4 as soon as one of them is 0x4

	X/W, single fire:
	[808][DBG][Cockpitlook] WarheadArmed: 0, NumberOfLaserSets : 1, NumberOfLasers : 4, NumWarheadLauncherGroups : 1, Countermeasures : 0
	[808][DBG][Cockpitlook] LaserNextHardpoint[0] : 0x0,
	[808][DBG][Cockpitlook] LaserNextHardpoint[0] : 0x1,
	[808][DBG][Cockpitlook] LaserNextHardpoint[0] : 0x2,
	[808][DBG][Cockpitlook] LaserNextHardpoint[0] : 0x3, ... then it goes back to 0
	The other indices in LaserNextHardpoint remain at 0:
	[808] [DBG] [Cockpitlook] LaserNextHardpoint[1]: 0x0, <-- This is the second set of lasers/ions (when it exists)
	[808] [DBG] [Cockpitlook] LaserNextHardpoint[2]: 0x0, <-- Maybe the third set of lasers, if it exists?

	For the B-Wing the first LaserNextHardpoint group goes 0x0, 0x1, 0x2
	The second group (ions) goes 0x3, 0x4, 0x5.
	LaserLinkStatus is either 0x1 or 0x3. There's no dual fire
	The B-Wing has 6 lasers and 2 groups

	For the T/D the first LaserNextHardpoint group goes 0x0, 0x1, 0x2, 0x3
	The second group goes: 0x4, 0x5
	LaserLinkStatus goes 0x1, 0x2, 0x3 and 0x4. There's single fire, dual fire, set fire and all sets fire.
	The T/D has 6 lasers and 2 groups.

	*/

	for (int i = 0; i < 3; i++)
		log_debug("LaserNextHardpoint[%d]: 0x%x, LaserLinkStatus[%d]: 0x%x",
			i, craftInstance->LaserNextHardpoint[i],
			i, craftInstance->LaserLinkStatus[i]);

	/*
	MIS:
	[14848] [DBG] [Cockpitlook] NumberOfLaserSets: 1, NumberOfLasers: 1, NumWarheadLauncherGroups: 2
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[0]: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[1]: 1
	[14848] [DBG] [Cockpitlook] [0] Type: 1, Count: 0  <-- Only 1 laser
	[14848] [DBG] [Cockpitlook] [1] Type: 3, Count: 20 <-- First warhead group
	[14848] [DBG] [Cockpitlook] [2] Type: 3, Count: 20
	[14848] [DBG] [Cockpitlook] [3] Type: 3, Count: 20 <-- Second warhead group
	[14848] [DBG] [Cockpitlook] [4] Type: 3, Count: 20

	GUN:
	2 laser sets (lasers, ion), 4 lasers in total (2 lasers 2 ion)
	WeaponType 1 is laser, 2 is ion, 3 is missiles
	[14848] [DBG] [Cockpitlook] NumberOfLaserSets: 2, NumberOfLasers: 4, NumWarheadLauncherGroups: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[0]: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[1]: 1
	[14848] [DBG] [Cockpitlook] [0] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [1] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [2] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [3] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [4] Type: 3, Count: 8
	[14848] [DBG] [Cockpitlook] [5] Type: 3, Count: 8

	T/D:
	2 laser sets (lasers, ion), 6 lasers in total (4 lasers, 2 ion)
	[14848] [DBG] [Cockpitlook] NumberOfLaserSets: 2, NumberOfLasers: 6, NumWarheadLauncherGroups: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[0]: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[1]: 1
	[14848] [DBG] [Cockpitlook] [0] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [1] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [2] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [3] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [4] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [5] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [6] Type: 3, Count: 1
	[14848] [DBG] [Cockpitlook] [7] Type: 3, Count: 1

	B/W:
	[14848] [DBG] [Cockpitlook] NumberOfLaserSets: 2, NumberOfLasers: 6, NumWarheadLauncherGroups: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[0]: 1
	[14848] [DBG] [Cockpitlook] WarheadNextHardpoint[1]: 1
	[14848] [DBG] [Cockpitlook] [0] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [1] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [2] Type: 1, Count: 0 <-- Laser
	[14848] [DBG] [Cockpitlook] [3] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [4] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [5] Type: 2, Count: 0 <-- Ion
	[14848] [DBG] [Cockpitlook] [6] Type: 3, Count: 6
	[14848] [DBG] [Cockpitlook] [7] Type: 3, Count: 6
	*/

#ifdef DISPLAY_HARDPOINT_DEBUG_DATA
	for (int i = 0; i < 16; i++) {
		// WeaponType: 0 == None, 1 == Lasers? 3 == Concussion Missiles? 4 == Gunner hardpoint!
		// NOTE: Gunner hardpoint's energy level never depletes.
		if (craftInstance->Hardpoints[i].WeaponType == 0)
			break;
		log_debug("[%d] Type: %d, Count: %d, Energy: %d", i,
			craftInstance->Hardpoints[i].WeaponType,
			craftInstance->Hardpoints[i].Count, // This only seems to apply to warheads, for ions and lasers this is 0
			craftInstance->Hardpoints[i].Energy // Only applies for lasers, max is 127, min is 0. For warheads, this is always 127
		);
	}
#endif

	//log_debug("Throttle: %0.3f", (float)craftInstance->EngineThrottleInput / 65535.0f);

	// 0x0001 is the CMD/Targeting computer.
	// 0x000E is the laser/ion display. Looks like all 3 bits must be on, but not sure what happens if the craft doesn't have ions
	// 0x0010 is the beam weapon
	// 0x0020 is the shields display
	// 0x0040 is the throttle (text) display
	// 0x0180 both sensors. Both bits must be on, if either bit is off, both sensors will shut down
	// 0x0200 lasers recharge rate
	// 0x0400 engine level
	// 0x0800 shields recharge rate
	// 0x1000 beam recharge rate
	/*
	FILE *FileMask = NULL;
	fopen_s(&FileMask, "CockpitDamage.txt", "rt");
	if (FileMask != NULL) {
		uint32_t Mask = 0x0;
		fscanf_s(FileMask, "0x%x", &Mask);
		fclose(FileMask);

		log_debug("InitialCockpitInstruments: 0x%x, CockpitInstrumentStatus: 0x%x",
			craftInstance->InitialCockpitInstruments, craftInstance->CockpitInstrumentStatus);
		//log_debug("InitialSubsystems: 0x%x, SubsystemStatus: 0x%x",
		//	craftInstance->InitialSubsystems, craftInstance->SubsystemStatus);
		craftInstance->CockpitInstrumentStatus = craftInstance->InitialCockpitInstruments & Mask;
		log_debug("Current Cockpit Instruments: 0x%x", craftInstance->CockpitInstrumentStatus);
	}
	*/

	//log_debug("External Camera Distance: %d", PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist);
	//log_debug("Dumping Debug info...");

	// Dump the current PlayerDataTable and CraftInstance
	/*
	try {
		error = fopen_s(&filePD, sFileNamePD, "wb");
	}
	catch (...) {
		log_debug("Could not create %s", sFileNamePD);
	}
	if (error != 0)
		return;
	
	size = fwrite(&(PlayerDataTable[*localPlayerIndex]), sizeof(PlayerDataEntry), 1, filePD);
	fclose(filePD);
	log_debug("Dumped %s", sFileNamePD);

	try {
		error = fopen_s(&fileCI, sFileNameCI, "wb");
	}
	catch (...) {
		log_debug("Could not create %s", sFileNameCI);
	}
	if (error != 0)
		return;

	size = fwrite(craftInstance, sizeof(CraftInstance), 1, fileCI);
	fclose(fileCI);
	log_debug("Dumped %s", sFileNameCI);
	*/
}

void ProcessKeyboard(int playerIndex, __int16 keycodePressed) {
	static bool bLastIKeyState = false, bLastJKeyState = false, bLastXKeyState = false, bLastTKeyState = false, bLastUKeyState = false;
	static bool bCurIKeyState = false, bCurJKeyState = false, bCurXKeyState = false, bCurTKeyState = false, bCurUKeyState = false;

	//bool bControl = *s_XwaIsControlKeyPressed;
	//bool bShift = *s_XwaIsShiftKeyPressed;
	//bool bAlt = *s_XwaIsAltKeyPressed;
	g_bCtrl			= GetAsyncKeyState(VK_CONTROL);
	bool bShift		= GetAsyncKeyState(VK_SHIFT);
	bool bAlt		= GetAsyncKeyState(VK_MENU);
	bool bRightAlt	= GetAsyncKeyState(VK_RMENU);
	bool bLeftAlt	= GetAsyncKeyState(VK_LMENU);
	g_bNumPadAdd	= GetAsyncKeyState(VK_ADD);
	g_bNumPadSub	= GetAsyncKeyState(VK_SUBTRACT);
	g_bNumPad7		= GetAsyncKeyState(VK_NUMPAD7);
	g_bNumPad9		= GetAsyncKeyState(VK_NUMPAD9);
	// I,J,X,T key states:
	bLastIKeyState	= bCurIKeyState;
	bLastJKeyState	= bCurJKeyState;
	bLastXKeyState	= bCurXKeyState;
	bLastTKeyState	= bCurTKeyState;
	bLastUKeyState	= bCurUKeyState;
	bCurJKeyState	= GetAsyncKeyState(VK_J_KEY);
	bCurIKeyState	= GetAsyncKeyState(VK_I_KEY);
	bCurXKeyState	= GetAsyncKeyState(VK_X_KEY);
	bCurTKeyState	= GetAsyncKeyState(VK_T_KEY);
	bCurUKeyState	= GetAsyncKeyState(VK_U_KEY);

	//log_debug("L: %d, ACS: %d,%d,%d", bLKey, bAlt, bCtrl, bShift);

	// It's not a good idea to use Ctrl+L to reload the cockpit look hook settings because
	// Ctrl+L is already used by the landing gear hook. So, let's use a different key: J
	if (g_bCtrl && bLastJKeyState && !bCurJKeyState)
	{
		log_debug("*********** RELOADING CockpitLookHook.cfg ***********");
		LoadParams();
	}

	// Ctrl+X: Dump debug info
	if (g_bCtrl && bLastXKeyState && !bCurXKeyState) {
		DumpDebugInfo(playerIndex);
	}

	// Alt+T: Reload TrackIR
	if (g_TrackerType == TRACKER_TRACKIR)
	{
		if (bAlt && bLastTKeyState && !bCurTKeyState) {
			if (g_bTrackIRLoaded) {
				log_debug("Unloading TrackIR");
				ShutdownTrackIR();
			}
			else {
				log_debug("Reloading TrackIR");
				InitTrackIR();
			}
			g_bTrackIRLoaded = !g_bTrackIRLoaded;
		}
	}

	if (g_bCtrl && bLastIKeyState && !bCurIKeyState) {
		g_bCockpitInertiaEnabled = !g_bCockpitInertiaEnabled;
		g_bExtInertiaEnabled = !g_bExtInertiaEnabled;
	}

	if (bAlt || g_bCtrl) {
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
	float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
	float yawSign = 1.0f, pitchSign = 1.0f;
	bool dataReady = false, enableTrackedYawPitch = true;
	bool bExternalCamera = PlayerDataTable[playerIndex].Camera.ExternalCamera;
	static bool bLastExternalCamera = bExternalCamera;
	static short lastCameraYaw = 0, lastCameraPitch = 0; // These are the pre-inertia values from the last frame
	static int lastCameraDist = 1024;
	static short prevCameraYaw = 0, prevCameraPitch = 0; // These are the post-inertia values from the last frame
	static int prevCameraDist = 1024;
	float yawInertia = 0.0f, pitchInertia = 0.0f, distInertia = 0.0f;
	/*static bool bFirstFrame = true;
	if (bFirstFrame) {
		log_debug("External Dist: %d", PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist);
		bFirstFrame = false;
	}*/

	/*
	// DEBUG: This wlll update the message on every frame
	static int index = 0;
	shared_msg[0] = 'A' + index;
	index = (index + 1) % 10;
	*/

	// For some reason, TrackIR won't load if the game is run from the launcher. So, let's
	// try to reload TrackIR here.
	if (g_TrackerType == TRACKER_TRACKIR) {
		static int TrackIRRetries = 3;
		if (TrackIRRetries > 0 && !g_bTrackIRLoaded) {
			log_debug("TrackIR wasn't loaded, retrying...");
			g_bTrackIRLoaded = InitTrackIR();
			TrackIRRetries--;
		}
	}

	if (g_bUDPEnabled) SendXWADataOverUDP();

	// Restore the position of the external camera if external inertia is enabled.
	if (bExternalCamera) 
	{
		//log_debug("------------------");
		//log_debug("yaw,pitch: %d, %d", PlayerDataTable[playerIndex].Camera.Yaw, PlayerDataTable[playerIndex].Camera.Pitch);
		// Detect changes to the camera performed between calls to this hook:
		short yawDiff   = PlayerDataTable[playerIndex].Camera.Yaw - prevCameraYaw;
		short pitchDiff = PlayerDataTable[playerIndex].Camera.Pitch - prevCameraPitch;
		int   distDiff  = PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist - prevCameraDist;
		//log_debug("diff: %d, %d", yawDiff, pitchDiff);
		// Adjust the pre-inertia yaw/pitch if the camera moved between calls to this hook. This adjustment
		// is only possible if prevCameraYaw/Pitch is valid (that is, if the previous frame was rendered in
		// external camera view)
		if (bLastExternalCamera) 
		{
			lastCameraYaw   += yawDiff;
			lastCameraPitch += pitchDiff;
			lastCameraDist  += distDiff;
		}
		//log_debug("lastCamera (1): %d, %d", lastCameraYaw, lastCameraPitch);

		// Restore the position of the camera before adding external view inertia
		//if (g_bExtInertiaEnabled) 
		{
			PlayerDataTable[playerIndex].Camera.Yaw = lastCameraYaw;
			PlayerDataTable[playerIndex].Camera.Pitch = lastCameraPitch;
			PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = lastCameraDist;
		}
	}

	//XwaDIKeyboardUpdateShiftControlAltKeysPressedState();
	__int16 keycodePressed = *keyPressedAfterLocaleAfterMapping;	
	ProcessKeyboard(playerIndex, keycodePressed);

	// Update the Hyperspace FSM
	UpdateHyperspaceState(playerIndex);

	// This hook "works" for MP; but unfortunately, code in XWA keeps trying to sync all the mouse
	// look parameters, so that makes MP unplayable. We won't be able to enable this hook in MP
	// until the MP networking code has been translated/fixed to allow per-player mouse look params
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
					if (g_bKeyboardLook) {
						if (g_bLeftKeyDown)  fake_yaw -= 1.0f;
						if (g_bRightKeyDown) fake_yaw += 1.0f;
						if (g_bDownKeyDown)  fake_pitch -= 1.0f;
						if (g_bUpKeyDown)	 fake_pitch += 1.0f;
					}
				}

				if (g_bKeyboardLook) {
					yaw   = fake_yaw;
					pitch = fake_pitch;
				}

				if (g_bKeyboardLean)
					g_headPos = g_headCenter;
				else
					g_headPos.set(0, 0, 0, 0);
				
				// Mouse Look is enabled, apply the head's position right here
				if (*mouseLook && !*inMissionFilmState && !*viewingFilmState) {
					Vector4 Rs, Us, Fs;
					Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(playerIndex, Rs, Us, Fs, true);
					if (g_bCockpitInertiaEnabled || g_bExtInertiaEnabled) {
						float XDisp = 0.0f, YDisp = 0.0f, ZDisp = 0.0f;
						if (g_bInHyperspace)
							ComputeInertia(g_prevHeadingMatrix, g_LastFsBeforeHyperspace, g_fLastSpeedBeforeHyperspace, playerIndex, &XDisp, &YDisp, &ZDisp);
						else {
							//log_debug("Fs: %0.3f, %0.3f, %0.3f", Fs.x, Fs.y, Fs.z);
							ComputeInertia(HeadingMatrix, Fs, (float)PlayerDataTable[playerIndex].currentSpeed, playerIndex, &XDisp, &YDisp, &ZDisp);
						}
						// Apply the current POVOffset
						g_headPos.x += g_SharedData.POVOffsetX;
						g_headPos.y += g_SharedData.POVOffsetY;
						g_headPos.z += g_SharedData.POVOffsetZ;
						// Apply inertia:
						if (g_bCockpitInertiaEnabled) {
							g_headPos.x += XDisp;
							g_headPos.y += YDisp;
							g_headPos.z += ZDisp;
						}

						yawInertia = XDisp; pitchInertia = YDisp; distInertia = ZDisp;
					}

					//if (g_bInHyperspace)
					//	g_headPos = g_prevHeadingMatrix * g_headPos;
					//else
					//	g_headPos = HeadingMatrix * g_headPos;
					// TRACKER_NONE path, when mouse look is enabled
					//PlayerDataTable[playerIndex].Camera.ShakeX = (int)(g_fXWAUnitsToMetersScale * g_headPos.x);
					//PlayerDataTable[playerIndex].Camera.ShakeY = (int)(g_fXWAUnitsToMetersScale * g_headPos.y);
					//PlayerDataTable[playerIndex].Camera.ShakeZ = (int)(g_fXWAUnitsToMetersScale * g_headPos.z);
					dataReady = false;
				} 
				else if (!*mouseLook) {
					// If mouse look is disabled, then change the orientation and position of the camera
					// using the regular path -- but only if the internal keyboard override is set! Otherwise
					// we'll break the Joystick POV hat/Keypad look!
					if (g_bKeyboardLook)
						dataReady = true;
					else {
						// mouseLook is off and keyboardlook is disabled, apply cockpit inertia here.
						// I'm sure this section and the above can be refactored...
						Vector4 Rs, Us, Fs;
						Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(playerIndex, Rs, Us, Fs, true);
						if (g_bCockpitInertiaEnabled || g_bExtInertiaEnabled) {
							float XDisp = 0.0f, YDisp = 0.0f, ZDisp = 0.0f;
							if (g_bInHyperspace)
								ComputeInertia(g_prevHeadingMatrix, g_LastFsBeforeHyperspace, g_fLastSpeedBeforeHyperspace, playerIndex, &XDisp, &YDisp, &ZDisp);
							else {
								//log_debug("Fs: %0.3f, %0.3f, %0.3f", Fs.x, Fs.y, Fs.z);
								ComputeInertia(HeadingMatrix, Fs, (float)PlayerDataTable[playerIndex].currentSpeed, playerIndex, &XDisp, &YDisp, &ZDisp);
							}
							// Apply the current POVOffset
							g_headPos.x += g_SharedData.POVOffsetX;
							g_headPos.y += g_SharedData.POVOffsetY;
							g_headPos.z += g_SharedData.POVOffsetZ;
							// Apply inertia:
							if (g_bCockpitInertiaEnabled) {
								g_headPos.x += XDisp;
								g_headPos.y += YDisp;
								g_headPos.z += ZDisp;
							}
							
							yawInertia = XDisp; pitchInertia = YDisp; distInertia = ZDisp;
						}

						//if (g_bInHyperspace)
						//	g_headPos = g_prevHeadingMatrix * g_headPos;
						//else
						//	g_headPos = HeadingMatrix * g_headPos;
						//// TRACKER_NONE path, when mouse look is disabled
						//PlayerDataTable[playerIndex].Camera.ShakeX = (int)(g_fXWAUnitsToMetersScale * g_headPos.x);
						//PlayerDataTable[playerIndex].Camera.ShakeY = (int)(g_fXWAUnitsToMetersScale * g_headPos.y);
						//PlayerDataTable[playerIndex].Camera.ShakeZ = (int)(g_fXWAUnitsToMetersScale * g_headPos.z);
					}
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
				else
					g_headPosFromKeyboard.set(0, 0, 0);

				// The Z-axis should now be inverted because of XWA's coord system
				pitchSign = -1.0f;
				if (ReadFreePIE(g_iFreePIESlot)) {
					if (g_bResetHeadCenter) {
						g_headRotationHome.y = g_FreePIEData.yaw;
						g_headRotationHome.x = g_FreePIEData.pitch;
						g_headRotationHome.z = g_FreePIEData.roll;
						g_headCenter.x =  g_FreePIEData.x;
						g_headCenter.y =  g_FreePIEData.y;
						g_headCenter.z = -g_FreePIEData.z;
					}
					yaw    = (g_FreePIEData.yaw   - g_headRotationHome.y) * g_fYawMultiplier;
					pitch  = (g_FreePIEData.pitch - g_headRotationHome.x) * g_fPitchMultiplier;

					if (g_bYawPitchFromMouseOverride) {
						// If FreePIE could not be read, then get the yaw/pitch from the mouse:
						yaw   =  (float)PlayerDataTable[playerIndex].MousePositionX   / 32768.0f * 180.0f;
						pitch = -(float)PlayerDataTable[playerIndex].MousePositionY / 32768.0f * 180.0f;
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
				else
					g_headPosFromKeyboard.set(0, 0, 0);

				dataReady = GetSteamVRPositionalData(&yaw, &pitch, &roll, &x, &y, &z);
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
				roll     *= RAD_TO_DEG * g_fRollMultiplier;
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
				
				if (g_bGlobalDebug) log_debug("[TrackIR] Case start");
				if (g_bKeyboardLean) {
					ComputeCockpitLean(&g_headPosFromKeyboard);
					g_headPosFromKeyboard = -g_headPosFromKeyboard;
				}

				/*
				 * TrackIR is a bit special. If TrackIR is installed; but turned off, then
				 * ReadTrackIRData will return false; but if we want to apply cockpit inertia
				 * then we need to set dataReady = true. However, setting dataReady = true will
				 * also write the yaw/pitch, which will disable the POV/Keypad when TrackIR is
				 * off. So, we need another flag (enableTrackedYawPitch) to prevent writing to
				 * yaw/pitch; but allow writing to cockpitX/Y/ZReference.
				 * If TrackIR is on, we allow writing to yaw/pitch and cockpitX/Y/ZReference.
				 * This will disable the POV hat; but you probably don't need it if you're using
				 * your head to look around.
				 */
				if (g_bGlobalDebug) log_debug("[TrackIR] Reading TrackIR data");
				if (ReadTrackIRData(&yaw, &pitch, &x, &y, &z)) {
					if (g_bGlobalDebug) log_debug("[TrackIR] Data read, (%0.3f, %0.3f), (%0.3f, %0.3f, %0.3f)",
						yaw, pitch, x, y, z);
					x		 *=  scale_x;
					y		 *=  scale_y;
					z		 *=  scale_z;
					yaw		 *=  g_fYawMultiplier;
					pitch	 *=  g_fPitchMultiplier;
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
					enableTrackedYawPitch = true;
				}
				else {
					if (g_bGlobalDebug) log_debug("[TrackIR] Data read failed. g_headPos <- (0,0,0,0)");
					g_headPos.set(0, 0, 0, 0);
					enableTrackedYawPitch = false;
				}
				dataReady = true;
				
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
				// I think the following two lines will reset the yaw/pitch when using they keypad/POV hat to
				// look around
				if (enableTrackedYawPitch) {
					PlayerDataTable[playerIndex].MousePositionX   = (short)(yawSign   * yaw   / 360.0f * 65535.0f);
					PlayerDataTable[playerIndex].MousePositionY = (short)(pitchSign * pitch / 360.0f * 65535.0f);
					// Save roll value to use later in another hooked function
					g_fRoll = (short)(roll / 360.0f * 65535.0f);
				}

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
				Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(playerIndex, Rs, Us, Fs, true);
				if (g_bCockpitInertiaEnabled || g_bExtInertiaEnabled) {
					float XDisp = 0.0f, YDisp = 0.0f, ZDisp = 0.0f;
					if (g_bInHyperspace)
						ComputeInertia(g_prevHeadingMatrix, g_LastFsBeforeHyperspace, g_fLastSpeedBeforeHyperspace, playerIndex, &XDisp, &YDisp, &ZDisp);
					else
						ComputeInertia(HeadingMatrix, Fs, (float)PlayerDataTable[playerIndex].currentSpeed, playerIndex, &XDisp, &YDisp, &ZDisp);
					// Apply the current POVOffset
					g_headPos.x += g_SharedData.POVOffsetX;
					g_headPos.y += g_SharedData.POVOffsetY;
					g_headPos.z += g_SharedData.POVOffsetZ;
					// Apply inertia:
					if (g_bCockpitInertiaEnabled) {
						g_headPos.x += XDisp;
						g_headPos.y += YDisp;
						g_headPos.z += ZDisp;
					}

					yawInertia = XDisp; pitchInertia = YDisp; distInertia = ZDisp;
				}

				//if (g_bInHyperspace)
				//	g_headPos = g_prevHeadingMatrix * g_headPos;
				//else
					// It is not necessary to apply the headingmatrix transformation when applying the positional offset
					// in CockpitPositionTransform instead of Shake. 
					//g_headPos = HeadingMatrix * g_headPos;

				// MOVED TO PlayerCameraUpdateHook() to avoid using Camera.Shake
				// Regular path: write the X,Y,Z position from g_headPos (when tracking is on).
				//PlayerDataTable[playerIndex].Camera.ShakeX = (int)(g_fXWAUnitsToMetersScale * g_headPos.x);
				//PlayerDataTable[playerIndex].Camera.ShakeY = (int)(g_fXWAUnitsToMetersScale * g_headPos.y);
				//PlayerDataTable[playerIndex].Camera.ShakeZ = (int)(g_fXWAUnitsToMetersScale * g_headPos.z);

			/*} else {
				PlayerDataTable[playerIndex].Camera.Yaw   = (short)(yawSign   * yaw   / 360.0f * 65535.0f);
				PlayerDataTable[playerIndex].Camera.Pitch = (short)(pitchSign * pitch / 360.0f * 65535.0f);
			}*/
		}

		int moveDelta = *(int *)0x005AA000 >> 1;
		if (g_bTestJoystick && XwaGetConnectedJoysticksCount() == 0)
		{
			switch (keycodePressed)
			{
			case KeyCode_NUMPAD4:
			case KeyCode_NUMPAD6:
			case KeyCode_NUMPAD8:
			case KeyCode_NUMPAD2:
			case KeyCode_NUMPAD5:
				keycodePressed = 0;
				break;
			}
		}

		// TODO: I changed the arrow keys for numpad keys because the keys would move the
		//       view and I want to use them for something else. I need to validate that this
		//		 change didn't actually break the code.
		//if (*win32NumPad4Pressed || keycodePressed == KeyCode_ARROWLEFT)
		// NUMPAD1,NUMPAD3 rolls the craft; but it doesn't affect the yaw/pitch
		if (*win32NumPad4Pressed || keycodePressed == KeyCode_NUMPAD4)
			PlayerDataTable[playerIndex].MousePositionX -= moveDelta; // This was 1200 previously

		//if (*win32NumPad6Pressed || keycodePressed == KeyCode_ARROWRIGHT)
		if (*win32NumPad6Pressed || keycodePressed == KeyCode_NUMPAD6)
			PlayerDataTable[playerIndex].MousePositionX += moveDelta;

		//if (*win32NumPad8Pressed || keycodePressed == KeyCode_ARROWDOWN)
		if (*win32NumPad8Pressed || keycodePressed == KeyCode_NUMPAD8)
			PlayerDataTable[playerIndex].MousePositionY += moveDelta;

		//if (*win32NumPad2Pressed || keycodePressed == KeyCode_ARROWUP)
		if (*win32NumPad2Pressed || keycodePressed == KeyCode_NUMPAD2)
			PlayerDataTable[playerIndex].MousePositionY -= moveDelta;

		if (*win32NumPad5Pressed || keycodePressed == KeyCode_NUMPAD5 || keycodePressed == KeyCode_PERIOD)
		{
			if (!bExternalCamera) {
				// Reset cockpit camera
				PlayerDataTable[playerIndex].MousePositionX = 0;
				PlayerDataTable[playerIndex].MousePositionY = 0;
			} else {
				// Reset external camera
				PlayerDataTable[playerIndex].Camera.Yaw   = 0;
				PlayerDataTable[playerIndex].Camera.Pitch = 0;
			}
			//cockpitRefX = 0;
			//cockpitRefY = 0;
			//cockpitRefZ = 0;
		}

		// Use NumPad9 and NumPad7 to zoom in/out the external camera
		if (bExternalCamera) {
			// These limits (80, 8192) were observed empirically.
			if (g_bNumPad9 && PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist > 80) {
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist -= 16;
				if (PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist < 80)
					PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = 80;
			}

			if (g_bNumPad7 && PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist < 8192) {
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist += 16;
				if (PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist > 8192)
					PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = 8192;
			}
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
					//if (PlayerDataTable[playerIndex].hyperspacePhase)
					//	log_debug("[DBG] [Mouse] [Hyper]: %d, %d", *mouseLook_X, *mouseLook_Y);

					if (abs(*mouseLook_X) > 85 || abs(*mouseLook_Y) > 85)
					{
						char _mouseLookInverted = *mouseLookInverted;

						if (!bExternalCamera) {
							PlayerDataTable[playerIndex].MousePositionX += 40 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].MousePositionY +=  40 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].MousePositionY += -40 * _mouseLook_Y;
						}
						else {
							PlayerDataTable[playerIndex].Camera.Yaw += 40 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].Camera.Pitch +=  40 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].Camera.Pitch += -40 * _mouseLook_Y;
						}
					}
					else
					{
						char _mouseLookInverted = *mouseLookInverted;

						if (!bExternalCamera) {
							PlayerDataTable[playerIndex].MousePositionX += 15 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].MousePositionY += 15 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].MousePositionY += -15 * _mouseLook_Y;
						}
						else {
							PlayerDataTable[playerIndex].Camera.Yaw += 15 * *mouseLook_X;
							if (_mouseLookInverted)
								PlayerDataTable[playerIndex].Camera.Pitch +=  15 * _mouseLook_Y;
							else
								PlayerDataTable[playerIndex].Camera.Pitch += -15 * _mouseLook_Y;
						}
					}
				}
			}

			if (*mouseLookResetPosition)
			{
				PlayerDataTable[playerIndex].MousePositionX = 0;
				PlayerDataTable[playerIndex].MousePositionY = 0;
				PlayerDataTable[playerIndex].Camera.Yaw = 0;
				PlayerDataTable[playerIndex].Camera.Pitch = 0;
			}
			if (*mouseLookWasNotEnabled)
				*mouseLookWasNotEnabled = 0;
		}

		// Apply External View Inertia
		if (bExternalCamera) 
		{
			// Save the current yaw/pitch before adding external view inertia, we'll restore these values the
			// next time we enter this hook
			lastCameraYaw   = PlayerDataTable[playerIndex].Camera.Yaw;
			lastCameraPitch = PlayerDataTable[playerIndex].Camera.Pitch;
			lastCameraDist  = PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist;
			//log_debug("lastCamera (2): %d, %d", lastCameraYaw, lastCameraPitch);

			SmoothInertia(&yawInertia, &pitchInertia);
			// Apply inertia
			if (g_bExtInertiaEnabled) {
				PlayerDataTable[playerIndex].Camera.Yaw = lastCameraYaw + (short)(yawInertia * g_fExtInertia);
				PlayerDataTable[playerIndex].Camera.Pitch = lastCameraPitch + (short)(pitchInertia * g_fExtInertia);
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = lastCameraDist + (int)(distInertia * g_fExtDistInertia);
			}
			else {
				PlayerDataTable[playerIndex].Camera.Yaw = lastCameraYaw;
				PlayerDataTable[playerIndex].Camera.Pitch = lastCameraPitch;
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = lastCameraDist;
			}

			// Add the tilt even if external inertia is off:
			PlayerDataTable[playerIndex].Camera.Pitch += g_externalTilt;
			// Save the final values for yaw/pitch -- this will help us detect changes performed in other places
			prevCameraYaw   = PlayerDataTable[playerIndex].Camera.Yaw;
			prevCameraPitch = PlayerDataTable[playerIndex].Camera.Pitch;
			prevCameraDist  = PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist;
			//log_debug("prevCamera: %d, %d", prevCameraYaw, prevCameraPitch);
			//log_debug("=================");
		}
	}
	else 
	{ 
		// The mouse look is disabled because the cockpit isn't displayed, the gunner turret is active, or
		// we're in multiplayer: let's update the yaw/pitch and tilt to avoid spinning out of control when
		// switching from no-cockpit to exterior view
		lastCameraYaw   = PlayerDataTable[playerIndex].Camera.Yaw;
		lastCameraPitch = PlayerDataTable[playerIndex].Camera.Pitch;
		lastCameraDist  = PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist;

		// Apply External View Inertia when the cockpit is not displayed
		if (bExternalCamera && *numberOfPlayersInGame == 1) 
		{
			float XDisp = 0.0f, YDisp = 0.0f, ZDisp = 0.0f;
			if (g_bInHyperspace)
				ComputeInertia(g_prevHeadingMatrix, g_LastFsBeforeHyperspace, g_fLastSpeedBeforeHyperspace, playerIndex, &XDisp, &YDisp, &ZDisp);
			else {
				Vector4 Rs, Us, Fs;
				Matrix4 HeadingMatrix = GetCurrentHeadingMatrix(playerIndex, Rs, Us, Fs, true);
				ComputeInertia(HeadingMatrix, Fs, (float)PlayerDataTable[playerIndex].currentSpeed, playerIndex, &XDisp, &YDisp, &ZDisp);
			}
			yawInertia = XDisp; pitchInertia = YDisp; distInertia = ZDisp;

			SmoothInertia(&yawInertia, &pitchInertia);
			// Apply the inertia
			if (g_bExtInertiaEnabled) {
				PlayerDataTable[playerIndex].Camera.Yaw = lastCameraYaw + (short)(yawInertia * g_fExtInertia);
				PlayerDataTable[playerIndex].Camera.Pitch = lastCameraPitch + (short)(pitchInertia * g_fExtInertia);
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = lastCameraDist + (int)(distInertia * g_fExtDistInertia);
			}
			else {
				PlayerDataTable[playerIndex].Camera.Yaw = lastCameraYaw;
				PlayerDataTable[playerIndex].Camera.Pitch = lastCameraPitch;
				PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist = lastCameraDist;
			}

			// Add the tilt even if external inertia is off:
			PlayerDataTable[playerIndex].Camera.Pitch += g_externalTilt;
		}
		prevCameraYaw   = PlayerDataTable[playerIndex].Camera.Yaw;
		prevCameraPitch = PlayerDataTable[playerIndex].Camera.Pitch;
		prevCameraDist  = PlayerDataTable[playerIndex].Camera.ExternalCameraZoomDist;
	}
	
//out:
	g_bResetHeadCenter = false;
	bLastExternalCamera = bExternalCamera;

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
			else if (_stricmp(param, POSE_CORRECTED_HEADTRACKING) == 0) {
				log_debug("Using pose corrected head tracking");
				g_bCorrectedHeadTracking = (bool)fValue;
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
			else if (_stricmp(param, "debug_mode") == 0) {
				g_bGlobalDebug = (bool)fValue;
			}
			// Cockpit Lean/Look
			else if (_stricmp(param, "keyboard_lean") == 0) {
				g_bKeyboardLean = (bool)fValue;
			}
			else if (_stricmp(param, "keyboard_look") == 0) {
				g_bKeyboardLook = (bool)fValue;
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
			else if (_stricmp(param, "cockpit_inertia_enabled") == 0) {
				g_bCockpitInertiaEnabled = (bool)fValue;
			}
			else if (_stricmp(param, "cockpit_inertia") == 0) {
				g_fCockpitInertia = fValue;
			}
			else if (_stricmp(param, "cockpit_max_inertia") == 0) {
				g_fCockpitMaxInertia = fValue;
			}
			else if (_stricmp(param, "cockpit_speed_inertia") == 0) {
				g_fCockpitSpeedInertia = fValue;
			}
			else if (_stricmp(param, "external_inertia_enabled") == 0) {
				g_bExtInertiaEnabled = (bool)fValue;
			}
			else if (_stricmp(param, "external_inertia") == 0) {
				g_fExtInertia = -fValue * 16384.0f;
			}
			else if (_stricmp(param, "external_max_inertia") == 0) {
				g_fExtMaxInertia = fValue;
			}
			else if (_stricmp(param, "external_dist_inertia") == 0) {
				g_fExtDistInertia = fValue;
			}
			else if (_stricmp(param, "external_tilt") == 0) {
				g_externalTilt = (short)(fValue * 32768.0f / 180.0f);
				log_debug("External tilt: %d", g_externalTilt);
			}
			
			else if (_stricmp(param, "write_5dof_to_freepie_slot") == 0) {
				g_FreePIEOutputSlot = (int)fValue;
				if (g_FreePIEOutputSlot != -1) {
					log_debug("Writing 5dof to slot %d", g_FreePIEOutputSlot);
					InitFreePIE();
				}
			}

			else if (_stricmp(param, "test_joystick") == 0) {
				g_bTestJoystick = (bool)fValue;
			}

			/*
			else if (_stricmp(param, "predicted_seconds_to_photons") == 0) {
				g_fPredictedSecondsToPhotons = fValue;
				log_debug("predicted_seconds_to_photons set to: %0.6f", g_fPredictedSecondsToPhotons);
			}
			*/

			// UDP settings
			else if (_stricmp(param, "UDP_telemetry_enabled") == 0) {
				g_bUDPEnabled = (bool)fValue;
				log_debug("[UDP] Telemetry Enabled: %d", g_bUDPEnabled);
			}
			else if (_stricmp(param, "UDP_telemetry_port") == 0) {
				g_iUDPPort = (int)fValue;
				log_debug("[UDP] Telemetry Port: %d", g_iUDPPort);
			}
			else if (_stricmp(param, "UDP_telemetry_server") == 0) {
				_snprintf_s(g_sUDPServer, 80, "%s", svalue);
				log_debug("[UDP] Telemetry Server: %s", g_sUDPServer);
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
	GetAsyncKeyState(VK_J_KEY);
	GetAsyncKeyState(VK_I_KEY);
	GetAsyncKeyState(VK_X_KEY);
	GetAsyncKeyState(VK_T_KEY);
	GetAsyncKeyState(VK_U_KEY);
	GetAsyncKeyState(VK_ADD);
	GetAsyncKeyState(VK_SUBTRACT);
	GetAsyncKeyState(VK_NUMPAD7);
	GetAsyncKeyState(VK_NUMPAD9);
}

void InitSharedMem() {
	SharedDataProxy *pSharedData = (SharedDataProxy *)g_SharedMem.GetMemoryPtr();
	if (pSharedData == nullptr) {
		log_debug("Could not load shared data ptr");
		return;
	}
	pSharedData->pSharedData = &g_SharedData;
	pSharedData->bDataReady = true;
}

/*int PlayerCameraUpdateHook(int* params)
{
	//log_debug("Running hooked PlayerCameraUpdate");
	
	int playerIndex = params[0];

	int(*PlayerCameraUpdate)(int) = (int(*)(int)) 0x004EE820;
	return PlayerCameraUpdate(params[0]);
}*/

int CockpitPositionTransformHook(int* params)
{
	//log_debug("Running hooked Vector3Transform in CockpitPositionTransformHook\n");
	
	Vector3_float* vec = (Vector3_float*)params[0];	
	
	// Recommended value for g_fXWAUnitsToMetersScale = 25.
	vec->x += (g_fXWAUnitsToMetersScale * g_headPos.x);
	vec->z += (g_fXWAUnitsToMetersScale * g_headPos.y);
	vec->y -= (g_fXWAUnitsToMetersScale * g_headPos.z);
	Vector3Transform( (Vector3_float*)vec, (XwaMatrix3x3*)params[1]);
	return 0;
}

int UpdateCameraTransformHook(int* params) {
	// This function will run when the engine tries to apply the Camera.Pitch to the transformation matrix
	// used later for 3D rendering. We can inject the roll after applying the Pitch (pitch->roll->yaw)
	// I have tried also to apply it before roll->pitch->yaw and pitch->yaw->roll, but this one seems 
	// to provide the correct result.
	// TODO: fix things that this breaks
	// - the sound (it enters some kind of loop)
	// - the targeting reticle is not anymore where it should be.

	void (*DoRotation)(int, int, int, __int16) = (void (*)(int, int, int, __int16)) 0x440E40;

	// Obtain the current rear-looking vector that we need to rotate around for applying roll
	int* g_objectTransformRear_X = (int*)0x910934;
	int* g_objectTransformRear_Y = (int*)0x910938;
	int* g_objectTransformRear_Z = (int*)0x910944;

	// Apply the expected Pitch rotation
	DoRotation(params[0], params[1], params[2], params[3]);

	DoRotation(*g_objectTransformRear_X, *g_objectTransformRear_Y, *g_objectTransformRear_Z, (short) -g_fRoll);
		
	return 0;
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
		log_debug("Parameters loaded");
		InitKeyboard();
		InitHeadingMatrix();
		// UDP Telemetry Initialization
		if (g_bUDPEnabled) {
			InitializeUDP();
			InitializeUDPSocket();
		}
		InitSharedMem();

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
			g_bTrackIRLoaded = InitTrackIR();
			break;
		}
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		log_debug("Unloading Cockpitlook hook");
		if (g_bUDPEnabled) CloseUDP();
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