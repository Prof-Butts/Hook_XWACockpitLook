#pragma once
#include "hook_function.h"
#include "cockpitlook.h"


static const HookFunction g_hookFunctions[] =
{
	{ 0x4F9A66, CockpitLookHook },

};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	{ 0xF8E61, "66399369A08B00", "E8BAF00A009090" },
};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
