#include "evb_phone_overlays.h"

#include <stdio.h>

#include "evb_bluetooth.h"
#include "evb_ui_kit.h"

using namespace evb;

namespace {

constexpr int RING_CENTER_Y = 158;
constexpr uint32_t TOAST_SHOWN_MS = 4000;

struct PhoneOverlays {
    lv_obj_t *call_layer;
    lv_obj_t *pulse_ring;
    lv_obj_t *caller;
    lv_obj_t *incoming_text;
    lv_obj_t *duration;
    lv_obj_t *decline_button;
    lv_obj_t *decline_text;
    lv_obj_t *answer_button;
    lv_obj_t *hang_up_note;

    lv_obj_t *toast;
    lv_obj_t *toast_text;
    uint32_t shown_notification_seq;
    uint32_t toast_until_ms;
    bool has_shown;

    PhoneCallStatus shown_status;
};

PhoneOverlays overlays;

void on_answer_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    evb_bluetooth_answer_call();
}

void on_decline_tapped(lv_event_t *event)
{
    LV_UNUSED(event);
    if (overlays.shown_status == PhoneCallStatus::Active) {
        evb_bluetooth_end_call();
    } else {
        evb_bluetooth_reject_call();
    }
}

/* PillButton.qml */
lv_obj_t *add_pill(lv_obj_t *parent, int center_x, int top, lv_color_t fill, lv_color_t text_color, const char *text, lv_obj_t **label)
{
    lv_obj_t *pill = add_color_block(parent, center_x - 90, top, 180, 46, fill, LV_OPA_COVER, 23);
    *label = add_text(pill, &evb_font_bold_16, text_color, text);
    lv_obj_center(*label);
    return pill;
}

void grow_pulse(void *ring, int32_t step)
{
    int size = 118 + 70 * step / 100;
    lv_obj_set_size((lv_obj_t *)ring, size, size);
    lv_obj_set_pos((lv_obj_t *)ring, 400 - size / 2, RING_CENTER_Y - size / 2);
    lv_obj_set_style_bg_opa((lv_obj_t *)ring, (lv_opa_t)(51 * (100 - step) / 100), 0);
}

void start_pulse(void)
{
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, overlays.pulse_ring);
    lv_anim_set_exec_cb(&anim, grow_pulse);
    lv_anim_set_values(&anim, 0, 100);
    lv_anim_set_duration(&anim, 900);
    lv_anim_set_reverse_duration(&anim, 900);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&anim);
}

void build_call_screen(lv_obj_t *parent)
{
    overlays.call_layer = add_full_screen_layer(parent);
    lv_obj_t *layer = overlays.call_layer;
    lv_obj_set_style_bg_color(layer, color::black, 0);
    lv_obj_set_style_bg_opa(layer, 237, 0);
    lv_obj_set_clickable(layer, true);

    overlays.pulse_ring = add_color_block(layer, 400 - 59, RING_CENTER_Y - 59, 118, 118, color::green, 51, LV_RADIUS_CIRCLE);
    lv_obj_t *disc = add_color_block(layer, 400 - 46, RING_CENTER_Y - 46, 92, 92, color::success_muted, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    add_tinted_picture(disc, EVB_ASSET_ICON_PHONE_40, 26, 26, color::text_primary);

    overlays.caller = add_text(layer, &evb_font_bold_30, color::text_primary, "");
    place_by_top_center(overlays.caller, 400, 224);
    overlays.incoming_text = add_text(layer, &evb_font_regular_16, color::text_secondary, "Incoming call");
    place_by_top_center(overlays.incoming_text, 400, 262);
    overlays.duration = add_text(layer, &evb_font_regular_18, lv_color_hex(0x8FB4AB), "0:00");
    place_by_top_center(overlays.duration, 400, 262);

    overlays.decline_button = add_pill(layer, 290, 300, lv_color_hex(0x3A1618), lv_color_hex(0xF0303F), "Decline", &overlays.decline_text);
    call_on_tap(overlays.decline_button, on_decline_tapped, NULL);
    lv_obj_t *answer_label;
    overlays.answer_button = add_pill(layer, 510, 300, lv_color_hex(0x143A2A), color::green, "Answer", &answer_label);
    call_on_tap(overlays.answer_button, on_answer_tapped, NULL);
    overlays.hang_up_note = add_text(layer, &evb_font_regular_13, color::text_muted, "Hang up on the phone or helmet headset");
    place_by_top_center(overlays.hang_up_note, 400, 356);

    set_shown(layer, false);
}

void build_toast(lv_obj_t *parent)
{
    overlays.toast = add_color_block(parent, 190, 250, 420, 56, color::surface_raised, LV_OPA_COVER, 12);
    add_tinted_picture(overlays.toast, EVB_ASSET_ICON_BELL, 16, 16, color::teal);
    overlays.toast_text = add_text(overlays.toast, &evb_font_regular_18, color::text_primary, "");
    lv_label_set_long_mode(overlays.toast_text, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(overlays.toast_text, 420 - 52 - 12);
    lv_obj_align(overlays.toast_text, LV_ALIGN_LEFT_MID, 52, 0);
    lv_obj_set_style_opa(overlays.toast, LV_OPA_TRANSP, 0);
}

void show_call(const PhoneState *phone, uint32_t now_ms)
{
    PhoneCallStatus status = phone->call_status;
    bool ringing = status == PhoneCallStatus::Ringing;
    bool active = status == PhoneCallStatus::Active;
    if (status != overlays.shown_status || !overlays.has_shown) {
        set_shown(overlays.call_layer, status != PhoneCallStatus::Idle);
        set_shown(overlays.pulse_ring, ringing);
        if (ringing) {
            start_pulse();
        } else {
            lv_anim_delete(overlays.pulse_ring, grow_pulse);
        }
        set_shown(overlays.incoming_text, ringing);
        set_shown(overlays.duration, active);
        set_shown(overlays.answer_button, ringing);
        lv_label_set_text(overlays.decline_text, active ? "End" : "Decline");
        overlays.shown_status = status;
    }
    if (status == PhoneCallStatus::Idle) {
        return;
    }
    lv_label_set_text(overlays.caller, phone->caller[0] != '\0' ? phone->caller : "Unknown");
    bool phone_ends_call = phone->link_kind != PhoneLinkKind::EvbikesApp;
    set_shown(overlays.hang_up_note, active && phone_ends_call);
    if (active) {
        uint32_t seconds = (now_ms - phone->call_started_ms) / 1000;
        lv_label_set_text_fmt(overlays.duration, "%d:%02d", (int)(seconds / 60), (int)(seconds % 60));
    }
}

void show_toast(const PhoneState *phone, bool moving, uint32_t now_ms)
{
    if (phone->notification_seq != overlays.shown_notification_seq) {
        overlays.shown_notification_seq = phone->notification_seq;
        if (overlays.has_shown) {
            char text[EVB_TEXT_MAX * 2 + 24];
            if (moving) {
                snprintf(text, sizeof(text), "%s sent a message", phone->notification_sender);
            } else {
                snprintf(text, sizeof(text), "%s: %s", phone->notification_sender, phone->notification_text);
            }
            lv_label_set_text(overlays.toast_text, text);
            fade_to(overlays.toast, LV_OPA_COVER, 240);
            overlays.toast_until_ms = now_ms + TOAST_SHOWN_MS;
        }
    }
    if (overlays.toast_until_ms != 0 && (int32_t)(now_ms - overlays.toast_until_ms) >= 0) {
        overlays.toast_until_ms = 0;
        fade_to(overlays.toast, LV_OPA_TRANSP, 240);
    }
}

} // namespace

void evb_phone_overlays_create(lv_obj_t *parent)
{
    overlays = PhoneOverlays();
    build_toast(parent);
    build_call_screen(parent);
}

void evb_phone_overlays_show(const PhoneState *phone, bool moving, uint32_t now_ms)
{
    show_toast(phone, moving, now_ms);
    show_call(phone, now_ms);
    overlays.has_shown = true;
}
