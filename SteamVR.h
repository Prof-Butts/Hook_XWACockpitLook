#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"
#include "Matrices.h"

extern float g_fPredictedSecondsToPhotons;
extern bool g_bCorrectedHeadTracking;

bool InitSteamVR();
void ShutdownSteamVR();
Matrix3 HmdMatrix34toMatrix3(const vr::HmdMatrix34_t& mat);
bool GetSteamVRPositionalData(float* yaw, float* pitch, float* roll, float* x, float* y, float* z, Matrix3* poseMatrix);
