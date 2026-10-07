#include "evb_wifi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evb_clock.h"
#include "evb_firmware_info.h"
#include "evb_phone_state.h"

#ifdef ESP_PLATFORM

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t WEATHER_EVERY_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t WEATHER_RETRY_MS = 60UL * 1000UL;
constexpr uint32_t SIGNAL_EVERY_MS = 5000;
constexpr int HTTP_TIMEOUT_MS = 8000;

enum class RequestType : uint8_t {
    Scan,
    Connect,
    Forget,
    CheckForUpdate,
    InstallUpdate,
};

struct Request {
    RequestType type;
    char ssid[33];
    char password[65];
};

struct WifiTask {
    QueueHandle_t requests;
    bool joining;
    uint32_t join_started_ms;
    bool was_connected;
    bool internet_services_started;
    bool network_upload_started;
    uint32_t next_weather_ms;
    uint32_t next_signal_ms;
    bool location_known;
    float latitude;
    float longitude;
    String update_url;
    bool save_network_when_joined;
    char joining_ssid[33];
    char joining_password[65];
};

WifiTask wifi;

void set_status(WifiStatus status)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = status;
    if (status != WifiStatus::Connected) {
        state->wifi_ip[0] = '\0';
    }
    evb_phone_state_end_edit();
}

void set_update_status(UpdateStatus status, const char *message, int percent = 0)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->update_status = status;
    state->update_percent = percent;
    if (message != nullptr) {
        evb_copy_text(state->update_message, sizeof(state->update_message), message);
    }
    evb_phone_state_end_edit();
}

bool has_ota_partition(void)
{
    return esp_ota_get_next_update_partition(NULL) != NULL;
}

/* ---------- joining ---------- */

void join(const char *ssid, const char *password)
{
    WiFi.disconnect(false, false);
    WiFi.begin(ssid, password);
    wifi.joining = true;
    wifi.join_started_ms = millis();
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = WifiStatus::Connecting;
    evb_copy_text(state->wifi_ssid, sizeof(state->wifi_ssid), ssid);
    evb_phone_state_end_edit();
}

/* Writing flash pauses PSRAM, which shakes the screen, so it is written only when it changed. */
void save_network(const char *ssid, const char *password)
{
    Preferences preferences;
    preferences.begin("evb-wifi", false);
    if (preferences.getString("ssid", "") == ssid && preferences.getString("password", "") == password) {
        preferences.end();
        return;
    }
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();
}

void forget_network(void)
{
    Preferences preferences;
    preferences.begin("evb-wifi", false);
    preferences.clear();
    preferences.end();
    WiFi.disconnect(false, true);
    wifi.joining = false;
    wifi.save_network_when_joined = false;
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = WifiStatus::NoNetworkSaved;
    state->wifi_ssid[0] = '\0';
    state->wifi_ip[0] = '\0';
    evb_phone_state_end_edit();
}

void join_saved_network(void)
{
    Preferences preferences;
    preferences.begin("evb-wifi", true);
    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    preferences.end();
    if (ssid.length() == 0) {
        set_status(WifiStatus::NoNetworkSaved);
        return;
    }
    join(ssid.c_str(), password.c_str());
}

void scan_networks(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_scanning = true;
    evb_phone_state_end_edit();

    int found = WiFi.scanNetworks(false, false);
    WifiNetwork networks[EVB_WIFI_LIST_MAX];
    int count = 0;
    for (int i = 0; i < found; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) {
            continue;
        }
        int existing = -1;
        for (int j = 0; j < count; j++) {
            if (strcmp(networks[j].ssid, ssid.c_str()) == 0) {
                existing = j;
            }
        }
        int rssi = WiFi.RSSI(i);
        if (existing >= 0) {
            if (rssi > networks[existing].rssi) {
                networks[existing].rssi = rssi;
            }
            continue;
        }
        if (count == EVB_WIFI_LIST_MAX) {
            /* Keep the strongest ones. */
            int weakest = 0;
            for (int j = 1; j < count; j++) {
                if (networks[j].rssi < networks[weakest].rssi) {
                    weakest = j;
                }
            }
            if (rssi <= networks[weakest].rssi) {
                continue;
            }
            existing = weakest;
        } else {
            existing = count++;
        }
        evb_copy_text(networks[existing].ssid, sizeof(networks[existing].ssid), ssid.c_str());
        networks[existing].rssi = rssi;
        networks[existing].secured = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();

    for (int i = 1; i < count; i++) {
        for (int j = i; j > 0 && networks[j].rssi > networks[j - 1].rssi; j--) {
            WifiNetwork swap = networks[j];
            networks[j] = networks[j - 1];
            networks[j - 1] = swap;
        }
    }

    state = evb_phone_state_begin_edit();
    state->wifi_scanning = false;
    state->wifi_network_count = count;
    memcpy(state->wifi_networks, networks, sizeof(WifiNetwork) * (size_t)count);
    evb_phone_state_end_edit();
}

/* ---------- internet services ---------- */

void on_time_synced(struct timeval *time)
{
    (void)time;
    evb_clock_mark_synced("Internet");
}

void start_network_upload(void)
{
    if (wifi.network_upload_started || !has_ota_partition()) {
        return;
    }
    ArduinoOTA.setHostname(EVB_NETWORK_UPLOAD_HOSTNAME);
    ArduinoOTA.setPassword(EVB_NETWORK_UPLOAD_PASSWORD);
    ArduinoOTA.onStart([]() { set_update_status(UpdateStatus::Downloading, "Receiving from Arduino IDE", 0); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        set_update_status(UpdateStatus::Downloading, nullptr, total > 0 ? (int)(done * 100ULL / total) : 0);
    });
    ArduinoOTA.onEnd([]() { set_update_status(UpdateStatus::Done, "Installed. Restarting...", 100); });
    ArduinoOTA.onError([](ota_error_t error) {
        (void)error;
        set_update_status(UpdateStatus::Failed, "Network upload failed");
    });
    ArduinoOTA.begin();
    wifi.network_upload_started = true;

    PhoneState *state = evb_phone_state_begin_edit();
    state->network_upload_ready = true;
    evb_phone_state_end_edit();
}

void start_internet_services(void)
{
    char tz[24];
    evb_clock_posix_timezone(tz, sizeof(tz));
    sntp_set_time_sync_notification_cb(on_time_synced);
    configTzTime(tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    start_network_upload();
    wifi.internet_services_started = true;
    wifi.next_weather_ms = millis();
}

bool http_get(const String &url, String *body)
{
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    bool started;
    NetworkClientSecure secure;
    NetworkClient plain;
    if (url.startsWith("https://")) {
        /* No certificate store on the cluster: the data fetched this way is not secret. */
        secure.setInsecure();
        started = http.begin(secure, url);
    } else {
        started = http.begin(plain, url);
    }
    if (!started) {
        return false;
    }
    int code = http.GET();
    bool ok = code == HTTP_CODE_OK;
    if (ok) {
        *body = http.getString();
    }
    http.end();
    return ok;
}

bool json_number(const String &json, const char *key, int from, float *value)
{
    String pattern = String("\"") + key + "\":";
    int at = json.indexOf(pattern, from);
    if (at < 0) {
        return false;
    }
    *value = strtof(json.c_str() + at + pattern.length(), nullptr);
    return true;
}

bool json_text(const String &json, const char *key, String *value)
{
    String pattern = String("\"") + key + "\":\"";
    int at = json.indexOf(pattern);
    if (at < 0) {
        return false;
    }
    int start = at + pattern.length();
    int end = json.indexOf('"', start);
    if (end < 0) {
        return false;
    }
    *value = json.substring(start, end);
    return true;
}

/* Location from the internet address (ip-api.com), temperature from open-meteo.com; neither needs a key. */
bool update_weather(void)
{
    String body;
    if (!wifi.location_known) {
        if (!http_get("http://ip-api.com/json/?fields=status,city,lat,lon", &body)) {
            return false;
        }
        String city;
        if (!json_number(body, "lat", 0, &wifi.latitude) || !json_number(body, "lon", 0, &wifi.longitude)) {
            return false;
        }
        json_text(body, "city", &city);
        wifi.location_known = true;
        PhoneState *state = evb_phone_state_begin_edit();
        evb_copy_text(state->weather_place, sizeof(state->weather_place), city.c_str());
        evb_phone_state_end_edit();
    }
    char url[160];
    snprintf(url, sizeof(url), "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m",
             wifi.latitude, wifi.longitude);
    if (!http_get(url, &body)) {
        return false;
    }
    int current = body.indexOf("\"current\":");
    float temperature = 0.0f;
    if (current < 0 || !json_number(body, "temperature_2m", current, &temperature)) {
        return false;
    }
    PhoneState *state = evb_phone_state_begin_edit();
    state->weather_valid = true;
    state->weather_temp_c = (int)(temperature + (temperature >= 0 ? 0.5f : -0.5f));
    evb_phone_state_end_edit();
    return true;
}

/* ---------- firmware updates ---------- */

bool is_newer_version(const char *offered, const char *running)
{
    for (int part = 0; part < 3; part++) {
        long a = strtol(offered, (char **)&offered, 10);
        long b = strtol(running, (char **)&running, 10);
        if (a != b) {
            return a > b;
        }
        if (*offered == '.') {
            offered++;
        }
        if (*running == '.') {
            running++;
        }
    }
    return false;
}

void check_for_update(void)
{
    if (strlen(EVB_UPDATE_MANIFEST_URL) == 0) {
        set_update_status(UpdateStatus::NoServer, "No update server set (evb_firmware_info.h)");
        return;
    }
    if (!has_ota_partition()) {
        set_update_status(UpdateStatus::NoOtaPartition, "Choose a partition scheme with OTA");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        set_update_status(UpdateStatus::Failed, "Connect to Wi-Fi first");
        return;
    }
    set_update_status(UpdateStatus::Checking, "Checking...");
    String body;
    String version;
    String url;
    if (!http_get(EVB_UPDATE_MANIFEST_URL, &body) || !json_text(body, "version", &version) || !json_text(body, "url", &url)) {
        set_update_status(UpdateStatus::Failed, "Could not read the update server");
        return;
    }
    PhoneState *state = evb_phone_state_begin_edit();
    evb_copy_text(state->update_version, sizeof(state->update_version), version.c_str());
    evb_phone_state_end_edit();
    if (!is_newer_version(version.c_str(), EVB_FIRMWARE_VERSION)) {
        set_update_status(UpdateStatus::UpToDate, "This is the newest firmware");
        return;
    }
    wifi.update_url = url;
    set_update_status(UpdateStatus::Available, "A new firmware is ready to install");
}

void install_update(void)
{
    if (wifi.update_url.length() == 0 || WiFi.status() != WL_CONNECTED) {
        set_update_status(UpdateStatus::Failed, "Check for updates first");
        return;
    }
    set_update_status(UpdateStatus::Downloading, "Downloading...", 0);
    httpUpdate.rebootOnUpdate(false);
    httpUpdate.onProgress([](int done, int total) {
        set_update_status(UpdateStatus::Downloading, nullptr, total > 0 ? (int)(done * 100LL / total) : 0);
    });
    HTTPUpdateResult result;
    if (wifi.update_url.startsWith("https://")) {
        NetworkClientSecure client;
        client.setInsecure();
        result = httpUpdate.update(client, wifi.update_url);
    } else {
        NetworkClient client;
        result = httpUpdate.update(client, wifi.update_url);
    }
    if (result != HTTP_UPDATE_OK) {
        set_update_status(UpdateStatus::Failed, "Download failed");
        return;
    }
    set_update_status(UpdateStatus::Done, "Installed. Restarting...", 100);
    delay(1500);
    ESP.restart();
}

/* ---------- task ---------- */

void follow_connection(uint32_t now_ms)
{
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected && !wifi.was_connected) {
        wifi.joining = false;
        PhoneState *state = evb_phone_state_begin_edit();
        state->wifi_status = WifiStatus::Connected;
        evb_copy_text(state->wifi_ssid, sizeof(state->wifi_ssid), WiFi.SSID().c_str());
        evb_copy_text(state->wifi_ip, sizeof(state->wifi_ip), WiFi.localIP().toString().c_str());
        state->wifi_rssi = WiFi.RSSI();
        evb_phone_state_end_edit();
        if (wifi.save_network_when_joined) {
            wifi.save_network_when_joined = false;
            save_network(wifi.joining_ssid, wifi.joining_password);
        }
        Serial.printf("Wi-Fi joined %s, IP %s. Internal RAM free %u bytes, largest block %u bytes\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        if (!wifi.internet_services_started) {
            start_internet_services();
        }
        wifi.next_weather_ms = now_ms;
    } else if (!connected && wifi.was_connected) {
        set_status(WifiStatus::Connecting);
    }
    wifi.was_connected = connected;

    if (wifi.joining && !connected && now_ms - wifi.join_started_ms > CONNECT_TIMEOUT_MS) {
        wifi.joining = false;
        wifi.save_network_when_joined = false;
        set_status(WifiStatus::Failed);
    }
    if (connected && (int32_t)(now_ms - wifi.next_signal_ms) >= 0) {
        wifi.next_signal_ms = now_ms + SIGNAL_EVERY_MS;
        PhoneState *state = evb_phone_state_begin_edit();
        state->wifi_rssi = WiFi.RSSI();
        evb_phone_state_end_edit();
    }
}

void handle(const Request &request)
{
    switch (request.type) {
    case RequestType::Scan:
        scan_networks();
        break;
    case RequestType::Connect:
        join(request.ssid, request.password);
        wifi.save_network_when_joined = true;
        evb_copy_text(wifi.joining_ssid, sizeof(wifi.joining_ssid), request.ssid);
        evb_copy_text(wifi.joining_password, sizeof(wifi.joining_password), request.password);
        break;
    case RequestType::Forget:
        forget_network();
        break;
    case RequestType::CheckForUpdate:
        check_for_update();
        break;
    case RequestType::InstallUpdate:
        install_update();
        break;
    }
}

void run_wifi_task(void *arg)
{
    (void)arg;
    /* Saved by this file in "evb-wifi"; stops the Wi-Fi driver writing flash on every join. */
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(EVB_NETWORK_UPLOAD_HOSTNAME);
    WiFi.setAutoReconnect(true);
    join_saved_network();
    if (strlen(EVB_UPDATE_MANIFEST_URL) == 0) {
        set_update_status(UpdateStatus::NoServer, "No update server set (evb_firmware_info.h)");
    }
    if (!has_ota_partition()) {
        set_update_status(UpdateStatus::NoOtaPartition, "Choose a partition scheme with OTA");
    }

    for (;;) {
        Request request;
        if (xQueueReceive(wifi.requests, &request, pdMS_TO_TICKS(50)) == pdTRUE) {
            handle(request);
        }
        uint32_t now_ms = millis();
        follow_connection(now_ms);
        if (WiFi.status() == WL_CONNECTED) {
            if (wifi.network_upload_started) {
                ArduinoOTA.handle();
            }
            if ((int32_t)(now_ms - wifi.next_weather_ms) >= 0) {
                wifi.next_weather_ms = now_ms + (update_weather() ? WEATHER_EVERY_MS : WEATHER_RETRY_MS);
            }
        }
    }
}

void post(const Request &request)
{
    if (wifi.requests != NULL) {
        xQueueSend(wifi.requests, &request, 0);
    }
}

} // namespace

void evb_wifi_start(void)
{
    wifi.requests = xQueueCreate(4, sizeof(Request));
    xTaskCreatePinnedToCore(run_wifi_task, "evb_wifi", 12 * 1024, NULL, 1, NULL, 0);
}

void evb_wifi_scan(void)
{
    Request request = {RequestType::Scan, "", ""};
    post(request);
}

void evb_wifi_connect(const char *ssid, const char *password)
{
    Request request = {RequestType::Connect, "", ""};
    evb_copy_text(request.ssid, sizeof(request.ssid), ssid);
    evb_copy_text(request.password, sizeof(request.password), password);
    post(request);
}

void evb_wifi_forget(void)
{
    Request request = {RequestType::Forget, "", ""};
    post(request);
}

void evb_wifi_check_for_update(void)
{
    Request request = {RequestType::CheckForUpdate, "", ""};
    post(request);
}

void evb_wifi_install_update(void)
{
    Request request = {RequestType::InstallUpdate, "", ""};
    post(request);
}

#else /* PC build: a pretend network list so the screens can be tested. */

void evb_wifi_start(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = WifiStatus::NoNetworkSaved;
    state->update_status = UpdateStatus::NoServer;
    evb_copy_text(state->update_message, sizeof(state->update_message), "No update server set (evb_firmware_info.h)");
    evb_phone_state_end_edit();
}

void evb_wifi_scan(void)
{
    static const WifiNetwork pretend[] = {{"Home_5G", -48, true}, {"EVBikes-Workshop", -61, true}, {"Cafe Free WiFi", -77, false}};
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_network_count = 3;
    memcpy(state->wifi_networks, pretend, sizeof(pretend));
    evb_phone_state_end_edit();
}

void evb_wifi_connect(const char *ssid, const char *password)
{
    (void)password;
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = WifiStatus::Connected;
    evb_copy_text(state->wifi_ssid, sizeof(state->wifi_ssid), ssid);
    evb_copy_text(state->wifi_ip, sizeof(state->wifi_ip), "192.168.1.42");
    state->wifi_rssi = -52;
    state->weather_valid = true;
    state->weather_temp_c = 24;
    evb_copy_text(state->weather_place, sizeof(state->weather_place), "Bengaluru");
    state->network_upload_ready = true;
    evb_phone_state_end_edit();
    evb_clock_mark_synced("Internet");
}

void evb_wifi_forget(void)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->wifi_status = WifiStatus::NoNetworkSaved;
    state->wifi_ssid[0] = '\0';
    evb_phone_state_end_edit();
}

void evb_wifi_check_for_update(void)
{
}

void evb_wifi_install_update(void)
{
}

#endif
