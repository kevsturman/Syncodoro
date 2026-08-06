#include "pomo_model.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <time.h>

static const char *TAG = "pomo_model";

// =========================================================
// GLOBAL SESSION STATE
// =========================================================
// This holds the actual data in RAM during a timer session.
// When the timer hits 0, this struct will be passed to your
// SD card logic (and eventually Notion sync).

pomoTimer_t active_session = {
    .timerID = "session_001",
    .focusDuration = 20,
    .remaining_secs = 20 * 60,
    .pomoCount = 0.0,
    .classTag = "Focus",   /* Fallback; overwritten at boot with first tag from list */
    .dateAndTime = "--:--",
    .isCompleted = false,
    .timerState = pomoTimer_IDLE
};

// =========================================================
// CONCURRENCY
// The HTTP server (webhook) and the LVGL ticker run in different
// tasks, so every model mutation is guarded by this mutex.
// =========================================================

static SemaphoreHandle_t s_mutex = NULL;

static void model_lock(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void model_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

// =========================================================
// INTERNAL HELPERS
// =========================================================

// Stamp the session start time in UTC (YYYY-MM-DD HH:MM).
// gmtime_r is always UTC regardless of TZ setting, so no timezone
// configuration is needed. The year check guards against using the
// epoch (1970-01-01) before SNTP has synced.
static void stamp_start_time(void)
{
    time_t now = time(NULL);
    struct tm ts;
    gmtime_r(&now, &ts);
    if (ts.tm_year > 70) {
        strftime(active_session.dateAndTime,
                 sizeof(active_session.dateAndTime),
                 "%Y-%m-%d %H:%M", &ts);
    } else {
        strlcpy(active_session.dateAndTime, "unknown",
                sizeof(active_session.dateAndTime));
    }
}

// =========================================================
// CONTROL API
// =========================================================

esp_err_t pomo_start_session(const char *tag, int duration_min)
{
    model_lock();

    if (duration_min >= 5 && duration_min <= 120) {
        // Round to the nearest 5-minute step to match the on-device arrows.
        int rounded = ((duration_min + 2) / 5) * 5;
        if (rounded < 5) rounded = 5;
        if (rounded > 120) rounded = 120;
        active_session.focusDuration = rounded;
    }

    if (tag != NULL && tag[0] != '\0') {
        strlcpy(active_session.classTag, tag, sizeof(active_session.classTag));
    }

    active_session.remaining_secs = active_session.focusDuration * 60;
    active_session.isCompleted = false;
    stamp_start_time();
    active_session.timerState = pomoTimer_COUNTING;

    model_unlock();
    ESP_LOGI(TAG, "Session started: %d min, tag=%s",
             active_session.focusDuration, active_session.classTag);
    return ESP_OK;
}

esp_err_t pomo_pause_session(void)
{
    model_lock();
    if (active_session.timerState == pomoTimer_COUNTING) {
        active_session.timerState = pomoTimer_STOP;
        ESP_LOGI(TAG, "Session paused");
    }
    model_unlock();
    return ESP_OK;
}

esp_err_t pomo_resume_session(void)
{
    model_lock();
    if (active_session.timerState == pomoTimer_STOP) {
        active_session.timerState = pomoTimer_COUNTING;
        ESP_LOGI(TAG, "Session resumed");
    }
    model_unlock();
    return ESP_OK;
}

esp_err_t pomo_reset_session(void)
{
    model_lock();
    active_session.timerState = pomoTimer_IDLE;
    active_session.isCompleted = false;
    active_session.remaining_secs = active_session.focusDuration * 60;
    model_unlock();
    ESP_LOGI(TAG, "Session reset");
    return ESP_OK;
}

esp_err_t pomo_get_state(pomoTimer_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    model_lock();
    *out = active_session;
    model_unlock();
    return ESP_OK;
}

bool pomo_tick(void)
{
    bool completed = false;

    model_lock();
    if (active_session.timerState == pomoTimer_COUNTING) {
        if (active_session.remaining_secs > 0) {
            active_session.remaining_secs--;
        }
        if (active_session.remaining_secs <= 0) {
            active_session.timerState = pomoTimer_COMPLETED;
            active_session.isCompleted = true;
            active_session.pomoCount += 1.0;
            completed = true;
            ESP_LOGI(TAG, "Session completed (%s, %d min)",
                     active_session.classTag, active_session.focusDuration);
        }
    }
    model_unlock();

    return completed;
}
