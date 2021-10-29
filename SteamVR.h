#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"

extern float g_fPredictedSecondsToPhotons;
extern bool g_bCorrectedHeadTracking;

bool InitSteamVR();
void ShutdownSteamVR();
bool GetSteamVRPositionalData(float *yaw, float *pitch, float *x, float *y, float *z);
