#pragma once

#include <stdint.h>

/*
 * Bluetooth Low Energy link to the rider's phone. The ESP32-S3 has BLE only (no Classic
 * Bluetooth), so call audio and music audio stay on the phone or the helmet headset; the
 * cluster shows them and controls them.
 *
 * What works with which phone:
 *   iPhone (pair "EVBikes Cluster" in Settings > Bluetooth, no app needed):
 *     incoming call with caller name, answer / decline, message notifications,
 *     now-playing title and artist, play / pause / next / previous / volume, clock.
 *     Uses Apple's ANCS, AMS and Current Time Service.
 *   Any phone (pair in Bluetooth settings): play / pause / next / previous / volume
 *     through standard Bluetooth media keys (HID consumer control).
 *   EVBikes phone app (mobile-bridge in the EVBikes repo): everything above plus
 *     turn-by-turn navigation, contacts, reminders, dialling a contact, phone battery
 *     and signal, over the EVBikes Phone Link service (docs/03-protocols.md).
 */

enum class MediaCommand : uint8_t {
    PlayPause,
    Next,
    Previous,
    VolumeUp,
    VolumeDown,
};

/* Starts the BLE stack, the services and advertising. Call once from setup(). */
void evb_bluetooth_start(void);

/* Call often from the UI task: finishes media key presses and drops stale phone data. */
void evb_bluetooth_poll(uint32_t now_ms);

void evb_bluetooth_send_media_command(MediaCommand command);
void evb_bluetooth_answer_call(void);
void evb_bluetooth_reject_call(void);
void evb_bluetooth_end_call(void);
void evb_bluetooth_call_contact(int slot);

/* Removes every bonded phone and drops the connection. */
void evb_bluetooth_forget_phones(void);
