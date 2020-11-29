#pragma once

#include <windows.h>

// This is the data that will be shared between different DLLs
// The bDataReady is set to true once the writer has initialized
// the structure.
// pDataPtr points to the actual data to be shared in the regular
// heap space
struct SharedData {
	bool bDataReady;
	void *pDataPtr;
};

constexpr auto SHARED_MEM_NAME = "Local\\CockpitLookHook";
constexpr auto SHARED_MEM_SIZE = sizeof(SharedData);

// This is the shared memory handler. Call GetMemoryPtr to get a pointer
// to a SharedData structure
class SharedMem {
private:
	HANDLE hMapFile;
	void *pSharedMemPtr;
	bool InitMemory(bool OpenCreate);

public:
	// Specify true in OpenCreate to create the shared memory handle. Set it to false to
	// open an existing shared memory handle.
	SharedMem(bool OpenCreate);
	~SharedMem();
	void *GetMemoryPtr();
};
