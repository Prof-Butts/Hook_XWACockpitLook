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
vr::IVRChaperone* g_pChaperone = NULL;
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
	g_pChaperone = vr::VRChaperone();

	return true;
}

void ShutdownSteamVR() {
	log_debug("ShutdownSteamVR()");
	vr::VR_Shutdown();
	g_pHMD = NULL;
	log_debug("SteamVR shut down");
}

void ResetZeroPose() {
	if (g_pChaperone == NULL)
		return;
	log_debug("***** Resetting SteamVR *****");
	g_pChaperone->ResetZeroPose(vr::TrackingUniverseSeated);
}


Matrix3 HmdMatrix34toMatrix3(const vr::HmdMatrix34_t& mat) {
	return Matrix3(
		mat.m[0][0], mat.m[1][0], mat.m[2][0],
		mat.m[0][1], mat.m[1][1], mat.m[2][1],
		mat.m[0][2], mat.m[1][2], mat.m[2][2]
	);
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

/* DEPRECATED, WE APPLY ROTATION MATRIX DIRECTLY INSTEAD
Formulas from https://www.geometrictools.com/Documentation/EulerAngles.pdf

The formulas are for Extrinsic convention, so we take the formulas for the rotation in inverse order
(see https://en.wikipedia.org/wiki/Davenport_chained_rotations#Conversion_between_intrinsic_and_extrinsic_rotations)

We should use ZYX to match the order of rotations in XWA (pitch->yaw->roll in ddraw). However, this convention has singularities
in yaw = +-90, with an inversion of the axes that are not acceptable.

An alternative YZX is used to move the singularity to roll=+-90 which is a much less pose, and behavior is similar to the original
matrix->quaternion->Euler functions.

*/
void rotMatrixToEuler(vr::HmdMatrix34_t pose, float* yaw, float* pitch, float* roll)
{
	float thetaX, thetaY, thetaZ;

/* Extrinsic ZYX */
	//if(pose.m[2][0] < 0.999f)
	//{
	//	if(pose.m[2][0] > -0.999f)
	//	{
	//		thetaY = asin(-pose.m[2][0]);
	//		thetaZ = atan2(pose.m[1][0], pose.m[0][0]);
	//		thetaX = atan2(pose.m[2][1], pose.m[2][2]);
	//	}
	//	else // r20 = -1
	//	{
	//		thetaY = PI / 2;
	//		thetaZ = -(atan2(-pose.m[1][2], pose.m[1][1]));
	//		thetaX = 0;
	//	}
	//}
	//else // r20 = +1
	//{
	//	thetaY = -PI / 2;
	//	thetaZ = atan2(-pose.m[1][2], pose.m[1][1]);
	//	thetaX = 0;
	//}

/* Extrinsic ZXY */
	//if(pose.m[2][1] < 0.999f)
	//{
	//	if(pose.m[2][1] > -0.999f)
	//	{
	//		thetaX = asin(pose.m[2][1]);
	//		thetaZ = atan2(-pose.m[0][1], pose.m[1][1]);
	//		thetaY = atan2(-pose.m[2][0], pose.m[2][2]);
	//	}
	//	else // r21 = -1
	//	{
	//		thetaX = -PI / 2;
	//		thetaZ = -(atan2(pose.m[0][2], pose.m[0][0]));
	//		thetaY = 0;
	//	}
	//}
	//else // r21 = +1
	//{
	//	thetaX = PI / 2;
	//	thetaZ = atan2(pose.m[0][2], pose.m[0][0]);
	//	thetaY = 0;
	//}

/* Extrinsic XYZ */
	//if (pose.m[0][2] < 0.999f)
	//{
	//	if (pose.m[0][2] > -0.999f)
	//	{
	//		thetaY = asin(pose.m[0][2]);
	//		thetaX = atan2(-pose.m[1][2], pose.m[2][2]);
	//		thetaZ = atan2(-pose.m[0][1], pose.m[0][0]);
	//	}
	//	else // r21 = -1
	//	{
	//		thetaY = -PI / 2;
	//		thetaX = -(atan2(pose.m[1][0], pose.m[1][1]));
	//		thetaZ = 0;
	//	}
	//}
	//else // r21 = +1
	//{
	//	thetaY = PI / 2;
	//	thetaX = atan2(pose.m[1][0], pose.m[1][1]);
	//	thetaZ = 0;
	//}

/* Extrinsic XZY */
	//if (pose.m[0][1] < 0.999f)
	//{
	//	if (pose.m[0][1] > -0.999f)
	//	{
	//		thetaZ = asin(-pose.m[0][1]);
	//		thetaX = atan2(pose.m[2][1], pose.m[1][1]);
	//		thetaY = atan2(pose.m[0][2], pose.m[0][0]);
	//	}
	//	else // r01 = -1
	//	{
	//		thetaZ = PI / 2;
	//		thetaX = -(atan2(-pose.m[2][0], pose.m[2][2]));
	//		thetaY = 0;
	//	}
	//}
	//else // r01 = +1
	//{
	//	thetaZ = -PI / 2;
	//	thetaX = atan2(-pose.m[2][0], pose.m[2][2]);
	//	thetaY = 0;
	//}

/* Extrinsic YZX */
	if (pose.m[1][0] < 0.999f)
	{
		if (pose.m[1][0] > -0.999f)
		{
			thetaZ = asin(pose.m[1][0]);
			thetaY = atan2(-pose.m[2][0], pose.m[0][0]);
			thetaX = atan2(-pose.m[1][2], pose.m[1][1]);
		}
		else // r10 = -1
		{
			thetaZ = -PI / 2;
			thetaY = -(atan2(pose.m[2][1], pose.m[2][2]));
			thetaX = 0;
		}
	}
	else // r10 = +1
	{
		thetaZ = PI / 2;
		thetaY = atan2(pose.m[2][1], pose.m[2][2]);
		thetaX = 0;
	}


	*pitch = thetaX;
	*yaw = -thetaY;
	*roll = -thetaZ;
}

bool GetSteamVRPositionalData(float *yaw, float *pitch, float *roll, float *x, float *y, float *z, Matrix3* rotMatrix)
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

	vr::TrackedDeviceIndex_t unDevice = vr::k_unTrackedDeviceIndex_Hmd;
	vr::Compositor_FrameTiming frametiming;
	frametiming.m_nSize = sizeof(vr::Compositor_FrameTiming);
	if (!g_pHMD->IsTrackedDeviceConnected(unDevice))
		return false;

	vr::VRControllerState_t state;
	if (g_pHMD->GetControllerState(unDevice, &state, sizeof(state)))
	{
		vr::TrackedDevicePose_t trackedDevicePoseArray[vr::k_unMaxTrackedDeviceCount];
		vr::HmdMatrix34_t poseMatrix;
		vr::HmdQuaternionf_t q;

		vr::VRCompositor()->WaitGetPoses(trackedDevicePoseArray, vr::k_unMaxTrackedDeviceCount, NULL, 0);

		if (trackedDevicePoseArray[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
		//if (g_hmdPose.bPoseIsValid) {
			g_hmdPose = trackedDevicePoseArray[vr::k_unTrackedDeviceIndex_Hmd]; // This matrix contains all positional and rotational data.
			poseMatrix = g_hmdPose.mDeviceToAbsoluteTracking; // This matrix contains all positional and rotational data.
			//rotMatrixToEuler(poseMatrix, yaw, pitch, roll);
			*rotMatrix = HmdMatrix34toMatrix3(poseMatrix);

			q = rotationToQuaternion(poseMatrix);
			quatToEuler(q, yaw, pitch, roll);
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
