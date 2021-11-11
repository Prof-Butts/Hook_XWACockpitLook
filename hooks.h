#pragma once
#include "hook_function.h"
#include "cockpitlook.h"


static const HookFunction g_hookFunctions[] =
{
	{ 0x4F9A66, CockpitLookHook },
	//{ 0x4F219B, PlayerCameraUpdateHook }, // Fix according to RandomStarfighter's instructions
	{ 0x497ABB, CockpitPositionTransformHook },
};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	// First hook in PlayerInflightSystemInput to overwrite the mouselook values for pitch,yaw.
	{ 0xF8E61, "66399369A08B00", "E8BAF00A009090" },
	// Second hook in PlayerCameraUpdate to add positional tracking (credit: RandomStarfighter)
	//{ 0x4F2196 - 0x400C00, "E885C6FFFF", "E885690B00"},
	// Hook last call to Vector3Transform() inside UpdatePlayerMovement() that provides the values for CockpitPositionTransformed()
	{ 0x497AB6 - 0x400C00, "E87520FAFF", "E865101100"},

};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
