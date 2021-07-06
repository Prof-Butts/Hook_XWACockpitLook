#include "SteamVR.h"
#include "SharedMem.h"

// The default predicted seconds to photons was previously 0.011 in Release 1.1.5.
// I'm not sure, but it looks like this may have caused some jittering issues for
// a number of players. Before that release, this value was set to 0, so let's use
// that again to see if that helps.
constexpr float DEFAULT_PREDICTED_SECONDS_TO_PHOTONS = 0.0f;
float g_fPredictedSecondsToPhotons = DEFAULT_PREDICTED_SECONDS_TO_PHOTONS;
float g_fVsyncToPhotons, g_fHMDDisplayFreq = 0;

void log_debug(const char *format, ...);
bool g_bSteamVRInitialized = false;
vr::IVRSystem *g_pHMD = NULL;
extern SharedMem g_SharedMem;
extern vr::TrackedDevicePose_t g_hmdPose;

bool InitSteamVR()
{
	log_debug("InitSteamVR()");
	vr::EVRInitError eError = vr::VRInitError_None;
	g_pHMD = vr::VR_Init(&eError, vr::VRApplication_Scene);
	log_debug("VR_Init --> g_pHMD: 0x%x, error: 0x%x", g_pHMD, eError);

	if (eError != vr::VRInitError_None)
	{
		g_pHMD = NULL;
		log_debug("Unable to init VR runtime: %s", vr::VR_GetVRInitErrorAsEnglishDescription(eError));
		return false;
	}
	log_debug("VR runtime loaded");

	g_bSteamVRInitialized = true;
	vr::TrackedDeviceIndex_t unDevice = vr::k_unTrackedDeviceIndex_Hmd;
	g_fVsyncToPhotons = g_pHMD->GetFloatTrackedDeviceProperty(unDevice, vr::ETrackedDeviceProperty::Prop_SecondsFromVsyncToPhotons_Float);
	g_fHMDDisplayFreq = g_pHMD->GetFloatTrackedDeviceProperty(unDevice, vr::ETrackedDeviceProperty::Prop_DisplayFrequency_Float);

	// Put the address of g_hmdPose in shared memory. We only need to do this once.
	// Setting bDataReady to true means that pDataPtr has been initialized to a valid
	// address.
	SharedData* pSharedData = (SharedData *)g_SharedMem.GetMemoryPtr();
	if (pSharedData != nullptr) {
		pSharedData->pDataPtr = &(g_hmdPose);
		pSharedData->bDataReady = true;
	}
	return true;
}

void ShutdownSteamVR() {
	log_debug("ShutdownSteamVR()");
	vr::VR_Shutdown();
	g_pHMD = NULL;
	log_debug("SteamVR shut down");
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

bool GetSteamVRPositionalData(float *yaw, float *pitch, float *x, float *y, float *z)
{
	if (g_pHMD == NULL) {
		log_debug("GetSteamVRPositional Data with g_pHMD = NULL");
		// Try to initialize once more
		if (!g_bSteamVRInitialized) {
			log_debug("Attempting SteamVR initialization again...");
			InitSteamVR();
			g_bSteamVRInitialized = true; // Don't try initializing again if this fails
		}
		if (g_pHMD == NULL)
			return false;
		log_debug("SteamVR initialized in the second attempt, continuing");
	}

	float roll;
	vr::TrackedDeviceIndex_t unDevice = vr::k_unTrackedDeviceIndex_Hmd;
	vr::Compositor_FrameTiming frametiming;
	frametiming.m_nSize = sizeof(vr::Compositor_FrameTiming);
	if (!g_pHMD->IsTrackedDeviceConnected(unDevice))
		return false;

	vr::VRControllerState_t state;
	if (g_pHMD->GetControllerState(unDevice, &state, sizeof(state)))
	{
		//vr::TrackedDevicePose_t trackedDevicePose;
		//vr::TrackedDevicePose_t trackedDevicePoseArray[vr::k_unMaxTrackedDeviceCount];
		vr::HmdMatrix34_t poseMatrix;
		vr::HmdQuaternionf_t q;
		//vr::ETrackedDeviceClass trackedDeviceClass = vr::VRSystem()->GetTrackedDeviceClass(unDevice);

		//vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0.029, &trackedDevicePose, 1);
		//vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0.042f, &g_hmdPose, 1);
		//vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, 0.011f, &g_hmdPose, 1);
		vr::VRCompositor()->GetFrameTiming(&frametiming);

		//g_fPredictedSecondsToPhotons = GetFrameTimingRemaining + (m_nNumMisPresented / Prop_DisplayFrequency_Float) + Prop_SecondsFromVsyncToPhotons_Float;
		g_fPredictedSecondsToPhotons = vr::VRCompositor()->GetFrameTimeRemaining() + frametiming.m_nNumVSyncsToFirstView / g_fHMDDisplayFreq + g_fVsyncToPhotons;
		//log_debug("[DBG][CockpitLook] g_fPredictedSecondsToPhotons = %f",g_fPredictedSecondsToPhotons);
		vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseSeated, g_fPredictedSecondsToPhotons, &g_hmdPose, 1);

		/* Get the last pose predicted for the current frame during WaitGetPoses for the last frame.
		   This should remove jitter although it may introduce some error due to the prediction when doing quick changes of velocity/direction.
		   Also, it removes the need to deal with prediction time calculations. All is handled by WaitGetPoses as part of running start algorithm.
		*/
		//vr::VRCompositor()->GetLastPoses(NULL, 0, trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount);
		//vr::VRCompositor()->GetLastPoses(trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount, NULL, 0);
		//vr::VRCompositor()->WaitGetPoses(trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount, NULL, 0);

		//if (trackedDevicePoseArray[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
		if (g_hmdPose.bPoseIsValid) {
			//poseMatrix = trackedDevicePoseArray[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking; // This matrix contains all positional and rotational data.
			poseMatrix = g_hmdPose.mDeviceToAbsoluteTracking; // This matrix contains all positional and rotational data.
			q = rotationToQuaternion(poseMatrix);
			quatToEuler(q, yaw, pitch, &roll);
			*x = poseMatrix.m[0][3];
			*y = poseMatrix.m[1][3];
			*z = poseMatrix.m[2][3];
			return true;
		}
		else
		{
			//log_debug("[DBG] HMD pose not valid");
			return false;
		}
	}
	return false;
}
