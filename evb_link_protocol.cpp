#include "evb_link_protocol.h"
#include "evb_crc16.h"

#include <cstring>

namespace evb::link {

namespace {

class Reader
{
public:
    Reader(const uint8_t *data, std::size_t len) : m_data(data), m_len(len) {}

    bool ok() const { return m_ok; }

    uint8_t u8()
    {
        if (m_pos + 1 > m_len) { m_ok = false; return 0; }
        return m_data[m_pos++];
    }

    uint16_t u16()
    {
        const uint16_t lo = u8();
        const uint16_t hi = u8();
        return static_cast<uint16_t>(lo | (hi << 8));
    }

    uint32_t u32()
    {
        const uint32_t lo = u16();
        const uint32_t hi = u16();
        return lo | (hi << 16);
    }

    // Length-prefixed UTF-8. Truncates on a UTF-8 boundary to fit `cap`.
    void text(char *dst, std::size_t cap)
    {
        const std::size_t n = u8();
        if (!m_ok || m_pos + n > m_len) { m_ok = false; dst[0] = '\0'; return; }
        std::size_t copy = n < cap ? n : cap;
        while (copy > 0 && copy < n && (m_data[m_pos + copy] & 0xC0) == 0x80)
            --copy;
        std::memcpy(dst, m_data + m_pos, copy);
        dst[copy] = '\0';
        m_pos += n;
    }

private:
    const uint8_t *m_data;
    std::size_t m_len;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

class Writer
{
public:
    Writer(uint8_t *data, std::size_t cap) : m_data(data), m_cap(cap) {}

    bool ok() const { return m_ok; }
    std::size_t size() const { return m_pos; }

    void u8(uint8_t v)
    {
        if (m_pos + 1 > m_cap) { m_ok = false; return; }
        m_data[m_pos++] = v;
    }
    void u16(uint16_t v) { u8(static_cast<uint8_t>(v)); u8(static_cast<uint8_t>(v >> 8)); }
    void u32(uint32_t v) { u16(static_cast<uint16_t>(v)); u16(static_cast<uint16_t>(v >> 16)); }

    void text(const char *s)
    {
        std::size_t n = std::strlen(s);
        if (n > kMaxText) n = kMaxText;
        u8(static_cast<uint8_t>(n));
        for (std::size_t i = 0; i < n; ++i)
            u8(static_cast<uint8_t>(s[i]));
    }

private:
    uint8_t *m_data;
    std::size_t m_cap;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

} // namespace

void Parser::reset()
{
    m_pos = 0;
}

void Parser::dropUntilNextSof(std::size_t from)
{
    std::size_t j = from;
    while (j < m_pos && m_buf[j] != kSof)
        ++j;
    if (j >= m_pos) {
        m_pos = 0;
        return;
    }
    std::memmove(m_buf, m_buf + j, m_pos - j);
    m_pos -= j;
}

void Parser::process()
{
    while (m_pos > 0) {
        if (m_buf[0] != kSof) {
            dropUntilNextSof(1);
            continue;
        }
        if (m_pos < kHeaderSize)
            return;

        const std::size_t payloadLen = static_cast<std::size_t>(m_buf[3] | (m_buf[4] << 8));
        if (m_buf[1] != kVersion || payloadLen > kMaxPayload) {
            ++m_bad;
            m_handler.onFrameError();
            dropUntilNextSof(1);
            continue;
        }

        const std::size_t need = kHeaderSize + payloadLen + kCrcSize;
        if (m_pos < need)
            return;

        const uint16_t rxCrc = static_cast<uint16_t>(m_buf[need - 2] | (m_buf[need - 1] << 8));
        const uint16_t calc = crc16Ccitt(m_buf + 1, kHeaderSize - 1 + payloadLen);
        if (rxCrc != calc) {
            ++m_bad;
            m_handler.onFrameError();
            dropUntilNextSof(1);
            continue;
        }

        ++m_good;
        dispatch(static_cast<MsgType>(m_buf[2]), m_buf + kHeaderSize, payloadLen);
        dropUntilNextSof(need);
    }
}

void Parser::feed(const uint8_t *data, std::size_t len)
{
    for (std::size_t i = 0; i < len; ++i) {
        if (m_pos == 0 && data[i] != kSof)
            continue;
        m_buf[m_pos++] = data[i];
        process();
    }
}

void Parser::dispatch(MsgType type, const uint8_t *payload, std::size_t len)
{
    Reader r(payload, len);
    switch (type) {
    case MsgType::NavUpdate: {
        NavUpdate nav;
        const uint8_t m = r.u8();
        nav.maneuver = m < static_cast<uint8_t>(Maneuver::Count) ? static_cast<Maneuver>(m) : Maneuver::None;
        nav.roundaboutExit = r.u8();
        nav.distanceToManeuverM = r.u32();
        nav.distanceRemainingM = r.u32();
        nav.etaMinutes = r.u16();
        nav.laneMask = r.u8();
        nav.recommendedLaneMask = r.u8();
        r.text(nav.roadName, kMaxText);
        if (r.ok()) m_handler.onNavUpdate(nav); else m_handler.onFrameError();
        break;
    }
    case MsgType::NavStop:
        m_handler.onNavStop();
        break;
    case MsgType::CallState: {
        CallState c;
        const uint8_t s = r.u8();
        c.status = s <= static_cast<uint8_t>(CallStatus::Active) ? static_cast<CallStatus>(s) : CallStatus::Idle;
        r.text(c.caller, kMaxText);
        if (r.ok()) m_handler.onCallState(c); else m_handler.onFrameError();
        break;
    }
    case MsgType::MediaState: {
        MediaState ms;
        ms.playing = r.u8() != 0;
        ms.volume = r.u8();
        ms.positionS = r.u16();
        ms.durationS = r.u16();
        r.text(ms.title, kMaxText);
        r.text(ms.artist, kMaxText);
        if (r.ok()) m_handler.onMediaState(ms); else m_handler.onFrameError();
        break;
    }
    case MsgType::Notification: {
        Notification n;
        n.appId = r.u8();
        r.text(n.sender, kMaxText);
        r.text(n.text, kMaxText);
        if (r.ok()) m_handler.onNotification(n); else m_handler.onFrameError();
        break;
    }
    case MsgType::ListEntry: {
        ListEntry e;
        const uint8_t list = r.u8();
        e.list = list < static_cast<uint8_t>(ListId::Count) ? static_cast<ListId>(list) : ListId::Contacts;
        e.slot = r.u8();
        r.text(e.title, kMaxText);
        r.text(e.text, kMaxText);
        if (r.ok() && e.slot < kMaxListSlots) m_handler.onListEntry(e); else m_handler.onFrameError();
        break;
    }
    case MsgType::TimeSync: {
        TimeSync t;
        t.unixSeconds = r.u32();
        t.utcOffsetMinutes = static_cast<int16_t>(r.u16());
        if (r.ok()) m_handler.onTimeSync(t); else m_handler.onFrameError();
        break;
    }
    case MsgType::PhoneStatus: {
        PhoneStatus p;
        p.batteryPercent = r.u8();
        p.signalBars = r.u8();
        p.internet = r.u8() != 0;
        if (r.ok()) m_handler.onPhoneStatus(p); else m_handler.onFrameError();
        break;
    }
    case MsgType::Heartbeat:
        m_handler.onHeartbeat();
        break;
    default:
        break;
    }
}

std::size_t buildFrame(MsgType type, const uint8_t *payload, std::size_t len, uint8_t *out, std::size_t outSize)
{
    const std::size_t total = kHeaderSize + len + kCrcSize;
    if (len > kMaxPayload || total > outSize)
        return 0;
    out[0] = kSof;
    out[1] = kVersion;
    out[2] = static_cast<uint8_t>(type);
    out[3] = static_cast<uint8_t>(len);
    out[4] = static_cast<uint8_t>(len >> 8);
    if (len)
        std::memcpy(out + kHeaderSize, payload, len);
    const uint16_t crc = crc16Ccitt(out + 1, kHeaderSize - 1 + len);
    out[kHeaderSize + len] = static_cast<uint8_t>(crc);
    out[kHeaderSize + len + 1] = static_cast<uint8_t>(crc >> 8);
    return total;
}

std::size_t encodeNavUpdate(const NavUpdate &nav, uint8_t *out, std::size_t outSize)
{
    uint8_t payload[kMaxPayload];
    Writer w(payload, sizeof(payload));
    w.u8(static_cast<uint8_t>(nav.maneuver));
    w.u8(nav.roundaboutExit);
    w.u32(nav.distanceToManeuverM);
    w.u32(nav.distanceRemainingM);
    w.u16(nav.etaMinutes);
    w.u8(nav.laneMask);
    w.u8(nav.recommendedLaneMask);
    w.text(nav.roadName);
    if (!w.ok())
        return 0;
    return buildFrame(MsgType::NavUpdate, payload, w.size(), out, outSize);
}

std::size_t encodeListEntry(const ListEntry &entry, uint8_t *out, std::size_t outSize)
{
    uint8_t payload[kMaxPayload];
    Writer w(payload, sizeof(payload));
    w.u8(static_cast<uint8_t>(entry.list));
    w.u8(entry.slot);
    w.text(entry.title);
    w.text(entry.text);
    if (!w.ok())
        return 0;
    return buildFrame(MsgType::ListEntry, payload, w.size(), out, outSize);
}

} // namespace evb::link
