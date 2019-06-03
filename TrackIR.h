#pragma once

bool InitTrackIR();
void ShutdownTrackIR();
bool ReadTrackIRData(float *yaw, float *pitch);
