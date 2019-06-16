#pragma once
#include <headers/openvr.h>
#include "cockpitlook.h"

bool InitSteamVR();
void ShutdownSteamVR();
bool GetSteamVRPositionalData(float *yaw, float *pitch);
