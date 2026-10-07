#include "evb_phone_protocols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evb_clock.h"
#include "evb_link_protocol.h"
#include "evb_phone_state.h"

namespace evb::phone {

namespace {

constexpr uint32_t APP_LINK_QUIET_MS = 10000;

uint32_t le32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

uint16_t le16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

void put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

void copy_bytes_as_text(char *destination, size_t capacity, const uint8_t *bytes, size_t length)
{
    char text[EVB_TEXT_MAX * 2 + 1];
    if (length > sizeof(text) - 1) {
        length = sizeof(text) - 1;
    }
    memcpy(text, bytes, length);
    text[length] = '\0';
    evb_copy_text(destination, capacity, text);
}

/* Newest message first; the oldest row falls off. */
void push_message_row(PhoneState *state, const char *sender, const char *text)
{
    for (int i = EVB_LIST_SLOTS - 1; i > 0; i--) {
        state->contacts[i] = state->contacts[i - 1];
    }
    evb_copy_text(state->contacts[0].title, sizeof(state->contacts[0].title), sender);
    evb_copy_text(state->contacts[0].text, sizeof(state->contacts[0].text), text);
}

void show_notification(PhoneState *state, const char *sender, const char *text)
{
    evb_copy_text(state->notification_sender, sizeof(state->notification_sender), sender);
    evb_copy_text(state->notification_text, sizeof(state->notification_text), text);
    state->notification_seq++;
}

/* ---------- EVBikes Phone Link ---------- */

uint32_t app_now_ms = 0;
uint32_t last_app_frame_ms = 0;
bool app_link_seen = false;

class AppLinkHandler : public evb::link::Handler {
public:
    void onNavUpdate(const evb::link::NavUpdate &update) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        state->nav.active = true;
        state->nav.demo = false;
        state->nav.maneuver = (uint8_t)update.maneuver;
        state->nav.roundabout_exit = update.roundaboutExit;
        state->nav.distance_to_maneuver_m = update.distanceToManeuverM;
        state->nav.distance_remaining_m = update.distanceRemainingM;
        state->nav.eta_minutes = update.etaMinutes;
        evb_copy_text(state->nav.road, sizeof(state->nav.road), update.roadName);
        evb_phone_state_end_edit();
    }

    void onNavStop() override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        state->nav.active = false;
        evb_phone_state_end_edit();
    }

    void onCallState(const evb::link::CallState &call) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        PhoneCallStatus status = PhoneCallStatus::Idle;
        if (call.status == evb::link::CallStatus::Ringing) {
            status = PhoneCallStatus::Ringing;
        } else if (call.status == evb::link::CallStatus::Active) {
            status = PhoneCallStatus::Active;
        }
        if (status == PhoneCallStatus::Active && state->call_status != PhoneCallStatus::Active) {
            state->call_started_ms = app_now_ms;
        }
        state->call_status = status;
        evb_copy_text(state->caller, sizeof(state->caller), call.caller);
        evb_phone_state_end_edit();
    }

    void onMediaState(const evb::link::MediaState &media) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        state->media_info_available = true;
        state->media_playing = media.playing;
        state->media_volume_percent = media.volume > 100 ? 100 : media.volume;
        state->media_position_s = media.positionS;
        state->media_duration_s = media.durationS;
        state->media_position_at_ms = app_now_ms;
        evb_copy_text(state->media_title, sizeof(state->media_title), media.title);
        evb_copy_text(state->media_artist, sizeof(state->media_artist), media.artist);
        evb_phone_state_end_edit();
    }

    void onNotification(const evb::link::Notification &notification) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        show_notification(state, notification.sender, notification.text);
        evb_phone_state_end_edit();
    }

    void onListEntry(const evb::link::ListEntry &entry) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        TextRow *rows = entry.list == evb::link::ListId::Contacts ? state->contacts : state->reminders;
        if (entry.slot < EVB_LIST_SLOTS) {
            evb_copy_text(rows[entry.slot].title, sizeof(rows[entry.slot].title), entry.title);
            evb_copy_text(rows[entry.slot].text, sizeof(rows[entry.slot].text), entry.text);
        }
        evb_phone_state_end_edit();
    }

    void onTimeSync(const evb::link::TimeSync &time) override
    {
        evb_clock_set_utc_offset(time.utcOffsetMinutes);
        evb_clock_set_from_utc(time.unixSeconds, "Phone app");
    }

    void onPhoneStatus(const evb::link::PhoneStatus &status) override
    {
        PhoneState *state = evb_phone_state_begin_edit();
        state->phone_battery_percent = status.batteryPercent;
        state->phone_signal_bars = status.signalBars;
        evb_phone_state_end_edit();
    }
};

AppLinkHandler app_handler;
evb::link::Parser app_parser(app_handler);

void mark_app_link(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->link_kind = PhoneLinkKind::EvbikesApp;
    state->media_controls_available = true;
    state->call_controls_available = true;
    state->can_dial = true;
    evb_phone_state_end_edit();
}

/* ---------- ANCS ---------- */

constexpr uint8_t ANCS_EVENT_ADDED = 0;
constexpr uint8_t ANCS_EVENT_MODIFIED = 1;
constexpr uint8_t ANCS_EVENT_REMOVED = 2;
constexpr uint8_t ANCS_FLAG_SILENT = 1 << 0;
constexpr uint8_t ANCS_FLAG_PRE_EXISTING = 1 << 2;
constexpr uint8_t ANCS_CATEGORY_OTHER = 0;
constexpr uint8_t ANCS_CATEGORY_INCOMING_CALL = 1;
constexpr uint8_t ANCS_CATEGORY_MISSED_CALL = 2;
constexpr uint8_t ANCS_CATEGORY_SOCIAL = 4;
constexpr uint8_t ANCS_CATEGORY_SCHEDULE = 5;
constexpr uint8_t ANCS_CATEGORY_EMAIL = 6;
constexpr uint8_t ANCS_COMMAND_GET_ATTRIBUTES = 0;
constexpr uint8_t ANCS_COMMAND_PERFORM_ACTION = 2;
constexpr uint8_t ANCS_ATTRIBUTE_APP = 0;
constexpr uint8_t ANCS_ATTRIBUTE_TITLE = 1;
constexpr uint8_t ANCS_ATTRIBUTE_MESSAGE = 3;
constexpr uint8_t ANCS_ACTION_POSITIVE = 0;
constexpr uint8_t ANCS_ACTION_NEGATIVE = 1;

struct AncsCall {
    bool ringing;
    bool answered;
    uint32_t uid;
};

AncsCall ancs_call;
AncsRequest expected_request;
bool expecting_response = false;
uint8_t response[512];
size_t response_length = 0;

int attribute_count(AncsRequestKind kind)
{
    return kind == AncsRequestKind::Message ? 3 : 1;
}

bool shows_message(uint8_t category)
{
    return category == ANCS_CATEGORY_OTHER || category == ANCS_CATEGORY_SOCIAL || category == ANCS_CATEGORY_SCHEDULE
        || category == ANCS_CATEGORY_EMAIL;
}

struct AncsAttributes {
    char app[EVB_TEXT_MAX + 1];
    char title[EVB_TEXT_MAX + 1];
    char message[EVB_TEXT_MAX + 1];
};

/* Returns false while the response is still arriving in pieces. */
bool parse_ancs_response(AncsAttributes *attributes)
{
    memset(attributes, 0, sizeof(*attributes));
    if (response_length < 5) {
        return false;
    }
    size_t position = 5;
    for (int i = 0; i < attribute_count(expected_request.kind); i++) {
        if (position + 3 > response_length) {
            return false;
        }
        uint8_t id = response[position];
        uint16_t length = le16(response + position + 1);
        position += 3;
        if (position + length > response_length) {
            return false;
        }
        char *target = NULL;
        if (id == ANCS_ATTRIBUTE_APP) {
            target = attributes->app;
        } else if (id == ANCS_ATTRIBUTE_TITLE) {
            target = attributes->title;
        } else if (id == ANCS_ATTRIBUTE_MESSAGE) {
            target = attributes->message;
        }
        if (target != NULL) {
            copy_bytes_as_text(target, EVB_TEXT_MAX + 1, response + position, length);
        }
        position += length;
    }
    return true;
}

void apply_ancs_attributes(const AncsAttributes &attributes)
{
    PhoneState *state = evb_phone_state_begin_edit();
    if (expected_request.kind == AncsRequestKind::Caller) {
        if (ancs_call.uid == expected_request.uid && state->call_status != PhoneCallStatus::Idle) {
            evb_copy_text(state->caller, sizeof(state->caller), attributes.title);
        }
    } else if (expected_request.kind == AncsRequestKind::MissedCall) {
        show_notification(state, "Missed call", attributes.title);
    } else {
        const char *sender = attributes.title[0] != '\0' ? attributes.title : attributes.app;
        push_message_row(state, sender, attributes.message);
        show_notification(state, sender, attributes.message);
    }
    evb_phone_state_end_edit();
}

/* ---------- AMS ---------- */

constexpr uint8_t AMS_ENTITY_PLAYER = 0;
constexpr uint8_t AMS_ENTITY_TRACK = 2;
constexpr uint8_t AMS_PLAYER_PLAYBACK_INFO = 1;
constexpr uint8_t AMS_PLAYER_VOLUME = 2;
constexpr uint8_t AMS_TRACK_ARTIST = 0;
constexpr uint8_t AMS_TRACK_TITLE = 2;
constexpr uint8_t AMS_TRACK_DURATION = 3;

/* ---------- CTS ---------- */

/* Days since 1970-01-01 for a civil date (proleptic Gregorian). */
int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

} // namespace

/* ---------- EVBikes Phone Link ---------- */

void feed_app_bytes(const uint8_t *data, size_t length, uint32_t now_ms)
{
    app_now_ms = now_ms;
    uint32_t good_before = app_parser.goodFrames();
    app_parser.feed(data, length);
    if (app_parser.goodFrames() != good_before) {
        last_app_frame_ms = now_ms;
        if (!app_link_seen) {
            app_link_seen = true;
            mark_app_link();
        }
    }
}

bool app_link_alive(uint32_t now_ms)
{
    return app_link_seen && now_ms - last_app_frame_ms < APP_LINK_QUIET_MS;
}

void forget_app_link(void)
{
    if (!app_link_seen) {
        return;
    }
    app_link_seen = false;
    app_parser.reset();
    PhoneState *state = evb_phone_state_begin_edit();
    if (!state->nav.demo) {
        state->nav.active = false;
    }
    memset(state->contacts, 0, sizeof(state->contacts));
    memset(state->reminders, 0, sizeof(state->reminders));
    state->can_dial = false;
    if (state->link_kind == PhoneLinkKind::EvbikesApp) {
        state->link_kind = state->phone_connected ? PhoneLinkKind::OtherPhone : PhoneLinkKind::None;
    }
    evb_phone_state_end_edit();
}

size_t build_app_media_command(MediaCommand command, uint8_t *out, size_t capacity)
{
    AppMediaCommand code = AppMediaCommand::PlayPause;
    switch (command) {
    case MediaCommand::PlayPause:
        code = AppMediaCommand::PlayPause;
        break;
    case MediaCommand::Next:
        code = AppMediaCommand::Next;
        break;
    case MediaCommand::Previous:
        code = AppMediaCommand::Previous;
        break;
    case MediaCommand::VolumeUp:
        code = AppMediaCommand::VolumeUp;
        break;
    case MediaCommand::VolumeDown:
        code = AppMediaCommand::VolumeDown;
        break;
    }
    const uint8_t payload[1] = {(uint8_t)code};
    return evb::link::buildFrame(evb::link::MsgType::MediaCommand, payload, sizeof(payload), out, capacity);
}

size_t build_app_call_command(AppCallCommand command, int slot, uint8_t *out, size_t capacity)
{
    const uint8_t payload[2] = {(uint8_t)command, (uint8_t)slot};
    size_t payload_length = command == AppCallCommand::DialContactSlot ? 2 : 1;
    return evb::link::buildFrame(evb::link::MsgType::CallCommand, payload, payload_length, out, capacity);
}

/* ---------- ANCS ---------- */

bool ancs_on_notification_source(const uint8_t *data, size_t length, uint32_t now_ms, AncsRequest *request)
{
    (void)now_ms;
    if (length < 8) {
        return false;
    }
    uint8_t event = data[0];
    uint8_t flags = data[1];
    uint8_t category = data[2];
    uint32_t uid = le32(data + 4);
    bool fresh = (flags & ANCS_FLAG_PRE_EXISTING) == 0;

    if (category == ANCS_CATEGORY_INCOMING_CALL) {
        if (event == ANCS_EVENT_ADDED || event == ANCS_EVENT_MODIFIED) {
            bool new_call = !ancs_call.ringing || ancs_call.uid != uid;
            ancs_call.ringing = true;
            ancs_call.answered = false;
            ancs_call.uid = uid;
            PhoneState *state = evb_phone_state_begin_edit();
            state->call_status = PhoneCallStatus::Ringing;
            if (new_call) {
                state->caller[0] = '\0';
            }
            evb_phone_state_end_edit();
            request->uid = uid;
            request->kind = AncsRequestKind::Caller;
            return new_call;
        }
        if (event == ANCS_EVENT_REMOVED && ancs_call.uid == uid) {
            ancs_call.ringing = false;
            PhoneState *state = evb_phone_state_begin_edit();
            if (!ancs_call.answered) {
                state->call_status = PhoneCallStatus::Idle;
            }
            evb_phone_state_end_edit();
        }
        return false;
    }
    if (event != ANCS_EVENT_ADDED || !fresh) {
        return false;
    }
    if (category == ANCS_CATEGORY_MISSED_CALL) {
        request->uid = uid;
        request->kind = AncsRequestKind::MissedCall;
        return true;
    }
    if (shows_message(category) && (flags & ANCS_FLAG_SILENT) == 0) {
        request->uid = uid;
        request->kind = AncsRequestKind::Message;
        return true;
    }
    return false;
}

size_t ancs_build_attribute_request(const AncsRequest &request, uint8_t *out, size_t capacity)
{
    if (capacity < 16) {
        return 0;
    }
    size_t length = 0;
    out[length++] = ANCS_COMMAND_GET_ATTRIBUTES;
    put_le32(out + length, request.uid);
    length += 4;
    if (request.kind == AncsRequestKind::Message) {
        out[length++] = ANCS_ATTRIBUTE_APP;
    }
    out[length++] = ANCS_ATTRIBUTE_TITLE;
    out[length++] = (uint8_t)EVB_TEXT_MAX;
    out[length++] = 0;
    if (request.kind == AncsRequestKind::Message) {
        out[length++] = ANCS_ATTRIBUTE_MESSAGE;
        out[length++] = (uint8_t)EVB_TEXT_MAX;
        out[length++] = 0;
    }
    return length;
}

void ancs_expect_response(const AncsRequest &request)
{
    expected_request = request;
    expecting_response = true;
    response_length = 0;
}

bool ancs_on_data_source(const uint8_t *data, size_t length, uint32_t now_ms)
{
    (void)now_ms;
    if (!expecting_response) {
        return false;
    }
    size_t room = sizeof(response) - response_length;
    if (length > room) {
        length = room;
    }
    memcpy(response + response_length, data, length);
    response_length += length;

    AncsAttributes attributes;
    if (!parse_ancs_response(&attributes)) {
        return response_length >= sizeof(response);
    }
    expecting_response = false;
    if (le32(response + 1) == expected_request.uid) {
        apply_ancs_attributes(attributes);
    }
    return true;
}

size_t ancs_build_call_action(bool answer, uint32_t now_ms, uint8_t *out, size_t capacity)
{
    if (!ancs_call.ringing || capacity < 6) {
        return 0;
    }
    out[0] = ANCS_COMMAND_PERFORM_ACTION;
    put_le32(out + 1, ancs_call.uid);
    out[5] = answer ? ANCS_ACTION_POSITIVE : ANCS_ACTION_NEGATIVE;
    ancs_call.answered = answer;

    PhoneState *state = evb_phone_state_begin_edit();
    if (answer) {
        state->call_status = PhoneCallStatus::Active;
        state->call_started_ms = now_ms;
    } else {
        state->call_status = PhoneCallStatus::Idle;
    }
    evb_phone_state_end_edit();
    return 6;
}

/* ---------- AMS ---------- */

size_t ams_build_player_registration(uint8_t *out, size_t capacity)
{
    if (capacity < 3) {
        return 0;
    }
    out[0] = AMS_ENTITY_PLAYER;
    out[1] = AMS_PLAYER_PLAYBACK_INFO;
    out[2] = AMS_PLAYER_VOLUME;
    return 3;
}

size_t ams_build_track_registration(uint8_t *out, size_t capacity)
{
    if (capacity < 4) {
        return 0;
    }
    out[0] = AMS_ENTITY_TRACK;
    out[1] = AMS_TRACK_ARTIST;
    out[2] = AMS_TRACK_TITLE;
    out[3] = AMS_TRACK_DURATION;
    return 4;
}

uint8_t ams_remote_command(MediaCommand command)
{
    switch (command) {
    case MediaCommand::PlayPause:
        return 2;
    case MediaCommand::Next:
        return 3;
    case MediaCommand::Previous:
        return 4;
    case MediaCommand::VolumeUp:
        return 5;
    case MediaCommand::VolumeDown:
        return 6;
    }
    return 2;
}

void ams_on_entity_update(const uint8_t *data, size_t length, uint32_t now_ms)
{
    if (length < 3) {
        return;
    }
    uint8_t entity = data[0];
    uint8_t attribute = data[1];
    char value[EVB_TEXT_MAX * 2 + 1];
    copy_bytes_as_text(value, sizeof(value), data + 3, length - 3);

    PhoneState *state = evb_phone_state_begin_edit();
    state->media_info_available = true;
    state->media_controls_available = true;
    if (entity == AMS_ENTITY_PLAYER && attribute == AMS_PLAYER_PLAYBACK_INFO) {
        /* "state,rate,elapsed", for example "1,1.0,35.2" */
        char *cursor = value;
        long playback = strtol(cursor, &cursor, 10);
        if (*cursor == ',') {
            strtof(cursor + 1, &cursor);
        }
        float elapsed = 0.0f;
        if (*cursor == ',') {
            elapsed = strtof(cursor + 1, NULL);
        }
        state->media_playing = playback == 1;
        state->media_position_s = (int)elapsed;
        state->media_position_at_ms = now_ms;
    } else if (entity == AMS_ENTITY_PLAYER && attribute == AMS_PLAYER_VOLUME) {
        state->media_volume_percent = (int)(strtof(value, NULL) * 100.0f + 0.5f);
    } else if (entity == AMS_ENTITY_TRACK && attribute == AMS_TRACK_ARTIST) {
        evb_copy_text(state->media_artist, sizeof(state->media_artist), value);
    } else if (entity == AMS_ENTITY_TRACK && attribute == AMS_TRACK_TITLE) {
        evb_copy_text(state->media_title, sizeof(state->media_title), value);
    } else if (entity == AMS_ENTITY_TRACK && attribute == AMS_TRACK_DURATION) {
        state->media_duration_s = (int)strtof(value, NULL);
    }
    evb_phone_state_end_edit();
}

/* ---------- CTS ---------- */

void cts_apply_current_time(const uint8_t *current_time, size_t length, const uint8_t *local_time_info, size_t info_length)
{
    if (length < 7) {
        return;
    }
    int year = le16(current_time);
    unsigned month = current_time[2];
    unsigned day = current_time[3];
    if (year < 2020 || month < 1 || month > 12 || day < 1 || day > 31) {
        return;
    }
    int64_t local_seconds = days_from_civil(year, month, day) * 86400 + current_time[4] * 3600 + current_time[5] * 60 + current_time[6];

    int offset_minutes = evb_clock_utc_offset_minutes();
    if (local_time_info != NULL && info_length >= 2 && (int8_t)local_time_info[0] != -128) {
        int quarter_hours = (int8_t)local_time_info[0];
        uint8_t dst = local_time_info[1];
        if (dst <= 8) {
            quarter_hours += dst;
        }
        offset_minutes = quarter_hours * 15;
        evb_clock_set_utc_offset(offset_minutes);
    }
    evb_clock_set_from_utc((uint32_t)(local_seconds - (int64_t)offset_minutes * 60), "iPhone");
}

void forget_phone_data(void)
{
    ancs_call = AncsCall();
    expecting_response = false;
    response_length = 0;
    app_link_seen = false;
    app_parser.reset();

    PhoneState *state = evb_phone_state_begin_edit();
    state->call_status = PhoneCallStatus::Idle;
    state->caller[0] = '\0';
    state->media_info_available = false;
    state->media_controls_available = false;
    state->call_controls_available = false;
    state->can_dial = false;
    state->media_playing = false;
    state->media_title[0] = '\0';
    state->media_artist[0] = '\0';
    state->media_position_s = 0;
    state->media_duration_s = 0;
    if (!state->nav.demo) {
        state->nav.active = false;
    }
    memset(state->contacts, 0, sizeof(state->contacts));
    memset(state->reminders, 0, sizeof(state->reminders));
    state->phone_battery_percent = 0;
    state->phone_signal_bars = 0;
    evb_phone_state_end_edit();
}

} // namespace evb::phone
