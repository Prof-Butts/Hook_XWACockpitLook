#pragma once
#include "hook_function.h"
#include "cockpitlook.h"


static const HookFunction g_hookFunctions[] =
{
	{ 0x4F9A66, CockpitLookHook },
	//{ 0x4F219B, PlayerCameraUpdateHook }, // Fix according to RandomStarfighter's instructions
	{ 0x497ABB, CockpitPositionTransformHook },
	{ 0x43FB57, UpdateCameraTransformHook }, //DoRotation(Pitch)
	//{ 0x43FB67, UpdateCameraTransformHook }, //DoRotation(Yaw)
};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	// First hook in PlayerInflightSystemInput to overwrite the mouselook values for pitch,yaw.
	{ 0xF8E61, "66399369A08B00", "E8BAF00A009090" },

	// Hook last call to Vector3Transform() inside UpdatePlayerMovement() that provides the values for CockpitPositionTransformed.
	// To add positional tracking. This only works in cockpit view mode!
	{ 0x497AB6 - 0x400C00, "E87520FAFF", "E865101100"},

	// Hook call to DoRotation(Pitch) inside UpdateCameraTransform (0x43FB52)
	// jmp ((0x5A8B20 - 0x43FB57) = 0x168FC9
	{ 0x43FB52 - 0x400C00, "E8E9120000", "E8C98F1600"},

	// Hook call to DoRotation(Yaw) inside UpdateCameraTransform (0x43FB52)
	//{ 0x43FB62 - 0x400C00, "E8D9120000", "E8B98F1600"},
};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
