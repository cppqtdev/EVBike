#include "evb_boot_screens.h"

#include <math.h>

#include "evb_ui_kit.h"

using namespace evb;

namespace {

/* ---------- boot chrome: fingerprint and progress bar (BootChrome.qml) ---------- */

constexpr int PROGRESS_X = 244;
constexpr int PROGRESS_Y = 435;
constexpr int PROGRESS_WIDTH = 311;
constexpr int PROGRESS_FILL_WIDTH = 310;

struct BootChrome {
    lv_obj_t *print;
    lv_obj_t *progress_glow;
    lv_obj_t *progress_fill;
};

BootChrome add_boot_chrome(lv_obj_t *parent)
{
    BootChrome chrome;
    add_tinted_picture(parent, EVB_ASSET_FINGERPRINT_DISC, 362, 283, color::neutral1d);
    chrome.print = add_tinted_picture(parent, EVB_ASSET_FINGERPRINT, 362, 283, color::mint_highlight);
    chrome.progress_glow = add_tinted_picture(parent, EVB_ASSET_PROGRESS_GLOW, 235, 422, color::mint);

    lv_obj_t *track = add_color_block(parent, PROGRESS_X, PROGRESS_Y, PROGRESS_WIDTH, 14, lv_color_hex(0x5F8E6F), LV_OPA_COVER, 7);
    paint_vertical_gradient(track, lv_color_hex(0x5F8E6F), lv_color_hex(0x7AB08D));
    add_color_block(parent, PROGRESS_X + 2, PROGRESS_Y + 3, PROGRESS_WIDTH - 4, 8, lv_color_hex(0x132316), LV_OPA_COVER, 4);
    chrome.progress_fill = add_color_block(parent, PROGRESS_X + 1, PROGRESS_Y, 14, 14, lv_color_hex(0x91E4C3), LV_OPA_COVER, 7);
    paint_vertical_gradient(chrome.progress_fill, lv_color_hex(0x91E4C3), lv_color_hex(0x7FF074));
    return chrome;
}

void show_boot_progress(const BootChrome &chrome, float progress)
{
    int fill_width = (int)(PROGRESS_FILL_WIDTH * progress);
    lv_obj_set_width(chrome.progress_fill, fill_width < 14 ? 14 : fill_width);
    lv_obj_set_style_image_opa(chrome.progress_glow, (lv_opa_t)(0.45f * progress * 255.0f), 0);
}

/* ---------- splash ---------- */

constexpr int STROKE_COUNT = 8;
const evb_asset_id_t STROKE_PICTURES[STROKE_COUNT] = {
    EVB_ASSET_FRONT_LINE_0, EVB_ASSET_FRONT_LINE_1, EVB_ASSET_FRONT_LINE_2, EVB_ASSET_FRONT_LINE_3,
    EVB_ASSET_FRONT_LINE_4, EVB_ASSET_FRONT_LINE_5, EVB_ASSET_FRONT_LINE_6, EVB_ASSET_FRONT_LINE_7,
};

struct Splash {
    lv_obj_t *layer;
    lv_obj_t *line_art;
    lv_obj_t *strokes[STROKE_COUNT];
    lv_obj_t *headlight;
    lv_obj_t *logo_layer;
    BootChrome chrome;
    int shown_step;
};

Splash splash;

/* ---------- unlock ---------- */

constexpr int SCAN_DOT_COUNT = 8;
const int PROFILE_CENTER_X[EVB_PROFILE_COUNT] = {244, 395, 549};
const char *const PROFILE_NAMES[EVB_PROFILE_COUNT] = {"RIDER 1", "RIDER 2", "RIDER 3"};

struct ProfileTile {
    lv_obj_t *large_avatar;
    lv_obj_t *small_avatar;
    lv_obj_t *name;
};

struct Auth {
    lv_obj_t *layer;
    lv_obj_t *profiles_layer;
    ProfileTile tiles[EVB_PROFILE_COUNT];
    lv_obj_t *status_row;
    lv_obj_t *status_text;
    lv_obj_t *scan_dots[SCAN_DOT_COUNT];
    lv_obj_t *denied_icon;
    lv_obj_t *matched_icon;
    BootChrome chrome;
    evb_auth_handlers_t handlers;
    bool has_shown;
    evb_auth_state_t shown_state;
    int shown_profile;
    int shown_dot;
};

Auth auth;

/* ---------- pre-ride ---------- */

constexpr int STATUS_CELL_COUNT = 3;

struct PreRide {
    lv_obj_t *layer;
    lv_obj_t *stand_alert;
    lv_obj_t *glow_band;
    lv_obj_t *band_text;
    lv_obj_t *status_icons[STATUS_CELL_COUNT];
    lv_obj_t *lift_stand_hint;
    void (*on_ready_tapped)(void);
    bool has_shown;
    bool side_stand_down;
    bool phone_connected;
};

PreRide pre_ride;

void forward_profile_tap(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    if (auth.handlers.on_profile_tapped != NULL) {
        auth.handlers.on_profile_tapped(index);
    }
}

/* "No access" goes back to the rider list, like picking a rider with the buttons does. */
void forward_status_tap(lv_event_t *event)
{
    LV_UNUSED(event);
    if (auth.shown_state == EVB_AUTH_DENIED && auth.handlers.on_profile_tapped != NULL) {
        auth.handlers.on_profile_tapped(auth.shown_profile);
    }
}

void forward_fingerprint_tap(lv_event_t *event)
{
    LV_UNUSED(event);
    if (auth.handlers.on_fingerprint_tapped != NULL) {
        auth.handlers.on_fingerprint_tapped();
    }
}

void forward_pre_ride_tap(lv_event_t *event)
{
    LV_UNUSED(event);
    if (pre_ride.side_stand_down || pre_ride.on_ready_tapped == NULL) {
        return;
    }
    pre_ride.on_ready_tapped();
}

void build_profile_tile(lv_obj_t *parent, int index)
{
    lv_obj_t *tile = add_plain_box(parent, PROFILE_CENTER_X[index] - 60, 118, 120, 150);
    ProfileTile &look = auth.tiles[index];
    look.large_avatar = add_tinted_picture(tile, EVB_ASSET_AVATAR_LARGE, 7, 10, color::text_soft_white);
    look.small_avatar = add_tinted_picture(tile, EVB_ASSET_AVATAR_SMALL, 14, 13, color::divider_cool);
    look.name = add_text(tile, &evb_font_regular_16, color::icon_muted, PROFILE_NAMES[index]);
    lv_obj_align(look.name, LV_ALIGN_TOP_MID, 0, 122);
    call_on_tap(tile, forward_profile_tap, (void *)(intptr_t)index);
}

void show_selected_profile(int profile_index)
{
    for (int i = 0; i < EVB_PROFILE_COUNT; i++) {
        bool selected = i == profile_index;
        set_shown(auth.tiles[i].large_avatar, selected);
        set_shown(auth.tiles[i].small_avatar, !selected);
        lv_obj_set_style_text_color(auth.tiles[i].name, selected ? color::text_primary : color::icon_muted, 0);
    }
}

void build_status_row(lv_obj_t *parent)
{
    auth.status_row = add_plain_box(parent, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(auth.status_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(auth.status_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    place_by_top_center(auth.status_row, 400, 204);
    call_on_tap(auth.status_row, forward_status_tap, NULL);

    auth.status_text = add_text(auth.status_row, &evb_font_regular_34, color::white, "Scan");
    lv_obj_t *icon_box = add_plain_box(auth.status_row, 0, 0, 44, 44);
    for (int i = 0; i < SCAN_DOT_COUNT; i++) {
        float angle = i * 0.785f;
        int x = (int)(22 + 10 * cosf(angle) - 2);
        int y = (int)(24 + 10 * sinf(angle) - 2);
        auth.scan_dots[i] = add_color_block(icon_box, x, y, 4, 4, color::white, LV_OPA_COVER, 2);
    }
    auth.denied_icon = add_tinted_picture(icon_box, EVB_ASSET_ICON_TRIANGLE_32, 8, 10, color::red);
    auth.matched_icon = add_tinted_picture(icon_box, EVB_ASSET_ICON_CHECK_32, 8, 10, color::teal);
}

void show_scan_dots(int dot)
{
    for (int i = 0; i < SCAN_DOT_COUNT; i++) {
        lv_obj_set_style_bg_opa(auth.scan_dots[i], (lv_opa_t)(((i - dot + SCAN_DOT_COUNT) % SCAN_DOT_COUNT) * 255 / SCAN_DOT_COUNT), 0);
    }
}

void show_auth_state(evb_auth_state_t state)
{
    bool idle = state == EVB_AUTH_IDLE;
    fade_to(auth.profiles_layer, idle ? LV_OPA_COVER : LV_OPA_TRANSP, 240);
    set_shown(auth.status_row, !idle);

    const char *text = "Match";
    lv_color_t print_color = color::mint_highlight;
    if (state == EVB_AUTH_SCANNING) {
        text = "Scan";
    } else if (state == EVB_AUTH_DENIED) {
        text = "No access";
        print_color = color::red;
    } else if (state == EVB_AUTH_MATCHED) {
        print_color = color::teal;
    }
    lv_label_set_text(auth.status_text, text);
    tint_picture(auth.chrome.print, print_color);
    for (int i = 0; i < SCAN_DOT_COUNT; i++) {
        set_shown(auth.scan_dots[i], state == EVB_AUTH_SCANNING);
    }
    set_shown(auth.denied_icon, state == EVB_AUTH_DENIED);
    set_shown(auth.matched_icon, state == EVB_AUTH_MATCHED);
}

void build_stand_alert(lv_obj_t *parent)
{
    pre_ride.stand_alert = add_full_screen_layer(parent);
    lv_obj_t *alert = pre_ride.stand_alert;
    lv_obj_t *glow = add_tinted_picture(alert, EVB_ASSET_GLOW_BLOB_80, 333, 165, color::red);
    lv_obj_set_style_image_opa(glow, 204, 0);
    add_color_block(alert, 365, 197, 16, 16, color::red, LV_OPA_COVER, LV_RADIUS_CIRCLE);

    static lv_point_precise_t rule_points[2] = {{373, 205}, {512, 161}};
    lv_obj_t *rule = lv_line_create(alert);
    lv_line_set_points(rule, rule_points, 2);
    lv_obj_set_style_line_color(rule, lv_color_hex(0xC0283A), 0);
    lv_obj_set_style_line_width(rule, 2, 0);

    add_tinted_picture(alert, EVB_ASSET_CALLOUT, 510, 110, lv_color_hex(0x2A1A1C));
    add_tinted_picture(alert, EVB_ASSET_ICON_TRIANGLE_24, 535, 115, lv_color_hex(0xE05A68));
    lv_obj_t *warning = add_text(alert, &evb_font_italic_22, lv_color_hex(0xE05A68), "Warning");
    lv_obj_set_pos(warning, 563, 112);
    add_tinted_picture(alert, EVB_ASSET_CALLOUT, 505, 143, lv_color_hex(0xA81C2C));
    lv_obj_t *stand = add_text(alert, &evb_font_italic_22, color::white, "Side stand alert");
    place_by_top_center(stand, 625, 146);
}

} // namespace

/* ---------- splash ---------- */

void evb_splash_create(lv_obj_t *parent)
{
    splash = Splash();
    splash.layer = add_full_screen_layer(parent);

    splash.line_art = add_plain_box(splash.layer, panel_x_of_design_x(817) - 180, 100, 360, 270);
    for (int i = 0; i < STROKE_COUNT; i++) {
        splash.strokes[i] = add_tinted_picture(splash.line_art, STROKE_PICTURES[i], 0, 0, color::white);
        lv_obj_set_style_opa(splash.strokes[i], LV_OPA_TRANSP, 0);
    }
    splash.headlight = add_tinted_picture(splash.line_art, EVB_ASSET_FRONT_HEADLIGHT, 0, 0, color::white);
    lv_obj_set_style_opa(splash.headlight, LV_OPA_TRANSP, 0);

    splash.logo_layer = add_full_screen_layer(splash.layer);
    lv_obj_set_style_opa(splash.logo_layer, LV_OPA_TRANSP, 0);
    lv_obj_t *logo = add_text(splash.logo_layer, &evb_font_logo_64, color::white, "EVBIKES");
    lv_obj_set_style_text_letter_space(logo, 6, 0);
    place_by_top_center(logo, 400, 160);
    splash.chrome = add_boot_chrome(splash.logo_layer);
    show_boot_progress(splash.chrome, 0.05f);
    splash.shown_step = -1;
}

void evb_splash_show_step(int step)
{
    if (step == splash.shown_step) {
        return;
    }
    for (int i = 0; i < STROKE_COUNT; i++) {
        fade_to(splash.strokes[i], step > i ? LV_OPA_COVER : LV_OPA_TRANSP, 200);
    }
    fade_to(splash.headlight, step >= 9 ? LV_OPA_COVER : LV_OPA_TRANSP, 500);
    fade_to(splash.line_art, step < 13 ? LV_OPA_COVER : LV_OPA_TRANSP, 600);
    fade_to(splash.logo_layer, step >= 16 ? LV_OPA_COVER : LV_OPA_TRANSP, 400);

    float progress = (step - 16) / 9.0f;
    progress = progress < 0.05f ? 0.05f : (progress > 0.95f ? 0.95f : progress);
    show_boot_progress(splash.chrome, progress);
    splash.shown_step = step;
}

void evb_splash_destroy(void)
{
    if (splash.layer != NULL) {
        lv_obj_delete(splash.layer);
    }
    splash = Splash();
}

/* ---------- unlock ---------- */

void evb_auth_create(lv_obj_t *parent, const evb_auth_handlers_t *handlers)
{
    auth = Auth();
    if (handlers != NULL) {
        auth.handlers = *handlers;
    }
    auth.layer = add_full_screen_layer(parent);

    auth.profiles_layer = add_full_screen_layer(auth.layer);
    for (int i = 0; i < EVB_PROFILE_COUNT; i++) {
        build_profile_tile(auth.profiles_layer, i);
    }
    build_status_row(auth.layer);
    auth.chrome = add_boot_chrome(auth.layer);
    show_boot_progress(auth.chrome, 1.0f);
    add_touch_area(auth.layer, 310, 260, 180, 125, forward_fingerprint_tap, NULL);
}

void evb_auth_show(evb_auth_state_t auth_state, int profile_index, uint32_t elapsed_ms)
{
    bool first = !auth.has_shown;
    if (first || auth_state != auth.shown_state) {
        show_auth_state(auth_state);
    }
    if (first || profile_index != auth.shown_profile) {
        show_selected_profile(profile_index);
    }
    int dot = (int)(elapsed_ms / 120) % SCAN_DOT_COUNT;
    if (auth_state == EVB_AUTH_SCANNING && (first || dot != auth.shown_dot)) {
        show_scan_dots(dot);
    }
    auth.has_shown = true;
    auth.shown_state = auth_state;
    auth.shown_profile = profile_index;
    auth.shown_dot = dot;
}

void evb_auth_destroy(void)
{
    if (auth.layer != NULL) {
        lv_obj_delete(auth.layer);
    }
    auth = Auth();
}

/* ---------- pre-ride ---------- */

void evb_pre_ride_create(lv_obj_t *parent, void (*on_ready_tapped)(void))
{
    pre_ride = PreRide();
    pre_ride.on_ready_tapped = on_ready_tapped;
    pre_ride.layer = add_full_screen_layer(parent);
    call_on_tap(pre_ride.layer, forward_pre_ride_tap, NULL);

    add_picture(pre_ride.layer, EVB_ASSET_BIKE, 308, 96);
    build_stand_alert(pre_ride.layer);

    pre_ride.glow_band = add_tinted_picture(pre_ride.layer, EVB_ASSET_GLOW_BAND, 245, 262, lv_color_hex(0x1F6F58));
    pre_ride.band_text = add_text(pre_ride.layer, &evb_font_bold_italic_16, lv_color_hex(0xECEFF0), "CONNECTED");
    place_by_top_center(pre_ride.band_text, 405, 266);

    const evb_asset_id_t icons[STATUS_CELL_COUNT] = {EVB_ASSET_ICON_BLUETOOTH_30, EVB_ASSET_ICON_HELMET_30, EVB_ASSET_ICON_WATCH_30};
    for (int i = 0; i < STATUS_CELL_COUNT; i++) {
        lv_obj_t *cell = add_color_block(pre_ride.layer, 326 + i * 60, 308, 46, 40, color::housing, LV_OPA_COVER, 6);
        pre_ride.status_icons[i] = add_tinted_picture(cell, icons[i], 8, 5, color::text_steel);
    }

    pre_ride.lift_stand_hint = add_text(pre_ride.layer, &evb_font_regular_13, color::text_muted, "Lift the side stand to continue");
    place_by_top_center(pre_ride.lift_stand_hint, 400, 372);
}

void evb_pre_ride_show(bool side_stand_down, bool phone_connected)
{
    bool first = !pre_ride.has_shown;
    if (first || side_stand_down != pre_ride.side_stand_down) {
        bool ready = !side_stand_down;
        set_shown(pre_ride.stand_alert, side_stand_down);
        set_shown(pre_ride.lift_stand_hint, side_stand_down);
        tint_picture(pre_ride.glow_band, ready ? lv_color_hex(0x1F6F58) : lv_color_hex(0x6E1420));
        lv_label_set_text(pre_ride.band_text, ready ? "CONNECTED" : "CONNECTIVITY");
        tint_picture(pre_ride.status_icons[1], ready ? lv_color_hex(0x7FE39A) : color::text_steel);
    }
    if (first || phone_connected != pre_ride.phone_connected) {
        tint_picture(pre_ride.status_icons[0], phone_connected ? color::teal : color::divider_cool);
    }
    pre_ride.has_shown = true;
    pre_ride.side_stand_down = side_stand_down;
    pre_ride.phone_connected = phone_connected;
}

void evb_pre_ride_destroy(void)
{
    if (pre_ride.layer != NULL) {
        lv_obj_delete(pre_ride.layer);
    }
    pre_ride = PreRide();
}

void evb_boot_screens_release_pictures(void)
{
    const evb_asset_id_t boot_only[] = {
        EVB_ASSET_SHELL_BOOT, EVB_ASSET_SHELL_PRERIDE_READY, EVB_ASSET_SHELL_PRERIDE_STAND, EVB_ASSET_FLOOR_GLOW,
        EVB_ASSET_FRONT_LINE_0, EVB_ASSET_FRONT_LINE_1, EVB_ASSET_FRONT_LINE_2, EVB_ASSET_FRONT_LINE_3,
        EVB_ASSET_FRONT_LINE_4, EVB_ASSET_FRONT_LINE_5, EVB_ASSET_FRONT_LINE_6, EVB_ASSET_FRONT_LINE_7,
        EVB_ASSET_FRONT_HEADLIGHT, EVB_ASSET_FINGERPRINT_DISC, EVB_ASSET_FINGERPRINT, EVB_ASSET_PROGRESS_GLOW,
        EVB_ASSET_AVATAR_LARGE, EVB_ASSET_AVATAR_SMALL, EVB_ASSET_ICON_TRIANGLE_32, EVB_ASSET_ICON_CHECK_32,
        EVB_ASSET_ICON_BLUETOOTH_30, EVB_ASSET_ICON_HELMET_30, EVB_ASSET_ICON_WATCH_30, EVB_ASSET_ICON_TRIANGLE_24,
        EVB_ASSET_GLOW_BLOB_80, EVB_ASSET_CALLOUT, EVB_ASSET_GLOW_BAND,
    };
    for (evb_asset_id_t asset : boot_only) {
        evb_asset_unload(asset);
    }
}
