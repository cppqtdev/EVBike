#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Wall clock. The board has no battery-backed clock chip, so the time comes from the
 * internet (NTP over Wi-Fi), from an iPhone (Current Time Service) or from the EVBikes
 * app. The clock stays hidden until one of them has set it.
 */

/* Loads the saved time zone. Call once at start-up. */
void evb_clock_begin(void);

void evb_clock_set_from_utc(uint32_t unix_seconds, const char *source);
void evb_clock_mark_synced(const char *source);

int evb_clock_utc_offset_minutes(void);
void evb_clock_set_utc_offset(int minutes);

/* POSIX TZ text for the current offset, e.g. "<+0530>-05:30" for India. */
void evb_clock_posix_timezone(char *text, size_t capacity);

/* False until the clock has been set from a real source. */
bool evb_clock_read_local(int *hours, int *minutes);
