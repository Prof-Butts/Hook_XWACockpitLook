#pragma once
#include "hook_function.h"
#include "cockpitlook.h"
#include "config.h"

class HooksConfig
{
public:
	HooksConfig()
	{
		auto lines = GetFileLines("CockpitLook.cfg");

		if (lines.empty())
		{
			lines = GetFileLines("hooks.ini", "hook_cockpitlook");
		}

		this->DisableRandomCamera = GetFileKeyValueInt(lines, DISABLE_HANGAR_RANDOM_CAMERA, 0) != 0;
	}

	bool DisableRandomCamera;
};

static HooksConfig g_hooksConfig;

static const HookFunction g_hookFunctions[] =
{
	//{ 0x4F9A66, CockpitLookHook },
	{ 0x4EE9DA, UpdateCameraTransformHook }, //UpdateCameraTransform for cockpit view
	{ 0x4EEB56, UpdateCameraTransformHook }, //UpdateCameraTransform for external camera
	{ 0x4EF044, UpdateCameraTransformHook }, //UpdateCameraTransform for no cockpit camera
	{ 0x459AC7, UpdateCameraTransformHook }, //UpdateCameraTransform for hangar external camera
	{ 0x4EE844, MapCameraUpdateHook}, //UpdateCameraTransform for map
	{ 0x497ABB, CockpitPositionTransformHook },
	{ 0x43FB57, DoRotationPitchHook}, //DoRotation(Pitch)
	{ 0x43FB67, DoRotationYawHook }, //DoRotation(Yaw)
	{ 0x4654A6, SetupReticleHook }, //TransformVector() inside SetupReticle()
};

static const HookPatchItem g_patch[] =
{
	//////////////////// Hook Patches ////////////////////

	// Entry
	// First hook in PlayerInflightSystemInput to overwrite the mouselook values for pitch,yaw.
	//{ 0xF8E61, "66399369A08B00", "E8BAF00A009090" },

	// Hook calls to UpdateCameraTransform inside PlayerCameraUpdate()
	// call (0x5A8B20 - 0x4EE9DA) = 0x0BA146
	{ 0x4EE9D5 - 0x400C00, "E8060FF5FF", "E846A10B00"},
	// call (0x5A8B20 - 0x4EEB56) = 0x0B9FCA
	{ 0x4EEB51 - 0x400C00, "E88A0DF5FF", "E8CA9F0B00"},
	// call (0x5A8B20 - 0x4EF044) = 0x0B9ADC
	{ 0x4EF03F - 0x400C00, "E89C08F5FF", "E8DC9A0B00"},
	
	// Hook calls to UpdateCameraTransform inside DrawHangarView()
	// call (0x5A8B20 - 0x459AC7) = 0x14F059
	{ 0x459AC2 - 0x400C00, "E8195EFEFF", "E859F01400"},

	// Hook call to MapCameraUpdate (0x49EE90) inside PlayerCameraUpdate()
	// call (0x5A8B20 - 0x4EE844) = 0x107804
	{ 0x4EE83F - 0x400C00, "E84C06FBFF", "E8DCA20B00"},

	// Hook last call to Vector3Transform() inside UpdatePlayerMovement() that provides the values for CockpitPositionTransformed.
	// To add positional tracking. This only works in cockpit view mode!
	{ 0x497AB6 - 0x400C00, "E87520FAFF", "E865101100"},

	// Hook call to DoRotation(Pitch) inside UpdateCameraTransform (0x43FB52)
	// call ((0x5A8B20 - 0x43FB57) = 0x168FC9
	{ 0x43FB52 - 0x400C00, "E8E9120000", "E8C98F1600"},

	// Hook call to DoRotation(Yaw) inside UpdateCameraTransform (0x43FB52)
	// call((0x5A8B20 - 0x43FB67) = 0x168FB9
	{ 0x43FB62 - 0x400C00, "E8D9120000", "E8B98F1600"},

	// Hook call to TransformVector() inside SetupReticle () (0x4652F0)
	// call((0x5A8B20 - 0x4654A6) = 0x168FC9
	{ 0x4654A1 - 0x400C00, "E80ADB0300", "E87A361400"},

	//Patches for disabling random camera switch in the hangar
	//Pre-launch
	{0x05B583,"E8B83A0000", g_hooksConfig.DisableRandomCamera ? "9090909090" : "E8B83A0000"},
	//Launch
	{0x05AD61,"E8DA420000", g_hooksConfig.DisableRandomCamera ? "9090909090" : "E8DA420000"},
	//Re-entry
	{0x057866,"E8D5770000", g_hooksConfig.DisableRandomCamera ? "9090909090" : "E8D5770000"},
};

static const HookPatch g_patches[] =
{
	MAKE_HOOK_PATCH("XWA Cockpit Look", g_patch),
};
