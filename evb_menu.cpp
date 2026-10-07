#include "evb_menu.h"

#include <stdio.h>
#include <string.h>

#include "evb_bluetooth.h"
#include "evb_clock.h"
#include "evb_firmware_info.h"
#include "evb_ui_kit.h"
#include "evb_wifi.h"

/*
 * Menu pages keep their design size; their design x is moved by 245 so the design
 * centre (x 645) lands on the panel centre (x 400). Hardware buttons become taps:
 * the side tabs and chevrons move the carousel, a swipe does too, and each page
 * reacts to taps on its own controls.
 */

using namespace evb;

namespace {

constexpr int MENU_PAGE_COUNT = 13;
constexpr int CAROUSEL_TOP = 303;
constexpr uint32_t LOCK_HINT_MS = 2000;
constexpr uint32_t PHONE_PAGE_REBUILD_MS = 500;
constexpr uint32_t REFRESH_MS = 250;

enum MenuPageIndex {
    PAGE_PROFILE,
    PAGE_DIGILOCKER,
    PAGE_SEAT,
    PAGE_CHARGING,
    PAGE_BIKE_STATUS,
    PAGE_SECURITY,
    PAGE_PAYMENT,
    PAGE_CUSTOMIZE,
    PAGE_MISC,
    PAGE_CONNECTIVITY,
    PAGE_WIFI,
    PAGE_SYSTEM,
    PAGE_VITALS,
};

const char *const PAGE_TITLES[MENU_PAGE_COUNT] = {
    "Profile", "Digilocker", "Seat", "Charging", "Bike status", "Security",
    "Payment", "Customize", "Misc.", "Connections", "Wi-Fi", "System", "Vitals",
};

int page_x(int design_x)
{
    return panel_x_of_page_x(design_x);
}

/* ---------- menu state ---------- */

struct Menu {
    evb_cluster_settings_t *settings;
    evb_vehicle_state_t *vehicle;
    const PhoneState *phone;
    uint32_t now_ms;
    uint32_t shown_phone_revision;
    uint32_t phone_rebuild_elapsed_ms;

    lv_obj_t *keyboard_layer;
    lv_obj_t *password_title;
    lv_obj_t *password_area;
    char password_ssid[33];

    lv_obj_t *layer;
    lv_obj_t *page_host;
    lv_obj_t *page;
    lv_obj_t *tab_labels[3];
    lv_obj_t *dots[MENU_PAGE_COUNT];
    lv_obj_t *lock_hint;
    uint32_t lock_hint_left_ms;

    bool open;
    int page_index;
    int sub_index;
    int sub_level;
    uint32_t refresh_elapsed_ms;
};

Menu menu;

/* Live labels of the page on screen; refreshed every REFRESH_MS. */
struct LiveLabels {
    lv_obj_t *charging_title;
    lv_obj_t *charging_soc;
    lv_obj_t *charging_minutes;
    lv_obj_t *charging_arc;
    lv_obj_t *charging_plug_mark;
    lv_obj_t *music_knob;
    lv_obj_t *music_time;
    lv_obj_t *vitals_power;
    lv_obj_t *vitals_range;
    lv_obj_t *vitals_pack_temp;
};

LiveLabels live;

void rebuild_page(void);

/* ---------- shared page parts ---------- */

lv_obj_t *add_wrapped_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text, int center_x, int top, int width)
{
    lv_obj_t *label = add_text(parent, font, color, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    place_by_top_center(label, center_x, top);
    return label;
}

lv_obj_t *add_centered_text(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text, int center_x, int top)
{
    lv_obj_t *label = add_text(parent, font, color, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    place_by_top_center(label, center_x, top);
    return label;
}

lv_obj_t *add_text_at(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text, int x, int y)
{
    lv_obj_t *label = add_text(parent, font, color, text);
    lv_obj_set_pos(label, x, y);
    return label;
}

struct GlassLook {
    lv_color_t top;
    lv_color_t bottom;
};

const GlassLook PLAIN_GLASS = {color::neutral40, color::neutral28};

/* GlassButton.qml: a lit edge on top, a soft glow behind when selected. */
lv_obj_t *add_glass_button(lv_obj_t *parent, int x, int y, int width, int height, const char *text,
                           const lv_font_t *font, bool selected, lv_color_t glow, const GlassLook &look)
{
    bool wide = width > 180;
    evb_asset_id_t glow_picture = wide ? EVB_ASSET_GLOW_BLOB_320 : EVB_ASSET_GLOW_SPOT;
    if (selected) {
        lv_obj_t *halo = add_tinted_picture(parent, glow_picture,
                                            x + width / 2 - picture_width(glow_picture) / 2,
                                            y + height / 2 - picture_height(glow_picture) / 2, glow);
        lv_obj_set_style_image_opa(halo, 89, 0);
    }
    lv_obj_t *button = add_color_block(parent, x, y, width, height, look.top, LV_OPA_COVER, 4);
    paint_vertical_gradient(button, look.top, look.bottom);
    add_color_block(button, 0, 0, width, 1, selected ? color::text_cool : lv_color_hex(0x5B6164), LV_OPA_COVER, 0);
    lv_obj_t *label = add_text(button, font, color::text_primary, text);
    lv_obj_center(label);
    return button;
}

void add_demo_tag(lv_obj_t *parent)
{
    lv_obj_t *tag = add_color_block(parent, page_x(900), 96, 58, 18, color::surface_raised, LV_OPA_COVER, 3);
    lv_obj_t *label = add_text(tag, &evb_font_bold_10, color::text_muted, "DEMO");
    lv_obj_center(label);
}

/* TabStrip.qml: three labels in a dark strip, the current one on a green pill. Returns the strip. */
lv_obj_t *add_tab_strip(lv_obj_t *parent, int top, const char *const labels[3], int current, lv_event_cb_t on_tab_tapped)
{
    constexpr int padding = 16;
    constexpr int cell_height = 30;
    constexpr int inset = 3;

    int widths[3];
    int total = 0;
    for (int i = 0; i < 3; i++) {
        lv_point_t size;
        lv_text_get_size(&size, labels[i], &evb_font_regular_15, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        widths[i] = size.x + padding * 2;
        total += widths[i];
    }
    lv_obj_t *strip = add_color_block(parent, 0, top, total + inset * 2, cell_height + inset * 2, color::neutral21, LV_OPA_COVER, 6);
    place_by_top_center(strip, 400, top);

    int x = inset;
    for (int i = 0; i < 3; i++) {
        bool selected = i == current;
        lv_obj_t *cell = add_plain_box(strip, x, inset, widths[i], cell_height);
        if (selected) {
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, 5, 0);
            paint_vertical_gradient(cell, color::success_muted, lv_color_hex(0x1D5E49));
        }
        lv_obj_t *label = add_text(cell, &evb_font_regular_15, selected ? color::text_primary : color::text_secondary, labels[i]);
        lv_obj_center(label);
        call_on_tap(cell, on_tab_tapped, (void *)(intptr_t)i);
        x += widths[i];
    }
    return strip;
}

/* ---------- Profile ---------- */

const int PROFILE_CENTER_X[EVB_PROFILE_COUNT] = {244, 395, 549};
const char *const PROFILE_NAMES[EVB_PROFILE_COUNT] = {"RIDER 1", "RIDER 2", "RIDER 3"};

void on_profile_tapped(lv_event_t *event)
{
    menu.settings->profile_index = (int)(intptr_t)lv_event_get_user_data(event);
    rebuild_page();
}

void build_profile_page(lv_obj_t *page)
{
    for (int i = 0; i < EVB_PROFILE_COUNT; i++) {
        bool selected = menu.settings->profile_index == i;
        lv_obj_t *tile = add_plain_box(page, PROFILE_CENTER_X[i] - 60, 90, 120, 150);
        if (selected) {
            add_tinted_picture(tile, EVB_ASSET_AVATAR_LARGE, 7, 10, color::text_soft_white);
        } else {
            add_tinted_picture(tile, EVB_ASSET_AVATAR_SMALL, 14, 17, color::divider_cool);
        }
        lv_obj_t *name = add_text(tile, &evb_font_regular_16, selected ? color::text_primary : color::icon_muted, PROFILE_NAMES[i]);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 122);
        call_on_tap(tile, on_profile_tapped, (void *)(intptr_t)i);
    }
    add_centered_text(page, &evb_font_regular_13, color::text_muted, "Tap a rider to switch", 400, 262);
}

/* ---------- Digilocker ---------- */

const char *const DOCUMENT_TITLES[3] = {"Insurance", "Aadhar card", "Driving license"};
const int DOCUMENT_SLOT_Y[3] = {119, 166, 221};

void on_document_tapped(lv_event_t *event)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(event);
    if (slot == 0) {
        menu.sub_index = (menu.sub_index + 2) % 3;
    } else if (slot == 2) {
        menu.sub_index = (menu.sub_index + 1) % 3;
    }
    rebuild_page();
}

void build_digilocker_page(lv_obj_t *page)
{
    add_demo_tag(page);
    for (int i = 0; i < 3; i++) {
        int slot = (i - menu.sub_index + 4) % 3;
        bool centre = slot == 1;
        lv_obj_t *row = add_color_block(page, page_x(512), DOCUMENT_SLOT_Y[slot], 273, centre ? 45 : 36, color::neutral28, LV_OPA_COVER, 4);
        paint_vertical_gradient(row, color::neutral28, color::neutral1c);
        lv_obj_set_style_opa(row, centre ? LV_OPA_COVER : 89, 0);
        lv_obj_t *title = add_text(row, centre ? &evb_font_bold_italic_22 : &evb_font_bold_italic_20, lv_color_hex(0xB8BDBF), DOCUMENT_TITLES[i]);
        lv_obj_center(title);
        call_on_tap(row, on_document_tapped, (void *)(intptr_t)slot);
    }
}

/* ---------- Seat ---------- */

void on_seat_up_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.settings->seat_level = clamp_int(menu.settings->seat_level + 1, 0, EVB_MAX_SEAT_LEVEL);
    rebuild_page();
}

void on_seat_down_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.settings->seat_level = clamp_int(menu.settings->seat_level - 1, 0, EVB_MAX_SEAT_LEVEL);
    rebuild_page();
}

void on_seat_reset_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.settings->seat_level = 2;
    rebuild_page();
}

void build_seat_page(lv_obj_t *page)
{
    int level = menu.settings->seat_level;
    lv_color_t arrow_red = lv_color_hex(0xD4553A);
    add_picture(page, EVB_ASSET_SEAT, page_x(492), 146 - level * 6);
    add_tinted_picture(page, EVB_ASSET_ICON_ARROW_UP, page_x(776), 120, level < EVB_MAX_SEAT_LEVEL ? arrow_red : color::text_muted);
    add_tinted_picture(page, EVB_ASSET_ICON_ARROW_DOWN, page_x(776), 190, level > 0 ? arrow_red : color::text_muted);
    add_touch_area(page, page_x(776) - 12, 108, 44, 44, on_seat_up_tapped, NULL);
    add_touch_area(page, page_x(776) - 12, 178, 44, 44, on_seat_down_tapped, NULL);

    lv_obj_t *reset = add_glass_button(page, page_x(591), 232, 112, 32, "RESET", &evb_font_regular_16, level == 2, color::teal, PLAIN_GLASS);
    call_on_tap(reset, on_seat_reset_tapped, NULL);

    char text[16];
    snprintf(text, sizeof(text), "Level %d", level);
    add_text_at(page, &evb_font_regular_14, color::text_secondary, text, page_x(812), 158);
}

/* ---------- Charging ---------- */

void on_auto_turn_off_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.settings->auto_turn_off = !menu.settings->auto_turn_off;
    rebuild_page();
}

/* ToggleSwitch.qml: the knob sits left when on. */
lv_obj_t *add_toggle_switch(lv_obj_t *parent, int x, int y, bool checked)
{
    lv_obj_t *track = add_color_block(parent, x, y, 86, 38, color::neutral3d, LV_OPA_COVER, 19);
    paint_horizontal_gradient(track, checked ? lv_color_hex(0x4E9E6E) : color::neutral3d, checked ? lv_color_hex(0x9CF0B6) : color::neutral4e);
    lv_obj_t *knob = add_color_block(track, checked ? 3 : 86 - 32 - 3, 3, 32, 32, lv_color_hex(0x9A9FA2), LV_OPA_COVER, 16);
    paint_vertical_gradient(knob, lv_color_hex(0x9A9FA2), lv_color_hex(0x5C6164));
    return track;
}

lv_obj_t *add_ring(lv_obj_t *parent, int center_x, int center_y, int radius, int stroke, lv_color_t color, bool rounded)
{
    int size = (radius + stroke / 2) * 2;
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, size, size);
    lv_obj_set_pos(arc, center_x - size / 2, center_y - size / 2);
    lv_obj_set_clickable(arc, false);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, stroke, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, rounded, LV_PART_INDICATOR);
    return arc;
}

void show_ring_part(lv_obj_t *arc, float from_fraction, float to_fraction)
{
    lv_arc_set_angles(arc, (lv_value_precise_t)(360.0f * from_fraction), (lv_value_precise_t)(360.0f * to_fraction));
}

void refresh_charging_page(void)
{
    int soc = menu.vehicle->battery_percent;
    bool plugged = menu.vehicle->charger_plugged;
    float fraction = soc / 100.0f;
    fraction = fraction < 0.01f ? 0.01f : (fraction > 0.999f ? 0.999f : fraction);
    lv_label_set_text(live.charging_title, plugged ? "CHARGING GUN INSERTED" : "CHARGER NOT CONNECTED");
    lv_label_set_text_fmt(live.charging_soc, "%d", soc);
    lv_label_set_text_fmt(live.charging_minutes, "%d MINS", (int)((100 - soc) * 0.75f + 0.5f));
    show_ring_part(live.charging_arc, 0.0f, fraction);
    set_shown(live.charging_plug_mark, plugged);
}

void build_charging_page(lv_obj_t *page)
{
    constexpr int ring_center_x = 407;
    constexpr int ring_center_y = 193;
    constexpr int left_column_x = 180;
    constexpr int right_column_x = 620;

    live.charging_title = add_centered_text(page, &evb_font_regular_16, color::text_primary, "", 400, 74);
    add_centered_text(page, &evb_font_bold_17, color::text_primary, "AUTO TURNOFF", left_column_x, 150);
    lv_obj_t *toggle = add_toggle_switch(page, left_column_x - 43, 187, menu.settings->auto_turn_off);
    call_on_tap(toggle, on_auto_turn_off_tapped, NULL);

    lv_obj_t *track = add_ring(page, ring_center_x, ring_center_y, 80, 12, color::neutral1f, false);
    show_ring_part(track, 0.0f, 1.0f);
    live.charging_arc = add_ring(page, ring_center_x, ring_center_y, 80, 11, lv_color_hex(0xB85A3C), true);
    add_tinted_picture(page, EVB_ASSET_ICON_PLUG, page_x(633), 150, color::text_soft_white);
    live.charging_plug_mark = add_color_block(page, page_x(631), 148, 9, 3, color::green, LV_OPA_COVER, 1);

    lv_obj_t *soc_row = add_plain_box(page, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(soc_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(soc_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    place_by_top_center(soc_row, 400, 193);
    live.charging_soc = add_text(soc_row, &evb_font_bold_30, lv_color_hex(0xC0603F), "0");
    lv_obj_t *percent = add_text(soc_row, &evb_font_bold_24, lv_color_hex(0xC0603F), "%");
    lv_obj_set_style_translate_y(percent, evb_font_bold_24.base_line - evb_font_bold_30.base_line, 0);

    live.charging_minutes = add_centered_text(page, &evb_font_bold_20, color::text_primary, "", right_column_x, 150);
    add_centered_text(page, &evb_font_regular_19, color::text_secondary, "left to full charge", right_column_x, 186);
    refresh_charging_page();
}

/* ---------- Bike status ---------- */

constexpr int PETROL_RUPEES_PER_KM = 3;
constexpr int CO2_GRAMS_PER_KM = 45;

void on_bike_status_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.sub_level = menu.sub_level == 0 ? 1 : 0;
    rebuild_page();
}

void add_stat_card(lv_obj_t *parent, int x, int y, evb_asset_id_t icon, const char *title, const char *value)
{
    constexpr int width = 205;
    constexpr int height = 70;
    lv_obj_t *glow = add_tinted_picture(parent, EVB_ASSET_GLOW_BLOB_260, x + width / 2 - 130, y + height / 2 - 60, lv_color_hex(0x3E7A55));
    lv_obj_set_style_image_opa(glow, 89, 0);
    lv_obj_t *card = add_color_block(parent, x, y, width, height, lv_color_hex(0x2B3A31), LV_OPA_COVER, 6);
    paint_vertical_gradient(card, lv_color_hex(0x2B3A31), lv_color_hex(0x344E3E));
    add_tinted_picture(card, icon, 12, 17, color::text_primary);
    lv_obj_t *text = add_text(card, &evb_font_regular_16, color::text_primary, "");
    lv_label_set_text_fmt(text, "%s\n%s", title, value);
    lv_obj_set_width(text, width - 64);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(text, 56, 12);
}

void add_trip_stat(lv_obj_t *parent, int center_x, int top, const char *title, const char *value)
{
    lv_obj_t *text = add_text(parent, &evb_font_regular_18, color::text_primary, "");
    lv_label_set_text_fmt(text, "%s\n%s", title, value);
    lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    place_by_top_center(text, center_x, top);
}

void build_bike_status_summary(lv_obj_t *page)
{
    int odometer_km = menu.vehicle->odometer_km;
    char travelled[24];
    char fuel[24];
    char carbon[32];
    char co2[24];
    snprintf(travelled, sizeof(travelled), "%d km", odometer_km);
    snprintf(fuel, sizeof(fuel), "%d Rs", odometer_km * PETROL_RUPEES_PER_KM);
    snprintf(carbon, sizeof(carbon), "%d trees planted", odometer_km / 4000 > 1 ? odometer_km / 4000 : 1);
    snprintf(co2, sizeof(co2), "%d Kg", (odometer_km * CO2_GRAMS_PER_KM + 50000) / 100000);

    add_centered_text(page, &evb_font_regular_18, color::text_primary, "SUMMARY", 400, 72);
    add_stat_card(page, page_x(423), 118, EVB_ASSET_ICON_ROUTE_LOOP, "Travelled:", travelled);
    add_stat_card(page, page_x(666), 118, EVB_ASSET_ICON_FUEL_CAN, "Fuel savings:", fuel);
    add_stat_card(page, page_x(423), 206, EVB_ASSET_ICON_SPROUT, "Carbon savings:", carbon);
    add_stat_card(page, page_x(666), 206, EVB_ASSET_ICON_CLOUD, "CO2 saved:", co2);
}

void build_last_ride(lv_obj_t *page)
{
    constexpr int eco_share = 34;
    constexpr int normal_share = 33;
    constexpr int sport_share = 33;
    constexpr int ring_center_x = 396;
    constexpr int ring_center_y = 208;

    add_color_block(page, 165, 68, 470, 280, color::neutral18, 242, 4);
    add_centered_text(page, &evb_font_bold_18, color::text_primary, "Last ride  ›", 400, 82);

    lv_obj_t *track = add_ring(page, ring_center_x, ring_center_y, 85, 20, lv_color_hex(0x12402F), false);
    show_ring_part(track, 0.0f, 1.0f);
    float eco_end = eco_share / 100.0f;
    float normal_end = eco_end + normal_share / 100.0f;
    float sport_end = normal_end + sport_share / 100.0f;
    show_ring_part(add_ring(page, ring_center_x, ring_center_y, 85, 18, lv_color_hex(0x2FE07F), false), 0.0f, eco_end);
    show_ring_part(add_ring(page, ring_center_x, ring_center_y, 85, 18, lv_color_hex(0xD9B561), false), eco_end, normal_end);
    show_ring_part(add_ring(page, ring_center_x, ring_center_y, 85, 18, lv_color_hex(0xD34A3E), false), normal_end, sport_end);

    lv_obj_t *shares = add_text(page, &evb_font_regular_18, color::text_primary, "");
    lv_label_set_text_fmt(shares, "%d%% Eco\n%d%% Normal\n%d%% Sport", eco_share, normal_share, sport_share);
    lv_obj_set_style_text_align(shares, LV_TEXT_ALIGN_CENTER, 0);
    place_by_center(shares, ring_center_x, ring_center_y);

    char distance[24];
    snprintf(distance, sizeof(distance), "%d km", menu.vehicle->trip_km);
    add_trip_stat(page, 232, 128, "Ride time:", "--");
    add_trip_stat(page, 568, 128, "Distance:", distance);
    add_trip_stat(page, 232, 254, "Fuel savings:", "--");
    add_trip_stat(page, 568, 254, "SOC consumed:", "--");
}

void build_bike_status_page(lv_obj_t *page)
{
    if (menu.sub_level == 1) {
        build_last_ride(page);
    } else {
        build_bike_status_summary(page);
    }
    call_on_tap(page, on_bike_status_tapped, NULL);
}

/* ---------- Security ---------- */

const GlassLook ALARM_GLASS = {lv_color_hex(0xA8342E), lv_color_hex(0x6A1A18)};

void on_theft_captures_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.sub_index = 0;
    menu.settings->theft_captures = 0;
    rebuild_page();
}

void on_anti_theft_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.sub_index = 1;
    menu.settings->anti_theft_armed = !menu.settings->anti_theft_armed;
    rebuild_page();
}

void build_security_page(lv_obj_t *page)
{
    lv_obj_t *shield = add_tinted_picture(page, EVB_ASSET_SHIELD, page_x(550), 66, color::neutral4e);
    lv_obj_set_style_image_opa(shield, 204, 0);

    lv_obj_t *captures = add_glass_button(page, page_x(515), 141, 260, 37, "Anti-Theft Captures", &evb_font_regular_22,
                                          menu.sub_index == 0, color::red, ALARM_GLASS);
    call_on_tap(captures, on_theft_captures_tapped, NULL);
    if (menu.settings->theft_captures > 0) {
        lv_obj_t *badge = add_color_block(page, page_x(757), 130, 22, 22, lv_color_hex(0xE9E3E3), LV_OPA_COVER, LV_RADIUS_CIRCLE);
        lv_obj_t *count = add_text(badge, &evb_font_bold_13, lv_color_hex(0x8E1C22), "");
        lv_label_set_text_fmt(count, "%d", menu.settings->theft_captures);
        lv_obj_center(count);
    }

    lv_obj_t *arm = add_glass_button(page, page_x(515), 199, 260, 37, menu.settings->anti_theft_armed ? "Activated ✓" : "Activation",
                                     &evb_font_regular_22, menu.sub_index == 1, color::teal, PLAIN_GLASS);
    call_on_tap(arm, on_anti_theft_tapped, NULL);
}

/* ---------- Payment ---------- */

void on_confirm_payment_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    menu.settings->payment_done = true;
    rebuild_page();
}

void build_payment_page(lv_obj_t *page)
{
    add_demo_tag(page);
    add_picture(page, EVB_ASSET_PAYMENT_CARD, page_x(565), 72);
    add_text_at(page, &evb_font_regular_15, color::text_primary, "Payment To :", page_x(520), 180);
    add_text_at(page, &evb_font_regular_15, color::text_primary, "Toll Naka Office", page_x(616), 180);
    add_color_block(page, page_x(520), 204, 250, 1, color::stroke2, LV_OPA_COVER, 0);
    bool paid = menu.settings->payment_done;
    lv_obj_t *confirm = add_glass_button(page, page_x(580), 222, 124, 30, paid ? "PAID ✓" : "CONFIRM", &evb_font_regular_14,
                                         true, paid ? color::green : color::teal, PLAIN_GLASS);
    call_on_tap(confirm, on_confirm_payment_tapped, NULL);
}

/* ---------- Customize ---------- */

constexpr int THEME_ROW_COUNT = 6;

void on_customize_tab_tapped(lv_event_t *event)
{
    menu.sub_index = (int)(intptr_t)lv_event_get_user_data(event);
    menu.sub_level = 0;
    rebuild_page();
}

void change_theme_row(int row)
{
    evb_cluster_settings_t *settings = menu.settings;
    switch (row) {
    case 0:
        settings->night_mode = !settings->night_mode;
        break;
    case 1:
        settings->brightness_percent = settings->brightness_percent >= 100 ? 20 : settings->brightness_percent + 20;
        break;
    case 2:
        settings->use_24_hour = !settings->use_24_hour;
        break;
    case 3:
        settings->use_miles = !settings->use_miles;
        break;
    case 4:
        settings->speedo_style = settings->speedo_style == EVB_SPEEDO_HEX ? EVB_SPEEDO_CLASSIC : EVB_SPEEDO_HEX;
        break;
    default:
        settings->demo_running = !settings->demo_running;
        break;
    }
}

void on_theme_row_tapped(lv_event_t *event)
{
    int row = (int)(intptr_t)lv_event_get_user_data(event);
    menu.sub_level = row + 1;
    change_theme_row(row);
    rebuild_page();
}

/* MenuRow.qml at 30 px high. */
void add_menu_row(lv_obj_t *parent, int x, int y, int width, evb_asset_id_t icon, const char *title, const char *value, bool selected, int row)
{
    constexpr int height = 30;
    lv_obj_t *box = add_plain_box(parent, x, y, width, height);
    if (selected) {
        lv_obj_set_style_bg_color(box, lv_color_hex(0x1D4A3E), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, 8, 0);
        add_color_block(box, 0, 8, 3, height - 16, color::teal, LV_OPA_COVER, 2);
    }
    lv_obj_t *picture = add_tinted_picture(box, icon, 16, (height - 24) / 2, selected ? color::teal : color::text_secondary);
    lv_obj_t *name = add_text(box, selected ? &evb_font_bold_18 : &evb_font_regular_18, color::text_primary, title);
    lv_obj_align_to(name, picture, LV_ALIGN_OUT_RIGHT_MID, 12, 0);
    lv_obj_t *shown_value = add_text(box, &evb_font_regular_18, selected ? color::teal : color::text_secondary, value);
    lv_obj_align(shown_value, LV_ALIGN_RIGHT_MID, -16, 0);
    call_on_tap(box, on_theme_row_tapped, (void *)(intptr_t)row);
}

void build_theme_rows(lv_obj_t *page)
{
    const evb_cluster_settings_t *settings = menu.settings;
    char brightness[8];
    snprintf(brightness, sizeof(brightness), "%d%%", settings->brightness_percent);
    struct Row {
        evb_asset_id_t icon;
        const char *title;
        const char *value;
    };
    const Row rows[THEME_ROW_COUNT] = {
        {EVB_ASSET_ICON_SUN, "Theme", settings->night_mode ? "Night" : "Day"},
        {EVB_ASSET_ICON_SUN, "Brightness", brightness},
        {EVB_ASSET_ICON_CLOCK, "Clock format", settings->use_24_hour ? "24 h" : "12 h"},
        {EVB_ASSET_ICON_COMPASS_24, "Units", settings->use_miles ? "Miles" : "Kilometres"},
        {EVB_ASSET_ICON_COMPASS_24, "Speedometer style", settings->speedo_style == EVB_SPEEDO_HEX ? "Hexagon" : "Classic"},
        {EVB_ASSET_ICON_INFO, "Demo mode", settings->demo_running ? "On" : "Off"},
    };
    for (int i = 0; i < THEME_ROW_COUNT; i++) {
        add_menu_row(page, 180, 118 + i * 30, 440, rows[i].icon, rows[i].title, rows[i].value, menu.sub_level - 1 == i, i);
    }
}

void build_customize_page(lv_obj_t *page)
{
    static const char *const tabs[3] = {"help", "Shortcut keys", "theme"};
    add_tab_strip(page, 80, tabs, menu.sub_index, on_customize_tab_tapped);
    if (menu.sub_index == 0) {
        add_wrapped_text(page, &evb_font_regular_17, color::text_secondary,
                         "Swipe or tap the side tabs to move between pages\nTap a value to change it\nTap the settings icon to close the menu",
                         400, 132, 420);
    } else if (menu.sub_index == 1) {
        add_picture(page, EVB_ASSET_SHORTCUT_KEYS, 400 - picture_width(EVB_ASSET_SHORTCUT_KEYS) / 2, 150);
    } else {
        build_theme_rows(page);
    }
}

/* ---------- Misc ---------- */

void on_misc_tab_tapped(lv_event_t *event)
{
    menu.sub_index = (int)(intptr_t)lv_event_get_user_data(event);
    menu.sub_level = 0;
    rebuild_page();
}

void on_list_row_tapped(lv_event_t *event)
{
    int row = (int)(intptr_t)lv_event_get_user_data(event);
    menu.sub_level = menu.sub_level == row + 1 ? 0 : row + 1;
    rebuild_page();
}

void on_call_contact_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    if (menu.sub_level > 0) {
        evb_bluetooth_call_contact(menu.sub_level - 1);
    }
}

void on_media_command_tapped(lv_event_t *event)
{
    evb_bluetooth_send_media_command((MediaCommand)(intptr_t)lv_event_get_user_data(event));
}

/* MessageRow.qml */
void add_message_row(lv_obj_t *parent, int x, int y, const TextRow &entry, lv_color_t avatar_color, bool hide_text_while_moving,
                     bool selected, int row)
{
    constexpr int width = 280;
    constexpr int height = 42;
    lv_obj_t *outline = add_color_block(parent, x, y, width, height, selected ? color::teal : lv_color_hex(0x6E7375), LV_OPA_COVER, height / 2);
    lv_obj_t *inside = add_color_block(outline, 1, 1, width - 2, height - 2, selected ? lv_color_hex(0x16221F) : lv_color_hex(0x0E1011),
                                       LV_OPA_COVER, (height - 2) / 2);
    lv_obj_t *avatar = add_color_block(inside, 7, 4, 32, 32, avatar_color, LV_OPA_COVER, 16);
    char initial[2] = {entry.title[0], '\0'};
    lv_obj_center(add_text(avatar, &evb_font_bold_14, color::text_primary, initial));

    bool hide_text = hide_text_while_moving && menu.vehicle->speed_kmh > 0;
    lv_obj_t *name = add_text_at(inside, &evb_font_regular_15, color::text_primary, entry.title, 49, 4);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(name, 220);
    lv_obj_t *body = add_text_at(inside, &evb_font_regular_12, selected ? color::text_primary : color::text_secondary,
                                 hide_text ? "Stop to read" : entry.text, 49, 22);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(body, 220);
    call_on_tap(outline, on_list_row_tapped, (void *)(intptr_t)row);
}

lv_obj_t *add_small_button(lv_obj_t *parent, int x, int y, int width, const char *text, lv_event_cb_t on_tapped, void *context)
{
    lv_obj_t *button = add_color_block(parent, x, y, width, 30, color::surface_selected, LV_OPA_COVER, 4);
    lv_obj_center(add_text(button, &evb_font_regular_14, color::text_primary, text));
    call_on_tap(button, on_tapped, context);
    return button;
}

/* A round media button; the touch area is bigger than the icon. */
void add_media_button(lv_obj_t *parent, int center_x, int center_y, evb_asset_id_t icon, const char *text, MediaCommand command,
                      bool enabled, lv_color_t icon_color)
{
    lv_obj_t *button = add_color_block(parent, center_x - 17, center_y - 17, 34, 34, color::neutral22, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    lv_color_t shown = enabled ? icon_color : color::text_muted;
    if (text != NULL) {
        lv_obj_center(add_text(button, &evb_font_regular_20, shown, text));
    } else {
        lv_obj_t *picture = add_tinted_picture(button, icon, 0, 0, shown);
        lv_obj_center(picture);
    }
    if (enabled) {
        call_on_tap(button, on_media_command_tapped, (void *)(intptr_t)command);
    }
}

int media_position_now(const PhoneState *phone, uint32_t now_ms)
{
    int position = phone->media_position_s;
    if (phone->media_playing) {
        position += (int)((now_ms - phone->media_position_at_ms) / 1000);
    }
    if (phone->media_duration_s > 0 && position > phone->media_duration_s) {
        position = phone->media_duration_s;
    }
    return position;
}

void refresh_music_progress(void)
{
    const PhoneState *phone = menu.phone;
    int duration = phone->media_duration_s;
    int position = media_position_now(phone, menu.now_ms);
    int x = page_x(560) + (duration > 0 ? 170 * position / duration : 0) - 3;
    lv_obj_set_x(live.music_knob, x);
    lv_label_set_text_fmt(live.music_time, "%d:%02d", position / 60, position % 60);
}

/* The design's music block starts at y 118, under a tab strip that ends at y 128; it sits 12 px lower here. */
constexpr int MUSIC_TOP = 130;

void build_music_tab(lv_obj_t *page)
{
    const PhoneState *phone = menu.phone;
    if (!phone->phone_connected) {
        add_wrapped_text(page, &evb_font_regular_14, color::text_muted,
                         "Pair your phone in its Bluetooth settings to control music", 400, 170, 360);
        return;
    }
    bool has_track = phone->media_info_available && phone->media_title[0] != '\0';
    bool controls = phone->media_controls_available;
    add_centered_text(page, &evb_font_italic_11, color::text_secondary, "NOW PLAYING", 400, MUSIC_TOP);
    add_picture(page, EVB_ASSET_ALBUM_ART, 405 - 38, MUSIC_TOP + 16);
    lv_obj_t *title = add_centered_text(page, &evb_font_regular_15, color::text_primary,
                                        has_track ? phone->media_title : (phone->media_info_available ? "Nothing playing" : "Your phone's music"),
                                        400, MUSIC_TOP + 96);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 300);
    add_centered_text(page, &evb_font_regular_10, color::text_secondary, has_track ? phone->media_artist : "", 400, MUSIC_TOP + 114);

    int controls_y = MUSIC_TOP + 139;
    add_media_button(page, 290, controls_y, EVB_ASSET_ICON_PREV, "-", MediaCommand::VolumeDown, controls, color::divider_cool);
    add_media_button(page, page_x(580) + 9, controls_y, EVB_ASSET_ICON_PREV, NULL, MediaCommand::Previous, controls, color::divider_cool);
    add_media_button(page, page_x(636) + 9, controls_y, phone->media_playing ? EVB_ASSET_ICON_PAUSE : EVB_ASSET_ICON_PLAY, NULL,
                     MediaCommand::PlayPause, controls, color::text_primary);
    add_media_button(page, page_x(692) + 9, controls_y, EVB_ASSET_ICON_NEXT, NULL, MediaCommand::Next, controls, color::divider_cool);
    add_media_button(page, 510, controls_y, EVB_ASSET_ICON_NEXT, "+", MediaCommand::VolumeUp, controls, color::divider_cool);

    if (phone->media_info_available) {
        lv_obj_t *volume = add_text_at(page, &evb_font_regular_11, color::text_muted, "", 540, controls_y - 7);
        lv_label_set_text_fmt(volume, "Vol %d%%", phone->media_volume_percent);
    }
    if (phone->media_duration_s > 0) {
        add_color_block(page, page_x(560), MUSIC_TOP + 162, 170, 1, color::stroke2, LV_OPA_COVER, 0);
        live.music_knob = add_color_block(page, page_x(560), MUSIC_TOP + 159, 7, 7, color::text_primary, LV_OPA_COVER, LV_RADIUS_CIRCLE);
        live.music_time = add_text_at(page, &evb_font_regular_10, color::text_secondary, "", page_x(560) - 34, MUSIC_TOP + 156);
        lv_obj_t *length = add_text_at(page, &evb_font_regular_10, color::text_secondary, "", page_x(560) + 176, MUSIC_TOP + 156);
        lv_label_set_text_fmt(length, "%d:%02d", phone->media_duration_s / 60, phone->media_duration_s % 60);
        refresh_music_progress();
    }
}

void build_phone_list(lv_obj_t *page, bool messages)
{
    const PhoneState *phone = menu.phone;
    const TextRow *rows = messages ? phone->contacts : phone->reminders;
    int count = 0;
    for (int i = 0; i < EVB_LIST_SLOTS; i++) {
        if (rows[i].title[0] != '\0') {
            count = i + 1;
        }
    }
    if (count == 0) {
        const char *empty = messages ? "No messages yet" : "No reminders yet";
        if (!phone->phone_connected) {
            empty = "Pair your phone in its Bluetooth settings";
        } else if (!messages && phone->link_kind != PhoneLinkKind::EvbikesApp) {
            empty = "Reminders come from the EVBikes app";
        }
        add_centered_text(page, &evb_font_regular_14, color::text_muted, empty, 400, 170);
        return;
    }
    lv_color_t avatar_color = messages ? color::success_muted : lv_color_hex(0x8A5A2F);
    for (int i = 0; i < count; i++) {
        add_message_row(page, page_x(505), 133 + i * 48, rows[i], avatar_color, messages, menu.sub_level - 1 == i, i);
    }
    bool row_selected = menu.sub_level > 0 && menu.sub_level <= count;
    if (messages && row_selected && phone->can_dial) {
        char text[EVB_TEXT_MAX + 8];
        snprintf(text, sizeof(text), "Call %s", rows[menu.sub_level - 1].title);
        lv_obj_t *call = add_color_block(page, 0, 274, 200, 26, lv_color_hex(0x143A2A), LV_OPA_COVER, 13);
        place_by_top_center(call, 400, 275);
        lv_obj_t *label = add_text(call, &evb_font_bold_13, color::green, text);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(label, 180);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(label);
        call_on_tap(call, on_call_contact_tapped, NULL);
        return;
    }
    const char *hint = "Tap a row to select it";
    if (messages && row_selected) {
        hint = "Reply on your phone";
    }
    add_centered_text(page, &evb_font_regular_11, color::text_muted, hint, 400, 280);
}

void build_misc_page(lv_obj_t *page)
{
    static const char *const tabs[3] = {"message", "music", "reminder"};
    add_tab_strip(page, 92, tabs, menu.sub_index, on_misc_tab_tapped);
    if (menu.sub_index == 1) {
        build_music_tab(page);
    } else {
        build_phone_list(page, menu.sub_index == 0);
    }
}

/* ---------- Connections ---------- */

void move_menu_to(int page_index);

void on_forget_phones_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_bluetooth_forget_phones();
}

void on_wifi_settings_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    move_menu_to(PAGE_WIFI);
}

const char *bluetooth_summary(const PhoneState *phone)
{
    if (!phone->bluetooth_ready) {
        return "Bluetooth is starting...";
    }
    if (!phone->phone_connected) {
        return "No phone. On the phone open Bluetooth settings and pair \"EVBikes Cluster\".";
    }
    switch (phone->link_kind) {
    case PhoneLinkKind::Iphone:
        return "iPhone connected: calls, messages, music and clock.";
    case PhoneLinkKind::EvbikesApp:
        return "EVBikes app connected: navigation, calls, contacts and music.";
    default:
        return phone->phone_bonded ? "Phone connected: music keys. Install the EVBikes app for navigation and calls."
                                   : "Phone connected. Accept the pairing request on the phone.";
    }
}

const char *wifi_summary(const PhoneState *phone, char *text, size_t capacity)
{
    switch (phone->wifi_status) {
    case WifiStatus::Connected:
        snprintf(text, capacity, "Wi-Fi: %s (%s)", phone->wifi_ssid, phone->wifi_ip);
        break;
    case WifiStatus::Connecting:
        snprintf(text, capacity, "Wi-Fi: joining %s...", phone->wifi_ssid);
        break;
    case WifiStatus::Failed:
        snprintf(text, capacity, "Wi-Fi: could not join %s", phone->wifi_ssid);
        break;
    default:
        snprintf(text, capacity, "Wi-Fi: no network saved");
        break;
    }
    return text;
}

void build_connectivity_page(lv_obj_t *page)
{
    constexpr int left = 155;
    const PhoneState *phone = menu.phone;
    add_text_at(page, &evb_font_regular_22, color::text_primary, "Connections", left, 72);
    lv_obj_t *bluetooth = add_text_at(page, &evb_font_regular_16, color::text_secondary, bluetooth_summary(phone), left, 106);
    lv_label_set_long_mode(bluetooth, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(bluetooth, 480);
    if (phone->phone_connected && phone->phone_battery_percent > 0) {
        lv_obj_t *battery = add_text_at(page, &evb_font_regular_14, color::text_muted, "", left, 150);
        lv_label_set_text_fmt(battery, "Phone battery %d%%", phone->phone_battery_percent);
    }
    add_small_button(page, left, 172, 200, "Forget paired phones", on_forget_phones_tapped, NULL);

    char wifi[80];
    add_text_at(page, &evb_font_regular_16, color::text_secondary, wifi_summary(phone, wifi, sizeof(wifi)), left, 216);
    add_small_button(page, left + 330, 212, 150, "Wi-Fi networks", on_wifi_settings_tapped, NULL);

    lv_obj_t *footer = add_text_at(page, &evb_font_regular_12, color::text_muted,
                                   "Call and music audio stay on the phone or helmet headset: this board has Bluetooth Low Energy only.",
                                   left, 256);
    lv_label_set_long_mode(footer, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(footer, 480);
}

/* ---------- Wi-Fi ---------- */

constexpr int WIFI_ROWS_SHOWN = 5;

void on_wifi_scan_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_wifi_scan();
}

void on_wifi_forget_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_wifi_forget();
}

void open_password_keyboard(const char *ssid);

void on_wifi_network_tapped(lv_event_t *event)
{
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    const WifiNetwork &network = menu.phone->wifi_networks[index];
    if (network.secured) {
        open_password_keyboard(network.ssid);
    } else {
        evb_wifi_connect(network.ssid, "");
    }
}

/* Four signal bars from the RSSI, like a phone's status bar. */
void add_signal_bars(lv_obj_t *parent, int x, int y, int rssi)
{
    int bars = rssi >= -55 ? 4 : (rssi >= -65 ? 3 : (rssi >= -75 ? 2 : 1));
    for (int i = 0; i < 4; i++) {
        int height = 4 + i * 3;
        add_color_block(parent, x + i * 5, y + 13 - height, 3, height, i < bars ? color::text_primary : color::stroke, LV_OPA_COVER, 1);
    }
}

void build_wifi_page(lv_obj_t *page)
{
    constexpr int left = 155;
    constexpr int width = 480;
    const PhoneState *phone = menu.phone;
    char status[80];
    add_text_at(page, &evb_font_regular_16, color::text_secondary, wifi_summary(phone, status, sizeof(status)), left, 72);
    add_small_button(page, left, 98, 120, phone->wifi_scanning ? "Scanning..." : "Scan", on_wifi_scan_tapped, NULL);
    if (phone->wifi_status != WifiStatus::NoNetworkSaved) {
        add_small_button(page, left + width - 120, 98, 120, "Forget", on_wifi_forget_tapped, NULL);
    }

    int shown = phone->wifi_network_count < WIFI_ROWS_SHOWN ? phone->wifi_network_count : WIFI_ROWS_SHOWN;
    if (shown == 0) {
        add_centered_text(page, &evb_font_regular_14, color::text_muted,
                          phone->wifi_scanning ? "Looking for networks..." : "Tap Scan to find networks", 400, 190);
        return;
    }
    for (int i = 0; i < shown; i++) {
        const WifiNetwork &network = phone->wifi_networks[i];
        bool current = phone->wifi_status == WifiStatus::Connected && strcmp(network.ssid, phone->wifi_ssid) == 0;
        lv_obj_t *row = add_plain_box(page, left, 136 + i * 32, width, 30);
        if (current) {
            lv_obj_set_style_bg_color(row, lv_color_hex(0x1D4A3E), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(row, 8, 0);
        }
        add_signal_bars(row, 12, 8, network.rssi);
        lv_obj_t *name = add_text_at(row, &evb_font_regular_16, color::text_primary, network.ssid, 46, 5);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, 300);
        lv_obj_t *kind = add_text(row, &evb_font_regular_13, current ? color::teal : color::text_muted,
                                  current ? "connected" : (network.secured ? "secured" : "open"));
        lv_obj_align(kind, LV_ALIGN_RIGHT_MID, -12, 0);
        call_on_tap(row, on_wifi_network_tapped, (void *)(intptr_t)i);
    }
}

/* ---------- System: clock, time zone and firmware updates ---------- */

void on_time_zone_tapped(lv_event_t *event)
{
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    evb_clock_set_utc_offset(evb_clock_utc_offset_minutes() + step * 30);
    rebuild_page();
}

void on_check_update_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_wifi_check_for_update();
}

void on_install_update_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_wifi_install_update();
}

void build_system_page(lv_obj_t *page)
{
    constexpr int left = 155;
    const PhoneState *phone = menu.phone;

    lv_obj_t *firmware = add_text_at(page, &evb_font_regular_18, color::text_primary, "", left, 70);
    lv_label_set_text_fmt(firmware, "Firmware %s", EVB_FIRMWARE_VERSION);

    int hours = 0;
    int minutes = 0;
    lv_obj_t *clock = add_text_at(page, &evb_font_regular_15, color::text_secondary, "", left, 100);
    if (evb_clock_read_local(&hours, &minutes)) {
        lv_label_set_text_fmt(clock, "Clock %02d:%02d, set by %s", hours, minutes, phone->clock_source);
    } else {
        lv_label_set_text(clock, "Clock not set yet: join Wi-Fi or connect a phone");
    }

    int offset = evb_clock_utc_offset_minutes();
    int magnitude = offset < 0 ? -offset : offset;
    lv_obj_t *zone = add_text_at(page, &evb_font_regular_15, color::text_secondary, "", left, 130);
    lv_label_set_text_fmt(zone, "Time zone UTC%c%02d:%02d", offset < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
    add_small_button(page, left + 330, 124, 70, "- 30m", on_time_zone_tapped, (void *)(intptr_t)-1);
    add_small_button(page, left + 410, 124, 70, "+ 30m", on_time_zone_tapped, (void *)(intptr_t)1);

    add_color_block(page, left, 164, 480, 1, color::neutral3e, LV_OPA_COVER, 0);
    lv_obj_t *message = add_text_at(page, &evb_font_regular_15, color::text_primary, phone->update_message, left, 174);
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(message, 320);
    if (phone->update_status == UpdateStatus::Downloading || phone->update_status == UpdateStatus::Done) {
        lv_obj_t *track = add_color_block(page, left, 202, 300, 6, color::stroke, LV_OPA_COVER, 3);
        add_color_block(track, 0, 0, 300 * phone->update_percent / 100, 6, color::teal, LV_OPA_COVER, 3);
        lv_obj_t *percent = add_text_at(page, &evb_font_regular_13, color::text_secondary, "", left + 310, 197);
        lv_label_set_text_fmt(percent, "%d%%", phone->update_percent);
    } else if (phone->update_status == UpdateStatus::Available) {
        char text[40];
        snprintf(text, sizeof(text), "Install %s", phone->update_version);
        add_small_button(page, left + 330, 170, 150, text, on_install_update_tapped, NULL);
    } else {
        add_small_button(page, left + 330, 170, 150, "Check for updates", on_check_update_tapped, NULL);
    }

    lv_obj_t *upload = add_text_at(page, &evb_font_regular_12, color::text_muted, "", left, 226);
    lv_label_set_long_mode(upload, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(upload, 480);
    if (phone->network_upload_ready) {
        lv_label_set_text_fmt(upload, "Arduino IDE upload over Wi-Fi: Tools > Port > %s (%s), password \"%s\".", EVB_NETWORK_UPLOAD_HOSTNAME,
                              phone->wifi_ip, EVB_NETWORK_UPLOAD_PASSWORD);
    } else {
        lv_label_set_text(upload, "Upload over Wi-Fi starts once Wi-Fi is joined. It needs an OTA partition scheme, "
                                  "for example \"8M with spiffs (3MB APP/1.5MB SPIFFS)\".");
    }
    if (phone->weather_valid) {
        lv_obj_t *weather = add_text_at(page, &evb_font_regular_12, color::text_muted, "", left, 270);
        lv_label_set_text_fmt(weather, "Outside temperature from open-meteo.com for %s: %d C", phone->weather_place, phone->weather_temp_c);
    }
}

/* ---------- password keyboard ---------- */

void close_password_keyboard(void)
{
    set_shown(menu.keyboard_layer, false);
}

void on_password_keyboard_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) {
        evb_wifi_connect(menu.password_ssid, lv_textarea_get_text(menu.password_area));
        close_password_keyboard();
    } else if (code == LV_EVENT_CANCEL) {
        close_password_keyboard();
    }
}

void on_cancel_password_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    close_password_keyboard();
}

void on_show_password_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_textarea_set_password_mode(menu.password_area, !lv_textarea_get_password_mode(menu.password_area));
}

void build_password_keyboard(lv_obj_t *parent)
{
    menu.keyboard_layer = add_full_screen_layer(parent);
    lv_obj_t *layer = menu.keyboard_layer;
    lv_obj_set_style_bg_color(layer, color::black, 0);
    lv_obj_set_style_bg_opa(layer, 242, 0);
    lv_obj_set_clickable(layer, true);

    menu.password_title = add_text(layer, &evb_font_regular_18, color::text_primary, "");
    place_by_top_center(menu.password_title, 400, 74);

    menu.password_area = lv_textarea_create(layer);
    lv_textarea_set_one_line(menu.password_area, true);
    lv_textarea_set_password_mode(menu.password_area, true);
    lv_textarea_set_max_length(menu.password_area, 64);
    lv_textarea_set_placeholder_text(menu.password_area, "Password");
    lv_obj_set_size(menu.password_area, 400, 44);
    place_by_top_center(menu.password_area, 380, 104);
    lv_obj_set_style_text_font(menu.password_area, &evb_font_regular_20, 0);
    lv_obj_set_style_text_color(menu.password_area, color::text_primary, 0);
    lv_obj_set_style_bg_color(menu.password_area, color::surface, 0);
    lv_obj_set_style_border_color(menu.password_area, color::teal, 0);

    add_small_button(layer, 595, 111, 70, "Show", on_show_password_tapped, NULL);
    add_small_button(layer, 675, 111, 80, "Cancel", on_cancel_password_tapped, NULL);

    lv_obj_t *keyboard = lv_keyboard_create(layer);
    lv_obj_set_size(keyboard, 800, 240);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(keyboard, menu.password_area);
    lv_obj_set_style_bg_color(keyboard, color::neutral18, 0);
    lv_obj_set_style_bg_color(keyboard, color::surface_selected, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, color::text_primary, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, color::neutral3a, (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED);
    lv_obj_set_style_text_color(keyboard, color::text_primary, (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(keyboard, color::success_muted, (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_add_event_cb(keyboard, on_password_keyboard_event, LV_EVENT_ALL, NULL);

    set_shown(layer, false);
}

void open_password_keyboard(const char *ssid)
{
    evb_copy_text(menu.password_ssid, sizeof(menu.password_ssid), ssid);
    lv_label_set_text_fmt(menu.password_title, "Password for %s", ssid);
    lv_textarea_set_text(menu.password_area, "");
    lv_textarea_set_password_mode(menu.password_area, true);
    set_shown(menu.keyboard_layer, true);
}

/* ---------- Vitals ---------- */

void refresh_vitals_page(void)
{
    lv_label_set_text_fmt(live.vitals_power, "%d %%", menu.vehicle->power_percent);
    lv_label_set_text_fmt(live.vitals_range, menu.settings->use_miles ? "%d mi" : "%d km",
                          menu.settings->use_miles ? (int)(menu.vehicle->range_km * 0.621371f + 0.5f) : menu.vehicle->range_km);
    lv_label_set_text_fmt(live.vitals_pack_temp, "%d °C", menu.vehicle->pack_temp_c);
}

lv_obj_t *add_vital(lv_obj_t *page, int x, int top, const char *title, const char *value)
{
    add_text_at(page, &evb_font_regular_16, color::text_secondary, title, x, top);
    return add_text_at(page, &evb_font_regular_22, color::text_primary, value, x, top + 22);
}

void build_vitals_page(lv_obj_t *page)
{
    constexpr int left = 155;
    constexpr int right = 495;
    add_text_at(page, &evb_font_regular_16, color::text_muted, "TYRE", left, 86);
    add_text_at(page, &evb_font_regular_16, color::text_muted, "BATTERY", right, 86);
    add_color_block(page, left, 108, 480, 1, color::neutral3e, LV_OPA_COVER, 0);

    char front[24];
    char rear[24];
    snprintf(front, sizeof(front), "%d.%d psi", menu.vehicle->tyre_front_psi_x10 / 10, menu.vehicle->tyre_front_psi_x10 % 10);
    snprintf(rear, sizeof(rear), "%d.%d psi", menu.vehicle->tyre_rear_psi_x10 / 10, menu.vehicle->tyre_rear_psi_x10 % 10);
    add_vital(page, left, 120, "Front", front);
    add_vital(page, left, 180, "Rear", rear);
    live.vitals_power = add_vital(page, left, 240, "Power", "");
    live.vitals_range = add_vital(page, right, 120, "Range", "");
    live.vitals_pack_temp = add_vital(page, right, 180, "Pack temperature", "");
    lv_obj_t *errors = add_vital(page, right, 240, "Errors", "None");
    if (menu.vehicle->fault_code != 0) {
        lv_label_set_text_fmt(errors, "Code %d", menu.vehicle->fault_code);
        lv_obj_set_style_text_color(errors, color::red, 0);
    }
    refresh_vitals_page();
}

/* ---------- carousel ---------- */

typedef void (*PageBuilder)(lv_obj_t *page);

const PageBuilder PAGE_BUILDERS[MENU_PAGE_COUNT] = {
    build_profile_page, build_digilocker_page, build_seat_page, build_charging_page,
    build_bike_status_page, build_security_page, build_payment_page, build_customize_page,
    build_misc_page, build_connectivity_page, build_wifi_page, build_system_page, build_vitals_page,
};

int wrapped_page(int index)
{
    return (index + MENU_PAGE_COUNT) % MENU_PAGE_COUNT;
}

void show_carousel(void)
{
    for (int i = 0; i < 3; i++) {
        lv_label_set_text(menu.tab_labels[i], PAGE_TITLES[wrapped_page(menu.page_index + i - 1)]);
    }
    for (int i = 0; i < MENU_PAGE_COUNT; i++) {
        lv_obj_set_style_bg_color(menu.dots[i], i == menu.page_index ? color::text_primary : color::stroke2, 0);
    }
}

void rebuild_page(void)
{
    if (menu.page != NULL) {
        lv_obj_delete(menu.page);
    }
    live = LiveLabels();
    menu.page = add_full_screen_layer(menu.page_host);
    lv_obj_set_height(menu.page, CAROUSEL_TOP);
    PAGE_BUILDERS[menu.page_index](menu.page);
}

void move_menu(int step)
{
    menu.page_index = wrapped_page(menu.page_index + step);
    menu.sub_index = 0;
    menu.sub_level = 0;
    menu.settings->payment_done = false;
    show_carousel();
    rebuild_page();
    lv_obj_set_style_opa(menu.page, LV_OPA_TRANSP, 0);
    fade_to(menu.page, LV_OPA_COVER, 240);
}

void move_menu_to(int page_index)
{
    move_menu(wrapped_page(page_index - menu.page_index));
}

void on_carousel_tab_tapped(lv_event_t *event)
{
    move_menu((int)(intptr_t)lv_event_get_user_data(event));
}

void on_menu_swiped(lv_event_t *event)
{
    LV_UNUSED(event);
    lv_dir_t direction = lv_indev_get_gesture_dir(lv_indev_active());
    if (direction == LV_DIR_LEFT) {
        move_menu(1);
    } else if (direction == LV_DIR_RIGHT) {
        move_menu(-1);
    }
}

void build_carousel(lv_obj_t *parent)
{
    for (int i = 0; i < 3; i++) {
        bool centre = i == 1;
        lv_obj_t *tab = add_color_block(parent, page_x(465) + i * 120, CAROUSEL_TOP, 120, 34,
                                        centre ? color::surface_selected : color::neutral18, LV_OPA_COVER, 0);
        menu.tab_labels[i] = add_text(tab, &evb_font_italic_16, centre ? color::text_primary : color::text_muted, "");
        lv_label_set_long_mode(menu.tab_labels[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(menu.tab_labels[i], 112);
        lv_obj_set_style_text_align(menu.tab_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(menu.tab_labels[i]);
        if (!centre) {
            call_on_tap(tab, on_carousel_tab_tapped, (void *)(intptr_t)(i - 1));
        }
    }
    add_tinted_picture(parent, EVB_ASSET_ICON_CHEVRON_LEFT, page_x(433), CAROUSEL_TOP + 8, color::text_secondary);
    add_tinted_picture(parent, EVB_ASSET_ICON_CHEVRON_RIGHT, page_x(839), CAROUSEL_TOP + 8, color::text_secondary);
    add_touch_area(parent, page_x(420), CAROUSEL_TOP - 5, 44, 44, on_carousel_tab_tapped, (void *)(intptr_t)-1);
    add_touch_area(parent, page_x(826), CAROUSEL_TOP - 5, 44, 44, on_carousel_tab_tapped, (void *)(intptr_t)1);

    for (int i = 0; i < MENU_PAGE_COUNT; i++) {
        menu.dots[i] = add_color_block(parent, page_x(577) + i * 13, CAROUSEL_TOP + 44, 5, 5, color::stroke2, LV_OPA_COVER, 2);
    }
    add_tinted_picture(parent, EVB_ASSET_FADE_LINE, page_x(500), CAROUSEL_TOP + 57, color::stroke2);
}

void build_lock_hint(lv_obj_t *parent)
{
    menu.lock_hint = add_color_block(parent, 230, 262, 340, 34, color::neutral4f, LV_OPA_COVER, 17);
    lv_obj_t *inside = add_color_block(menu.lock_hint, 1, 1, 338, 32, color::neutral21, LV_OPA_COVER, 16);
    lv_obj_center(add_text(inside, &evb_font_regular_14, color::text_primary, "Stop the bike to open the menu (P = park)"));
    lv_obj_set_style_opa(menu.lock_hint, LV_OPA_TRANSP, 0);
}

} // namespace

void evb_menu_create(lv_obj_t *parent, evb_cluster_settings_t *settings, evb_vehicle_state_t *vehicle, const PhoneState *phone)
{
    menu = Menu();
    live = LiveLabels();
    menu.settings = settings;
    menu.vehicle = vehicle;
    menu.phone = phone;

    menu.layer = add_full_screen_layer(parent);
    menu.page_host = add_plain_box(menu.layer, 0, 0, 800, CAROUSEL_TOP);
    lv_obj_set_clickable(menu.page_host, true);
    lv_obj_set_gesture_bubble(menu.page_host, false);
    lv_obj_add_event_cb(menu.page_host, on_menu_swiped, LV_EVENT_GESTURE, NULL);
    build_carousel(menu.layer);
    set_shown(menu.layer, false);

    build_lock_hint(parent);
    build_password_keyboard(parent);
}

void evb_menu_open(void)
{
    menu.open = true;
    menu.sub_index = 0;
    menu.sub_level = 0;
    menu.settings->payment_done = false;
    show_carousel();
    rebuild_page();
    set_shown(menu.layer, true);
    lv_obj_set_style_opa(menu.layer, LV_OPA_TRANSP, 0);
    fade_to(menu.layer, LV_OPA_COVER, 240);
}

void evb_menu_close(void)
{
    close_password_keyboard();
    menu.open = false;
    menu.sub_level = 0;
    if (menu.page != NULL) {
        lv_obj_delete(menu.page);
        menu.page = NULL;
    }
    live = LiveLabels();
    set_shown(menu.layer, false);
}

bool evb_menu_is_open(void)
{
    return menu.open;
}

/* Pages that show phone or Wi-Fi data are drawn again when that data changes. */
bool page_shows_phone_data(int page_index)
{
    return page_index == PAGE_MISC || page_index == PAGE_CONNECTIVITY || page_index == PAGE_WIFI || page_index == PAGE_SYSTEM;
}

void evb_menu_refresh(uint32_t elapsed_ms, uint32_t now_ms)
{
    menu.now_ms = now_ms;
    if (menu.lock_hint_left_ms > 0) {
        menu.lock_hint_left_ms = elapsed_ms >= menu.lock_hint_left_ms ? 0 : menu.lock_hint_left_ms - elapsed_ms;
        if (menu.lock_hint_left_ms == 0) {
            fade_to(menu.lock_hint, LV_OPA_TRANSP, 240);
        }
    }
    if (!menu.open) {
        return;
    }

    menu.phone_rebuild_elapsed_ms += elapsed_ms;
    if (page_shows_phone_data(menu.page_index) && menu.phone->revision != menu.shown_phone_revision
        && menu.phone_rebuild_elapsed_ms >= PHONE_PAGE_REBUILD_MS) {
        menu.phone_rebuild_elapsed_ms = 0;
        menu.shown_phone_revision = menu.phone->revision;
        rebuild_page();
    }

    menu.refresh_elapsed_ms += elapsed_ms;
    if (menu.refresh_elapsed_ms < REFRESH_MS) {
        return;
    }
    menu.refresh_elapsed_ms = 0;
    if (live.charging_title != NULL) {
        refresh_charging_page();
    }
    if (live.music_knob != NULL) {
        refresh_music_progress();
    }
    if (live.vitals_power != NULL) {
        refresh_vitals_page();
    }
}

void evb_menu_show_lock_hint(void)
{
    menu.lock_hint_left_ms = LOCK_HINT_MS;
    fade_to(menu.lock_hint, LV_OPA_COVER, 240);
}
