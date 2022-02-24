#pragma once

// General constants
const float PI = 3.141592f;
const float RAD_TO_DEG = 180.0f / PI;

//int CockpitLookHook(int* params);
int UpdateTrackingData();
int UpdateCameraTransformHook(int* params);
int MapCameraUpdateHook(int* params);
int CockpitPositionTransformHook(int* params);
int DoRotationPitchHook(int* params);
int DoRotationYawHook(int* params);
