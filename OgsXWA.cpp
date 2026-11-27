/*
 * Reads the player's craft from the XWA game structures and feeds the OGS-TS
 * publisher. Enabled with UDP_telemetry_enabled = 1 and UDP_telemetry_format = OGS.
 *
 * Several scales here are assumptions awaiting in-game calibration:
 *   - ogs_speed_scale: metres per second per displayed speed unit (currentSpeed / 2.25)
 *   - shields are read as percent (0..100) from the shared telemetry memory
 *   - hyperspacePhase != 0 means "in hyperspace" and the stream goes inactive
 */
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include "XWAObject.h"
#include "SharedMem.h"
#include "UDP.h"
#include "OgsPublisher.h"

void log_debug(const char *format, ...);

extern const int *localPlayerIndex;
extern ObjectEntry **objects;
extern PlayerDataEntry *PlayerDataTable;
extern CraftDefinitionEntry *CraftDefinitionTable;

// cockpitlook.cpp: craft axes in the OGS-TS standard frame.
bool GetCraftBasisStd(int playerIndex, float R[3], float U[3], float F[3]);

// Fixed by the standard (implementation-udp.md): not configurable.
static const char *OGS_GROUP = "239.255.17.17";
static const int   OGS_PORT = 40123;

float g_fOGSMotionHz = 60.0f;
float g_fOGSSpeedScale = 1.0f; // m/s per displayed speed unit; unverified

bool OgsXWAStart()
{
	if (!OgsPublisher::Start(OGS_GROUP, OGS_PORT, g_fOGSMotionHz, "X-Wing Alliance")) {
		log_debug("[OGS] Failed to start publisher");
		return false;
	}
	return true;
}

void OgsXWAStop()
{
	OgsPublisher::Stop();
}

static uint32_t Mix(uint32_t h, uint32_t v)
{
	h ^= v;
	h *= 16777619u;
	return h;
}

static float Clamp01(float x)
{
	return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

void UpdateOGSTelemetry()
{
	if (!OgsPublisher::Running()) return;

	OgsPublisher::Sample s = {};
	s.throttle = s.hull = s.shieldFront = s.shieldBack = -1.0f;

	if (localPlayerIndex == nullptr || objects == nullptr || PlayerDataTable == nullptr) {
		OgsPublisher::Update(s);
		return;
	}
	const int playerIndex = *localPlayerIndex;
	const PlayerDataEntry &pd = PlayerDataTable[playerIndex];
	const int16_t objectIndex = (int16_t)pd.objectIndex;
	static int16_t lastObjectIndex = -1;
	static int lastShdF = -1, lastShdB = -1;
	static long long lastHullDamage = -1;
	static bool lastCollision = false;

	ObjectEntry *object = objectIndex >= 0 ? &((*objects)[objectIndex]) : nullptr;
	MobileObjectEntry *mo = object ? object->MobileObjectPtr : nullptr;
	CraftInstance *ci = mo ? mo->craftInstancePtr : nullptr;
	if (object == nullptr || mo == nullptr || ci == nullptr) {
		lastObjectIndex = -1;
		OgsPublisher::Update(s);
		return;
	}

	// A new craft (respawn, different ship) restarts the impact trackers.
	if (objectIndex != lastObjectIndex) {
		lastObjectIndex = objectIndex;
		lastShdF = lastShdB = -1;
		lastHullDamage = -1;
		lastCollision = false;
	}

	s.valid = GetCraftBasisStd(playerIndex, s.right, s.up, s.fwd);
	s.inactive = (pd.hyperspacePhase != 0);
	s.speedMs = ((float)pd.currentSpeed / 2.25f) * g_fOGSSpeedScale;

	s.stamp = 2166136261u;
	s.stamp = Mix(s.stamp, (uint32_t)object->x);
	s.stamp = Mix(s.stamp, (uint32_t)object->y);
	s.stamp = Mix(s.stamp, (uint32_t)object->z);
	s.stamp = Mix(s.stamp, (uint32_t)(uint16_t)object->yaw);
	s.stamp = Mix(s.stamp, (uint32_t)(uint16_t)object->pitch);
	s.stamp = Mix(s.stamp, (uint32_t)(uint16_t)object->roll);
	s.stamp = Mix(s.stamp, (uint32_t)mo->lastUpdateTime);
	s.stamp = Mix(s.stamp, (uint32_t)(uint16_t)pd.currentSpeed);

	s.throttle = Clamp01(ci->EngineThrottleInput / 65535.0f);
	const float hullStrength = (float)ci->HullStrength;
	if (hullStrength > 0.0f)
		s.hull = Clamp01(1.0f - (float)ci->HullDamageReceived / hullStrength);

	int shdF = -1, shdB = -1;
	if (g_pSharedDataTelemetry != nullptr) {
		shdF = g_pSharedDataTelemetry->shieldsFwd;
		shdB = g_pSharedDataTelemetry->shieldsBck;
		if (shdF >= 0) s.shieldFront = Clamp01(shdF / 100.0f);
		if (shdB >= 0) s.shieldBack = Clamp01(shdB / 100.0f);
	}

	if (CraftDefinitionTable != nullptr) {
		CraftDefinitionEntry *def = &(CraftDefinitionTable[ci->CraftType]);
		if (def != nullptr && def->pCraftShortName != nullptr)
			s.typeLabel = (const char *)def->pCraftShortName;
	}

	OgsPublisher::Update(s);

	// Impact events. A drop in a shield facing is a hit from that side (front hit:
	// source ahead, +Z in body axes); a hull loss without a shield drop and a new
	// collision carry no direction.
	bool hit = false;
	if (lastShdF >= 0 && shdF >= 0 && shdF < lastShdF) {
		OgsPublisher::FireImpact((float)(lastShdF - shdF) / 20.0f + 0.15f, 0.0f, 0.0f, 1.0f, true);
		hit = true;
	}
	if (lastShdB >= 0 && shdB >= 0 && shdB < lastShdB) {
		OgsPublisher::FireImpact((float)(lastShdB - shdB) / 20.0f + 0.15f, 0.0f, 0.0f, -1.0f, true);
		hit = true;
	}
	if (lastHullDamage >= 0 && (long long)ci->HullDamageReceived > lastHullDamage && !hit && hullStrength > 0.0f) {
		const float frac = (float)((long long)ci->HullDamageReceived - lastHullDamage) / hullStrength;
		OgsPublisher::FireImpact(0.2f + 10.0f * frac, 0.0f, 0.0f, 0.0f, false);
	}
	const bool collision = (mo->inCollision != 0);
	if (collision && !lastCollision)
		OgsPublisher::FireImpact(0.7f, 0.0f, 0.0f, 0.0f, false);

	lastShdF = shdF;
	lastShdB = shdB;
	lastHullDamage = (long long)ci->HullDamageReceived;
	lastCollision = collision;
}
