#include "ui_timer.h"
#include "ui_theme.h"
#include "ui_settings.h"
#include "ui_history.h"
#include "pomo_model.h"
#include "session_csv.h"
#include "nvs_config.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_timer";

LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_16);

// =========================================================
// UI STATE
// =========================================================
typedef struct {
    lv_obj_t *arc_progress;
    lv_obj_t *label_time;
    lv_obj_t *label_tag;
    lv_obj_t *label_status;
    lv_obj_t *arrow_left;
    lv_obj_t *arrow_right;
    lv_obj_t *btn_settings;   // Hidden while timer is running (clean UX)
    lv_obj_t *btn_history;
    lv_timer_t *lv_tick;       // Persistent LVGL tick timer
} pomo_ui_t;

static pomo_ui_t pui = {0};
static lv_obj_t *modal_overlay = NULL;

// Forward declarations
static void update_time_display(void);
static void update_ui_state(void);
static void show_done_modal(void);
static void clear_pui_widgets(void);

// =========================================================
// TAG SELECTION MODAL
// =========================================================
static void modal_tag_cancel_cb(lv_event_t *e)
{
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }
}

static void modal_tag_select_cb(lv_event_t *e)
{
    static const char *FALLBACK_TAG = "Focus";
    lv_obj_t *roller = lv_event_get_user_data(e);
    if (roller) {
        lv_roller_get_selected_str(roller, active_session.classTag, sizeof(active_session.classTag));
        if (active_session.classTag[0] == '\0') {
            strlcpy(active_session.classTag, FALLBACK_TAG, sizeof(active_session.classTag));
        }
        if (pui.label_tag) lv_label_set_text(pui.label_tag, active_session.classTag);
    }
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }
}

static void show_tag_modal(void)
{
    modal_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(modal_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(modal_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal_overlay, 0, 0);
    lv_obj_set_style_radius(modal_overlay, 0, 0);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(modal_overlay);
    lv_obj_set_size(panel, 260, 210);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, OVERTEC_BG_SURFACE, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);

    lv_obj_t *label_title = lv_label_create(panel);
    lv_label_set_text(label_title, "Select Tag");
    lv_obj_set_style_text_color(label_title, OVERTEC_TEXT_PRIMARY, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 0);

    // Load tags from NVS; fall back to built-in list when none are saved yet.
    // Never pass an empty list to the roller (avoids NULL/empty selection errors).
    static const char *DEFAULT_TAGS = "Coding\nReading\nWriting\nDesign\nGaming\nStudying";
    static const char *FALLBACK_TAG  = "Focus\n";
    char tag_buf[512];
    const char *tag_options = DEFAULT_TAGS;
    if (nvs_config_get_tags(tag_buf, sizeof(tag_buf)) == ESP_OK && tag_buf[0] != '\0' && tag_buf[0] != '\n') {
        tag_options = tag_buf;
    }
    if (tag_options[0] == '\0' || tag_options[0] == '\n') {
        tag_options = FALLBACK_TAG;
    }

    lv_obj_t *roller = lv_roller_create(panel);
    lv_roller_set_options(roller, tag_options, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(roller, OVERTEC_BG_MAIN, 0);
    lv_obj_set_style_text_color(roller, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_set_style_text_color(roller, OVERTEC_TEXT_PRIMARY, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(roller, OVERTEC_STATE_START, LV_PART_SELECTED);
    lv_roller_set_visible_row_count(roller, 4);
    lv_obj_set_width(roller, 200);
    lv_obj_align(roller, LV_ALIGN_TOP_MID, 0, 25);

    lv_obj_t *btn_cancel = lv_button_create(panel);
    lv_obj_set_size(btn_cancel, 85, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_cancel, OVERTEC_STATE_STOP, 0);
    lv_obj_add_event_cb(btn_cancel, modal_tag_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Cancel");
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_select = lv_button_create(panel);
    lv_obj_set_size(btn_select, 85, 40);
    lv_obj_align(btn_select, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_select, OVERTEC_STATE_START, 0);
    lv_obj_add_event_cb(btn_select, modal_tag_select_cb, LV_EVENT_CLICKED, roller);
    lv_obj_t *lbl_select = lv_label_create(btn_select);
    lv_label_set_text(lbl_select, "Save");
    lv_obj_center(lbl_select);
}

static void tag_clicked_cb(lv_event_t *e)
{
    if (active_session.timerState != pomoTimer_IDLE) return;
    show_tag_modal();
}

// =========================================================
// PAUSE MODAL
// =========================================================
static void modal_resume_cb(lv_event_t *e)
{
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }
    pomo_resume_session();
    update_ui_state();
}

static void modal_giveup_cb(lv_event_t *e)
{
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }
    pomo_reset_session();
    update_time_display();
    update_ui_state();
}

static void show_pause_modal(void)
{
    modal_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(modal_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(modal_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal_overlay, 0, 0);
    lv_obj_set_style_radius(modal_overlay, 0, 0);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(modal_overlay);
    lv_obj_set_size(panel, 260, 160);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, OVERTEC_BG_SURFACE, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);

    lv_obj_t *label = lv_label_create(panel);
    lv_label_set_text(label, "Session Paused\nFinish early?");
    lv_obj_set_style_text_color(label, OVERTEC_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn_resume = lv_button_create(panel);
    lv_obj_set_size(btn_resume, 100, 45);
    lv_obj_align(btn_resume, LV_ALIGN_BOTTOM_LEFT, 0, -10);
    lv_obj_set_style_bg_color(btn_resume, OVERTEC_STATE_START, 0);
    lv_obj_add_event_cb(btn_resume, modal_resume_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_resume = lv_label_create(btn_resume);
    lv_label_set_text(lbl_resume, "Resume");
    lv_obj_center(lbl_resume);

    lv_obj_t *btn_giveup = lv_button_create(panel);
    lv_obj_set_size(btn_giveup, 100, 45);
    lv_obj_align(btn_giveup, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
    lv_obj_set_style_bg_color(btn_giveup, OVERTEC_STATE_STOP, 0);
    lv_obj_add_event_cb(btn_giveup, modal_giveup_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_giveup = lv_label_create(btn_giveup);
    lv_label_set_text(lbl_giveup, "Give Up");
    lv_obj_center(lbl_giveup);
}

// =========================================================
// DONE MODAL (shown when timer hits 0)
// =========================================================
static void modal_save_cb(lv_event_t *e)
{
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }

    // Generate a unique session ID from the uptime counter
    snprintf(active_session.timerID, sizeof(active_session.timerID),
             "s%lld", esp_timer_get_time() / 1000000LL);

    esp_err_t ret = session_csv_append(&active_session);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "session_csv_append failed: %s", esp_err_to_name(ret));
    }

    // Reset model for next session
    pomo_reset_session();

    // Navigate to history so the user can immediately see the saved entry
    clear_pui_widgets();
    ui_history_load();
}

static void modal_discard_cb(lv_event_t *e)
{
    if (modal_overlay) { lv_obj_delete(modal_overlay); modal_overlay = NULL; }
    pomo_reset_session();
    update_time_display();
    update_ui_state();
}

static void show_done_modal(void)
{
    modal_overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(modal_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(modal_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(modal_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal_overlay, 0, 0);
    lv_obj_set_style_radius(modal_overlay, 0, 0);
    lv_obj_add_flag(modal_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *panel = lv_obj_create(modal_overlay);
    lv_obj_set_size(panel, 260, 175);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, OVERTEC_BG_SURFACE, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 12, 0);

    lv_obj_t *label = lv_label_create(panel);
    char buf[64];
    snprintf(buf, sizeof(buf), "Session Complete!\n%s  %d min",
             active_session.classTag, active_session.focusDuration);
    lv_label_set_text(label, buf);
    lv_obj_set_style_text_color(label, OVERTEC_TEXT_PRIMARY, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *lbl_hint = lv_label_create(panel);
    lv_label_set_text(lbl_hint, "Save to history?");
    lv_obj_set_style_text_color(lbl_hint, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_align(lbl_hint, LV_ALIGN_TOP_MID, 0, 72);

    lv_obj_t *btn_save = lv_button_create(panel);
    lv_obj_set_size(btn_save, 100, 45);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, 0, -10);
    lv_obj_set_style_bg_color(btn_save, OVERTEC_STATE_START, 0);
    lv_obj_add_event_cb(btn_save, modal_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, LV_SYMBOL_SAVE " Save");
    lv_obj_center(lbl_save);

    lv_obj_t *btn_discard = lv_button_create(panel);
    lv_obj_set_size(btn_discard, 100, 45);
    lv_obj_align(btn_discard, LV_ALIGN_BOTTOM_LEFT, 0, -10);
    lv_obj_set_style_bg_color(btn_discard, OVERTEC_STATE_STOP, 0);
    lv_obj_add_event_cb(btn_discard, modal_discard_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_discard = lv_label_create(btn_discard);
    lv_label_set_text(lbl_discard, "Discard");
    lv_obj_center(lbl_discard);
}

// =========================================================
// UI UPDATE HELPERS
// =========================================================
static void update_time_display(void)
{
    if (!pui.label_time) return;

    int mm = active_session.remaining_secs / 60;
    int ss = active_session.remaining_secs % 60;
    lv_label_set_text_fmt(pui.label_time, "%02d:%02d", mm, ss);

    if (pui.arc_progress) {
        int total = active_session.focusDuration * 60;
        lv_arc_set_range(pui.arc_progress, 0, total);
        lv_arc_set_value(pui.arc_progress, active_session.remaining_secs);
    }
}

static void update_ui_state(void)
{
    if (!pui.label_status) return;

    if (pui.label_tag) {
        lv_label_set_text(pui.label_tag,
                          active_session.classTag[0] != '\0' ? active_session.classTag : "Focus");
    }

    switch (active_session.timerState) {
        case pomoTimer_IDLE:
            lv_label_set_text(pui.label_status, "IDLE");
            lv_obj_set_style_text_color(pui.label_status, OVERTEC_TEXT_SECONDARY, 0);
            if (pui.arc_progress)
                lv_obj_set_style_arc_color(pui.arc_progress, OVERTEC_TEXT_SECONDARY, LV_PART_INDICATOR);
            if (pui.arrow_left)  lv_obj_remove_flag(pui.arrow_left,  LV_OBJ_FLAG_HIDDEN);
            if (pui.arrow_right) lv_obj_remove_flag(pui.arrow_right, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_settings) lv_obj_remove_flag(pui.btn_settings, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_history)  lv_obj_remove_flag(pui.btn_history,  LV_OBJ_FLAG_HIDDEN);
            break;

        case pomoTimer_COUNTING:
            lv_label_set_text(pui.label_status, "FOCUS");
            lv_obj_set_style_text_color(pui.label_status, OVERTEC_STATE_START, 0);
            if (pui.arc_progress)
                lv_obj_set_style_arc_color(pui.arc_progress, OVERTEC_STATE_START, LV_PART_INDICATOR);
            if (pui.arrow_left)  lv_obj_add_flag(pui.arrow_left,  LV_OBJ_FLAG_HIDDEN);
            if (pui.arrow_right) lv_obj_add_flag(pui.arrow_right, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_settings) lv_obj_add_flag(pui.btn_settings, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_history)  lv_obj_add_flag(pui.btn_history,  LV_OBJ_FLAG_HIDDEN);
            break;

        case pomoTimer_STOP:
            lv_label_set_text(pui.label_status, "PAUSED");
            lv_obj_set_style_text_color(pui.label_status, OVERTEC_STATE_PAUSE, 0);
            if (pui.arc_progress)
                lv_obj_set_style_arc_color(pui.arc_progress, OVERTEC_STATE_PAUSE, LV_PART_INDICATOR);
            if (pui.arrow_left)  lv_obj_add_flag(pui.arrow_left,  LV_OBJ_FLAG_HIDDEN);
            if (pui.arrow_right) lv_obj_add_flag(pui.arrow_right, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_settings) lv_obj_remove_flag(pui.btn_settings, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_history)  lv_obj_remove_flag(pui.btn_history,  LV_OBJ_FLAG_HIDDEN);
            break;

        case pomoTimer_COMPLETED:
            lv_label_set_text(pui.label_status, "DONE");
            lv_obj_set_style_text_color(pui.label_status, OVERTEC_STATE_STOP, 0);
            if (pui.arc_progress)
                lv_obj_set_style_arc_color(pui.arc_progress, OVERTEC_STATE_STOP, LV_PART_INDICATOR);
            if (pui.arrow_left)  lv_obj_add_flag(pui.arrow_left,  LV_OBJ_FLAG_HIDDEN);
            if (pui.arrow_right) lv_obj_add_flag(pui.arrow_right, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_settings) lv_obj_remove_flag(pui.btn_settings, LV_OBJ_FLAG_HIDDEN);
            if (pui.btn_history)  lv_obj_remove_flag(pui.btn_history,  LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

// =========================================================
// TOUCH EVENT CALLBACKS
// =========================================================
static void scr_clicked_cb(lv_event_t *e)
{
    switch (active_session.timerState) {
        case pomoTimer_IDLE:
            pomo_start_session(NULL, 0);
            break;
        case pomoTimer_COUNTING:
            pomo_pause_session();
            show_pause_modal();
            break;
        case pomoTimer_STOP:
            break;
        case pomoTimer_COMPLETED:
            break; // handled by done modal
    }
    update_ui_state();
}

static void arrow_left_cb(lv_event_t *e)
{
    if (active_session.timerState != pomoTimer_IDLE) return;
    if (active_session.focusDuration > 5) {
        active_session.focusDuration -= 5;
        active_session.remaining_secs = active_session.focusDuration * 60;
        update_time_display();
    }
}

static void arrow_right_cb(lv_event_t *e)
{
    if (active_session.timerState != pomoTimer_IDLE) return;
    if (active_session.focusDuration < 120) {
        active_session.focusDuration += 5;
        active_session.remaining_secs = active_session.focusDuration * 60;
        update_time_display();
    }
}

// =========================================================
// NAVIGATION CALLBACKS
// Nullify widget pointers before switching screens so the persistent
// lv_tick timer skips UI updates while off this screen.
// =========================================================
static void clear_pui_widgets(void)
{
    pui.arc_progress  = NULL;
    pui.label_time    = NULL;
    pui.label_tag     = NULL;
    pui.label_status  = NULL;
    pui.arrow_left    = NULL;
    pui.arrow_right   = NULL;
    pui.btn_settings  = NULL;
    pui.btn_history   = NULL;
}

static void nav_settings_cb(lv_event_t *e)
{
    clear_pui_widgets();
    ui_settings_load();
}

static void nav_history_cb(lv_event_t *e)
{
    clear_pui_widgets();
    ui_history_load();
}

// =========================================================
// BACKGROUND TICKER
// Runs continuously (even off-screen). UI updates are gated
// by non-null widget pointers.
// =========================================================
static void lv_tick_cb(lv_timer_t *t)
{
    bool just_completed = pomo_tick();

    // Update display only when widgets are valid (we're on the timer screen).
    // update_ui_state() runs every tick so state changes made remotely
    // (e.g. via the webhook API) are reflected on the device.
    if (pui.label_time) {
        update_time_display();
    }
    if (pui.label_status) {
        update_ui_state();
        if (just_completed) {
            show_done_modal();
        }
    }
}

// =========================================================
// SCREEN CREATION
// =========================================================
void ui_timer_load(void)
{
    // Ensure remaining_secs is initialised (first boot or after reset)
    if (active_session.remaining_secs <= 0) {
        active_session.remaining_secs = active_session.focusDuration * 60;
    }

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, OVERTEC_BG_MAIN, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    // Screen is NOT the click target — a circle overlay handles start/stop.

    // ── Circular progress arc ─────────────────────────────
    pui.arc_progress = lv_arc_create(scr);
    lv_obj_set_size(pui.arc_progress, 180, 180);
    lv_arc_set_rotation(pui.arc_progress, 270);
    lv_arc_set_bg_angles(pui.arc_progress, 0, 360);
    lv_obj_remove_style(pui.arc_progress, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(pui.arc_progress, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(pui.arc_progress, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(pui.arc_progress, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(pui.arc_progress, OVERTEC_BG_SURFACE, LV_PART_MAIN);
    lv_obj_align(pui.arc_progress, LV_ALIGN_CENTER, 0, -10);

    // ── Circular touch target (covers only the arc area) ──
    // Created right after the arc so that child widgets (tag label, time label)
    // that are clickable and created later sit on top in z-order and handle their
    // own events.  Transparent, no border — purely a touch region.
    lv_obj_t *touch_circle = lv_obj_create(scr);
    lv_obj_set_size(touch_circle, 180, 180);
    lv_obj_align(touch_circle, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_radius(touch_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(touch_circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_circle, 0, 0);
    lv_obj_remove_flag(touch_circle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(touch_circle, scr_clicked_cb, LV_EVENT_CLICKED, NULL);

    // ── Time label ────────────────────────────────────────
    pui.label_time = lv_label_create(scr);
    lv_obj_set_style_text_font(pui.label_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(pui.label_time, OVERTEC_TEXT_PRIMARY, 0);
    lv_obj_set_width(pui.label_time, 180);
    lv_obj_set_style_text_align(pui.label_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(pui.label_time, pui.arc_progress, LV_ALIGN_CENTER, 0, 10);

    // ── Tag label (clickable) ─────────────────────────────
    pui.label_tag = lv_label_create(scr);
    lv_label_set_text(pui.label_tag,
                      active_session.classTag[0] != '\0' ? active_session.classTag : "Focus");
    lv_obj_set_width(pui.label_tag, 180);
    lv_obj_set_style_text_align(pui.label_tag, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(pui.label_tag, OVERTEC_ACCENT, 0);
    lv_obj_align_to(pui.label_tag, pui.label_time, LV_ALIGN_OUT_TOP_MID, 0, -5);
    lv_obj_add_flag(pui.label_tag, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pui.label_tag, tag_clicked_cb, LV_EVENT_CLICKED, NULL);

    // ── Status label ──────────────────────────────────────
    pui.label_status = lv_label_create(scr);
    lv_obj_set_width(pui.label_status, 180);
    lv_obj_set_style_text_align(pui.label_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(pui.label_status, pui.arc_progress, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

    // ── Duration arrows ───────────────────────────────────
    pui.arrow_left = lv_label_create(scr);
    lv_label_set_text(pui.arrow_left, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(pui.arrow_left, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_add_flag(pui.arrow_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pui.arrow_left, arrow_left_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_all(pui.arrow_left, 30, 0);
    lv_obj_align(pui.arrow_left, LV_ALIGN_LEFT_MID, 20, -10);

    pui.arrow_right = lv_label_create(scr);
    lv_label_set_text(pui.arrow_right, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(pui.arrow_right, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_add_flag(pui.arrow_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pui.arrow_right, arrow_right_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_pad_all(pui.arrow_right, 30, 0);
    lv_obj_align(pui.arrow_right, LV_ALIGN_RIGHT_MID, -20, -10);

    // ── Navigation icon buttons (hidden while timer is running) ──
    pui.btn_settings = lv_button_create(scr);
    lv_obj_set_size(pui.btn_settings, 44, 38);
    lv_obj_align(pui.btn_settings, LV_ALIGN_TOP_LEFT, 4, 4);
    lv_obj_set_style_bg_color(pui.btn_settings, OVERTEC_BG_SURFACE, 0);
    lv_obj_set_style_bg_opa(pui.btn_settings, LV_OPA_80, 0);
    lv_obj_set_ext_click_area(pui.btn_settings, 10);
    lv_obj_add_event_cb(pui.btn_settings, nav_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_settings = lv_label_create(pui.btn_settings);
    lv_label_set_text(lbl_settings, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(lbl_settings, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_center(lbl_settings);

    pui.btn_history = lv_button_create(scr);
    lv_obj_set_size(pui.btn_history, 44, 38);
    lv_obj_align(pui.btn_history, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(pui.btn_history, OVERTEC_BG_SURFACE, 0);
    lv_obj_set_style_bg_opa(pui.btn_history, LV_OPA_80, 0);
    lv_obj_set_ext_click_area(pui.btn_history, 10);
    lv_obj_add_event_cb(pui.btn_history, nav_history_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_history = lv_label_create(pui.btn_history);
    lv_label_set_text(lbl_history, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(lbl_history, OVERTEC_TEXT_SECONDARY, 0);
    lv_obj_center(lbl_history);

    // ── Start the persistent tick timer (only once) ───────
    if (pui.lv_tick == NULL) {
        pui.lv_tick = lv_timer_create(lv_tick_cb, 1000, NULL);
    }

    // ── Restore display state from model ─────────────────
    update_time_display();
    update_ui_state();

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}
