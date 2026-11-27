// OgsTelemetryFrame.h — Unreal Engine → OGS-TS frame conversion.
//
// UE is left-handed, X-forward, Y-right, Z-up. OGS-TS is left-handed,
// Z-forward, X-right, Y-up. The map is the cyclic axis permutation
//   (x, y, z)_std = (y, z, x)_ue — an even permutation, so handedness
//   is preserved (both left-handed).
// For orientation, the component map is the INVERSE cycle of q_p's
// rotation (q_p cycles X→Y→Z; the map cycles X→Z→Y), so the converted
// rotation is
//   q_std = q_p⁻¹ · q_ue · q_p
// (verified: identity fixed; UE yaw about Z-up rotates standard forward
// toward standard right — equal angle, equal sense; norm preserved).
#pragma once
#include <cmath>
#include "OgsTelemetryWire.h"

namespace ogs::telemetry
{

inline Vec3 ToStandard(const Vec3& ue) { return { ue.Y, ue.Z, ue.X }; }

inline Quat Multiply(const Quat& a, const Quat& b)
{
    // (w1,v1)*(w2,v2) with Hamilton product
    float aw = a.W, bw = b.W;
    float ax = a.X, ay = a.Y, az = a.Z;
    float bx = b.X, by = b.Y, bz = b.Z;
    return {
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    };
}

inline Quat ToStandard(const Quat& ue)
{
    static constexpr Quat Qp{ 0.5f, 0.5f, 0.5f, 0.5f };
    static constexpr Quat QpInv{ -0.5f, -0.5f, -0.5f, 0.5f };
    return Multiply(Multiply(QpInv, ue), Qp);
}

// rotate v by q (v' = q v q*)
inline Vec3 Rotate(const Quat& q, const Vec3& v)
{
    Quat p{ v.X, v.Y, v.Z, 0.0f };
    Quat qc{ -q.X, -q.Y, -q.Z, q.W };
    Quat r = Multiply(Multiply(q, p), qc);
    return { r.X, r.Y, r.Z };
}

// ---- pose → body-dynamics derivation ---------------------------------
// C++ twin of Ogs.Telemetry.BodyDynamicsDeriver (ogs-ts-dotnet); the
// two must stay behaviourally identical — that project's tests pin the
// semantics, and this twin serves the injected senders (uevr-ogs-ts,
// ue4ss-ogs-ts) whose games expose no accelerometer-class data.
//
// Semantics (schema bodyDynamics variant, accelerometer-style):
//   specificForce = a_world − g_world — +|g| on +heave at rest (what an
//   IMU reads), rotated into the C-4 body frame by the inverse of the
//   reported orientation (v_world = q · v_body for actor-style rotations
//   in the standard frame). g_world = GravityVector: standard gravity
//   along world −Y by default; set it to the gravity the stream declares
//   for the object (CONVENTIONS C-12).
//   angularVelocity = log(q_t⁻¹ ⊗ q_t₊₁)/dt, artifact-free at attitude
//   representation flips (unlike Euler differentiation).
//
// C-4 component sense (the subtle part): the receiver's two paths must
// agree — native ω versus d(euler)/dt of its EulerC4 extraction
// (pitch = asin(f.y) nose-up+, yaw = atan2(f.x, f.z) nose-right+,
// roll = atan2(u.x, u.y) right-side-down+). Pure-axis cases through that
// extraction show: a positive stored-Hamilton rotation about +y is
// nose-right (matches +yaw), but about +x it tilts the nose DOWN and
// about +z rolls left-side-down — so C-4 pitch/roll rates are the
// NEGATED stored-math components. ω is mapped component-wise as
// (−x, +y, −z), making sender-derived rates equal the Euler-
// differentiated rates the same receiver would otherwise compute.
//
// Velocity may be supplied by the caller when the engine measures it
// (UEVR GetVelocity); otherwise it is derived from position deltas.
// Acceleration is low-pass filtered (τ = 0.1 s) to tame sampling jitter;
// angular velocity is NOT filtered (onset cues need its edges). Teleports
// are detected as moves the best available velocity estimate cannot
// explain (engine-measured velocity when supplied, else the previously
// derived one) — NOT by a fixed per-sample displacement cap, which
// conflated "teleport" with "fast mover": flight and space sims routinely
// exceed 2.5 m per 20 ms sample (125 m/s) in perfectly normal flight,
// which reset the estimator every tick. Without a velocity measurement a
// single inconsistent sample is acceleration onset (or the first sample
// after a reset), so only a sustained inconsistency is treated as a
// teleport. The first sample after a reset yields the exact static value:
// gravity reaction rotated into the body frame, zero rates.
struct BodyDynamicsDeriver
{
    static constexpr float Gravity = 9.80665f;       // m/s²
    static constexpr double AccelFilterTau = 0.1;    // s
    static constexpr float TeleportThresholdMetres = 2.5f;

    Vec3 SpecificForce{};    // m/s², body frame, C-4 axes
    Vec3 AngularVelocity{};  // rad/s, body frame, C-4 axes

    /// World-frame gravity the specific force reacts to (m/s², pointing the
    /// way gravity pulls; C-12). Set before each Sample; a change applies
    /// from that sample on, with no estimator reset.
    Vec3 GravityVector{ 0.0f, -Gravity, 0.0f };

    /// Feeds one pose sample (standard frame: metres, unit quaternion,
    /// world space; dt in seconds — call every host tick, not just at
    /// publish cadence, so derivative denominators stay small).
    void Sample(double dt, const Vec3& position, const Quat& orientation,
                bool hasVelocity = false, const Vec3& engineVelocity = {})
    {
        if (!(dt > 0.0) || !hasHistory_)
        {
            Reset(position, orientation);
            return;
        }

        const Vec3 displacement = Sub(position, prevPosition_);
        Vec3 velocity;
        if (needsVelocitySeed_)
        {
            // the sample after a reset has no previous velocity to difference
            // against (a zero would read as a jump from rest to v in one
            // sample): seed it, keep the static force, and take the angular
            // rate, which needs only orientation history
            needsVelocitySeed_ = false;
            prevVelocity_ = hasVelocity ? engineVelocity
                                        : Scale(displacement, (float)(1.0 / dt));
            AngularVelocity = AngularVelocityFromDelta(prevOrientation_, orientation, dt);
            SpecificForce = RotateByInverse(orientation, Scale(GravityVector, -1.0f));
            prevPosition_ = position;
            prevOrientation_ = orientation;
            return;
        }
        if (hasVelocity)
        {
            // engine-measured velocity is authoritative: a move it cannot
            // explain (respawn, level change, floating-origin shift) is a
            // teleport, however fast the ship legitimately flies
            velocity = engineVelocity;
            const float expected = (float)dt * Len(engineVelocity);
            const float inconsistency =
                Len(Sub(displacement, Scale(engineVelocity, (float)dt)));
            if (inconsistency > TeleportThresholdMetres + 0.5f * expected)
            {
                Reset(position, orientation);
                return;
            }
        }
        else
        {
            velocity = Scale(displacement, (float)(1.0 / dt));
            const Vec3 expected = Scale(prevVelocity_, (float)dt);
            const float inconsistency = Len(Sub(displacement, expected));
            if (inconsistency > TeleportThresholdMetres + 0.5f * Len(expected))
            {
                // without a velocity measurement one inconsistent sample is
                // acceleration onset (or the first sample after a reset);
                // a sustained one is a teleport
                if (pendingTeleport_)
                {
                    Reset(position, orientation);
                    return;
                }
                pendingTeleport_ = true;
            }
            else pendingTeleport_ = false;
        }

        const Vec3 accel = Scale(Sub(velocity, prevVelocity_), (float)(1.0 / dt));
        // one-pole low-pass: y += (x − y)·dt/τ, stable for any dt
        const double blend = dt / AccelFilterTau < 1.0 ? dt / AccelFilterTau : 1.0;
        filteredAccel_ = Add(filteredAccel_,
                             Scale(Sub(accel, filteredAccel_), (float)blend));

        AngularVelocity = AngularVelocityFromDelta(prevOrientation_, orientation, dt);
        SpecificForce = RotateByInverse(orientation, Sub(filteredAccel_, GravityVector));

        prevPosition_ = position;
        prevVelocity_ = velocity;
        prevOrientation_ = orientation;
    }

    /// Static value after any reset: gravity reaction in the body frame,
    /// zero angular velocity.
    void Reset(const Vec3& position, const Quat& orientation)
    {
        SpecificForce = RotateByInverse(orientation, Scale(GravityVector, -1.0f));
        AngularVelocity = Vec3{};
        prevPosition_ = position;
        prevVelocity_ = Vec3{};
        prevOrientation_ = orientation;
        filteredAccel_ = Vec3{};
        hasHistory_ = true;
        needsVelocitySeed_ = true;
        pendingTeleport_ = false;
    }

    /// Body-frame angular velocity (rad/s, C-4 component sense) from the
    /// world-frame orientation delta; see the block comment above for the
    /// (−x, +y, −z) component map.
    static Vec3 AngularVelocityFromDelta(const Quat& previous,
                                         const Quat& current, double dt)
    {
        // dq = q_prev⁻¹ ⊗ q_cur  (body-frame rotation over dt)
        const Quat inv{ -previous.X, -previous.Y, -previous.Z, previous.W };
        Quat dq = Multiply(inv, current);
        if (dq.W < 0.0f) // hemisphere canonicalization: log stays in [−π, π]
            dq = Quat{ -dq.X, -dq.Y, -dq.Z, -dq.W };
        const Vec3 vec{ dq.X, dq.Y, dq.Z };
        const float vl = Len(vec);
        if (vl < 1e-8f || !(dt > 0.0)) return Vec3{};
        const float angle = 2.0f * std::atan2(vl, dq.W);
        const Vec3 stored = Scale(Scale(vec, 1.0f / vl), angle / (float)dt);
        return Vec3{ -stored.X, stored.Y, -stored.Z }; // C-4 sense
    }

    /// Rotates a world-frame vector into the body frame (v_body =
    /// q⁻¹ · v_world for the actor-style orientation q).
    static Vec3 RotateByInverse(const Quat& q, const Vec3& world)
    {
        return Rotate(Quat{ -q.X, -q.Y, -q.Z, q.W }, world);
    }

private:
    static Vec3 Add(const Vec3& a, const Vec3& b)
        { return Vec3{ a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
    static Vec3 Sub(const Vec3& a, const Vec3& b)
        { return Vec3{ a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
    static Vec3 Scale(const Vec3& v, float s)
        { return Vec3{ v.X * s, v.Y * s, v.Z * s }; }
    static float Len(const Vec3& v)
        { return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z); }

    Vec3 prevPosition_{}, prevVelocity_{}, filteredAccel_{};
    Quat prevOrientation_{};
    bool hasHistory_ = false;
    bool needsVelocitySeed_ = false; // next sample seeds prevVelocity_ instead of differencing
    bool pendingTeleport_ = false; // no-velocity path: one strike is accel onset, two is a teleport
};

} // namespace ogs::telemetry
