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

PlayerTelemetry g_PrevPlayerTelemetry;
TargetTelemetry g_TargetTelemetry;
LocationTelemetry g_LocationTelemetry;

void log_debug(const char *format, ...);

#ifdef TELEMETRY_JSON
void SendXWADataOverUDP()
{
	std::string msg = "";
	//char buf[512];
	//sprintf_s(buf, 512, "speed:%d", (int)(PlayerDataTable[*localPlayerIndex].currentSpeed / 2.25f));
	msg += "{\n";

	// PLAYER SECTION
	msg += "\t\"player\":\n\t{\n";
	{
		int16_t objectIndex = (int16_t)PlayerDataTable[*localPlayerIndex].objectIndex;
		if (objectIndex <= 0) goto target_section;
		int speed = (int)(PlayerDataTable[*localPlayerIndex].currentSpeed / 2.25f);
		ObjectEntry *object = &((*objects)[objectIndex]);
		if (object == NULL) goto target_section;
		MobileObjectEntry *mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) goto target_section;
		CraftInstance *craftInstance = mobileObject->craftInstancePtr;
		if (craftInstance == NULL) goto target_section;
		CraftDefinitionEntry *craftDefinition = &(CraftDefinitionTable[craftInstance->CraftType]);
		if (craftDefinition == NULL) goto target_section;
		char *name = (char *)craftDefinition->pCraftName;
		char *short_name = (char *)craftDefinition->pCraftShortName;
		int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		hull = max(0, hull);
		float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		int shields_front = (int)(100.0 * (float)craftInstance->ShieldPointsFront / total_shield_points);
		int shields_back = (int)(100.0 * (float)craftInstance->ShieldPointsBack / total_shield_points);
		shields_front = max(0, shields_front);
		shields_back = max(0, shields_back);

		msg += "\t\t\"name\" : \"" + std::string(name) + "\",\n";
		msg += "\t\t\"short name\" : \"" + std::string(short_name) + "\",\n";
		msg += "\t\t\"speed\" : " + std::to_string(speed) + ",\n";
		//msg += "\t\t\"current speed\" : " + std::to_string(mobileObject->currentSpeed) + "\n"; // Redundant
		msg += "\t\t\"craft type\" : " + std::to_string(craftInstance->CraftType) + ",\n";
		msg += "\t\t\"ELS lasers\" : " + std::to_string(craftInstance->ElsLasers) + ",\n";
		msg += "\t\t\"ELS shields\" : " + std::to_string(craftInstance->ElsShields) + ",\n";
		msg += "\t\t\"ELS beam\" : " + std::to_string(craftInstance->ElsBeam) + ",\n";
		msg += "\t\t\"s-foils\" : " + std::to_string(craftInstance->SfoilsState) + ",\n";
		msg += "\t\t\"shield direction\" : " + std::to_string(craftInstance->ShieldDirection) + ",\n";
		msg += "\t\t\"front shields\" : " + std::to_string(shields_front) + ",\n";
		msg += "\t\t\"back shields\" : " + std::to_string(shields_back) + ",\n";
		msg += "\t\t\"hull\" : " + std::to_string(hull) + ",\n";
		msg += "\t\t\"system\" : " + std::to_string(craftInstance->SystemStrength) + ",\n";
		msg += "\t\t\"beam active\" : " + std::to_string(craftInstance->BeamActive) + ",\n";
		msg += "\t\t\"beam energy\" : " + std::to_string(craftInstance->BeamEnergy) + "\n";

		//log_debug("[UDP] Throttle: %d", CraftDefinitionTable[objectIndex].EngineThrottle);
		//log_debug("[UDP] Throttle: %s", CraftDefinitionTable[objectIndex].CockpitFileName);
		//log_debug("[UDP] localPlayerIndex: %d, Index: %d", *localPlayerIndex, PlayerDataTable[*localPlayerIndex].objectIndex);
		//Prints 0, 0
		//log_debug("[UDP] CameraIdx: %d", PlayerDataTable[*localPlayerConnectedAs].cameraFG);
		// Prints -1

	}

target_section:
	msg += "\t},\n";
	msg += "\t\"target\":\n\t{\n";
	{
		//log_debug("[UDP] Player IFF: %d, team: %d", PlayerDataTable[*localPlayerIndex].IFF, PlayerDataTable[*localPlayerIndex].team);
		short currentTargetIndex = PlayerDataTable[*localPlayerIndex].currentTargetIndex;
		// currentTargetIndex can apparently be 0. I remember the game crashing, but it doesn't anymore!
		if (currentTargetIndex < 0) goto status_section;
		ObjectEntry *object = &((*objects)[currentTargetIndex]);
		if (object == NULL) goto status_section;
		MobileObjectEntry *mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) goto status_section;
		CraftInstance *craftInstance = mobileObject->craftInstancePtr;
		if (craftInstance == NULL) goto status_section;
		CraftDefinitionEntry *craftDefinition = &(CraftDefinitionTable[craftInstance->CraftType]);
		if (craftDefinition == NULL) goto status_section;
		char *name = (char *)craftDefinition->pCraftName;
		char *short_name = (char *)craftDefinition->pCraftShortName;
		int IFF = object->MobileObjectPtr->IFF;
		int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		hull = max(0, hull);
		float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		int shields = (int)(100.0f * (craftInstance->ShieldPointsFront + craftInstance->ShieldPointsBack) / total_shield_points);
		shields = max(0, shields);

		msg += "\t\t\"name\" : \"" + std::string(name) + "\",\n";
		msg += "\t\t\"short name\" : \"" + std::string(short_name) + "\",\n";
		msg += "\t\t\"IFF\" : " + std::to_string(IFF) + ",\n";
		msg += "\t\t\"cargo\" : \"" + std::string(craftInstance->Cargo) + "\",\n";
		msg += "\t\t\"craft type\" : " + std::to_string(craftInstance->CraftType) + ",\n";
		//msg += "\t\t\"shield direction\" : " + std::to_string(craftInstance->ShieldDirection) + "\n";
		msg += "\t\t\"shields\" : " + std::to_string(shields) + ",\n";
		msg += "\t\t\"hull\" : " + std::to_string(hull) + ",\n";
		// state is 0 when the craft is static
		// state is 3 when the craft is destroyed
		msg += "\t\t\"state\" : " + std::to_string(craftInstance->CraftState) + ",\n";
		//msg += "\t\t\"system\" : " + std::to_string(craftInstance->SystemStrength) + "\n";
		// CycleTime is always 236, CycleTimer counts down from CycleTime to -1 and starts over
		//msg += "\t\t\"cycle time\" : " + std::to_string(craftInstance->CycleTime) + "\n";
		//msg += "\t\t\"cycle timer\" : " + std::to_string(craftInstance->CycleTimer) + "\n";

		//log_debug("[UDP] %s", object->MobileObjectPtr->pChar); // Displays (null)
		//log_debug("[UDP] Target IFF: %d, Team: %d, MarkingColor: %d", object->MobileObjectPtr->IFF, object->MobileObjectPtr->Team, object->MobileObjectPtr->markingColor);
	}

status_section:
	msg += "\t},\n";
	msg += "\t{\n";
	msg += "\t\"status\":\n\t{\n";
	{
		msg += "\t\t\"location\" : ";
		if ((*g_playerInHangar))
			msg += "\"hangar\"\n";
		else
			msg += "\"space\"\n";
	}
	msg += "\t}\n";
	// Final bracket
	msg += "}";

	SendUDPMessage((char *)msg.c_str());
}
#endif

std::string ShieldDirectionStr(int x)
{
	switch (x)
	{
	case 0: return "front";
	case 1: return "even";
	case 2: return "back";
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

std::string ActiveWeaponToString(ActiveWeapon x)
{
	switch (x)
	{
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

#ifdef TELEMETRY_SIMPLIFIED
void SendXWADataOverUDP()
{
	std::string msg = "";

	int shields_front = 0;
	int shields_back  = 0;
	char shipName[TLM_MAX_SHIP_NAME];

	int tgtShds = 0, tgtHull = 0, tgtSys = 0;
	float tgtDist = 0;
	char *tgtName   = nullptr;
	char *tgtCargo  = nullptr;
	char *tgtSubCmp = nullptr;
	shipName[0] = 0;

	const int shake = abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeX) +
		abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeY) +
		abs(PlayerDataTable[*localPlayerIndex].Camera.ShakeZ);

	if (g_pSharedDataTelemetry != nullptr)
	{
		shields_front = g_pSharedDataTelemetry->shieldsFwd;
		shields_back  = g_pSharedDataTelemetry->shieldsBck;
		strncpy_s(shipName, g_pSharedDataTelemetry->shipName, TLM_MAX_SHIP_NAME);

		tgtShds   = g_pSharedDataTelemetry->tgtShds;
		tgtHull   = g_pSharedDataTelemetry->tgtHull;
		tgtSys    = g_pSharedDataTelemetry->tgtSys;
		tgtDist   = g_pSharedDataTelemetry->tgtDist;
		tgtName   = g_pSharedDataTelemetry->tgtName;
		tgtCargo  = g_pSharedDataTelemetry->tgtCargo;
		tgtSubCmp = g_pSharedDataTelemetry->tgtSubCmp;
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
		char* name = (char*)craftDefinition->pCraftName;
		char* short_name = (char*)craftDefinition->pCraftShortName;
		int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		hull = max(0, hull);
		if (hull < g_PrevPlayerTelemetry.hull)
			msg += "player|hulldamage:" + std::to_string(g_PrevPlayerTelemetry.hull - hull) + "\n";
		//float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		//int shields_front = (int)(100.0 * (float)craftInstance->ShieldPointsFront / total_shield_points);
		//int shields_back = (int)(100.0 * (float)craftInstance->ShieldPointsBack / total_shield_points);
		//shields_front = max(0, shields_front);
		//shields_back = max(0, shields_back);
		const int throttle = (int)(100.0f * craftInstance->EngineThrottleInput / 65535.0f);
		const bool underTractorBeam = (craftInstance->IsUnderBeamEffect[1] != 0);
		const bool underJammingBeam = (craftInstance->IsUnderBeamEffect[2] != 0);
		const ActiveWeapon activeWeapon = GetCurrentActiveWeapon();

		if (strncmp(shipName, g_PrevPlayerTelemetry.shipName, TLM_MAX_SHIP_NAME) != 0)
			msg += "player|name:" + std::string(shipName) + "\n";
		if (name != g_PrevPlayerTelemetry.name)
			msg += "player|crafttypename:" + std::string(name) + "\n";
		if (short_name != g_PrevPlayerTelemetry.short_name)
			msg += "player|shortcrafttypename:" + std::string(short_name) + "\n";
		if (speed != g_PrevPlayerTelemetry.speed)
			msg += "player|speed:" + std::to_string(speed) + "\n";
		if (throttle != g_PrevPlayerTelemetry.throttle)
			msg += "player|throttle:" + std::to_string(throttle) + "\n";
		//msg += "\t\t\"current speed\" : " + std::to_string(mobileObject->currentSpeed) + "\n"; // Redundant
		if (craftInstance->ElsLasers != g_PrevPlayerTelemetry.ElsLasers)
			msg += "player|elslasers:" + std::to_string(craftInstance->ElsLasers) + "\n";
		if (craftInstance->ElsShields != g_PrevPlayerTelemetry.ElsShields)
			msg += "player|elsshields:" + std::to_string(craftInstance->ElsShields) + "\n";
		if (craftInstance->ElsBeam != g_PrevPlayerTelemetry.ElsBeam)
			msg += "player|elsbeam:" + std::to_string(craftInstance->ElsBeam) + "\n";
		if (craftInstance->SfoilsState != g_PrevPlayerTelemetry.SfoilsState)
			msg += "player|sfoils:" + std::to_string(craftInstance->SfoilsState) + "\n";
		if (craftInstance->ShieldDirection != g_PrevPlayerTelemetry.ShieldDirection)
			msg += "player|shielddirection:" + ShieldDirectionStr(craftInstance->ShieldDirection) + "\n";
		if (shields_front != g_PrevPlayerTelemetry.shields_front)
			msg += "player|shieldfront:" + std::to_string(shields_front) + "\n";
		if (shields_back != g_PrevPlayerTelemetry.shields_back)
			msg += "player|shieldback:" + std::to_string(shields_back) + "\n";
		if (hull != g_PrevPlayerTelemetry.hull)
			msg += "player|hull:" + std::to_string(hull) + "\n";
		//msg = "player|system:" + std::to_string(craftInstance->SystemStrength);
		if (craftInstance->BeamActive != g_PrevPlayerTelemetry.BeamActive)
			msg += "player|beamactive:" + std::to_string(craftInstance->BeamActive) + "\n";
		//craftInstance->BeamEnergy
		if (shake != g_PrevPlayerTelemetry.shake)
			msg += "player|shake:" + std::to_string(shake) + "\n";

		if (underTractorBeam && !g_PrevPlayerTelemetry.underTractorBeam)
			msg += "player|undertractorbeam:on\n";
		else if (!underTractorBeam && g_PrevPlayerTelemetry.underTractorBeam)
			msg += "player|undertractorbeam:off\n";

		if (underJammingBeam && !g_PrevPlayerTelemetry.underJammingBeam)
			msg += "player|underjammingbeam:on\n";
		else if (!underJammingBeam && g_PrevPlayerTelemetry.underJammingBeam)
			msg += "player|underjammingbeam:off\n";

		if (activeWeapon != g_PrevPlayerTelemetry.activeWeapon)
			msg += "player|activeweapon:" + ActiveWeaponToString(activeWeapon) + "\n";

		//log_debug("[UDP] Throttle: %d", CraftDefinitionTable[objectIndex].EngineThrottle);
		//log_debug("[UDP] Throttle: %s", CraftDefinitionTable[objectIndex].CockpitFileName);
		//log_debug("[UDP] localPlayerIndex: %d, Index: %d", *localPlayerIndex, PlayerDataTable[*localPlayerIndex].objectIndex);
		//Prints 0, 0
		//log_debug("[UDP] CameraIdx: %d", PlayerDataTable[*localPlayerConnectedAs].cameraFG);
		// Prints -1

		// Store the data for the next frame
		g_PrevPlayerTelemetry.name = name;
		g_PrevPlayerTelemetry.short_name = short_name;
		strncpy_s(g_PrevPlayerTelemetry.shipName, shipName, TLM_MAX_SHIP_NAME);
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
	}

	// TARGET SECTION
target_section:
	{
		short currentTargetIndex = PlayerDataTable[*localPlayerIndex].currentTargetIndex;
		// currentTargetIndex can apparently be 0. I remember the game crashing, but it doesn't anymore!
		if (currentTargetIndex < 0) goto status_section;
		ObjectEntry *object = &((*objects)[currentTargetIndex]);
		if (object == NULL) goto status_section;
		MobileObjectEntry *mobileObject = object->MobileObjectPtr;
		if (mobileObject == NULL) goto status_section;
		CraftInstance *craftInstance = mobileObject->craftInstancePtr;
		if (craftInstance == NULL) goto status_section;
		CraftDefinitionEntry *craftDefinition = &(CraftDefinitionTable[craftInstance->CraftType]);
		if (craftDefinition == NULL) goto status_section;
		//char *name = (char *)craftDefinition->pCraftName;
		//char *short_name = (char *)craftDefinition->pCraftShortName;
		int IFF = object->MobileObjectPtr->IFF;
		//int hull = (int)(100.0f * (1.0f - (float)craftInstance->HullDamageReceived / (float)craftInstance->HullStrength));
		//hull = max(0, hull);
		//float total_shield_points = 2.0f * (float)craftDefinition->ShieldHitPoints;
		//int shields = (int)(100.0f * (craftInstance->ShieldPointsFront + craftInstance->ShieldPointsBack) / total_shield_points);
		//shields = max(0, shields);

		/*if (name != g_TargetTelemetry.name)
			msg += "target|crafttypename:" + std::string(name) + "\n";
		if (short_name != g_TargetTelemetry.short_name)
			msg += "target|shortcrafttypename:" + std::string(short_name) + "\n";*/
		if (tgtName != nullptr && strcmp(tgtName, g_TargetTelemetry.name) != 0)
			msg += "target|name:" + std::string(tgtName) + "\n";
		if (IFF != g_TargetTelemetry.IFF)
			msg += "target|IFF:" + std::to_string(IFF) + "\n";
		if (tgtShds != g_TargetTelemetry.shields)
			msg += "target|shields:" + std::to_string(tgtShds) + "\n";
		if (tgtHull != g_TargetTelemetry.hull)
			msg += "target|hull:" + std::to_string(tgtHull) + "\n";
		if (tgtSys != g_TargetTelemetry.sys)
			msg += "target|sys:" + std::to_string(tgtSys) + "\n";
		if (tgtDist != g_TargetTelemetry.dist)
		{
			std::stringstream stream;
			stream << std::fixed << std::setprecision(2) << tgtDist;
			msg += "target|dist:" + stream.str() + "\n";
		}
		if (tgtCargo != nullptr && strcmp(tgtCargo, g_TargetTelemetry.Cargo) != 0)
			msg += "target|cargo:" + std::string(tgtCargo) + "\n";
		if (tgtSubCmp != nullptr && strcmp(tgtSubCmp, g_TargetTelemetry.SubCmp) != 0)
			msg += "target|subcmp:" + std::string(tgtSubCmp) + "\n";
		// state is 0 when the craft is static
		// state is 3 when the craft is destroyed
		/*if (craftInstance->CraftState != g_TargetTelemetry.CraftState)
			msg += "target|state:" + std::to_string(craftInstance->CraftState) + "\n";*/

		// CycleTime is always 236, CycleTimer counts down from CycleTime to -1 and starts over
		//msg += "\t\t\"cycle time\" : " + std::to_string(craftInstance->CycleTime) + "\n";
		//msg += "\t\t\"cycle timer\" : " + std::to_string(craftInstance->CycleTimer) + "\n";

		// Store the data for the next frame
		//g_TargetTelemetry.name = name;
		//g_TargetTelemetry.short_name = short_name;
		g_TargetTelemetry.IFF = IFF;
		if (tgtName)   memcpy(g_TargetTelemetry.name,   tgtName,   TLM_MAX_NAME);
		if (tgtCargo)  memcpy(g_TargetTelemetry.Cargo,  tgtCargo,  TLM_MAX_CARGO);
		if (tgtSubCmp) memcpy(g_TargetTelemetry.SubCmp, tgtSubCmp, TLM_MAX_SUBCMP);
		g_TargetTelemetry.shields = tgtShds;
		g_TargetTelemetry.hull    = tgtHull;
		g_TargetTelemetry.sys     = tgtSys;
		g_TargetTelemetry.dist    = tgtDist;
		//g_TargetTelemetry.CraftState = craftInstance->CraftState;
	}
	
	// STATUS SECTION
status_section:
	{
		if (*g_playerInHangar != g_LocationTelemetry.playerInHangar)
		{
			msg += "status|location:";
			if ((*g_playerInHangar))
				msg += "hangar\n";
			else
				msg += "space\n";
		}

		if (g_HyperspacePhaseFSM != g_PrevHyperspacePhaseFSM)
		{
			msg += "status|location:";
			switch (g_HyperspacePhaseFSM)
			{
			case HS_INIT_ST:
				msg += "space\n";
				break;
			case HS_HYPER_ENTER_ST:
				msg += "hyperentry\n";
				break;
			case HS_HYPER_TUNNEL_ST:
				msg += "hyperspace\n";
				break;
			case HS_HYPER_EXIT_ST:
				msg += "hyperexit\n";
				break;
			}
		}

		g_LocationTelemetry.playerInHangar = *g_playerInHangar;
	}

	int len = msg.length();
	if (len > 0) {
		msg.erase(msg.find_last_not_of(" \t\n") + 1);
		if (msg.length() > 0)
			SendUDPMessage((char *)msg.c_str());
	}
}
#endif
