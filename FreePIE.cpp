#include <windows.h>
#include "FreePIE.h"

void log_debug(const char *format, ...);

typedef UINT32(__cdecl *freepie_io_6dof_slots_fun_type)();
typedef INT32(__cdecl *freepie_io_6dof_read_fun_type)(UINT32 index, UINT32 length, freepie_io_6dof_data *output);
typedef INT32(__cdecl *freepie_io_6dof_write_fun_type)(UINT32 index, UINT32 length, freepie_io_6dof_data *data);

freepie_io_6dof_slots_fun_type freepie_io_6dof_slots = NULL;
freepie_io_6dof_read_fun_type freepie_io_6dof_read = NULL;
freepie_io_6dof_write_fun_type freepie_io_6dof_write = NULL;

bool g_bFreePIELoaded = false, g_bFreePIEInitialized = false, g_bFreePIEAlreadyInitialized = false;
freepie_io_6dof_data g_FreePIEData;
HMODULE hFreePIE = NULL;

bool InitFreePIE() {
	LONG lRes = ERROR_SUCCESS;
	char regvalue[1024];
	DWORD size = 1024;
	
	if (g_bFreePIEAlreadyInitialized) {
		log_debug("FreePIE already initialized");
		return true;
	}

	log_debug("Initializing FreePIE");

	lRes = RegGetValueA(HKEY_CURRENT_USER, "Software\\FreePIE", "path", RRF_RT_ANY, NULL, regvalue, &size);
	if (lRes != ERROR_SUCCESS) {
		log_debug("Registry key for FreePIE was not found, error: 0x%x", lRes);
		return false;
	}

	if (size > 0) {
		log_debug("FreePIE path: %s", regvalue);
		SetDllDirectory(regvalue);
	}
	else {
		log_debug("Cannot load FreePIE, registry path is empty!");
		return false;
	}

	hFreePIE = LoadLibraryA("freepie_io.dll");

	if (hFreePIE != NULL) {
		log_debug("FreePIE loaded");
		freepie_io_6dof_slots = (freepie_io_6dof_slots_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_slots");
		freepie_io_6dof_read = (freepie_io_6dof_read_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_read");
		freepie_io_6dof_write = (freepie_io_6dof_write_fun_type)GetProcAddress(hFreePIE, "freepie_io_6dof_write");

		if (freepie_io_6dof_slots == NULL || freepie_io_6dof_read == NULL ||
			freepie_io_6dof_write == NULL) {
			log_debug("Could not load all of FreePIE's functions");
			log_debug("read: 0x%x", freepie_io_6dof_read);
			log_debug("write: 0x%x", freepie_io_6dof_write);
			return false;
		}
		g_bFreePIELoaded = true;

		UINT32 num_slots = freepie_io_6dof_slots();
		log_debug("num_slots: %d", num_slots);
		return true;
	}
	else {
		log_debug("Could not load FreePIE");
	}
	g_bFreePIEInitialized = true;
	g_bFreePIEAlreadyInitialized = true;
	return true;
}

void ShutdownFreePIE() {
	log_debug("Shutting down FreePIE");
	if (hFreePIE != NULL)
		FreeLibrary(hFreePIE);
}

bool ReadFreePIE(int slot) {
	// Check how many slots (values) the current FreePIE implementation provides.
	int error = freepie_io_6dof_read(slot, 1, &g_FreePIEData);
	if (error < 0) {
		log_debug("FreePIE error: %d", error);
		return false;
	}
	return true;
}

void WriteFreePIE(int slot) {
	int error = freepie_io_6dof_write(slot, 1, &g_FreePIEData);
	if (error != 0)
		log_debug("Could not write to FreePIE, error: 0x%x", error);
}