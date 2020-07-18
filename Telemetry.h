#pragma once
//#define TELEMETRY_JSON
#define TELEMETRY_SIMPLIFIED

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
	char *name;
	char *short_name;
	int IFF;
	char Cargo[16];
	int shields;
	int hull;
	BYTE CraftState;

	TargetTelemetry() {
		name = short_name = NULL;
		IFF = -1;
		ZeroMemory(Cargo, 16);
		shields = hull = -1;
		CraftState = 255;
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
