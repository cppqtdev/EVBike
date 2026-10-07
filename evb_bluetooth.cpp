#include "evb_bluetooth.h"

#include <string.h>

#include "evb_phone_protocols.h"
#include "evb_phone_state.h"

using namespace evb::phone;

#ifdef ESP_PLATFORM

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>

#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"

namespace {

constexpr const char *DEVICE_NAME = "EVBikes Cluster";
constexpr const char *PHONE_LINK_SERVICE = "6f7e0001-7a3c-4f1b-9e2d-45b1c0de0001";
constexpr const char *PHONE_LINK_RX = "6f7e0002-7a3c-4f1b-9e2d-45b1c0de0001";
constexpr const char *PHONE_LINK_TX = "6f7e0003-7a3c-4f1b-9e2d-45b1c0de0001";
constexpr uint16_t APPEARANCE_HID = 0x03C0;
constexpr uint8_t MEDIA_REPORT_ID = 1;
constexpr uint32_t ANCS_RESPONSE_TIMEOUT_MS = 3000;
constexpr int ANCS_QUEUE_LENGTH = 6;

/* Apple service and characteristic UUIDs, least significant byte first. */
const ble_uuid128_t ANCS_SERVICE = BLE_UUID128_INIT(0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);
const ble_uuid128_t ANCS_NOTIFICATION_SOURCE = BLE_UUID128_INIT(0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c, 0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
const ble_uuid128_t ANCS_CONTROL_POINT = BLE_UUID128_INIT(0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98, 0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
const ble_uuid128_t ANCS_DATA_SOURCE = BLE_UUID128_INIT(0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe, 0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);
const ble_uuid128_t AMS_SERVICE = BLE_UUID128_INIT(0xdc, 0xf8, 0x55, 0xad, 0x02, 0xc5, 0xf4, 0x8e, 0x3a, 0x43, 0x36, 0x0f, 0x2b, 0x50, 0xd3, 0x89);
const ble_uuid128_t AMS_REMOTE_COMMAND = BLE_UUID128_INIT(0xc2, 0x51, 0xca, 0xf7, 0x56, 0x0e, 0xdf, 0xb8, 0x8a, 0x4a, 0xb1, 0x57, 0xd8, 0x81, 0x3c, 0x9b);
const ble_uuid128_t AMS_ENTITY_UPDATE = BLE_UUID128_INIT(0x02, 0xc1, 0x96, 0xba, 0x92, 0xbb, 0x0c, 0x9a, 0x1f, 0x41, 0x8d, 0x80, 0xce, 0xab, 0x7c, 0x2f);
const ble_uuid16_t CTS_SERVICE = BLE_UUID16_INIT(0x1805);
const ble_uuid16_t CTS_CURRENT_TIME = BLE_UUID16_INIT(0x2A2B);
const ble_uuid16_t CTS_LOCAL_TIME_INFO = BLE_UUID16_INIT(0x2A0F);
const ble_uuid16_t CLIENT_CONFIG = BLE_UUID16_INIT(0x2902);

/* HID consumer control: one 16-bit usage, report 1. */
const uint8_t MEDIA_KEYS_REPORT_MAP[] = {
    0x05, 0x0C,       /* Usage Page (Consumer) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /*   Report ID (1) */
    0x15, 0x00,       /*   Logical Minimum (0) */
    0x26, 0xFF, 0x03, /*   Logical Maximum (1023) */
    0x19, 0x00,       /*   Usage Minimum (0) */
    0x2A, 0xFF, 0x03, /*   Usage Maximum (1023) */
    0x75, 0x10,       /*   Report Size (16) */
    0x95, 0x01,       /*   Report Count (1) */
    0x81, 0x00,       /*   Input (Data, Array) */
    0xC0,             /* End Collection */
};

uint16_t consumer_usage(MediaCommand command)
{
    switch (command) {
    case MediaCommand::PlayPause:
        return 0x00CD;
    case MediaCommand::Next:
        return 0x00B5;
    case MediaCommand::Previous:
        return 0x00B6;
    case MediaCommand::VolumeUp:
        return 0x00E9;
    case MediaCommand::VolumeDown:
        return 0x00EA;
    }
    return 0x00CD;
}

/* Handles found on the phone after pairing; 0 means the phone does not have it. */
struct PhoneServices {
    uint16_t ancs_start, ancs_end;
    uint16_t ams_start, ams_end;
    uint16_t cts_start, cts_end;
    uint16_t ancs_notification_source, ancs_notification_source_config;
    uint16_t ancs_control_point;
    uint16_t ancs_data_source, ancs_data_source_config;
    uint16_t ams_remote_command, ams_remote_command_config;
    uint16_t ams_entity_update, ams_entity_update_config;
    uint16_t cts_current_time, cts_current_time_config;
    uint16_t cts_local_time_info;
};

enum class Step : uint8_t {
    FindAncs,
    FindAncsCharacteristics,
    FindAncsDescriptors,
    FindAms,
    FindAmsCharacteristics,
    FindAmsDescriptors,
    FindCts,
    FindCtsCharacteristics,
    FindCtsDescriptors,
    SubscribeAncsDataSource,
    SubscribeAncsNotificationSource,
    SubscribeAmsEntityUpdate,
    RegisterAmsPlayer,
    RegisterAmsTrack,
    SubscribeAmsRemoteCommand,
    ReadLocalTimeInfo,
    ReadCurrentTime,
    SubscribeCurrentTime,
    Done,
};

struct PhoneConnection {
    uint16_t connection;
    bool connected;
    bool encrypted;
    uint32_t generation;
    Step step;
    PhoneServices services;
    uint8_t local_time_info[2];
    bool has_local_time_info;

    AncsRequest ancs_queue[ANCS_QUEUE_LENGTH];
    int ancs_queue_count;
    bool ancs_request_in_flight;
    uint32_t ancs_request_sent_ms;

    uint16_t media_key_down;
    uint32_t media_key_down_ms;
};

PhoneConnection phone;
BLEServer *server = nullptr;
BLECharacteristic *phone_link_tx = nullptr;
BLEHIDDevice *hid = nullptr;
BLECharacteristic *media_keys = nullptr;
ble_gap_event_listener gap_listener;

void run_step(Step step, uint32_t generation);

void *generation_tag(uint32_t generation)
{
    return (void *)(uintptr_t)generation;
}

bool is_current(void *arg)
{
    return phone.connected && (uint32_t)(uintptr_t)arg == phone.generation;
}

Step next_step(Step step)
{
    return (Step)((uint8_t)step + 1);
}

void publish_link_kind(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    bool apple = phone.services.ancs_control_point != 0 || phone.services.ams_remote_command != 0;
    state->phone_connected = phone.connected;
    state->phone_bonded = phone.encrypted;
    if (!phone.connected) {
        state->link_kind = PhoneLinkKind::None;
    } else if (state->link_kind != PhoneLinkKind::EvbikesApp) {
        state->link_kind = apple ? PhoneLinkKind::Iphone : PhoneLinkKind::OtherPhone;
    }
    if (phone.services.ancs_control_point != 0) {
        state->call_controls_available = true;
    }
    if (phone.encrypted) {
        state->media_controls_available = true;
    }
    evb_phone_state_end_edit();
}

/* ---------- discovery on the phone ---------- */

int on_service_found(uint16_t connection, const ble_gatt_error *error, const ble_gatt_svc *service, void *arg)
{
    (void)connection;
    if (!is_current(arg)) {
        return 0;
    }
    if (error->status == 0 && service != nullptr) {
        if (phone.step == Step::FindAncs) {
            phone.services.ancs_start = service->start_handle;
            phone.services.ancs_end = service->end_handle;
        } else if (phone.step == Step::FindAms) {
            phone.services.ams_start = service->start_handle;
            phone.services.ams_end = service->end_handle;
        } else if (phone.step == Step::FindCts) {
            phone.services.cts_start = service->start_handle;
            phone.services.cts_end = service->end_handle;
        }
        return 0;
    }
    run_step(next_step(phone.step), phone.generation);
    return 0;
}

int on_characteristic_found(uint16_t connection, const ble_gatt_error *error, const ble_gatt_chr *characteristic, void *arg)
{
    (void)connection;
    if (!is_current(arg)) {
        return 0;
    }
    if (error->status == 0 && characteristic != nullptr) {
        const ble_uuid_t *uuid = &characteristic->uuid.u;
        uint16_t handle = characteristic->val_handle;
        PhoneServices &found = phone.services;
        if (ble_uuid_cmp(uuid, &ANCS_NOTIFICATION_SOURCE.u) == 0) {
            found.ancs_notification_source = handle;
        } else if (ble_uuid_cmp(uuid, &ANCS_CONTROL_POINT.u) == 0) {
            found.ancs_control_point = handle;
        } else if (ble_uuid_cmp(uuid, &ANCS_DATA_SOURCE.u) == 0) {
            found.ancs_data_source = handle;
        } else if (ble_uuid_cmp(uuid, &AMS_REMOTE_COMMAND.u) == 0) {
            found.ams_remote_command = handle;
        } else if (ble_uuid_cmp(uuid, &AMS_ENTITY_UPDATE.u) == 0) {
            found.ams_entity_update = handle;
        } else if (ble_uuid_cmp(uuid, &CTS_CURRENT_TIME.u) == 0) {
            found.cts_current_time = handle;
        } else if (ble_uuid_cmp(uuid, &CTS_LOCAL_TIME_INFO.u) == 0) {
            found.cts_local_time_info = handle;
        }
        return 0;
    }
    run_step(next_step(phone.step), phone.generation);
    return 0;
}

int on_descriptor_found(uint16_t connection, const ble_gatt_error *error, uint16_t value_handle, const ble_gatt_dsc *descriptor, void *arg)
{
    (void)connection;
    if (!is_current(arg)) {
        return 0;
    }
    if (error->status == 0 && descriptor != nullptr) {
        if (ble_uuid_cmp(&descriptor->uuid.u, &CLIENT_CONFIG.u) == 0) {
            PhoneServices &found = phone.services;
            if (value_handle == found.ancs_notification_source) {
                found.ancs_notification_source_config = descriptor->handle;
            } else if (value_handle == found.ancs_data_source) {
                found.ancs_data_source_config = descriptor->handle;
            } else if (value_handle == found.ams_remote_command) {
                found.ams_remote_command_config = descriptor->handle;
            } else if (value_handle == found.ams_entity_update) {
                found.ams_entity_update_config = descriptor->handle;
            } else if (value_handle == found.cts_current_time) {
                found.cts_current_time_config = descriptor->handle;
            }
        }
        return 0;
    }
    run_step(next_step(phone.step), phone.generation);
    return 0;
}

int on_step_written(uint16_t connection, const ble_gatt_error *error, ble_gatt_attr *attribute, void *arg)
{
    (void)connection;
    (void)error;
    (void)attribute;
    if (is_current(arg)) {
        run_step(next_step(phone.step), phone.generation);
    }
    return 0;
}

int on_step_read(uint16_t connection, const ble_gatt_error *error, ble_gatt_attr *attribute, void *arg)
{
    (void)connection;
    if (!is_current(arg)) {
        return 0;
    }
    if (error->status == 0 && attribute != nullptr && attribute->om != nullptr) {
        uint8_t value[16];
        uint16_t length = 0;
        ble_hs_mbuf_to_flat(attribute->om, value, sizeof(value), &length);
        if (phone.step == Step::ReadLocalTimeInfo && length >= 2) {
            memcpy(phone.local_time_info, value, 2);
            phone.has_local_time_info = true;
        } else if (phone.step == Step::ReadCurrentTime) {
            cts_apply_current_time(value, length, phone.has_local_time_info ? phone.local_time_info : nullptr, 2);
        }
    }
    if (error->status != BLE_HS_EDONE) {
        run_step(next_step(phone.step), phone.generation);
    }
    return 0;
}

void subscribe(uint16_t config_handle, uint32_t generation)
{
    const uint8_t enable_notifications[2] = {0x01, 0x00};
    if (ble_gattc_write_flat(phone.connection, config_handle, enable_notifications, sizeof(enable_notifications), on_step_written,
                             generation_tag(generation)) != 0) {
        run_step(next_step(phone.step), generation);
    }
}

void write_step(uint16_t handle, const uint8_t *data, size_t length, uint32_t generation)
{
    if (ble_gattc_write_flat(phone.connection, handle, data, (uint16_t)length, on_step_written, generation_tag(generation)) != 0) {
        run_step(next_step(phone.step), generation);
    }
}

/* Walks through the steps one GATT request at a time; a step the phone cannot do is skipped. */
void run_step(Step step, uint32_t generation)
{
    if (!phone.connected || generation != phone.generation) {
        return;
    }
    phone.step = step;
    void *tag = generation_tag(generation);
    PhoneServices &found = phone.services;
    uint8_t data[8];
    int rc = 0;

    switch (step) {
    case Step::FindAncs:
        rc = ble_gattc_disc_svc_by_uuid(phone.connection, &ANCS_SERVICE.u, on_service_found, tag);
        break;
    case Step::FindAncsCharacteristics:
        if (found.ancs_start == 0) {
            return run_step(Step::FindAms, generation);
        }
        rc = ble_gattc_disc_all_chrs(phone.connection, found.ancs_start, found.ancs_end, on_characteristic_found, tag);
        break;
    case Step::FindAncsDescriptors:
        rc = ble_gattc_disc_all_dscs(phone.connection, found.ancs_start, found.ancs_end, on_descriptor_found, tag);
        break;
    case Step::FindAms:
        rc = ble_gattc_disc_svc_by_uuid(phone.connection, &AMS_SERVICE.u, on_service_found, tag);
        break;
    case Step::FindAmsCharacteristics:
        if (found.ams_start == 0) {
            return run_step(Step::FindCts, generation);
        }
        rc = ble_gattc_disc_all_chrs(phone.connection, found.ams_start, found.ams_end, on_characteristic_found, tag);
        break;
    case Step::FindAmsDescriptors:
        rc = ble_gattc_disc_all_dscs(phone.connection, found.ams_start, found.ams_end, on_descriptor_found, tag);
        break;
    case Step::FindCts:
        rc = ble_gattc_disc_svc_by_uuid(phone.connection, &CTS_SERVICE.u, on_service_found, tag);
        break;
    case Step::FindCtsCharacteristics:
        if (found.cts_start == 0) {
            return run_step(Step::SubscribeAncsDataSource, generation);
        }
        rc = ble_gattc_disc_all_chrs(phone.connection, found.cts_start, found.cts_end, on_characteristic_found, tag);
        break;
    case Step::FindCtsDescriptors:
        rc = ble_gattc_disc_all_dscs(phone.connection, found.cts_start, found.cts_end, on_descriptor_found, tag);
        break;
    case Step::SubscribeAncsDataSource:
        publish_link_kind();
        if (found.ancs_data_source_config == 0) {
            return run_step(Step::SubscribeAmsEntityUpdate, generation);
        }
        return subscribe(found.ancs_data_source_config, generation);
    case Step::SubscribeAncsNotificationSource:
        if (found.ancs_notification_source_config == 0) {
            return run_step(next_step(step), generation);
        }
        return subscribe(found.ancs_notification_source_config, generation);
    case Step::SubscribeAmsEntityUpdate:
        if (found.ams_entity_update_config == 0) {
            return run_step(Step::SubscribeAmsRemoteCommand, generation);
        }
        return subscribe(found.ams_entity_update_config, generation);
    case Step::RegisterAmsPlayer:
        return write_step(found.ams_entity_update, data, ams_build_player_registration(data, sizeof(data)), generation);
    case Step::RegisterAmsTrack:
        return write_step(found.ams_entity_update, data, ams_build_track_registration(data, sizeof(data)), generation);
    case Step::SubscribeAmsRemoteCommand:
        if (found.ams_remote_command_config == 0) {
            return run_step(next_step(step), generation);
        }
        return subscribe(found.ams_remote_command_config, generation);
    case Step::ReadLocalTimeInfo:
        if (found.cts_local_time_info == 0) {
            return run_step(next_step(step), generation);
        }
        rc = ble_gattc_read(phone.connection, found.cts_local_time_info, on_step_read, tag);
        break;
    case Step::ReadCurrentTime:
        if (found.cts_current_time == 0) {
            return run_step(Step::Done, generation);
        }
        rc = ble_gattc_read(phone.connection, found.cts_current_time, on_step_read, tag);
        break;
    case Step::SubscribeCurrentTime:
        if (found.cts_current_time_config == 0) {
            return run_step(Step::Done, generation);
        }
        return subscribe(found.cts_current_time_config, generation);
    case Step::Done:
        publish_link_kind();
        Serial.printf("Phone ready. Notifications (ANCS): %s, music (AMS): %s, time (CTS): %s\n",
                      found.ancs_notification_source_config != 0 ? "yes" : "no",
                      found.ams_remote_command != 0 ? "yes" : "no", found.cts_current_time != 0 ? "yes" : "no");
        return;
    }
    if (rc != 0) {
        run_step(next_step(step), generation);
    }
}

/* ---------- ANCS text requests: one at a time ---------- */

void send_next_ancs_request(uint32_t now_ms)
{
    if (phone.ancs_request_in_flight || phone.ancs_queue_count == 0 || phone.services.ancs_control_point == 0) {
        return;
    }
    AncsRequest request = phone.ancs_queue[0];
    memmove(phone.ancs_queue, phone.ancs_queue + 1, sizeof(AncsRequest) * (size_t)(phone.ancs_queue_count - 1));
    phone.ancs_queue_count--;

    uint8_t command[16];
    size_t length = ancs_build_attribute_request(request, command, sizeof(command));
    ancs_expect_response(request);
    phone.ancs_request_in_flight = true;
    phone.ancs_request_sent_ms = now_ms;
    ble_gattc_write_flat(phone.connection, phone.services.ancs_control_point, command, (uint16_t)length, nullptr, nullptr);
}

void queue_ancs_request(const AncsRequest &request, uint32_t now_ms)
{
    if (phone.ancs_queue_count < ANCS_QUEUE_LENGTH) {
        /* A caller's name goes first. */
        if (request.kind == AncsRequestKind::Caller) {
            memmove(phone.ancs_queue + 1, phone.ancs_queue, sizeof(AncsRequest) * (size_t)phone.ancs_queue_count);
            phone.ancs_queue[0] = request;
        } else {
            phone.ancs_queue[phone.ancs_queue_count] = request;
        }
        phone.ancs_queue_count++;
    }
    send_next_ancs_request(now_ms);
}

/* ---------- GAP events for the phone connection ---------- */

void on_notification(uint16_t attribute, os_mbuf *om)
{
    uint8_t data[256];
    uint16_t length = 0;
    if (ble_hs_mbuf_to_flat(om, data, sizeof(data), &length) != 0 && length == 0) {
        return;
    }
    uint32_t now_ms = millis();
    const PhoneServices &found = phone.services;
    if (attribute == found.ancs_notification_source) {
        AncsRequest request;
        Serial.printf("Phone notification: event %u, category %u\n", length > 0 ? data[0] : 0, length > 2 ? data[2] : 0);
        if (ancs_on_notification_source(data, length, now_ms, &request)) {
            queue_ancs_request(request, now_ms);
        }
    } else if (attribute == found.ancs_data_source) {
        if (ancs_on_data_source(data, length, now_ms)) {
            phone.ancs_request_in_flight = false;
            send_next_ancs_request(now_ms);
        }
    } else if (attribute == found.ams_entity_update) {
        ams_on_entity_update(data, length, now_ms);
    } else if (attribute == found.cts_current_time) {
        cts_apply_current_time(data, length, phone.has_local_time_info ? phone.local_time_info : nullptr, 2);
    }
}

void forget_link(void)
{
    uint32_t generation = phone.generation + 1;
    phone = PhoneConnection();
    phone.generation = generation;
    forget_phone_data();
    publish_link_kind();
}

/* The connection events arrive both through the BLE library callbacks and the GAP
 * listener; each handler ignores a repeat. */
void on_phone_connected(uint16_t connection)
{
    if (phone.connected && phone.connection == connection) {
        return;
    }
    forget_link();
    phone.connection = connection;
    phone.connected = true;
    publish_link_kind();
}

void on_phone_disconnected(uint16_t connection)
{
    if (phone.connected && phone.connection == connection) {
        forget_link();
    }
}

void on_phone_encrypted(uint16_t connection)
{
    if (!phone.connected || phone.connection != connection || phone.encrypted) {
        return;
    }
    phone.encrypted = true;
    publish_link_kind();
    run_step(Step::FindAncs, phone.generation);
}

int on_gap_event(ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            on_phone_connected(event->connect.conn_handle);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        on_phone_disconnected(event->disconnect.conn.conn_handle);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            on_phone_encrypted(event->enc_change.conn_handle);
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (phone.connected && event->notify_rx.conn_handle == phone.connection) {
            on_notification(event->notify_rx.attr_handle, event->notify_rx.om);
        }
        break;
    default:
        break;
    }
    return 0;
}

class PhoneConnectionEvents : public BLEServerCallbacks {
    void onConnect(BLEServer *server, ble_gap_conn_desc *description) override
    {
        (void)server;
        on_phone_connected(description->conn_handle);
    }

    void onDisconnect(BLEServer *server, ble_gap_conn_desc *description) override
    {
        (void)server;
        on_phone_disconnected(description->conn_handle);
    }
};

class PairingEvents : public BLESecurityCallbacks {
    void onAuthenticationComplete(ble_gap_conn_desc *description) override
    {
        if (description->sec_state.encrypted) {
            on_phone_encrypted(description->conn_handle);
        }
    }
};

/* ---------- EVBikes Phone Link service ---------- */

class PhoneLinkWrites : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *characteristic) override
    {
        String value = characteristic->getValue();
        feed_app_bytes((const uint8_t *)value.c_str(), value.length(), millis());
    }
};

void send_to_app(const uint8_t *frame, size_t length)
{
    if (phone_link_tx == nullptr || length == 0) {
        return;
    }
    phone_link_tx->setValue((uint8_t *)frame, length);
    phone_link_tx->notify();
}

/* ---------- advertising ---------- */

void start_advertising(void)
{
    /* Advertising: flags, HID appearance, HID service, and a request for Apple's ANCS so an
     * iPhone offers its notifications once paired. Scan response: the EVBikes Phone Link
     * service the EVBikes app looks for, and the name. */
    const uint8_t advertising[] = {
        0x02, 0x01, 0x06,
        0x03, 0x19, (uint8_t)(APPEARANCE_HID & 0xFF), (uint8_t)(APPEARANCE_HID >> 8),
        0x03, 0x03, 0x12, 0x18,
        0x11, 0x15, 0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4, 0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79,
    };
    const uint8_t scan_response[] = {
        0x11, 0x07, 0x01, 0x00, 0xde, 0xc0, 0xb1, 0x45, 0x2d, 0x9e, 0x1b, 0x4f, 0x3c, 0x7a, 0x01, 0x00, 0x7e, 0x6f,
        0x08, 0x09, 'E', 'V', 'B', 'i', 'k', 'e', 's',
    };
    BLEAdvertisementData advertising_data;
    advertising_data.addData((char *)advertising, sizeof(advertising));
    BLEAdvertisementData scan_response_data;
    scan_response_data.addData((char *)scan_response, sizeof(scan_response));

    BLEAdvertising *advertiser = BLEDevice::getAdvertising();
    advertiser->setAdvertisementData(advertising_data);
    advertiser->setScanResponseData(scan_response_data);
    advertiser->start();
}

void press_media_key(MediaCommand command)
{
    if (media_keys == nullptr || !phone.encrypted) {
        return;
    }
    uint16_t usage = consumer_usage(command);
    uint8_t report[2] = {(uint8_t)usage, (uint8_t)(usage >> 8)};
    media_keys->setValue(report, sizeof(report));
    media_keys->notify();
    phone.media_key_down = usage;
    phone.media_key_down_ms = millis();
}

void send_call_command(AppCallCommand command, int slot)
{
    uint8_t frame[16];
    send_to_app(frame, build_app_call_command(command, slot, frame, sizeof(frame)));
}

} // namespace

void evb_bluetooth_start(void)
{
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setMTU(185);

    BLESecurity *security = new BLESecurity();
    security->setAuthenticationMode(true, false, true);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setForceAuthentication(true);

    BLEDevice::setSecurityCallbacks(new PairingEvents());

    server = BLEDevice::createServer();
    server->setCallbacks(new PhoneConnectionEvents());
    server->advertiseOnDisconnect(true);

    BLEService *phone_link = server->createService(PHONE_LINK_SERVICE);
    BLECharacteristic *rx = phone_link->createCharacteristic(PHONE_LINK_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new PhoneLinkWrites());
    phone_link_tx = phone_link->createCharacteristic(PHONE_LINK_TX, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    phone_link->start();

    hid = new BLEHIDDevice(server);
    media_keys = hid->inputReport(MEDIA_REPORT_ID);
    hid->manufacturer()->setValue("EVBikes");
    hid->pnp(0x02, 0x303A, 0x4001, 0x0100);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t *)MEDIA_KEYS_REPORT_MAP, sizeof(MEDIA_KEYS_REPORT_MAP));
    hid->startServices();
    hid->setBatteryLevel(100);

    ble_gap_event_listener_register(&gap_listener, on_gap_event, nullptr);
    start_advertising();

    PhoneState *state = evb_phone_state_begin_edit();
    state->bluetooth_ready = true;
    evb_phone_state_end_edit();
}

void evb_bluetooth_poll(uint32_t now_ms)
{
    if (phone.media_key_down != 0 && now_ms - phone.media_key_down_ms >= 60 && media_keys != nullptr) {
        uint8_t released[2] = {0, 0};
        media_keys->setValue(released, sizeof(released));
        media_keys->notify();
        phone.media_key_down = 0;
    }
    if (phone.ancs_request_in_flight && now_ms - phone.ancs_request_sent_ms > ANCS_RESPONSE_TIMEOUT_MS) {
        phone.ancs_request_in_flight = false;
        send_next_ancs_request(now_ms);
    }
    if (!app_link_alive(now_ms)) {
        forget_app_link();
    }
}

void evb_bluetooth_send_media_command(MediaCommand command)
{
    if (!phone.connected) {
        return;
    }
    if (phone.services.ams_remote_command != 0) {
        uint8_t code = ams_remote_command(command);
        ble_gattc_write_flat(phone.connection, phone.services.ams_remote_command, &code, 1, nullptr, nullptr);
        return;
    }
    if (app_link_alive(millis())) {
        uint8_t frame[16];
        send_to_app(frame, build_app_media_command(command, frame, sizeof(frame)));
        return;
    }
    press_media_key(command);
}

void evb_bluetooth_answer_call(void)
{
    uint8_t action[8];
    size_t length = ancs_build_call_action(true, millis(), action, sizeof(action));
    if (length > 0 && phone.services.ancs_control_point != 0) {
        ble_gattc_write_flat(phone.connection, phone.services.ancs_control_point, action, (uint16_t)length, nullptr, nullptr);
        return;
    }
    send_call_command(AppCallCommand::Answer, 0);
}

void evb_bluetooth_reject_call(void)
{
    uint8_t action[8];
    size_t length = ancs_build_call_action(false, millis(), action, sizeof(action));
    if (length > 0 && phone.services.ancs_control_point != 0) {
        ble_gattc_write_flat(phone.connection, phone.services.ancs_control_point, action, (uint16_t)length, nullptr, nullptr);
        return;
    }
    send_call_command(AppCallCommand::Reject, 0);
}

void evb_bluetooth_end_call(void)
{
    if (app_link_alive(millis())) {
        send_call_command(AppCallCommand::End, 0);
        return;
    }
    /* An iPhone does not let accessories hang up a call that is already talking; the call
     * screen just closes and the rider ends it on the phone or headset. */
    PhoneState *state = evb_phone_state_begin_edit();
    state->call_status = PhoneCallStatus::Idle;
    evb_phone_state_end_edit();
}

void evb_bluetooth_call_contact(int slot)
{
    if (app_link_alive(millis())) {
        send_call_command(AppCallCommand::DialContactSlot, slot);
    }
}

void evb_bluetooth_forget_phones(void)
{
    if (phone.connected) {
        ble_gap_terminate(phone.connection, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_store_clear();
}

#else /* PC build: no radio; the screens are tested with PhoneState filled in directly. */

void evb_bluetooth_start(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->bluetooth_ready = true;
    evb_phone_state_end_edit();
}

void evb_bluetooth_poll(uint32_t now_ms)
{
    (void)now_ms;
}

void evb_bluetooth_send_media_command(MediaCommand command)
{
    PhoneState *state = evb_phone_state_begin_edit();
    if (command == MediaCommand::PlayPause) {
        state->media_playing = !state->media_playing;
    } else if (command == MediaCommand::VolumeUp && state->media_volume_percent < 100) {
        state->media_volume_percent += 10;
    } else if (command == MediaCommand::VolumeDown && state->media_volume_percent > 0) {
        state->media_volume_percent -= 10;
    }
    evb_phone_state_end_edit();
}

void evb_bluetooth_answer_call(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->call_status = PhoneCallStatus::Active;
    evb_phone_state_end_edit();
}

void evb_bluetooth_reject_call(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->call_status = PhoneCallStatus::Idle;
    evb_phone_state_end_edit();
}

void evb_bluetooth_end_call(void)
{
    evb_bluetooth_reject_call();
}

void evb_bluetooth_call_contact(int slot)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->call_status = PhoneCallStatus::Active;
    evb_copy_text(state->caller, sizeof(state->caller), state->contacts[slot].title);
    evb_phone_state_end_edit();
}

void evb_bluetooth_forget_phones(void)
{
}

#endif
