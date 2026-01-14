#pragma once

#include "SharedMem.h"
#include "UDP.h"
#include <chrono>

enum ActiveWeapon
{
	NONE = 1,
	LASERS = 2,
	IONS = 3,
	WARHEADS = 4,
	SEC_WARHEADS = 5,
};

template<typename T>
struct TelemetryValue {
	T value;
	std::chrono::steady_clock::time_point lastChange;
	int sendPersistMs = 200; // Minimum duration for sending a value
	int enabledPersistMs = 0; // Default persistence for ephemeral values when they are enabled (for example events that only last one frame)

	TelemetryValue(int const sendPersistenceMs = 200,int const enabledPersistenceMs = 0)
		: value{},
		lastChange(std::chrono::steady_clock::now() - std::chrono::milliseconds(sendPersistMs)),
		sendPersistMs(sendPersistenceMs),
		enabledPersistMs(enabledPersistenceMs)
	{}

	operator T() const {
		return value;
	}

	bool update(const T& newValue) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChange).count();

		if (newValue != value || enabledPersistMs != 0) {
			// If T is numeric, apply logic for ephemeral value persistence
			// This prevents sending a 0 value immediately after non-zero values that only lasted one frame
			if (std::is_arithmetic<T>::value) {
				if (newValue == T(0) && elapsed <= enabledPersistMs) {					
					return false;
				}
			}
			// For non-numeric types (like strings), always apply the change immediately
			value = newValue;
			lastChange = now;
			return true;
		}
		return false;
	}

	bool shouldSend() const {
		if (g_bContinuousTelemetry) return true;
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastChange).count();
		return (elapsed < sendPersistMs);
	}

	TelemetryValue<T>& operator=(const T& newValue) {
		update(newValue);
		return *this;
	}
};

class PlayerTelemetry {
public:
	// First param: minimum send persistence for more reliable network telemetry
	// Second param: enabled persistence to ensure ephemeral values (non-zero 
	// events that only last one frame) are captured
	TelemetryValue<std::string> craft_name{500};
	TelemetryValue<std::string> short_name{500};
	TelemetryValue<std::string> shipName{500};
	TelemetryValue<int> speed{200};
	TelemetryValue<int> throttle{200};
	TelemetryValue<BYTE> ElsLasers{200};
	TelemetryValue<BYTE> ElsShields{200};
	TelemetryValue<BYTE> ElsBeam{200};
	TelemetryValue<BYTE> SfoilsState{200};
	TelemetryValue<BYTE> ShieldDirection{200};
	TelemetryValue<int> shields_front{200};
	TelemetryValue<int> shields_back{200};
	TelemetryValue<int> hull{200};
	TelemetryValue<int> shake{200};
	TelemetryValue<BYTE> BeamActive{200};
	TelemetryValue<bool> underTractorBeam{200};
	TelemetryValue<bool> underJammingBeam{200};
	TelemetryValue<ActiveWeapon> activeWeapon{200};
	TelemetryValue<bool> laserFired{200,200};
	TelemetryValue<bool> warheadFired{200,200};
	TelemetryValue<float> yawInertia{200};
	TelemetryValue<float> pitchInertia{200};
	TelemetryValue<float> rollInertia{200};
	TelemetryValue<float> accelInertia{200};
	// Absolute yaw, pitch and roll. Effectively copied from YawVR (even when disabled).
	// absYaw is the absolute yaw, goes from 0 to 360 degrees.
	// absPitch and absRoll are also in degrees, but they gradually fade back to 0
	// because it would be uncomfortable to sit on a rotating platform that is tilted
	// for extended periods of time.
	TelemetryValue<float> absYaw{200};
	TelemetryValue<float> absPitch{200};
	TelemetryValue<float> absRoll{200};

	PlayerTelemetry() {
		craft_name = NULL;
		short_name = NULL;
		shipName = NULL;
		speed = -1;
		throttle = -1;
		ElsLasers = ElsShields = ElsBeam = 255;
		SfoilsState = 255;
		ShieldDirection = 255;
		shields_front = shields_back = -1;
		hull = -1;
		shake = 0;
		BeamActive = 255;
		underTractorBeam.value = false;
		underJammingBeam = false;
		activeWeapon.value = ActiveWeapon::NONE;
		laserFired = false;
		warheadFired = false;
		yawInertia = 0;
		pitchInertia = 0;
		rollInertia = 0;
		accelInertia = 0;
		absYaw = 0;
		absPitch = 0;
		absRoll = 0;
	}
};

class TargetTelemetry {
public:
	TelemetryValue<std::string> name;
	TelemetryValue<std::string> short_name;
	TelemetryValue<int> IFF;
	TelemetryValue<int> shields;
	TelemetryValue<int> hull;
	TelemetryValue<int> sys;
	TelemetryValue<float> dist;
	//BYTE CraftState;
	TelemetryValue<std::string> Cargo;
	TelemetryValue<std::string> SubCmp;

	TargetTelemetry() {
		short_name = NULL;
		IFF = -1;
		shields = hull = sys = -1;
		dist = -1;
		name = NULL;
		Cargo = NULL;
		SubCmp = NULL;
		//CraftState = 255;
		//ZeroMemory(name,   TLM_MAX_NAME);
		//ZeroMemory(Cargo,  TLM_MAX_CARGO);
		//ZeroMemory(SubCmp, TLM_MAX_SUBCMP);
	}
};

class LocationTelemetry {
public:
	TelemetryValue<int> playerInHangar;
	TelemetryValue<std::string> location;

	LocationTelemetry() {
		playerInHangar = -1;
		location = NULL;
	}
};

void SendXWADataOverUDP();

#define SEND_TELEMETRY_VALUE_JSON(obj, field, value, section, key) \
    if ((obj).field.update(value) || (obj).field.shouldSend()) { \
        msg += "\t\"" + std::string(section) + "." + std::string(key) + "\" : \"" + std::to_string(value) + "\",\n"; \
    }

#define SEND_TELEMETRY_VALUE_JSON_FLOAT(prev, field, value, section, key) \
    if (fabs((value) - (prev).field) > 0.00001f) { \
        msg += "\t\"" + std::string(section) + "." + std::string(key) + "\" : \"" + std::to_string(value) + ",\n"; \
    }

#define SEND_TELEMETRY_VALUE_SIMPLE(obj, field, value, section, key) \
    if ((obj).field.update(value) || (obj).field.shouldSend()) { \
        msg += section "|" key ":" + std::to_string(value) + "\n"; \
    }

extern PlayerTelemetry g_PlayerTelemetry;
extern PlayerTelemetry g_PrevPlayerTelemetry;
extern TargetTelemetry g_TargetTelemetry;
extern LocationTelemetry g_LocationTelemetry;