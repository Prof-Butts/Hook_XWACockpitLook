#include "SharedMem.h"

void log_debug(const char *format, ...);

void InitSharedMem() {
	// Create the shared memory file or open it if it already exists.
	g_SharedData = (SharedMemDataCockpitLook*)g_SharedMem.GetMemoryPointer();
	if (g_SharedData == nullptr) {
		log_debug("Could not get pointer to shared CockpitLook data");
		return;
	}

	// We consider the data is ready as soon as the shared memory is initialized
	// This works because we don't have actual concurrent threads accessing it.
	// All DLLs belong to the same thread.
	g_SharedMem.SetDataReady();

	g_pSharedDataTelemetry = (SharedMemDataTelemetry*)g_SharedMemTelemetry.GetMemoryPointer();
	if (g_pSharedDataTelemetry == nullptr) {
		log_debug("[TLM] Could not get pointer to shared Telemetry data");
		return;
	}
}

SharedMemDataCockpitLook* g_SharedData = nullptr;
SharedMemDataTelemetry* g_pSharedDataTelemetry = nullptr;

// Create the shared memory as a global variable
SharedMem<SharedMemDataCockpitLook> g_SharedMem(SHARED_MEM_NAME_COCKPITLOOK, true, true);
SharedMem<SharedMemDataTelemetry> g_SharedMemTelemetry(SHARED_MEM_NAME_TELEMETRY, true, true);
