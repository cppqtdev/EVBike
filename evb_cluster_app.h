#pragma once

/* Runs the whole cluster: start animation, unlock, pre-ride check, then the ride screen and
 * its settings menu, fed by the vehicle simulator. Call once with the LVGL lock held. */
void evb_cluster_app_start(void);
