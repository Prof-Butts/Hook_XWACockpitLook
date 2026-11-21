#pragma once

// OGS Telemetry Standard output. Enabled with UDP_telemetry_enabled = 1 and
// UDP_telemetry_format = OGS. See OgsXWA.cpp.

extern float g_fOGSMotionHz;
extern float g_fOGSSpeedScale;

bool OgsXWAStart();
void OgsXWAStop();
void UpdateOGSTelemetry();
