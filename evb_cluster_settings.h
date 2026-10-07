#pragma once

#include <stdbool.h>
#include <stdint.h>

constexpr int EVB_PROFILE_COUNT = 3;
constexpr int EVB_MAX_SEAT_LEVEL = 5;
constexpr int EVB_MENU_LOCK_SPEED_KMH = 5;

typedef enum {
    EVB_AUTH_IDLE,
    EVB_AUTH_SCANNING,
    EVB_AUTH_DENIED,
    EVB_AUTH_MATCHED,
} evb_auth_state_t;

typedef enum {
    EVB_SPEEDO_CLASSIC,
    EVB_SPEEDO_HEX,
} evb_speedo_style_t;

/* Rider settings and everything the menu pages change. Values match SystemData.cpp in EVBikes. */
typedef struct {
    int profile_index;
    int seat_level;
    bool auto_turn_off;
    bool anti_theft_armed;
    int theft_captures;
    bool use_24_hour;
    bool use_miles;
    bool night_mode;
    int brightness_percent;
    evb_speedo_style_t speedo_style;
    bool demo_running;
    bool payment_done;
} evb_cluster_settings_t;
