#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =========================================================
// STATE MACHINE (Aligned with Flowchart)
// =========================================================
typedef enum {
    pomoTimer_IDLE,
    pomoTimer_COUNTING,
    pomoTimer_STOP,       // Flowchart names the paused state as "STOP"
    pomoTimer_COMPLETED
} pomotimerState_t;

// =========================================================
// DATA MODEL (Aligned with Flowchart)
// =========================================================
typedef struct {
    char timerID[32];          // Unique ID for session tracking
    int focusDuration;         // Target duration in minutes
    int remaining_secs;        // Live countdown (persists across screen navigation)
    double pomoCount;          // Total accumulated pomodoros
    char classTag[32];         // "Coding", "Reading", etc.
    char dateAndTime[32];      // Timestamp string
    bool isCompleted;          // Flag for SD Card save trigger
    pomotimerState_t timerState;
} pomoTimer_t;

// Global active session data exposed to the rest of the application
extern pomoTimer_t active_session;

// =========================================================
// CONTROL API (thread-safe; callable from any task)
// =========================================================

// Start a fresh focus session. `tag` may be NULL (keep current tag);
// `duration_min` <= 0 keeps the current duration, otherwise it is clamped
// to 5..120 and rounded to the nearest 5-minute step.
esp_err_t pomo_start_session(const char *tag, int duration_min);

// Pause a running session (COUNTING -> PAUSED). No-op otherwise.
esp_err_t pomo_pause_session(void);

// Resume a paused session (PAUSED -> COUNTING). No-op otherwise.
esp_err_t pomo_resume_session(void);

// Abort/clear the current session back to IDLE with a full duration.
esp_err_t pomo_reset_session(void);

// Snapshot the current session state into `out`.
esp_err_t pomo_get_state(pomoTimer_t *out);

// Advance the countdown by one second. Returns true on the tick in which
// a session reaches zero (transitioned to COMPLETED). Call once per second
// from the LVGL ticker.
bool pomo_tick(void);

#ifdef __cplusplus
}
#endif