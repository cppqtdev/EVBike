#include "evb_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "evb_phone_state.h"

#ifdef ESP_PLATFORM
#include <Preferences.h>
#endif

namespace {

constexpr int DEFAULT_UTC_OFFSET_MINUTES = 330; /* India */
constexpr int MIN_UTC_OFFSET_MINUTES = -12 * 60;
constexpr int MAX_UTC_OFFSET_MINUTES = 14 * 60;

int utc_offset_minutes = DEFAULT_UTC_OFFSET_MINUTES;
bool clock_synced = false;

void apply_timezone(void)
{
    char tz[24];
    evb_clock_posix_timezone(tz, sizeof(tz));
    setenv("TZ", tz, 1);
    tzset();
}

void publish(const char *source)
{
    PhoneState *state = evb_phone_state_begin_edit();
    state->utc_offset_minutes = utc_offset_minutes;
    state->clock_synced = clock_synced;
    if (source != NULL) {
        evb_copy_text(state->clock_source, sizeof(state->clock_source), source);
    }
    evb_phone_state_end_edit();
}

void save_offset(void)
{
#ifdef ESP_PLATFORM
    Preferences preferences;
    preferences.begin("evb-clock", false);
    preferences.putInt("utc_offset", utc_offset_minutes);
    preferences.end();
#endif
}

} // namespace

void evb_clock_begin(void)
{
#ifdef ESP_PLATFORM
    Preferences preferences;
    preferences.begin("evb-clock", true);
    utc_offset_minutes = preferences.getInt("utc_offset", DEFAULT_UTC_OFFSET_MINUTES);
    preferences.end();
#endif
    apply_timezone();
    publish("");
}

void evb_clock_set_from_utc(uint32_t unix_seconds, const char *source)
{
    struct timeval now = {(time_t)unix_seconds, 0};
    settimeofday(&now, NULL);
    clock_synced = true;
    publish(source);
}

void evb_clock_mark_synced(const char *source)
{
    clock_synced = true;
    publish(source);
}

int evb_clock_utc_offset_minutes(void)
{
    return utc_offset_minutes;
}

void evb_clock_set_utc_offset(int minutes)
{
    if (minutes < MIN_UTC_OFFSET_MINUTES) {
        minutes = MIN_UTC_OFFSET_MINUTES;
    }
    if (minutes > MAX_UTC_OFFSET_MINUTES) {
        minutes = MAX_UTC_OFFSET_MINUTES;
    }
    if (minutes == utc_offset_minutes) {
        return;
    }
    utc_offset_minutes = minutes;
    apply_timezone();
    save_offset();
    publish(NULL);
}

void evb_clock_posix_timezone(char *text, size_t capacity)
{
    /* POSIX writes the offset with the opposite sign: UTC+05:30 is "-05:30". */
    int offset = utc_offset_minutes;
    char east_sign = offset >= 0 ? '+' : '-';
    char posix_sign = offset >= 0 ? '-' : '+';
    int magnitude = offset >= 0 ? offset : -offset;
    snprintf(text, capacity, "<%c%02d%02d>%c%02d:%02d", east_sign, magnitude / 60, magnitude % 60, posix_sign, magnitude / 60, magnitude % 60);
}

bool evb_clock_read_local(int *hours, int *minutes)
{
    if (!clock_synced) {
        return false;
    }
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    *hours = local.tm_hour;
    *minutes = local.tm_min;
    return true;
}
