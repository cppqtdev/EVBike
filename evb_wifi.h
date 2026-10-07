#pragma once

/*
 * Wi-Fi: joins the saved network, sets the clock from the internet, fetches the outside
 * temperature, and installs firmware updates (from the Arduino IDE network port, or from
 * an update server set in evb_firmware_info.h). All network work runs in its own task;
 * these calls only queue a request and never block the screen.
 */

/* Starts the Wi-Fi task and joins the saved network. Call once from setup(). */
void evb_wifi_start(void);

void evb_wifi_scan(void);
void evb_wifi_connect(const char *ssid, const char *password);
void evb_wifi_forget(void);
void evb_wifi_check_for_update(void);
void evb_wifi_install_update(void);
