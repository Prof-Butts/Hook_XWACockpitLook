// OgsTelemetryWire.h — OGS-TS frame builders and the OMT1 envelope.
//
// The engine-facing API: plain input structs in, a canonical-encoded
// datagram out. Frames are written through the generated schema writers
// (OgsTelemetrySchema.h — offsets from the official Cap'n Proto compiler,
// never hand-derived) and laid out by the canonical encoder
// (OgsTelemetryCanonical.h), so the bytes follow the standard's canonical
// OGS encoding and byte-match the conformance corpus
// (OGS-Telemetry-Standard/conformance/; Test/ proves it).
//
// Frames needing schema fields these input structs do not cover can use
// the schema writers directly and encode them with wire::Encode.
//
// Every datagram stays within the standard's 1,400-byte budget
// (implementation-udp.md § Datagram budget): PaginateDiscovery splits a
// descriptor set into self-contained pages, PlanMotionParts splits a tick's
// channel values into parts with the pose in part 0.
#pragma once
#include "OgsTelemetrySchema.h"
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ogs::telemetry
{

enum class FrameType : uint8_t { Discovery = 0, Motion = 1, Event = 2 };

/// Datagram budget (implementation-udp.md): bytes per datagram, envelope included.
constexpr size_t kDatagramBudget = 1400;

struct Vec3 { float X = 0, Y = 0, Z = 0; };
struct Quat { float X = 0, Y = 0, Z = 0, W = 1; };

/// Holds one encoded message (single segment, canonical form) and wraps
/// it in the 8-byte OMT1 envelope.
class WireBuilder
{
public:
    std::vector<uint64_t> W;

    /// OMT1 datagram: magic, frame type, 3 reserved bytes, one-word segment
    /// table (count-1 = 0, segment words), segment words — all little-endian,
    /// written by shifts.
    std::vector<uint8_t> Wrap(FrameType type) const
    {
        std::vector<uint8_t> d;
        d.reserve(16 + W.size() * 8);
        d.push_back('O'); d.push_back('M'); d.push_back('T'); d.push_back('1');
        d.push_back((uint8_t)type); d.push_back(0); d.push_back(0); d.push_back(0);
        auto put32 = [&d](uint32_t v) {
            for (int i = 0; i < 4; i++) d.push_back((uint8_t)(v >> (8 * i)));
        };
        put32(0); put32((uint32_t)W.size());
        for (uint64_t w : W)
            for (int i = 0; i < 8; i++) d.push_back((uint8_t)(w >> (8 * i)));
        return d;
    }
};

// ---- frame inputs ----

struct ChannelDecl
{
    std::string Name, Unit, Description;
    double Min = 0, Max = 0; bool HasRange = false;
    int32_t Id = -1; // -1 = its position in the object's channel list
    // the ChannelValue variant this channel uses; Unspecified = undeclared
    schema::ChannelType Type = schema::ChannelType::Unspecified;
};

/// Sender instance identity for receivers' session pickers and pinned
/// choices (implementation-udp.md § Senders and sessions); both optional.
struct Instance
{
    std::string Label; // user-set, e.g. "Rig 2"
    std::string Host;  // this machine's host name
    float MotionRateHz = 0; // nominal motion frame rate; 0 = not declared
};

/// A session-scoped channel value carried in discovery `values`.
struct SessionValue
{
    enum class Kind : uint8_t { Number, Boolean, Text };
    uint16_t Id = 0;
    Kind Type = Kind::Number;
    double Number = 0; bool Boolean = false; std::string Text;
};

struct ChannelValueInput
{
    uint16_t Id = 0;
    double Number = 0; // used when !IsBool
    bool Boolean = false, IsBool = false;
};

struct MotionObjectInput
{
    std::string Name;
    Vec3 Position; Quat Orientation;
    Vec3 SpecificForce;   // m/s², accelerometer-style: f = a − g (+heave
                          // gravity reaction at rest); body frame, C-4 axes
    Vec3 AngularVelocity; // rad/s, body frame, C-4 axes (+x nose-up,
                          // +y nose-right, +z right-side-down)
    bool HasBodyDynamics = false;
    Vec3 LocalGravity;    // m/s², world frame, pointing the way gravity
                          // pulls; per frame (CONVENTIONS C-12)
    bool HasLocalGravity = false;
    std::vector<ChannelValueInput> Channels;
    /// A later motion part listing the object: name and channel values
    /// only — pose, dynamics and local gravity are left default.
    bool ChannelsOnly = false;
};

struct ObjectDecl
{
    std::string Name, Location, TypeLabel, RefPointName = "origin";
    uint16_t Type = 1; bool Primary = true; // ObjectType ordinal; other
                         // holds 0 so the catch-all never interleaves with
                         // real classes: 0 other, 1 vehicle, 2 aircraft,
                         // 3 watercraft, 4 spacecraft, 5 humanoid, 6 camera,
                         // 7 creature
    std::vector<ChannelDecl> Channels; // ids by position unless ChannelDecl::Id is set
    std::vector<SessionValue> Values;  // session-scoped values (discovery `values`)
    /// A later discovery page listing the object: name, channels and
    /// values only — its identity rides the first page listing it.
    bool NameOnly = false;
};

struct EventInput
{
    uint32_t Id = 0;
    std::string Name, Object;
    float Intensity = 1, Duration = 0;
    Vec3 Direction; bool HasDirection = false;
};

struct Environment
{
    // standard gravity, the value BodyDynamicsDeriver assumes by default
    float Gravity = 9.80665f;
    // atmosphere (kg/m^3, degC, Pa): declared only when HasAtmosphere —
    // a game without an atmosphere model leaves it unset (noAtmosphere)
    bool HasAtmosphere = false;
    float AirDensity = 0, Temperature = 0, Pressure = 0;
    uint16_t Medium = 2; // ContactMedium ordinal; other holds 0 so the
                         // catch-all never interleaves with real mediums:
                         // 0 other, 1 air, 2 asphalt, 3 dirt, 4 grass,
                         // 5 gravel, 6 rumbleStrip, 7 water, 8 snow,
                         // 9 ice, 10 vacuum
    // gravity direction (CONVENTIONS C-12): unset = along world −Y with
    // magnitude Gravity; set = world-frame vector (Gravity = its length)
    Vec3 GravityVector; bool HasGravityVector = false;

    /// Session gravity as a world-frame vector (C-12 resolution).
    Vec3 GravityWorld() const
    {
        return HasGravityVector ? GravityVector : Vec3{ 0.0f, -Gravity, 0.0f };
    }

    /// Sets the session gravity from a world-frame vector: straight down
    /// rides as the scalar alone, any other direction also as the vector.
    void SetGravity(const Vec3& g)
    {
        Gravity = std::sqrt(g.X * g.X + g.Y * g.Y + g.Z * g.Z);
        HasGravityVector = !(g.X == 0.0f && g.Z == 0.0f && g.Y <= 0.0f);
        GravityVector = HasGravityVector ? g : Vec3{};
    }
};

// ---- frame builders ----

inline void SetVec3(schema::Vector3& out, Vec3 v)
{
    out.SetX(v.X); out.SetY(v.Y); out.SetZ(v.Z);
}

inline void BuildDiscovery(WireBuilder& b, uint16_t schemaVersion,
                           int64_t sessionUnixUs, const std::string& gameName,
                           const Environment& env,
                           const std::vector<ObjectDecl>& objects,
                           uint32_t generation = 0, uint16_t page = 0,
                           uint16_t pageCount = 0, const Instance& instance = {})
{
    schema::DiscoveryFrame d;
    d.SetSchemaVersion(schemaVersion);
    d.SetGameName(gameName);
    d.SetSessionStartUnixUs(sessionUnixUs);
    d.SetInstanceLabel(instance.Label);
    d.SetHost(instance.Host);
    d.SetMotionRateHz(instance.MotionRateHz);
    d.SetGeneration(generation);
    d.SetPage(page);
    d.SetPageCount(pageCount);
    schema::Environment& e = d.Environment();
    e.SetGravity(env.Gravity);
    if (env.HasAtmosphere)
    {
        schema::Atmosphere& air = e.Atmosphere().InitKnown();
        air.SetAirDensity(env.AirDensity); air.SetTemperature(env.Temperature);
        air.SetPressure(env.Pressure);
    }
    e.SetMedium((schema::ContactMedium)env.Medium);
    if (env.HasGravityVector) SetVec3(e.InitGravityVector(), env.GravityVector);
    for (const ObjectDecl& o : objects)
    {
        schema::ObjectDescriptor& od = d.AddObjects();
        od.SetName(o.Name);
        if (!o.NameOnly)
        {
            od.SetType((schema::ObjectType)o.Type);
            od.SetLocation(o.Location);
            od.SetTypeLabel(o.TypeLabel);
            od.SetPrimary(o.Primary);
            od.ReferencePoint().SetName(o.RefPointName);
        }
        for (size_t i = 0; i < o.Channels.size(); i++)
        {
            const ChannelDecl& c = o.Channels[i];
            schema::ChannelDescriptor& cd = od.AddChannels();
            cd.SetId(c.Id >= 0 ? (uint16_t)c.Id : (uint16_t)i);
            cd.SetName(c.Name);
            cd.SetUnit(c.Unit);
            cd.SetDescription(c.Description);
            cd.SetType(c.Type);
            if (c.HasRange)
            {
                schema::Range& r = cd.InitRange();
                r.SetMin(c.Min); r.SetMax(c.Max);
            }
        }
        for (const SessionValue& v : o.Values)
        {
            if (v.Type == SessionValue::Kind::Text)
            {
                // text values ride the TextValue list (`texts`)
                schema::TextValue& tv = od.AddTexts();
                tv.SetId(v.Id);
                tv.SetText(v.Text);
                continue;
            }
            schema::ChannelValue& cv = od.AddValues();
            cv.SetId(v.Id);
            if (v.Type == SessionValue::Kind::Boolean) cv.SetBoolean(v.Boolean);
            else cv.SetNumber((float)v.Number);
        }
    }
    b.W = wire::Encode(d);
}

inline void BuildMotion(WireBuilder& b, double timestamp,
                        const std::vector<MotionObjectInput>& objects,
                        uint32_t generation = 0, uint16_t part = 0,
                        uint16_t partCount = 0)
{
    schema::MotionFrame m;
    m.SetTimestamp(timestamp);
    m.SetGeneration(generation);
    m.SetPart(part);
    m.SetPartCount(partCount);
    for (const MotionObjectInput& o : objects)
    {
        schema::MotionObject& mo = m.AddObjects();
        mo.SetName(o.Name);
        if (o.ChannelsOnly)
        {
            for (const ChannelValueInput& c : o.Channels)
            {
                schema::ChannelValue& cv = mo.AddChannels();
                cv.SetId(c.Id);
                if (c.IsBool) cv.SetBoolean(c.Boolean);
                else cv.SetNumber((float)c.Number);
            }
            continue;
        }
        SetVec3(mo.Position(), o.Position);
        schema::Quaternion& q = mo.Orientation();
        q.SetX(o.Orientation.X); q.SetY(o.Orientation.Y);
        q.SetZ(o.Orientation.Z); q.SetW(o.Orientation.W);
        if (o.HasBodyDynamics)
        {
            schema::BodyDynamics& bd = mo.InitBodyDynamics();
            SetVec3(bd.SpecificForce(), o.SpecificForce);
            SetVec3(bd.AngularVelocity(), o.AngularVelocity);
        }
        if (o.HasLocalGravity) SetVec3(mo.LocalGravity().InitVector(), o.LocalGravity);
        for (const ChannelValueInput& c : o.Channels)
        {
            schema::ChannelValue& cv = mo.AddChannels();
            cv.SetId(c.Id);
            if (c.IsBool) cv.SetBoolean(c.Boolean);
            else cv.SetNumber((float)c.Number);
        }
    }
    b.W = wire::Encode(m);
}

// ---- datagram budget ----

/// Splits a descriptor set into discovery pages within the datagram budget
/// (implementation-udp.md § Paged discovery), measuring candidates with the
/// canonical encoder. An object's identity rides the first page listing it;
/// its channels follow, spilling onto pages that list it by name only; its
/// session values start on a page of their own. A set that fits one
/// datagram is a single unpaged frame. Returns the encoded datagrams.
/// Throws std::length_error when a single entry alone exceeds the budget.
inline std::vector<std::vector<uint8_t>> PaginateDiscovery(
    uint16_t schemaVersion, int64_t sessionUnixUs, const std::string& gameName,
    const Environment& env, const std::vector<ObjectDecl>& objects,
    uint32_t generation, size_t budget = kDatagramBudget, const Instance& instance = {})
{
    std::vector<std::vector<ObjectDecl>> pages(1);
    WireBuilder b;
    auto fits = [&](const std::vector<ObjectDecl>& page) {
        // measured as paged: the page fields are part of the size
        BuildDiscovery(b, schemaVersion, sessionUnixUs, gameName, env, page,
                       generation, (uint16_t)pages.size(), 0xFFFF, instance);
        return b.Wrap(FrameType::Discovery).size() <= budget;
    };
    auto continuation = [](const ObjectDecl& o) {
        ObjectDecl c; c.Name = o.Name; c.NameOnly = true; c.RefPointName = "";
        return c;
    };
    auto tooBig = [](const std::string& what) {
        throw std::length_error(what + " alone exceeds the datagram budget");
    };

    for (const ObjectDecl& source : objects)
    {
        ObjectDecl identity = source;
        identity.Channels.clear(); identity.Values.clear();
        pages.back().push_back(identity);
        if (!fits(pages.back()))
        {
            pages.back().pop_back();
            if (pages.back().empty()) tooBig("object '" + source.Name + "' identity");
            pages.emplace_back();
            pages.back().push_back(identity);
            if (!fits(pages.back())) tooBig("object '" + source.Name + "' identity");
        }
        for (size_t i = 0; i < source.Channels.size(); i++)
        {
            ChannelDecl c = source.Channels[i];
            if (c.Id < 0) c.Id = (int32_t)i;
            pages.back().back().Channels.push_back(c);
            if (fits(pages.back())) continue;
            pages.back().back().Channels.pop_back();
            pages.emplace_back();
            pages.back().push_back(continuation(source));
            pages.back().back().Channels.push_back(c);
            if (!fits(pages.back())) tooBig("channel '" + c.Name + "'");
        }
        if (!source.Values.empty())
        {
            // session values ride their own page(s)
            const ObjectDecl& last = pages.back().back();
            bool bare = pages.back().size() == 1 && last.NameOnly && last.Channels.empty();
            if (!bare)
            {
                pages.emplace_back();
                pages.back().push_back(continuation(source));
            }
            for (const SessionValue& v : source.Values)
            {
                pages.back().back().Values.push_back(v);
                if (fits(pages.back())) continue;
                pages.back().back().Values.pop_back();
                pages.emplace_back();
                pages.back().push_back(continuation(source));
                pages.back().back().Values.push_back(v);
                if (!fits(pages.back())) tooBig("session value");
            }
        }
    }

    const bool paged = pages.size() > 1;
    std::vector<std::vector<uint8_t>> datagrams;
    for (size_t i = 0; i < pages.size(); i++)
    {
        BuildDiscovery(b, schemaVersion, sessionUnixUs, gameName, env, pages[i], generation,
                       paged ? (uint16_t)i : 0, paged ? (uint16_t)pages.size() : 0, instance);
        datagrams.push_back(b.Wrap(FrameType::Discovery));
    }
    return datagrams;
}

/// Bytes one number or boolean channel value adds to a motion frame.
constexpr size_t kBytesPerChannelValue = 16;

/// Plans how `valueCount` channel values split into motion parts within the
/// budget (implementation-udp.md § Motion parts): (start, count) per part.
/// Part 0 carries the pose; sizes are measured with the largest variants
/// (body dynamics and local gravity selected), so the plan holds every tick.
inline std::vector<std::pair<size_t, size_t>> PlanMotionParts(
    size_t valueCount, const std::string& objectName, size_t budget = kDatagramBudget)
{
    std::vector<std::pair<size_t, size_t>> plan;
    if (valueCount == 0) { plan.push_back({ 0, 0 }); return plan; }
    WireBuilder b;
    auto capacity = [&](bool part0) {
        MotionObjectInput o; o.Name = objectName;
        o.ChannelsOnly = !part0;
        o.HasBodyDynamics = part0; o.SpecificForce = { 1, 1, 1 };
        o.HasLocalGravity = part0; o.LocalGravity = { 1, 1, 1 };
        o.Channels = { ChannelValueInput{ 0xFFFF, 1.0 } };
        BuildMotion(b, 1.0, { o }, 0xFFFFFFFFu, 1, 0xFFFF);
        size_t one = b.Wrap(FrameType::Motion).size();
        if (one > budget) throw std::length_error("a motion part with one value exceeds the datagram budget");
        return 1 + (budget - one) / kBytesPerChannelValue;
    };
    size_t first = capacity(true), later = capacity(false);
    size_t start = first < valueCount ? first : valueCount;
    plan.push_back({ 0, start });
    while (start < valueCount)
    {
        size_t count = later < valueCount - start ? later : valueCount - start;
        plan.push_back({ start, count });
        start += count;
    }
    return plan;
}

inline void BuildEventFrame(WireBuilder& b, double timestamp,
                            const std::vector<EventInput>& events)
{
    schema::EventFrame f;
    f.SetTimestamp(timestamp);
    for (const EventInput& ev : events)
    {
        schema::Event& e = f.AddEvents();
        e.SetId(ev.Id);
        e.SetName(ev.Name);
        e.SetIntensity(ev.Intensity);
        e.SetDuration(ev.Duration);
        e.SetObject(ev.Object);
        if (ev.HasDirection) SetVec3(e.InitDirection(), ev.Direction);
    }
    b.W = wire::Encode(f);
}

/// Splits an event burst into runs that each fit one datagram (implementation-udp.md
/// § Event splitting): (start, count) pairs over `events`, each run the longest
/// prefix of what remains that stays within `budget`, envelope included. Events
/// are self-contained and receivers dedup by id, so the runs need no numbering.
inline std::vector<std::pair<size_t, size_t>> PlanEventFrames(
    const std::vector<EventInput>& events, double timestamp,
    size_t budget = kDatagramBudget)
{
    auto fits = [&](size_t start, size_t count)
    {
        WireBuilder b;
        BuildEventFrame(b, timestamp,
            std::vector<EventInput>(events.begin() + (std::ptrdiff_t)start,
                                    events.begin() + (std::ptrdiff_t)(start + count)));
        return b.Wrap(FrameType::Event).size() <= budget;
    };
    std::vector<std::pair<size_t, size_t>> plan;
    size_t start = 0;
    while (start < events.size())
    {
        size_t lo = 1, hi = events.size() - start; // lo always fits
        if (fits(start, hi)) lo = hi;
        else
            while (lo < hi)
            {
                const size_t mid = (lo + hi + 1) / 2;
                if (fits(start, mid)) lo = mid; else hi = mid - 1;
            }
        plan.push_back({ start, lo });
        start += lo;
    }
    return plan;
}

} // namespace ogs::telemetry
