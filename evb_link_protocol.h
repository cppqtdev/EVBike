#pragma once

#include <cstddef>
#include <cstdint>

namespace evb {

// Phone <-> cluster link frame (BLE GATT write / notify, or UART from BLE module).
//
//  0      1        2        3..4        5..(5+len-1)   last 2
// [SOF][version][msgType][len LE u16][payload......][crc16 LE]
// CRC covers version..payload.
namespace link {

constexpr uint8_t kSof = 0xA5;
constexpr uint8_t kVersion = 1;
constexpr std::size_t kMaxPayload = 160;
constexpr std::size_t kHeaderSize = 5;
constexpr std::size_t kCrcSize = 2;
constexpr std::size_t kMaxFrame = kHeaderSize + kMaxPayload + kCrcSize;
constexpr std::size_t kMaxText = 48;

enum class MsgType : uint8_t {
    NavUpdate = 0x01,
    NavStop = 0x02,
    CallState = 0x10,
    MediaState = 0x11,
    Notification = 0x12,
    ListEntry = 0x13,
    TimeSync = 0x20,
    PhoneStatus = 0x21,
    Heartbeat = 0x30,
    MediaCommand = 0x40,
    CallCommand = 0x41,
    Ack = 0x7F,
};

// Keep values in sync with docs/03-protocols.md and NavigationData::Maneuver.
enum class Maneuver : uint8_t {
    None = 0,
    Straight,
    SlightLeft,
    Left,
    SharpLeft,
    SlightRight,
    Right,
    SharpRight,
    UTurnLeft,
    UTurnRight,
    RoundaboutEnter,
    RoundaboutExit,
    ForkLeft,
    ForkRight,
    MergeLeft,
    MergeRight,
    Destination,
    Count
};

struct NavUpdate
{
    Maneuver maneuver = Maneuver::None;
    uint8_t roundaboutExit = 0;
    uint32_t distanceToManeuverM = 0;
    uint32_t distanceRemainingM = 0;
    uint16_t etaMinutes = 0;
    uint8_t laneMask = 0;
    uint8_t recommendedLaneMask = 0;
    char roadName[kMaxText + 1] = {};
};

enum class CallStatus : uint8_t { Idle = 0, Ringing, Active };

struct CallState
{
    CallStatus status = CallStatus::Idle;
    char caller[kMaxText + 1] = {};
};

struct MediaState
{
    bool playing = false;
    uint8_t volume = 0;
    uint16_t positionS = 0;
    uint16_t durationS = 0;
    char title[kMaxText + 1] = {};
    char artist[kMaxText + 1] = {};
};

struct Notification
{
    uint8_t appId = 0;
    char sender[kMaxText + 1] = {};
    char text[kMaxText + 1] = {};
};

// One row of a list the phone keeps: the recent contacts and the owner's
// reminders. The cluster holds a few slots per list and redraws whatever the
// phone last sent, so nothing about who the rider knows is built into the
// firmware.
enum class ListId : uint8_t { Contacts = 0, Reminders = 1, Count };

constexpr uint8_t kMaxListSlots = 3;

struct ListEntry
{
    ListId list = ListId::Contacts;
    uint8_t slot = 0;
    char title[kMaxText + 1] = {};
    char text[kMaxText + 1] = {};
};

struct TimeSync
{
    uint32_t unixSeconds = 0;
    int16_t utcOffsetMinutes = 0;
};

struct PhoneStatus
{
    uint8_t batteryPercent = 0;
    uint8_t signalBars = 0;
    bool internet = false;
};

class Handler
{
public:
    virtual ~Handler() = default;
    virtual void onNavUpdate(const NavUpdate &) {}
    virtual void onNavStop() {}
    virtual void onCallState(const CallState &) {}
    virtual void onMediaState(const MediaState &) {}
    virtual void onNotification(const Notification &) {}
    virtual void onListEntry(const ListEntry &) {}
    virtual void onTimeSync(const TimeSync &) {}
    virtual void onPhoneStatus(const PhoneStatus &) {}
    virtual void onHeartbeat() {}
    virtual void onFrameError() {}
};

// Byte-stream parser. Feed bytes as they arrive (from BLE callback or UART ISR drain).
class Parser
{
public:
    explicit Parser(Handler &handler) : m_handler(handler) {}

    void feed(const uint8_t *data, std::size_t len);
    void reset();

    uint32_t goodFrames() const { return m_good; }
    uint32_t badFrames() const { return m_bad; }

private:
    void process();
    void dropUntilNextSof(std::size_t from);
    void dispatch(MsgType type, const uint8_t *payload, std::size_t len);

    Handler &m_handler;
    uint8_t m_buf[kMaxFrame] = {};
    std::size_t m_pos = 0;
    uint32_t m_good = 0;
    uint32_t m_bad = 0;
};

// Builds a frame into `out`. Returns total size or 0 if it does not fit.
std::size_t buildFrame(MsgType type, const uint8_t *payload, std::size_t len, uint8_t *out, std::size_t outSize);
std::size_t encodeNavUpdate(const NavUpdate &nav, uint8_t *out, std::size_t outSize);
std::size_t encodeListEntry(const ListEntry &entry, uint8_t *out, std::size_t outSize);

} // namespace link
} // namespace evb
