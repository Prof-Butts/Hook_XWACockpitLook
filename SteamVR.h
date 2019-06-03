#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"

bool InitSteamVR();
void ShutDownSteamVR();
void GetSteamVRPositionalData(float *yaw, float *pitch);
