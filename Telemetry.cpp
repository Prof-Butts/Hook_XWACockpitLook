#include <windows.h>
#include <Stdio.h>
#include <stdarg.h>
#include <string>
#include <sstream>
#include <iomanip>
#include "XWAObject.h"
#include "SharedMem.h"
#include "Telemetry.h"
#include "UDP.h"
#include "Vectors.h"

extern const int *localPlayerIndex;
extern const unsigned int *g_playerInHangar;
extern const __int8 *provingGrounds;
extern ObjectEntry **objects;
extern PlayerDataEntry *PlayerDataTable;
extern CraftDefinitionEntry *CraftDefinitionTable;
enum HyperspacePhaseEnum;
extern HyperspacePhaseEnum g_HyperspacePhaseFSM, g_PrevHyperspacePhaseFSM;

PlayerTelemetry g_PlayerTelemetry;
PlayerTelemetry g_PrevPlayerTelemetry;
TargetTelemetry g_TargetTelemetry;
LocationTelemetry g_LocationTelemetry;

void log_debug(const char *format, ...);

std::string ShieldDirectionStr(int x)
{
	switch (x)
	{
		case 0: return "front";
		case 1: return "even";
		case 2: return "back";
		default:
			return "none";
	}
}

ActiveWeapon GetCurrentActiveWeapon()
{
	// primarySec,   warHead,	Meaning:
	//		0			0		Lasers Armed
	//		0			1		Primary Warheads Armed
	//		1			0		Ion Cannons Armed
	//		1			1		Secondary Warheads Armed
	const char primArmed    = PlayerDataTable[*localPlayerIndex].primarySecondaryArmed;
	const char warheadArmed = PlayerDataTable[*localPlayerIndex].warheadArmed;

	if (primArmed == 0)
		return warheadArmed ? ActiveWeapon::WARHEADS : ActiveWeapon::LASERS;
	else
		return warheadArmed ? ActiveWeapon::SEC_WARHEADS : ActiveWeapon::IONS;
}

namespace std {
    // Overload std::to_string for ActiveWeapon
    std::string to_string(ActiveWeapon x) {
        switch (x) {
        case ActiveWeapon::LASERS:
            return "lasers";
        case ActiveWeapon::IONS:
            return "ions";
        case ActiveWeapon::WARHEADS:
            return "warheads";
        case ActiveWeapon::SEC_WARHEADS:
            return "secwarheads";
        default:
            return "none";
        }
    }
	// Overload std::to_string for std::string to reuse the same macro
	std::string to_string(std::string x) {
		return x;
	}
}

void SendXWADataOverUDP()
{
	std::string msg = "";

	int shields_front = 0;
	int shields_back  = 0;
	char shipName[TLM_MAX_SHIP_NAME];

	int tgtShds = 0, tgtHull = 0, tgtSys = 0;
	float tgtDist = 0;	
	std::string tgtName;
	std::string tgtCargo;
	std::string tgtSubCmp;


	const int shake = abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeX) +
		abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeY) +
		abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeZ);


	if (g_pSharedDataTelemetry != nullptr)
	{
		shields_front = g_pSharedDataTelemetry->shieldsFwd;
		shields_back  = g_pSharedDataTelemetry->shieldsBck;
		strncpy_s(shipName, g_pSharedDataTelemetry->shipName, TLM_MAX_SHIP_NAME);
	}

	// PLAYER SECTION
	{
		int16_t objectIndex = (int16_t)PlayerDataTable[*localPlayerIndex].objectIndex;
		if (objectIndex < 0 || objects == nullptr) goto target_section;
		int speed = (int)(PlayerDataTable[*localPlayerIndex].currentSpeed / 2.25f);
		//log_debug("[DBG] objectIndex: %d, *localPlayerIndex: %d, localPlayerIndex: 0x%x", objectIndex, *localPlayerIndex, localPlayerIndex);
		ObjectEntry* object = &((*objects)[objectIndex]);
		if (object == NULL) goto target_section;
		MobileObjectEntry* mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) goto target_section;
		CraftInstance* craftInstance = mobileObject->craftInstancePtr;
		if (craftInstance == NULL) goto target_section;
		CraftDefinitionEntry* craftDefinition = &(CraftDefinitionTable[craftInstance->CraftType]);
		if (craftDefinition == NULL) goto target_section;
		char* craft_name = (char*)craftDefinition->pCraftName;
		char* short_name = (char*)craftDefinition->pCraftShortName;
		int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		hull = max(0, hull);

		//float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		//int shields_front = (int)(100.0 * (float)craftInstance->ShieldPointsFront / total_shield_points);
		//int shields_back = (int)(100.0 * (float)craftInstance->ShieldPointsBack / total_shield_points);
		//shields_front = max(0, shields_front);
		//shields_back = max(0, shields_back);
		const int throttle = (int)(100.0f * craftInstance->EngineThrottleInput / 65535.0f);
		const bool underTractorBeam = (craftInstance->IsUnderBeamEffect[1] != 0);
		const bool underJammingBeam = (craftInstance->IsUnderBeamEffect[2] != 0);
		const ActiveWeapon activeWeapon = GetCurrentActiveWeapon();

		if (g_UDPFormat == TELEMETRY_FORMAT_JSON)
		{
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, shipName, shipName, "XWA.player", "shipname");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, craft_name, std::string(craft_name), "XWA.player", "crafttypename");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, short_name, std::string(short_name), "XWA.player", "shortcrafttypename");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, speed, speed, "XWA.player", "speed");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, throttle, throttle, "XWA.player", "throttle");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, ElsLasers, craftInstance->ElsLasers, "XWA.player", "ELSlasers");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, ElsShields, craftInstance->ElsShields, "XWA.player", "ELSshields");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, ElsBeam, craftInstance->ElsBeam, "XWA.player", "ELSbeam");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, SfoilsState, craftInstance->SfoilsState, "XWA.player", "s-foils");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, ShieldDirection, craftInstance->ShieldDirection, "XWA.player", "shielddirection");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, shields_front, shields_front, "XWA.player", "shieldfront");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, shields_back, shields_back, "XWA.player", "shieldback");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, hull, hull, "XWA.player", "hull");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, shake, shake, "XWA.player", "shake");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, BeamActive, craftInstance->BeamActive, "XWA.player", "beamactive");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, underTractorBeam, underTractorBeam, "XWA.player", "undertractorbeam");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, underJammingBeam, underJammingBeam, "XWA.player", "underjammingbeam");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, activeWeapon, activeWeapon, "XWA.player", "activeweapon");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, laserFired, g_PlayerTelemetry.laserFired, "XWA.player", "laserfired");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, warheadFired, g_PlayerTelemetry.warheadFired, "XWA.player", "warheadfired");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, yawInertia, g_PlayerTelemetry.yawInertia, "XWA.player", "yaw_inertia");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, pitchInertia, g_PlayerTelemetry.pitchInertia, "XWA.player", "pitch_inertia");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, rollInertia, g_PlayerTelemetry.rollInertia, "XWA.player", "roll_inertia");
			SEND_TELEMETRY_VALUE_JSON(g_PrevPlayerTelemetry, accelInertia, g_PlayerTelemetry.accelInertia, "XWA.player", "accel_inertia");
		}
		else // TELEMETRY_FORMAT_SIMPLIFIED
		{
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, shipName, shipName, "player", "name");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, craft_name, std::string(craft_name), "player", "crafttypename");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, short_name, std::string(short_name), "player", "shortcrafttypename");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, speed, speed, "player", "speed");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, throttle, throttle, "player", "throttle");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, ElsLasers, craftInstance->ElsLasers, "player", "elslasers");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, ElsShields, craftInstance->ElsShields, "player", "elsshields");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, ElsBeam, craftInstance->ElsBeam, "player", "elsbeam");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, SfoilsState, craftInstance->SfoilsState, "player", "sfoils");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, ShieldDirection, craftInstance->ShieldDirection, "player", "shielddirection");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, shields_front, shields_front, "player", "shieldfront");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, shields_back, shields_back, "player", "shieldback");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, hull, hull, "player", "hull");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, shake, shake, "player", "shake");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, BeamActive, craftInstance->BeamActive, "player", "beamactive");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, underTractorBeam, underTractorBeam, "player", "undertractorbeam");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, underJammingBeam, underJammingBeam, "player", "underjammingbeam");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, activeWeapon, activeWeapon, "player", "activeweapon");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, laserFired, g_PlayerTelemetry.laserFired, "player", "laserfired");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, warheadFired, g_PlayerTelemetry.warheadFired, "player", "warheadfired");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, yawInertia, g_PlayerTelemetry.yawInertia, "player", "yaw_inertia");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, pitchInertia, g_PlayerTelemetry.pitchInertia, "player", "pitch_inertia");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, rollInertia, g_PlayerTelemetry.rollInertia, "player", "roll_inertia");
			SEND_TELEMETRY_VALUE_SIMPLE(g_PrevPlayerTelemetry, accelInertia, g_PlayerTelemetry.accelInertia, "player", "accel_inertia");
		}

		//log_debug("[UDP] Throttle: %d", CraftDefinitionTable[objectIndex].EngineThrottle);
		//log_debug("[UDP] Throttle: %s", CraftDefinitionTable[objectIndex].CockpitFileName);
		//log_debug("[UDP] localPlayerIndex: %d, Index: %d", *localPlayerIndex, PlayerDataTable[*localPlayerIndex].objectIndex);
		//Prints 0, 0
		//log_debug("[UDP] CameraIdx: %d", PlayerDataTable[*localPlayerConnectedAs].cameraFG);
		// Prints -1

		// Store the data for the next frame
		g_PrevPlayerTelemetry.craft_name = craft_name;
		g_PrevPlayerTelemetry.short_name = short_name;
		//strncpy_s(g_PrevPlayerTelemetry.shipName, shipName, TLM_MAX_SHIP_NAME);
		g_PrevPlayerTelemetry.shipName = shipName;
		g_PrevPlayerTelemetry.speed = speed;
		g_PrevPlayerTelemetry.throttle = throttle;
		g_PrevPlayerTelemetry.ElsLasers = craftInstance->ElsLasers;
		g_PrevPlayerTelemetry.ElsShields = craftInstance->ElsShields;
		g_PrevPlayerTelemetry.ElsBeam = craftInstance->ElsBeam;
		g_PrevPlayerTelemetry.SfoilsState = craftInstance->SfoilsState;
		g_PrevPlayerTelemetry.ShieldDirection = craftInstance->ShieldDirection;
		g_PrevPlayerTelemetry.shields_front = shields_front;
		g_PrevPlayerTelemetry.shields_back = shields_back;
		g_PrevPlayerTelemetry.hull = hull;
		g_PrevPlayerTelemetry.shake = shake;
		g_PrevPlayerTelemetry.BeamActive = craftInstance->BeamActive;
		g_PrevPlayerTelemetry.underTractorBeam = underTractorBeam;
		g_PrevPlayerTelemetry.underJammingBeam = underJammingBeam;
		g_PrevPlayerTelemetry.activeWeapon = activeWeapon;
		g_PrevPlayerTelemetry.laserFired = g_PlayerTelemetry.laserFired;
		g_PrevPlayerTelemetry.warheadFired = g_PlayerTelemetry.warheadFired;
		g_PrevPlayerTelemetry.yawInertia   = g_PlayerTelemetry.yawInertia;
		g_PrevPlayerTelemetry.pitchInertia = g_PlayerTelemetry.pitchInertia;
		g_PrevPlayerTelemetry.rollInertia  = g_PlayerTelemetry.rollInertia;
		g_PrevPlayerTelemetry.accelInertia = g_PlayerTelemetry.accelInertia;

		//Reset the telemetry ephemeral flags
		g_PlayerTelemetry.laserFired = false;
		g_PlayerTelemetry.warheadFired = false;
	}

	// TARGET SECTION
target_section:
	{
		if (g_pSharedDataTelemetry != nullptr)
		{
			tgtShds = g_pSharedDataTelemetry->tgtShds;
			tgtHull = g_pSharedDataTelemetry->tgtHull;
			tgtSys = g_pSharedDataTelemetry->tgtSys;
			tgtDist = g_pSharedDataTelemetry->tgtDist;
			tgtName = g_pSharedDataTelemetry->tgtName;
			tgtCargo = g_pSharedDataTelemetry->tgtCargo;
			tgtSubCmp = g_pSharedDataTelemetry->tgtSubCmp;
		}
		short currentTargetIndex = PlayerDataTable[*localPlayerIndex].currentTargetIndex;
		// currentTargetIndex can apparently be 0. I remember the game crashing, but it doesn't anymore!
		if (currentTargetIndex < 0) goto status_section;
		ObjectEntry* object = &((*objects)[currentTargetIndex]);
		if (object == NULL) goto status_section;
		MobileObjectEntry* mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) goto status_section;
		CraftInstance* craftInstance = mobileObject->craftInstancePtr;
		if (craftInstance == NULL) goto status_section;
		CraftDefinitionEntry* craftDefinition = &(CraftDefinitionTable[craftInstance->CraftType]);
		if (craftDefinition == NULL) goto status_section;
		//char *name = (char *)craftDefinition->pCraftName;
		//char *short_name = (char *)craftDefinition->pCraftShortName;
		int IFF = object->MobileObjectPtr->IFF;
		//int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		//hull = max(0, hull);
		//float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		//int shields = (int)(100.0f * (craftInstance->ShieldPointsFront + craftInstance->ShieldPointsBack) / total_shield_points);
		//shields = max(0, shields);
		if (g_UDPFormat == TELEMETRY_FORMAT_JSON)
		{
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, name, tgtName, "XWA.target", "name");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, IFF, IFF, "XWA.target", "IFF");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, shields, tgtShds, "XWA.target", "shields");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, hull, tgtHull, "XWA.target", "hull");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, sys, tgtSys, "XWA.target", "sys");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, dist, tgtDist, "XWA.target", "dist");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, Cargo, tgtCargo, "XWA.target", "cargo");
			SEND_TELEMETRY_VALUE_JSON(g_TargetTelemetry, SubCmp, tgtSubCmp, "XWA.target", "subcmp");
		}
		else { // TELEMETRY_FORMAT_SIMPLIFIED
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, name, tgtName, "target", "name");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, IFF, IFF, "target", "IFF");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, shields, tgtShds, "target", "shields");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, hull, tgtHull, "target", "hull");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, sys, tgtSys, "target", "sys");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, dist, tgtDist, "target", "dist");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, Cargo, tgtCargo, "target", "cargo");
			SEND_TELEMETRY_VALUE_SIMPLE(g_TargetTelemetry, SubCmp, tgtSubCmp, "target", "subcmp");
		}
		// state is 0 when the craft is static
		// state is 3 when the craft is destroyed
		/*if (craftInstance->CraftState != g_TargetTelemetry.CraftState)
			msg += "target|state:" + std::to_string(craftInstance->CraftState) + "\n";*/

		// CycleTime is always 236, CycleTimer counts down from CycleTime to -1 and starts over
		//msg += "\t\"cycle time\" : " + std::to_string(craftInstance->CycleTime) + "\n";
		//msg += "\t\"cycle timer\" : " + std::to_string(craftInstance->CycleTimer) + "\n";

		// Store the data for the next frame
		//g_TargetTelemetry.name = name;
		//g_TargetTelemetry.short_name = short_name;
		//if (tgtName)   memcpy(g_TargetTelemetry.name,   tgtName,   TLM_MAX_NAME);
		//if (tgtCargo)  memcpy(g_TargetTelemetry.Cargo,  tgtCargo,  TLM_MAX_CARGO);
		//if (tgtSubCmp) memcpy(g_TargetTelemetry.SubCmp, tgtSubCmp, TLM_MAX_SUBCMP);
		g_TargetTelemetry.IFF     = IFF;
		g_TargetTelemetry.name    = tgtName;
		g_TargetTelemetry.Cargo   = tgtCargo;
		g_TargetTelemetry.SubCmp  = tgtSubCmp;
		g_TargetTelemetry.shields = tgtShds;
		g_TargetTelemetry.hull    = tgtHull;
		g_TargetTelemetry.sys     = tgtSys;
		g_TargetTelemetry.dist    = tgtDist;
		//g_TargetTelemetry.CraftState = craftInstance->CraftState;
	}
	
	// STATUS SECTION
status_section:
	{
		//g_LocationTelemetry.playerInHangar = *g_playerInHangar;
		std::string location;
		switch (g_HyperspacePhaseFSM)
		{
		case HS_INIT_ST:
			location = "space";
			break;
		case HS_HYPER_ENTER_ST:
			location = "hyperentry";
			break;
		case HS_HYPER_TUNNEL_ST:
			location = "hyperspace";			
			break;
		case HS_HYPER_EXIT_ST:
			location = "hyperexit";
			break;
		}
		
		if (g_UDPFormat == TELEMETRY_FORMAT_JSON)
		{
			SEND_TELEMETRY_VALUE_JSON(g_LocationTelemetry, playerInHangar, *g_playerInHangar, "XWA.status", "location");
			SEND_TELEMETRY_VALUE_JSON(g_LocationTelemetry, location, location, "XWA.status", "location");
		}
		else { // TELEMETRY_FORMAT_SIMPLIFIED
			SEND_TELEMETRY_VALUE_SIMPLE(g_LocationTelemetry, playerInHangar, *g_playerInHangar, "status", "hangar");
			SEND_TELEMETRY_VALUE_SIMPLE(g_LocationTelemetry, location, location, "status", "location");
		}

		g_LocationTelemetry.playerInHangar = *g_playerInHangar;
	}

	if (g_UDPFormat == TELEMETRY_FORMAT_JSON && msg.length() > 2)
	{
		// Add the opening bracket
		msg = "{\n" + msg;
		// Remove the last comma
		if (msg.size() >= 2 && msg.compare(msg.size() - 2, 2, ",\n") == 0) {
			msg.erase(msg.size() - 2, 2);
		}
		// Add the closing bracket
		msg += "\n}";
	}
	int len = msg.length();
	if (len > 0) {
		msg.erase(msg.find_last_not_of(" \t\n") + 1);
		if (msg.length() > 3)
			SendUDPMessage((char *)msg.c_str());
	}
}
