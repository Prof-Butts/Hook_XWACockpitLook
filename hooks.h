#pragma once
#include "hook_function.h"
#include "cockpitlook.h"


static const HookFunction g_hookFunctions[] =
{
	{ 0x4F9A66, CockpitLookHook },
	{ 0x4F219B, PlayerCameraUpdateHook }, // Fix according to RandomStarfighter's instructions
};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	// First hook in PlayerInflightSystemInput to overwrite the mouselook values for pitch,yaw.
	{ 0xF8E61, "66399369A08B00", "E8BAF00A009090" },
	// Second hook in PlayerCameraUpdate to add positional tracking (credit: RandomStarfighter)
	{ 0x004F2196 - 0x400C00, "E885C6FFFF", "E885690B00"},
	
	// Prevent a 4 bit shift applied to shake values that kills precision (didn't work)
	{ 0xEDE8F, "C1F804", "909090"},
	{ 0xEDEA6, "C1F904", "909090"},
	{ 0xEDEBD, "C1F804", "909090"}
};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
