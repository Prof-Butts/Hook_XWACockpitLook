/*
 * OGS-TS publisher. See OgsPublisher.h.
 *
 * The scheduling, discovery pacing, event replay and socket setup follow
 * ogs-ts-unreal's UTelemetryPublisherComponent; the serializer is the
 * vendored, conformance-locked core in OgsTelemetry/.
 */
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include "OgsPublisher.h"
#include "OgsTelemetry/OgsTelemetryWire.h"
#include "OgsTelemetry/OgsTelemetryFrame.h"

#pragma comment(lib,"ws2_32.lib")

void log_debug(const char *format, ...);

namespace OgsPublisher
{
	using namespace ogs::telemetry;

	namespace
	{
		// Channel ids are the declaration order below.
		enum ChannelId { CH_PAUSED = 0, CH_SPEED, CH_THROTTLE, CH_HULL, CH_SHIELD_F, CH_SHIELD_B, CH_COUNT };

		const float GRAVITY = BodyDynamicsDeriver::Gravity;
		// Tuning values, to be revisited with receiver traces (see XWVM design notes).
		const float FORCE_LIMIT = 2.0f * GRAVITY;  // soft limit on the maneuver force, tanh knee
		const float RATE_LIMIT = 3.0f;             // soft limit on angular rate, rad/s
		const double ENGAGE_SECONDS = 1.5;         // ramp-in after (re)gaining a live sample
		const double STALE_SECONDS = 1.0;          // no simulation step for this long: treat as paused
		const double DISCOVERY_SECONDS = 2.0;

		struct PendingEvent { EventInput ev; double at; bool sent; };

		SOCKET g_sock = INVALID_SOCKET;
		sockaddr_in g_dst = {};
		bool g_running = false;
		bool g_wsa = false;

		double g_hz = 60.0;
		std::string g_gameName, g_typeLabel, g_sentLabel;
		int64_t g_sessionUs = 0;
		uint32_t g_generation = 0;

		LARGE_INTEGER g_qpf = {}, g_qpcOrigin = {};
		double g_clock = 0.0, g_lastClock = 0.0;
		double g_lastDiscovery = -1e18, g_lastMotion = -1e18, g_lastEventSend = -1e18;
		bool g_discoverySent = false;
		std::vector<std::vector<uint8_t>> g_pages;
		bool g_pagesDirty = true;

		WireBuilder g_wire;
		BodyDynamicsDeriver g_deriver;

		// Motion state
		bool g_haveStamp = false;
		unsigned int g_lastStamp = 0;
		double g_lastStepClock = -1e18, g_lastSampleClock = 0.0;
		bool g_wasLive = false;
		double g_engage = 0.0;
		Vec3 g_pos = {};
		Quat g_q = { 0, 0, 0, 1 };
		Vec3 g_force = { 0, GRAVITY, 0 };  // conditioned specific force, body frame
		Vec3 g_rates = {};                 // conditioned angular rate, body frame
		Vec3 g_localGravity = { 0, -GRAVITY, 0 };
		bool g_pausedNow = true;
		float g_values[CH_COUNT] = {};

		uint32_t g_nextEventId = 1;
		std::vector<EventInput> g_pendingEvents;
		std::vector<PendingEvent> g_recent;

		double NowSeconds()
		{
			LARGE_INTEGER t;
			QueryPerformanceCounter(&t);
			return (double)(t.QuadPart - g_qpcOrigin.QuadPart) / (double)g_qpf.QuadPart;
		}

		float Len(const Vec3 &v) { return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z); }
		Vec3 Scale(const Vec3 &v, float s) { return Vec3{ v.X * s, v.Y * s, v.Z * s }; }

		Vec3 SoftLimit(const Vec3 &v, float limit)
		{
			const float m = Len(v);
			if (m < 1e-6f) return v;
			return Scale(v, limit * std::tanh(m / limit) / m);
		}

		bool Finite(float x) { return std::isfinite(x) != 0; }

		// Rotation matrix with columns (right, up, fwd) -> unit quaternion.
		bool QuatFromBasis(const float R[3], const float U[3], const float F[3], Quat &out)
		{
			const float m00 = R[0], m01 = U[0], m02 = F[0];
			const float m10 = R[1], m11 = U[1], m12 = F[1];
			const float m20 = R[2], m21 = U[2], m22 = F[2];
			Quat q;
			const float tr = m00 + m11 + m22;
			if (tr > 0.0f)
			{
				const float s = std::sqrt(tr + 1.0f) * 2.0f;
				q.W = 0.25f * s;
				q.X = (m21 - m12) / s;
				q.Y = (m02 - m20) / s;
				q.Z = (m10 - m01) / s;
			}
			else if (m00 > m11 && m00 > m22)
			{
				const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
				q.W = (m21 - m12) / s;
				q.X = 0.25f * s;
				q.Y = (m01 + m10) / s;
				q.Z = (m02 + m20) / s;
			}
			else if (m11 > m22)
			{
				const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
				q.W = (m02 - m20) / s;
				q.X = (m01 + m10) / s;
				q.Y = 0.25f * s;
				q.Z = (m12 + m21) / s;
			}
			else
			{
				const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
				q.W = (m10 - m01) / s;
				q.X = (m02 + m20) / s;
				q.Y = (m12 + m21) / s;
				q.Z = 0.25f * s;
			}
			const float n = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
			if (!(n > 1e-6f) || !Finite(n)) return false;
			out = Quat{ q.X / n, q.Y / n, q.Z / n, q.W / n };
			return true;
		}

		void Send(const std::vector<uint8_t> &d)
		{
			if (g_sock == INVALID_SOCKET || d.empty()) return;
			sendto(g_sock, (const char *)d.data(), (int)d.size(), 0, (const sockaddr *)&g_dst, sizeof g_dst);
		}

		// Names the default route's interface rather than leaving the choice to the OS
		// (implementation-udp.md, Network interfaces). Best effort.
		void SendOnDefaultRoute(SOCKET sock)
		{
			SOCKET probe = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (probe == INVALID_SOCKET) return;
			sockaddr_in remote = {};
			remote.sin_family = AF_INET;
			remote.sin_port = htons(9);
			inet_pton(AF_INET, "192.0.2.1", &remote.sin_addr); // RFC 5737 documentation address
			sockaddr_in local = {};
			int length = sizeof local;
			if (connect(probe, (const sockaddr *)&remote, sizeof remote) == 0
				&& getsockname(probe, (sockaddr *)&local, &length) == 0
				&& local.sin_addr.s_addr != htonl(INADDR_ANY)
				&& local.sin_addr.S_un.S_un_b.s_b1 != 127)
			{
				setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&local.sin_addr, sizeof local.sin_addr);
			}
			closesocket(probe);
		}

		void RebuildPages()
		{
			ObjectDecl obj;
			obj.Name = "player_craft";
			obj.Location = "local";
			obj.TypeLabel = g_typeLabel;
			obj.Type = (uint16_t)schema::ObjectType::Spacecraft;
			obj.Primary = true;

			auto number = [&](const char *name, const char *unit, double lo, double hi, const char *desc)
			{
				ChannelDecl c;
				c.Name = name; c.Unit = unit; c.Description = desc;
				c.Min = lo; c.Max = hi; c.HasRange = true;
				c.Type = schema::ChannelType::Number;
				obj.Channels.push_back(c);
			};
			ChannelDecl paused;
			paused.Name = "session.paused";
			paused.Description = "Simulation paused or not yet live for the player (C-11)";
			paused.Type = schema::ChannelType::Boolean;
			obj.Channels.push_back(paused);
			number("vehicle.speed", "m/s", 0.0, 1000.0, "Speed along the craft's forward axis");
			number("engine.throttle", "", 0.0, 1.0, "Pilot throttle");
			number("xwa.ship.hull", "", 0.0, 1.0, "Hull integrity");
			number("xwa.ship.shield.front", "", 0.0, 1.0, "Front shield level");
			number("xwa.ship.shield.rear", "", 0.0, 1.0, "Rear shield level");

			// Cabin gravity is declared per frame (vessel-fixed, C-12); the world
			// itself is a vacuum with none.
			Environment env;
			env.SetGravity(Vec3{ 0, 0, 0 });
			env.Medium = 10; // vacuum

			Instance inst;
			inst.MotionRateHz = (float)g_hz;
			g_pages = PaginateDiscovery(1, g_sessionUs, g_gameName, env, { obj }, g_generation,
				kDatagramBudget, inst);
			g_sentLabel = g_typeLabel;
			g_pagesDirty = false;
		}

		void SendDiscoveryIfDue()
		{
			if (g_discoverySent && g_typeLabel != g_sentLabel)
			{
				g_generation++;
				g_pagesDirty = true;
				g_lastDiscovery = -1e18;
			}
			const bool due = g_clock - g_lastDiscovery >= DISCOVERY_SECONDS;
			if (!due && !g_pagesDirty) return;
			if (g_pagesDirty) RebuildPages();
			for (const auto &p : g_pages) Send(p);
			g_discoverySent = true;
			g_lastDiscovery = g_clock;
		}

		void SendMotion()
		{
			MotionObjectInput o;
			o.Name = "player_craft";
			o.Position = g_pos;
			o.Orientation = g_q;
			o.HasLocalGravity = true;
			o.LocalGravity = g_localGravity;
			if (!g_pausedNow)
			{
				o.HasBodyDynamics = true;
				o.SpecificForce = g_force;
				o.AngularVelocity = g_rates;
			}
			for (int i = 0; i < CH_COUNT; i++)
			{
				ChannelValueInput c;
				c.Id = (uint16_t)i;
				if (i == CH_PAUSED) { c.IsBool = true; c.Boolean = g_pausedNow; }
				else c.Number = g_values[i];
				o.Channels.push_back(c);
			}
			BuildMotion(g_wire, g_clock, { o }, g_generation);
			Send(g_wire.Wrap(FrameType::Motion));
		}

		void SendEventsIfDue()
		{
			for (auto &e : g_pendingEvents) g_recent.push_back(PendingEvent{ e, g_clock, false });
			g_pendingEvents.clear();
			// an event leaves once its replay window has passed AND it has been sent once
			while (!g_recent.empty() && g_recent.front().sent && g_clock - g_recent.front().at > 0.100)
				g_recent.erase(g_recent.begin());
			if (g_recent.empty() || g_clock - g_lastEventSend < 0.050) return;
			std::vector<EventInput> evs;
			for (auto &r : g_recent) { evs.push_back(r.ev); r.sent = true; }
			for (const auto &run : PlanEventFrames(evs, g_clock))
			{
				BuildEventFrame(g_wire, g_clock,
					std::vector<EventInput>(evs.begin() + (std::ptrdiff_t)run.first,
						evs.begin() + (std::ptrdiff_t)(run.first + run.second)));
				Send(g_wire.Wrap(FrameType::Event));
			}
			g_lastEventSend = g_clock;
		}

		void ClearEvents()
		{
			g_pendingEvents.clear();
			g_recent.clear();
		}
	} // namespace

	bool Running() { return g_running; }

	bool Start(const char *group, int port, double motionHz, const char *gameName)
	{
		if (g_running) return true;
		WSADATA wsa = {};
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
		g_wsa = true;
		g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (g_sock == INVALID_SOCKET) { WSACleanup(); g_wsa = false; return false; }

		DWORD ttl = 1;
		setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof ttl);
		int loop = 1;
		setsockopt(g_sock, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof loop);
		g_dst = {};
		g_dst.sin_family = AF_INET;
		g_dst.sin_port = htons((u_short)port);
		if (inet_pton(AF_INET, group, &g_dst.sin_addr) != 1)
		{
			log_debug("[OGS] Invalid address: %s", group);
			closesocket(g_sock); g_sock = INVALID_SOCKET; WSACleanup(); g_wsa = false;
			return false;
		}
		if (IN_MULTICAST(ntohl(g_dst.sin_addr.s_addr))) SendOnDefaultRoute(g_sock);

		g_hz = std::max(1.0, motionHz);
		g_gameName = gameName ? gameName : "X-Wing Alliance";
		g_typeLabel.clear();
		g_sentLabel.clear();
		g_sessionUs = (int64_t)time(nullptr) * 1000000;
		g_generation = 0;
		QueryPerformanceFrequency(&g_qpf);
		QueryPerformanceCounter(&g_qpcOrigin);
		g_clock = g_lastClock = 0.0;
		g_lastDiscovery = g_lastMotion = g_lastEventSend = -1e18;
		g_discoverySent = false;
		g_pagesDirty = true;
		g_haveStamp = false;
		g_lastStepClock = -1e18;
		g_wasLive = false;
		g_engage = 0.0;
		g_pos = Vec3{};
		g_q = Quat{ 0, 0, 0, 1 };
		g_force = Vec3{ 0, GRAVITY, 0 };
		g_rates = Vec3{};
		g_localGravity = Vec3{ 0, -GRAVITY, 0 };
		g_pausedNow = true;
		for (float &v : g_values) v = 0.0f;
		g_nextEventId = 1;
		ClearEvents();
		g_deriver = BodyDynamicsDeriver{};
		g_running = true;
		log_debug("[OGS] Publishing to %s:%d at %0.1f Hz", group, port, g_hz);
		return true;
	}

	void Stop()
	{
		if (g_sock != INVALID_SOCKET) { closesocket(g_sock); g_sock = INVALID_SOCKET; }
		if (g_wsa) { WSACleanup(); g_wsa = false; }
		g_running = false;
	}

	void FireImpact(float intensity, float dx, float dy, float dz, bool hasDirection)
	{
		if (!g_running || g_pausedNow) return; // no stimuli while actuators settle (C-11)
		EventInput e;
		e.Id = g_nextEventId++;
		e.Name = "body.impact";
		e.Intensity = std::min(1.0f, std::max(0.0f, intensity));
		e.Duration = 0.0f;
		if (hasDirection) { e.Direction = Vec3{ dx, dy, dz }; e.HasDirection = true; }
		g_pendingEvents.push_back(e);
	}

	void Update(const Sample &s)
	{
		if (!g_running) return;
		g_lastClock = g_clock;
		g_clock = NowSeconds();

		if (s.typeLabel && g_typeLabel != s.typeLabel) g_typeLabel = s.typeLabel;
		SendDiscoveryIfDue();

		// A simulation step is a change of the caller's stamp. The game steps its
		// simulation at its own rate, so differencing on each rendered frame would
		// alternate zeros and spikes.
		const bool stepped = s.valid && (!g_haveStamp || s.stamp != g_lastStamp);
		if (stepped)
		{
			g_haveStamp = true;
			g_lastStamp = s.stamp;
			g_lastStepClock = g_clock;
		}
		const bool stale = (g_clock - g_lastStepClock) > STALE_SECONDS;

		Quat q;
		const bool basisOk = s.valid && QuatFromBasis(s.right, s.up, s.fwd, q)
			&& Finite(s.speedMs) && Finite(s.fwd[0]) && Finite(s.fwd[1]) && Finite(s.fwd[2]);
		const bool live = basisOk && !s.paused && !s.inactive && !stale;

		if (!live)
		{
			if (g_wasLive)
			{
				// Going inactive: keep the last pose, drop the cues, forget history so
				// resuming never differentiates across the gap.
				g_deriver = BodyDynamicsDeriver{};
				ClearEvents();
			}
			g_engage = 0.0;
			g_force = Vec3{ 0, GRAVITY, 0 };
			g_rates = Vec3{};
		}
		else
		{
			if (!g_wasLive)
			{
				g_deriver = BodyDynamicsDeriver{};
				g_lastSampleClock = g_clock;
				g_engage = 0.0;
			}
			if (stepped)
			{
				// keep the quaternion in the same hemisphere as the last one
				if (g_wasLive && (q.X * g_q.X + q.Y * g_q.Y + q.Z * g_q.Z + q.W * g_q.W) < 0.0f)
					q = Quat{ -q.X, -q.Y, -q.Z, -q.W };

				const Vec3 fwd{ s.fwd[0], s.fwd[1], s.fwd[2] };
				const Vec3 vel = Scale(fwd, s.speedMs);
				const double sdt = g_clock - g_lastSampleClock;
				g_lastSampleClock = g_clock;
				if (g_wasLive && sdt > 0.0)
					g_pos = Vec3{ g_pos.X + vel.X * (float)sdt, g_pos.Y + vel.Y * (float)sdt, g_pos.Z + vel.Z * (float)sdt };

				// Vessel-fixed gravity: the cabin's down is the craft's own -Y.
				g_localGravity = Rotate(q, Vec3{ 0, -GRAVITY, 0 });
				g_deriver.GravityVector = g_localGravity;
				g_deriver.Sample(sdt, g_pos, q, true, vel);
				g_q = q;

				const Vec3 baseline{ 0, GRAVITY, 0 };
				const Vec3 maneuver = SoftLimit(Vec3{ g_deriver.SpecificForce.X - baseline.X,
					g_deriver.SpecificForce.Y - baseline.Y,
					g_deriver.SpecificForce.Z - baseline.Z }, FORCE_LIMIT);
				const Vec3 rates = SoftLimit(g_deriver.AngularVelocity, RATE_LIMIT);
				const float e = (float)std::min(1.0, g_engage);
				g_force = Vec3{ baseline.X + e * maneuver.X, baseline.Y + e * maneuver.Y, baseline.Z + e * maneuver.Z };
				g_rates = Scale(rates, e);
			}
			g_engage = std::min(1.0, g_engage + (g_clock - g_lastClock) / ENGAGE_SECONDS);
		}
		g_wasLive = live;
		g_pausedNow = !live;

		auto clamp01 = [](float x) { return Finite(x) ? std::min(1.0f, std::max(0.0f, x)) : 0.0f; };
		g_values[CH_SPEED] = live ? std::max(0.0f, s.speedMs) : 0.0f;
		g_values[CH_THROTTLE] = clamp01(s.throttle);
		g_values[CH_HULL] = clamp01(s.hull);
		g_values[CH_SHIELD_F] = clamp01(s.shieldFront);
		g_values[CH_SHIELD_B] = clamp01(s.shieldBack);

		// Motion, scheduled by accumulation so the long-run rate holds at any frame rate.
		const double interval = 1.0 / g_hz;
		const double due = g_lastMotion + interval;
		if (g_clock >= due - 1e-6)
		{
			SendMotion();
			g_lastMotion = (g_lastMotion < -1e17 || g_clock - due > interval) ? g_clock : due;
		}
		SendEventsIfDue();
	}
} // namespace OgsPublisher
