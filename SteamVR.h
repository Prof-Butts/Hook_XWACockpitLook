#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"

bool InitSteamVR();
void ShutdownSteamVR();
void GetSteamVRPositionalData(float *yaw, float *pitch);
