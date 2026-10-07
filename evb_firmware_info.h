#pragma once

/* Shown on the System page and compared with the update server's version. */
#define EVB_FIRMWARE_VERSION "1.3.0"

/*
 * Update server: a JSON file such as
 *   {"version": "1.4.0", "url": "https://example.com/evbikes/firmware.bin"}
 * Leave empty to use only the Arduino IDE network upload.
 * In the Arduino IDE use Sketch > Export Compiled Binary to get firmware.bin.
 */
#define EVB_UPDATE_MANIFEST_URL ""

/* Name and password of the Arduino IDE network upload (Tools > Port > evbikes-cluster). */
#define EVB_NETWORK_UPLOAD_HOSTNAME "evbikes-cluster"
#define EVB_NETWORK_UPLOAD_PASSWORD "evbikes"
