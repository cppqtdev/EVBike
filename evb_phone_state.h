#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Everything the phone, Bluetooth and Wi-Fi tell the cluster. The Bluetooth stack and the
 * Wi-Fi task write it from their own tasks; the screens read a copy once per frame.
 */

constexpr size_t EVB_TEXT_MAX = 48;
constexpr int EVB_LIST_SLOTS = 3;
constexpr int EVB_WIFI_LIST_MAX = 10;

enum class PhoneLinkKind : uint8_t {
    None,       /* no phone connected */
    OtherPhone, /* paired phone, media keys only (Android without the EVBikes app) */
    Iphone,     /* iPhone: calls, notifications, music and time through Apple's ANCS, AMS and CTS */
    EvbikesApp, /* the EVBikes phone app: everything including navigation and dialling */
};

enum class PhoneCallStatus : uint8_t {
    Idle,
    Ringing,
    Active,
};

enum class WifiStatus : uint8_t {
    NoNetworkSaved,
    Connecting,
    Connected,
    Failed,
};

enum class UpdateStatus : uint8_t {
    Idle,
    NoServer,
    NoOtaPartition,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Failed,
    Done,
};

struct TextRow {
    char title[EVB_TEXT_MAX + 1];
    char text[EVB_TEXT_MAX + 1];
};

struct NavState {
    bool active;
    bool demo;
    uint8_t maneuver; /* evb::link::Maneuver */
    uint8_t roundabout_exit;
    uint32_t distance_to_maneuver_m;
    uint32_t distance_remaining_m;
    uint16_t eta_minutes;
    char road[EVB_TEXT_MAX + 1];
};

struct WifiNetwork {
    char ssid[33];
    int rssi;
    bool secured;
};

struct PhoneState {
    uint32_t revision;

    /* Bluetooth */
    bool bluetooth_ready;
    bool phone_connected;
    bool phone_bonded;
    PhoneLinkKind link_kind;
    bool media_info_available;
    bool media_controls_available;
    bool call_controls_available;
    bool can_dial;
    int phone_battery_percent;
    int phone_signal_bars;

    PhoneCallStatus call_status;
    char caller[EVB_TEXT_MAX + 1];
    uint32_t call_started_ms;

    bool media_playing;
    int media_volume_percent;
    int media_position_s;
    int media_duration_s;
    uint32_t media_position_at_ms;
    char media_title[EVB_TEXT_MAX + 1];
    char media_artist[EVB_TEXT_MAX + 1];

    NavState nav;

    TextRow contacts[EVB_LIST_SLOTS];
    TextRow reminders[EVB_LIST_SLOTS];

    uint32_t notification_seq;
    char notification_sender[EVB_TEXT_MAX + 1];
    char notification_text[EVB_TEXT_MAX + 1];

    /* Wi-Fi */
    WifiStatus wifi_status;
    char wifi_ssid[33];
    char wifi_ip[16];
    int wifi_rssi;
    bool wifi_scanning;
    int wifi_network_count;
    WifiNetwork wifi_networks[EVB_WIFI_LIST_MAX];

    bool weather_valid;
    int weather_temp_c;
    char weather_place[32];

    UpdateStatus update_status;
    int update_percent;
    char update_version[16];
    char update_message[64];
    bool network_upload_ready;

    /* Clock */
    bool clock_synced;
    char clock_source[16];
    int utc_offset_minutes;
};

/* Copies the whole state under the lock. */
void evb_phone_state_read(PhoneState *copy);

/* Locks the state for writing; every begin needs its end, which also bumps the revision. */
PhoneState *evb_phone_state_begin_edit(void);
void evb_phone_state_end_edit(void);

/* Bounded copy that always ends the text. */
void evb_copy_text(char *destination, size_t capacity, const char *source);
