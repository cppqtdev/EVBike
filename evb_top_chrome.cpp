#include "evb_top_chrome.h"

#include "evb_ui_kit.h"

using namespace evb;

namespace {

constexpr int TELLTALE_COUNT = 7;
constexpr int LOW_BATTERY_PERCENT = 15;

const lv_color_t TELLTALE_GREEN = lv_color_hex(0x3DDC84);
const lv_color_t TELLTALE_BLUE = lv_color_hex(0x3D8BFF);
const lv_color_t TELLTALE_AMBER = lv_color_hex(0xFFB020);
const lv_color_t TELLTALE_RED = lv_color_hex(0xF0303F);

struct TelltaleLook {
    evb_asset_id_t picture;
    int center_x;
    lv_color_t off_color;
};

const TelltaleLook TELLTALES[TELLTALE_COUNT] = {
    {EVB_ASSET_TT_LEFT, 254, lv_color_hex(0x8F8F8F)},
    {EVB_ASSET_TT_HIGH_BEAM, 301, lv_color_hex(0x515151)},
    {EVB_ASSET_TT_LOW_BEAM, 355, lv_color_hex(0x515151)},
    {EVB_ASSET_TT_WARNING, 406, lv_color_hex(0x636363)},
    {EVB_ASSET_TT_ABS, 460, lv_color_hex(0x3B3B3B)},
    {EVB_ASSET_TT_BATTERY, 515, lv_color_hex(0x5D5D5D)},
    {EVB_ASSET_TT_RIGHT, 568, lv_color_hex(0x8F8F8F)},
};

struct TopChrome {
    lv_obj_t *telltales[TELLTALE_COUNT];
    lv_color_t telltale_colors[TELLTALE_COUNT];
    lv_obj_t *status_corners;
    lv_obj_t *bluetooth_icon;
    lv_obj_t *signal_icon;
    lv_obj_t *ambient_temp_text;
    lv_obj_t *ambient_temp_degree;
    lv_obj_t *ambient_temp_unit;
    lv_obj_t *clock_text;
    lv_obj_t *clock_meridiem;

    bool has_shown;
    bool riding;
    bool phone_connected;
    int ambient_temp_c;
    int clock_hours;
    int clock_minutes;
    bool use_24_hour;
};

TopChrome chrome;

void show_telltale(int index, bool on, lv_color_t on_color)
{
    lv_color_t color = on ? on_color : TELLTALES[index].off_color;
    if (chrome.has_shown && lv_color_eq(color, chrome.telltale_colors[index])) {
        return;
    }
    chrome.telltale_colors[index] = color;
    tint_picture(chrome.telltales[index], color);
}

void show_telltales(const evb_vehicle_state_t *vehicle, bool self_test)
{
    bool battery_low = vehicle->battery_percent <= LOW_BATTERY_PERCENT;
    lv_color_t battery_color = vehicle->battery_percent <= 5 ? TELLTALE_RED : TELLTALE_AMBER;
    show_telltale(0, self_test || vehicle->indicator_left_on, TELLTALE_GREEN);
    show_telltale(1, self_test || vehicle->high_beam_on, TELLTALE_BLUE);
    show_telltale(2, self_test || vehicle->low_beam_on, TELLTALE_GREEN);
    show_telltale(3, self_test || vehicle->vehicle_warning_on, TELLTALE_AMBER);
    show_telltale(4, self_test || vehicle->abs_fault_on, TELLTALE_AMBER);
    show_telltale(5, self_test || battery_low, self_test ? TELLTALE_AMBER : battery_color);
    show_telltale(6, self_test || vehicle->indicator_right_on, TELLTALE_GREEN);
}

void show_ambient_temperature(int ambient_temp_c)
{
    lv_label_set_text_fmt(chrome.ambient_temp_text, "%d", ambient_temp_c);
    lv_obj_align_to(chrome.ambient_temp_degree, chrome.ambient_temp_text, LV_ALIGN_OUT_RIGHT_TOP, 1, -6);
    lv_obj_align_to(chrome.ambient_temp_unit, chrome.ambient_temp_text, LV_ALIGN_OUT_RIGHT_TOP, 5, 7);
}

void show_clock(int hours, int minutes, bool use_24_hour)
{
    int shown_hours = hours;
    if (!use_24_hour) {
        shown_hours = hours % 12 == 0 ? 12 : hours % 12;
    }
    lv_label_set_text_fmt(chrome.clock_text, "%02d:%02d", shown_hours, minutes);
    set_shown(chrome.clock_meridiem, !use_24_hour);
    lv_label_set_text(chrome.clock_meridiem, hours < 12 ? "am" : "pm");
    lv_obj_align_to(chrome.clock_meridiem, chrome.clock_text, LV_ALIGN_OUT_RIGHT_TOP, 1, 7);
}

void show_phone_link(bool connected)
{
    lv_color_t color = connected ? color::text_primary : color::text_muted;
    tint_picture(chrome.bluetooth_icon, color);
    tint_picture(chrome.signal_icon, color);
}

} // namespace

void evb_top_chrome_create(lv_obj_t *parent)
{
    chrome = TopChrome();
    for (int i = 0; i < TELLTALE_COUNT; i++) {
        chrome.telltales[i] = add_tinted_picture(parent, TELLTALES[i].picture, TELLTALES[i].center_x - 22, 9, TELLTALES[i].off_color);
    }

    chrome.status_corners = add_full_screen_layer(parent);
    lv_obj_set_height(chrome.status_corners, 64);
    chrome.bluetooth_icon = add_tinted_picture(chrome.status_corners, EVB_ASSET_ICON_BLUETOOTH, 126, 20, color::text_muted);
    chrome.ambient_temp_text = add_text(chrome.status_corners, &evb_font_regular_24, color::text_primary, "--");
    lv_obj_set_pos(chrome.ambient_temp_text, 167, 18);
    chrome.ambient_temp_degree = add_text(chrome.status_corners, &evb_font_regular_18, color::text_primary, "°");
    chrome.ambient_temp_unit = add_text(chrome.status_corners, &evb_font_regular_15, color::text_secondary, "C");

    chrome.signal_icon = add_tinted_picture(chrome.status_corners, EVB_ASSET_ICON_SIGNAL, 604, 25, color::text_muted);
    chrome.clock_text = add_text(chrome.status_corners, &evb_font_regular_22, color::text_primary, "--:--");
    lv_obj_set_pos(chrome.clock_text, 636, 20);
    chrome.clock_meridiem = add_text(chrome.status_corners, &evb_font_regular_16, color::text_primary, "am");
    set_shown(chrome.status_corners, false);
}

void evb_top_chrome_show(const evb_vehicle_state_t *vehicle, const evb_cluster_settings_t *settings, bool riding, bool self_test)
{
    bool first = !chrome.has_shown;
    show_telltales(vehicle, self_test);

    if (first || riding != chrome.riding) {
        set_shown(chrome.status_corners, riding);
    }
    if (first || vehicle->phone_connected != chrome.phone_connected) {
        show_phone_link(vehicle->phone_connected);
    }
    if (first || vehicle->ambient_temp_c != chrome.ambient_temp_c) {
        show_ambient_temperature(vehicle->ambient_temp_c);
    }
    if (first || vehicle->clock_hours != chrome.clock_hours || vehicle->clock_minutes != chrome.clock_minutes
        || settings->use_24_hour != chrome.use_24_hour) {
        show_clock(vehicle->clock_hours, vehicle->clock_minutes, settings->use_24_hour);
    }

    chrome.has_shown = true;
    chrome.riding = riding;
    chrome.phone_connected = vehicle->phone_connected;
    chrome.ambient_temp_c = vehicle->ambient_temp_c;
    chrome.clock_hours = vehicle->clock_hours;
    chrome.clock_minutes = vehicle->clock_minutes;
    chrome.use_24_hour = settings->use_24_hour;
}
