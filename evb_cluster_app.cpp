#include "evb_cluster_app.h"

#include "evb_boot_screens.h"
#include "evb_cluster_settings.h"
#include "evb_menu.h"
#include "evb_ride_screen.h"
#include "evb_top_chrome.h"
#include "evb_ui_kit.h"
#include "evb_vehicle_simulator.h"

using namespace evb;

namespace {

constexpr uint32_t TICK_MS = 20;
constexpr uint32_t SELF_TEST_MS = 1500;
constexpr int SPLASH_STEP_COUNT = 40;
constexpr uint32_t SCAN_MS = 1600;
constexpr uint32_t MATCH_SHOWN_MS = 1200;
constexpr uint32_t SIDE_STAND_DEMO_MS = 2000;
constexpr uint32_t PRE_RIDE_READY_MS = 2500;
constexpr uint32_t PARKED_FOR_MENU_MS = 8000;
const bool PROFILE_ENROLLED[EVB_PROFILE_COUNT] = {true, true, false};

enum class Stage {
    Splash,
    Auth,
    PreRide,
    Ride,
};

struct ClusterApp {
    Stage stage;
    uint32_t uptime_ms;
    uint32_t stage_elapsed_ms;
    uint32_t last_tick;

    evb_vehicle_state_t vehicle;
    evb_cluster_settings_t settings;

    evb_auth_state_t auth_state;
    uint32_t auth_elapsed_ms;
    uint32_t pre_ride_ready_ms;
    uint32_t park_request_left_ms;

    lv_obj_t *shell;
    evb_asset_id_t shown_shell;
    lv_obj_t *floor_glow;
    lv_obj_t *stage_layer;
};

ClusterApp app;

void show_shell(evb_asset_id_t shell)
{
    if (shell == app.shown_shell) {
        return;
    }
    show_picture(app.shell, shell);
    app.shown_shell = shell;
}

bool bike_is_stopped(void)
{
    return app.vehicle.speed_kmh <= EVB_MENU_LOCK_SPEED_KMH;
}

/* ---------- taps ---------- */

void on_profile_tapped(int profile_index)
{
    if (app.auth_state == EVB_AUTH_SCANNING || app.auth_state == EVB_AUTH_MATCHED) {
        return;
    }
    app.settings.profile_index = profile_index;
    app.auth_state = EVB_AUTH_IDLE;
}

void on_fingerprint_tapped(void)
{
    if (app.auth_state == EVB_AUTH_SCANNING || app.auth_state == EVB_AUTH_MATCHED) {
        return;
    }
    app.auth_state = EVB_AUTH_SCANNING;
    app.auth_elapsed_ms = 0;
}

void start_ride(void);

void on_pre_ride_ready_tapped(void)
{
    start_ride();
}

void on_ride_mode_tapped(void)
{
    app.vehicle.ride_mode = app.vehicle.ride_mode == EVB_RIDE_MODE_ECO ? EVB_RIDE_MODE_SPORT : EVB_RIDE_MODE_ECO;
}

void on_alerts_tapped(void)
{
    app.vehicle.alerts_muted = !app.vehicle.alerts_muted;
}

void on_settings_tapped(void)
{
    if (evb_menu_is_open()) {
        evb_menu_close();
        evb_ride_screen_show_menu_open(false);
        return;
    }
    if (!bike_is_stopped()) {
        evb_menu_show_lock_hint();
        app.park_request_left_ms = PARKED_FOR_MENU_MS;
        return;
    }
    app.park_request_left_ms = 0;
    evb_menu_open();
    evb_ride_screen_show_menu_open(true);
}

/* ---------- stages ---------- */

void start_auth(void)
{
    evb_splash_destroy();
    app.stage = Stage::Auth;
    app.stage_elapsed_ms = 0;
    app.auth_state = EVB_AUTH_IDLE;
    evb_auth_handlers_t handlers = {on_profile_tapped, on_fingerprint_tapped};
    evb_auth_create(app.stage_layer, &handlers);
}

void start_pre_ride(void)
{
    evb_auth_destroy();
    fade_to(app.floor_glow, LV_OPA_TRANSP, 480);
    app.stage = Stage::PreRide;
    app.stage_elapsed_ms = 0;
    app.pre_ride_ready_ms = 0;
    app.vehicle.side_stand_down = true;
    evb_pre_ride_create(app.stage_layer, on_pre_ride_ready_tapped);
}

void start_ride(void)
{
    if (app.stage == Stage::Ride) {
        return;
    }
    evb_pre_ride_destroy();
    lv_obj_delete(app.floor_glow);
    app.floor_glow = NULL;
    evb_boot_screens_release_pictures();
    app.shown_shell = EVB_ASSET_COUNT;
    show_shell(EVB_ASSET_SHELL_RIDE_ECO);

    app.stage = Stage::Ride;
    app.stage_elapsed_ms = 0;
    evb_ride_screen_handlers_t handlers = {on_ride_mode_tapped, on_alerts_tapped, on_settings_tapped};
    evb_ride_screen_create(app.stage_layer, &handlers);
    evb_menu_create(app.stage_layer, &app.settings, &app.vehicle);
}

void run_splash(void)
{
    int step = (int)(app.stage_elapsed_ms / EVB_SPLASH_STEP_MS);
    step = step > SPLASH_STEP_COUNT ? SPLASH_STEP_COUNT : step;
    evb_splash_show_step(step);
    if (step >= EVB_SPLASH_LAST_STEP) {
        start_auth();
    }
}

void show_floor_glow(void)
{
    if (app.auth_state == EVB_AUTH_DENIED) {
        tint_picture(app.floor_glow, color::red);
        fade_to(app.floor_glow, 140, 480);
    } else if (app.auth_state == EVB_AUTH_MATCHED) {
        tint_picture(app.floor_glow, color::teal);
        fade_to(app.floor_glow, 77, 480);
    } else {
        fade_to(app.floor_glow, LV_OPA_TRANSP, 480);
    }
}

/* The demo finger reader answers after SCAN_MS; rider 3 is not enrolled, like SystemData.cpp. */
void run_auth(uint32_t elapsed_ms)
{
    evb_auth_state_t before = app.auth_state;
    if (app.auth_state == EVB_AUTH_SCANNING || app.auth_state == EVB_AUTH_MATCHED) {
        app.auth_elapsed_ms += elapsed_ms;
    }
    if (app.auth_state == EVB_AUTH_SCANNING && app.auth_elapsed_ms >= SCAN_MS) {
        app.auth_state = PROFILE_ENROLLED[app.settings.profile_index] ? EVB_AUTH_MATCHED : EVB_AUTH_DENIED;
        app.auth_elapsed_ms = 0;
    }
    if (app.auth_state != before) {
        show_floor_glow();
    }
    evb_auth_show(app.auth_state, app.settings.profile_index, app.auth_elapsed_ms);
    if (app.auth_state == EVB_AUTH_MATCHED && app.auth_elapsed_ms >= MATCH_SHOWN_MS) {
        start_pre_ride();
    }
}

/* In the demo the side stand goes up by itself after SIDE_STAND_DEMO_MS. */
void run_pre_ride(uint32_t elapsed_ms)
{
    if (app.stage_elapsed_ms >= SIDE_STAND_DEMO_MS) {
        app.vehicle.side_stand_down = false;
    }
    show_shell(app.vehicle.side_stand_down ? EVB_ASSET_SHELL_PRERIDE_STAND : EVB_ASSET_SHELL_PRERIDE_READY);
    evb_pre_ride_show(app.vehicle.side_stand_down, app.vehicle.phone_connected);
    if (!app.vehicle.side_stand_down) {
        app.pre_ride_ready_ms += elapsed_ms;
        if (app.pre_ride_ready_ms >= PRE_RIDE_READY_MS) {
            start_ride();
        }
    }
}

void run_ride(uint32_t elapsed_ms)
{
    /* After "Stop the bike to open the menu" the demo brakes, then waits parked for a while. */
    if (app.park_request_left_ms > 0 && app.vehicle.speed_kmh == 0) {
        app.park_request_left_ms = elapsed_ms >= app.park_request_left_ms ? 0 : app.park_request_left_ms - elapsed_ms;
    }
    bool may_ride = app.settings.demo_running && !evb_menu_is_open() && app.park_request_left_ms == 0;
    evb_vehicle_simulator_step(&app.vehicle, elapsed_ms, may_ride);
    show_shell(app.vehicle.ride_mode == EVB_RIDE_MODE_SPORT ? EVB_ASSET_SHELL_RIDE_SPORT : EVB_ASSET_SHELL_RIDE_ECO);
    evb_ride_screen_show_state(&app.vehicle, &app.settings);
    evb_menu_refresh(elapsed_ms);
}

void run_cluster(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    uint32_t elapsed_ms = lv_tick_elaps(app.last_tick);
    app.last_tick = lv_tick_get();
    app.uptime_ms += elapsed_ms;
    app.stage_elapsed_ms += elapsed_ms;

    switch (app.stage) {
    case Stage::Splash:
        run_splash();
        break;
    case Stage::Auth:
        run_auth(elapsed_ms);
        break;
    case Stage::PreRide:
        run_pre_ride(elapsed_ms);
        break;
    case Stage::Ride:
        run_ride(elapsed_ms);
        break;
    }
    evb_top_chrome_show(&app.vehicle, &app.settings, app.stage == Stage::Ride, app.uptime_ms < SELF_TEST_MS);
}

void load_default_settings(evb_cluster_settings_t *settings)
{
    *settings = evb_cluster_settings_t();
    settings->profile_index = 1;
    settings->seat_level = 2;
    settings->auto_turn_off = true;
    settings->theft_captures = 1;
    settings->brightness_percent = 80;
    settings->speedo_style = EVB_SPEEDO_CLASSIC;
    settings->demo_running = true;
}

} // namespace

void evb_cluster_app_start(void)
{
    app = ClusterApp();
    evb_vehicle_simulator_reset(&app.vehicle);
    load_default_settings(&app.settings);

    lv_obj_t *root = lv_screen_active();
    lv_obj_clean(root);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_style_bg_color(root, color::black, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

    app.shell = add_picture(root, EVB_ASSET_SHELL_BOOT, 0, 0);
    app.shown_shell = EVB_ASSET_SHELL_BOOT;
    app.floor_glow = add_picture(root, EVB_ASSET_FLOOR_GLOW, panel_x_of_design_x(140), 311);
    lv_obj_set_style_opa(app.floor_glow, LV_OPA_TRANSP, 0);
    app.stage_layer = add_full_screen_layer(root);
    evb_top_chrome_create(root);

    app.stage = Stage::Splash;
    evb_splash_create(app.stage_layer);
    evb_splash_show_step(0);
    evb_top_chrome_show(&app.vehicle, &app.settings, false, true);

    app.last_tick = lv_tick_get();
    lv_timer_create(run_cluster, TICK_MS, NULL);
}
