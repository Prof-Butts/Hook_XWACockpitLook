#pragma once
#include "hook_function.h"
#include "cockpitlook.h"


static const HookFunction g_hookFunctions[] =
{
	{ 0x4F9A5F, CockpitLookHook },

};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	{ 0xF8E5A, "E8A11EF3FF", "E8C1F00A00" },
};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
