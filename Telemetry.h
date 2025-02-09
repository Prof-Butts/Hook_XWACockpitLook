#pragma once
//#define TELEMETRY_JSON
#define TELEMETRY_SIMPLIFIED

#include "SharedMem.h"

class PlayerTelemetry {
public:
	char *name;
	char *short_name;
	int speed;
	BYTE ElsLasers;
	BYTE ElsShields;
	BYTE ElsBeam;
	BYTE SfoilsState;
	BYTE ShieldDirection;
	int shields_front;
	int shields_back;
	int hull;
	BYTE BeamActive;

	PlayerTelemetry() {
		name = NULL;
		short_name = NULL;
		speed = -1;
		ElsLasers = ElsShields = ElsBeam = 255;
		SfoilsState = 255;
		ShieldDirection = 255;
		shields_front = shields_back = -1;
		hull = -1;
		BeamActive = 255;
	}
};

class TargetTelemetry {
public:
	char name[TLM_MAX_NAME];
	char *short_name;
	int IFF;
	int shields;
	int hull;
	int sys;
	float dist;
	//BYTE CraftState;
	char Cargo[TLM_MAX_CARGO];
	char SubCmp[TLM_MAX_SUBCMP];

	TargetTelemetry() {
		short_name = NULL;
		IFF = -1;
		shields = hull = sys = -1;
		dist = -1;
		//CraftState = 255;
		ZeroMemory(name,   TLM_MAX_NAME);
		ZeroMemory(Cargo,  TLM_MAX_CARGO);
		ZeroMemory(SubCmp, TLM_MAX_SUBCMP);
	}
};

class LocationTelemetry {
public:
	bool playerInHangar;

	LocationTelemetry() {
		playerInHangar = true;
	}
};

void SendXWADataOverUDP();
