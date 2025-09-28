#pragma once

// General constants
const float PI = 3.141592f;
const float RAD_TO_DEG = 180.0f / PI;

// cockpitlook.cfg parameter names
extern const char* TRACKER_TYPE; // Defines which tracker to use
extern const char* TRACKER_TYPE_FREEPIE; // Use FreePIE as the tracker
extern const char* TRACKER_TYPE_STEAMVR; // Use SteamVR as the tracker
extern const char* TRACKER_TYPE_TRACKIR; // Use TrackIR (or OpenTrack) as the tracker
extern const char* TRACKER_TYPE_NONE;
extern const char* DISABLE_HANGAR_RANDOM_CAMERA;
extern const char* YAW_MULTIPLIER;
extern const char* PITCH_MULTIPLIER;
extern const char* ROLL_MULTIPLIER;
extern const char* YAW_OFFSET;
extern const char* PITCH_OFFSET;
extern const char* ROLL_OFFSET;
extern const char* FREEPIE_SLOT;
extern const char* POS_X_MULTIPLIER_VRPARAM;
extern const char* POS_Y_MULTIPLIER_VRPARAM;
extern const char* POS_Z_MULTIPLIER_VRPARAM;
extern const char* MIN_POSITIONAL_X_VRPARAM;
extern const char* MAX_POSITIONAL_X_VRPARAM;
extern const char* MIN_POSITIONAL_Y_VRPARAM;
extern const char* MAX_POSITIONAL_Y_VRPARAM;
extern const char* MIN_POSITIONAL_Z_VRPARAM;
extern const char* MAX_POSITIONAL_Z_VRPARAM;

//int CockpitLookHook(int* params);
int UpdateTrackingData();
int UpdateCameraTransformHook(int* params);
int MapCameraUpdateHook(int* params);
int CockpitPositionTransformHook(int* params);
int DoRotationPitchHook(int* params);
int DoRotationYawHook(int* params);
int SetupReticleHook(int* params);

int LaserEffectHook(int* params);
int WarheadEffectHook(int* params);
