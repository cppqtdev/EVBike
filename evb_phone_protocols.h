#pragma once

#include <stddef.h>
#include <stdint.h>

#include "evb_bluetooth.h"

/*
 * The phone protocols without the radio: they turn received bytes into PhoneState and
 * build the bytes to send. Kept apart from the BLE stack so they can be tested on a PC.
 */
namespace evb::phone {

/* ---------- EVBikes Phone Link (docs/03-protocols.md) ---------- */

/* Commands the cluster sends. 1-3 and answer/reject are in the EVBikes protocol;
 * volume, end call and dial are additions this cluster sends and the app may handle. */
enum class AppMediaCommand : uint8_t {
    PlayPause = 1,
    Next = 2,
    Previous = 3,
    VolumeUp = 4,
    VolumeDown = 5,
};

enum class AppCallCommand : uint8_t {
    Answer = 1,
    Reject = 2,
    End = 3,
    DialContactSlot = 4, /* followed by the contact list slot */
};

void feed_app_bytes(const uint8_t *data, size_t length, uint32_t now_ms);
bool app_link_alive(uint32_t now_ms);
/* Clears what only the app keeps (navigation, lists) once it has gone quiet. */
void forget_app_link(void);

size_t build_app_media_command(MediaCommand command, uint8_t *out, size_t capacity);
size_t build_app_call_command(AppCallCommand command, int slot, uint8_t *out, size_t capacity);

/* ---------- Apple Notification Center Service ---------- */

enum class AncsRequestKind : uint8_t {
    Caller,
    MissedCall,
    Message,
};

struct AncsRequest {
    uint32_t uid;
    AncsRequestKind kind;
};

/* Returns true when the notification needs its text fetched with `request`. */
bool ancs_on_notification_source(const uint8_t *data, size_t length, uint32_t now_ms, AncsRequest *request);
size_t ancs_build_attribute_request(const AncsRequest &request, uint8_t *out, size_t capacity);
void ancs_expect_response(const AncsRequest &request);
/* Returns true when the expected response is complete. */
bool ancs_on_data_source(const uint8_t *data, size_t length, uint32_t now_ms);
/* Builds "perform action" for the ringing call; returns 0 when no call is ringing. */
size_t ancs_build_call_action(bool answer, uint32_t now_ms, uint8_t *out, size_t capacity);

/* ---------- Apple Media Service ---------- */

size_t ams_build_player_registration(uint8_t *out, size_t capacity);
size_t ams_build_track_registration(uint8_t *out, size_t capacity);
uint8_t ams_remote_command(MediaCommand command);
void ams_on_entity_update(const uint8_t *data, size_t length, uint32_t now_ms);

/* ---------- Current Time Service ---------- */

/* `local_time_info` may be NULL when the phone does not offer it. */
void cts_apply_current_time(const uint8_t *current_time, size_t length, const uint8_t *local_time_info, size_t info_length);

/* Clears everything a phone told the cluster (on disconnect). */
void forget_phone_data(void);

} // namespace evb::phone
