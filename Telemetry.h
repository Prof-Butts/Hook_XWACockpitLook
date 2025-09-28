#pragma once

#include "SharedMem.h"
#include "UDP.h"

enum class ActiveWeapon
{
	NONE,
	LASERS,
	IONS,
	WARHEADS,
	SEC_WARHEADS,
};

class PlayerTelemetry {
public:
	char *craft_name;
	char *short_name;
	char shipName[TLM_MAX_SHIP_NAME];
	int speed;
	int throttle;
	BYTE ElsLasers;
	BYTE ElsShields;
	BYTE ElsBeam;
	BYTE SfoilsState;
	BYTE ShieldDirection;
	int shields_front;
	int shields_back;
	int hull;
	int shake;
	BYTE BeamActive;
	bool underTractorBeam;
	bool underJammingBeam;
	ActiveWeapon activeWeapon;
	bool laserFired;
	bool warheadFired;
	float yawInertia;
	float pitchInertia;
	float rollInertia;
	float accelInertia;

	PlayerTelemetry() {
		craft_name = NULL;
		short_name = NULL;
		shipName[0] = 0;
		speed = -1;
		throttle = -1;
		ElsLasers = ElsShields = ElsBeam = 255;
		SfoilsState = 255;
		ShieldDirection = 255;
		shields_front = shields_back = -1;
		hull = -1;
		shake = 0;
		BeamActive = 255;
		underTractorBeam = false;
		underJammingBeam = false;
		activeWeapon = ActiveWeapon::NONE;
		laserFired = false;
		warheadFired = false;
		yawInertia = 0;
		pitchInertia = 0;
		rollInertia = 0;
		accelInertia = 0;
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
	int playerInHangar;

	LocationTelemetry() {
		playerInHangar = -1;
	}
};

void SendXWADataOverUDP();
