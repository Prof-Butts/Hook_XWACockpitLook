#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"

extern float g_fPredictedSecondsToPhotons;

bool InitSteamVR();
void ShutdownSteamVR();
bool GetSteamVRPositionalData(float *yaw, float *pitch, float *x, float *y, float *z);
