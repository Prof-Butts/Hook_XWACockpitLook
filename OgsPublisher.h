#pragma once
/*
 * OGS Telemetry Standard (OGS-TS) publisher for the XWA hook.
 *
 * The interface uses plain types only. The vendored OGS core headers
 * (OgsTelemetry/, C++17) are included from OgsPublisher.cpp alone.
 *
 * Frame: the caller supplies the craft's axes in the OGS-TS standard frame
 * (left-handed, +X right, +Y up, +Z forward). Velocity is the forward axis
 * times the speed, and the published position is that velocity integrated from
 * a session-local origin, so no absolute game coordinates are needed.
 *
 * Motion cueing follows the XWVM design: the pose carries the craft's true
 * orientation, gravity is vessel-fixed (local gravity points down the craft's
 * own -Y, so the specific force at rest is +g up whatever the attitude), and the
 * cues are specific force plus angular rate, soft-limited and ramped in.
 */

namespace OgsPublisher
{
	struct Sample
	{
		bool  valid;           // false: no usable craft this tick (menus, loading)
		bool  paused;          // the simulation is known to be paused
		bool  inactive;        // stream but report no motion (hyperspace, etc.)
		unsigned int stamp;    // changes whenever the game simulation has stepped
		float right[3], up[3], fwd[3]; // craft axes in the standard frame
		float speedMs;         // speed along the forward axis, m/s
		float throttle;        // 0..1, <0 if unknown
		float hull;            // 0..1, <0 if unknown
		float shieldFront;     // 0..1, <0 if unknown
		float shieldBack;      // 0..1, <0 if unknown
		const char *typeLabel; // craft type, e.g. "X-Wing"
	};

	bool Start(const char *group, int port, double motionHz, const char *gameName);
	void Stop();
	bool Running();

	// Call once per game frame.
	void Update(const Sample &s);

	// Discrete stimulus (the player was hit). Direction is toward the source, in
	// the craft's body axes (standard frame); ignored unless hasDirection.
	void FireImpact(float intensity, float dirX, float dirY, float dirZ, bool hasDirection);
}
