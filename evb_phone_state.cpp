#include "evb_phone_state.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

namespace {

PhoneState shared_state;

#ifdef ESP_PLATFORM
SemaphoreHandle_t state_lock = NULL;

void lock_state(void)
{
    if (state_lock == NULL) {
        static StaticSemaphore_t lock_storage;
        state_lock = xSemaphoreCreateMutexStatic(&lock_storage);
    }
    xSemaphoreTake(state_lock, portMAX_DELAY);
}

void unlock_state(void)
{
    xSemaphoreGive(state_lock);
}
#else
void lock_state(void)
{
}

void unlock_state(void)
{
}
#endif

} // namespace

void evb_phone_state_read(PhoneState *copy)
{
    lock_state();
    *copy = shared_state;
    unlock_state();
}

PhoneState *evb_phone_state_begin_edit(void)
{
    lock_state();
    return &shared_state;
}

void evb_phone_state_end_edit(void)
{
    shared_state.revision++;
    unlock_state();
}

void evb_copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) {
        return;
    }
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    size_t length = strnlen(source, capacity - 1);
    /* Do not cut a UTF-8 character in half. */
    while (length > 0 && length < strlen(source) && ((unsigned char)source[length] & 0xC0) == 0x80) {
        length--;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}
