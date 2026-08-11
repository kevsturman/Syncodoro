# Syncodoro Gets a Remote Control: A Developer Log

*How the ESP32 pomodoro timer gained a webhook API, survived a WiFi debugging saga, and ended up with a bash CLI.*

---

## The project

Syncodoro is a standalone Pomodoro focus timer built on the **ESP32-2432S028R** — the "Cheap Yellow Display" — with LVGL on a 2.8" resistive touchscreen. It tracks sessions, saves history to SPIFFS as CSV, syncs time over NTP, and runs an embedded web dashboard for browsing that history.

The catch? The timer was **only controllable from the touchscreen**. To start a session I had to be physically there. That felt wrong for a device designed to sit on a desk and be ignored, so the obvious next feature was: let me control it from anywhere on the LAN.

---

## Part 1 — The webhook API

### Making the model thread-safe first

The timer state lived in a global `pomoTimer_t active_session` struct that the LVGL ticker mutated directly every second. The webhook handlers run on the **HTTP server task**, the ticker runs on the **LVGL task** — two tasks touching the same struct with zero synchronization. That's a race waiting to corrupt the countdown.

> [!summary] Kev
> **What is LVGL?**
>
> **LVGL** (Light and Versatile Graphics Library) is a free, open-source C library for building GUIs on microcontrollers. It gives you ready-made widgets (labels, buttons, arcs, rollers, lists, modals), a theme system, touch input handling, and a rendering engine that draws into a framebuffer — all in C, tuned for the limited RAM of an MCU.
>
> In Syncodoro it's the entire display: the circular progress arc, the big `mm:ss` label, the clickable tag, the status text, the pause/done/tag modals — every one of those is an LVGL widget. The "LVGL ticker" mentioned in this post is just an LVGL timer (`lv_timer_create`) that calls a callback once per second, and `pui.*` are handles to the live LVGL objects on screen.
>
> Think of it as a mini-Qt/Tk for embedded: your code builds a tree of widgets, LVGL owns the screen and the event loop, and you get a callback when the user touches something.


So the first step was wrapping the model in a clean, mutex-guarded control API:

> [!summary] Kev
> **What is a mutex?** (I've seen them in Rust but need a refresher)
>
> Short for **mutual exclusion** — a lock that lets only one task/thread hold it at a time. Any other task that tries to take it *blocks* (waits) until it's released. That's how you make a shared memory region safe to touch from several threads.
>
> You've met them in Rust: `std::sync::Mutex<T>` + `.lock()` returns a `MutexGuard` that unlocks automatically when dropped (RAII). C has no such guard, so with FreeRTOS you manage it by hand:
>
> ```c
> xSemaphoreTake(mutex, portMAX_DELAY);   // acquire — blocks until free
> // ... mutate active_session ...
> xSemaphoreGive(mutex);                  // release — don't forget!
> ```
>
> `model_lock()` / `model_unlock()` exist precisely to wrap that so the release can't be forgotten. Why it matters here: without the mutex, the webhook handler (HTTP server task) and `pomo_tick()` (LVGL task) could touch `active_session` at the same instant — a data race where the countdown gets torn/corrupted.


```c
esp_err_t pomo_start_session(const char *tag, int duration_min);
esp_err_t pomo_pause_session(void);
esp_err_t pomo_resume_session(void);
esp_err_t pomo_reset_session(void);
esp_err_t pomo_get_state(pomoTimer_t *out);
bool pomo_tick(void);   // advance one second; true when a session completes
```
> [!summary] Kev
> **What is `esp_err_t`?**
>
> ESP-IDF's standard **error/result type**: `typedef int32_t esp_err_t`. `ESP_OK` is `0`, and any non-zero value is an error code (e.g. `ESP_ERR_INVALID_ARG`, `ESP_ERR_HTTPD_HANDLERS_FULL`). Functions return it so callers can check the outcome:
>
> ```c
> esp_err_t err = some_call();
> if (err != ESP_OK) { /* handle failure */ }
> ```
>
> Or `ESP_ERROR_CHECK(call())` — which aborts the program with a log line if the call fails (fine for setup steps that must succeed, not for runtime paths). `esp_err_to_name(err)` turns a numeric code back into a readable name, which is what the WiFi code uses when logging disconnect reasons.


Every mutation now takes a FreeRTOS mutex, and the LVGL ticker just calls `pomo_tick()` instead of poking `active_session` directly. Bonus: `stamp_start_time()` (the UTC timestamping with its `gmtime_r` epoch guard) moved out of the UI layer and into the model, where it belongs.

> [!summary] Kev
> **What is FreeRTOS?**
>
> A **real-time operating system (RTOS) for microcontrollers**. Its core job is preemptive multitasking: you create several *tasks* (threads), each with a priority, and the scheduler switches between them so they all appear to run at once. It also provides the synchronization primitives — mutexes, semaphores, queues, software timers.
>
> **ESP-IDF is built on FreeRTOS**, so everything in this project runs as one FreeRTOS task or another: the HTTP server has its own task, LVGL runs its loop on another, WiFi and NTP have theirs. Two of those tasks (HTTP server and LVGL ticker) both reach into `active_session` — hence the mutex in the model. The `freertos` entry added to `PRIV_REQUIRES` in `main/CMakeLists.txt` is what tells the build system to make FreeRTOS's headers/linkable code available to that component.


### The endpoints

Six new routes on the existing dashboard HTTP server:

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/timer/status` | GET | Current state, remaining seconds, duration, tag |
| `/api/timer/start` | POST | Start a session (`duration=<min>&tag=<name>` form body) |
| `/api/timer/pause` | POST | Pause a running session |
| `/api/timer/resume` | POST | Resume a paused session |
| `/api/timer/reset` | POST | Abort and clear back to idle |
| `/api/device/info` | GET | Device MAC, IP, and current timer state |

Every handler returns JSON, so `curl` is enough to drive the whole thing:

```bash
curl -X POST http://192.168.1.69/api/timer/start -d "duration=25&tag=Coding"
# {"ok":true,"state":"COUNTING","remaining_secs":1500,"focusDuration":25,"tag":"Coding","isCompleted":false}
```

The `max_uri_handlers` config had to be bumped from 10 to 12 to fit them all.

> [!summary] Kev
> **Is `max_uri_handlers` an ESP thing or a project level thing?**
>
> Both, in a way. The **field comes from ESP-IDF** — it's part of `httpd_config_t`, the config struct for ESP-IDF's `esp_http_server` component. It sets the size of the server's fixed-size URI→handler lookup table. If you try to register more handlers than it allows, `httpd_register_uri_handler()` returns `ESP_ERR_HTTPD_HANDLERS_FULL`.
>
> The **value is set per-project** in `http_server.c` when the server starts:
>
> ```c
> httpd_config_t config = HTTPD_DEFAULT_CONFIG();
> config.max_uri_handlers = 12;   // was 10 — we added 6 new routes
> ```
>
> The project had already bumped it from the default (8) to 10 before this work; adding the six `/api/timer/*` + `/api/device/info` routes overflowed it, so it went to 12.


### The UI now follows the model

Because the ticker now runs `update_ui_state()` on *every* tick (not just on state changes), anything the webhook does remotely is reflected on the display within a second. The device and the network view stay in sync — that one decision saved a lot of "but it shows the wrong thing" bugs down the line.

---

## Part 2 — The WiFi debugging saga

The webhook API is useless if the device can't stay on the network. Flashing the new firmware immediately uncovered a nasty crash:

### The duplicate-netif assert

Boot went straight into an ESP-IDF assert:

```
assert failed: esp_netif_create_default_wifi_sta
```

Calling `esp_netif_create_default_wifi_sta()` twice on the same STA interface is an assert-level error in ESP-IDF. On the button's second press, the WiFi manager was happily creating a duplicate default netif. Fix: guard it with a `s_sta_netif_created` flag, and make a second start just issue a manual `esp_wifi_connect()` reconnect instead.

### "Operation not permitted" is not the firmware's fault

While debugging I got so used to sandbox permission errors from my shell tooling that I almost mistook a `WIFI_EVENT_STA_DISCONNECTED` log for one. Adding the disconnect reason to the log turned the guessing game into an answer sheet:

- `reason=17` — `WIFI_REASON_IE_IN_4WAY_DIFFERS` → **wrong password / stale credentials** in NVS.
- `reason=201` — `WIFI_REASON_NO_AP_FOUND` → the phone hotspot was on **5 GHz**, and the ESP32 is 2.4 GHz only.

The ESP32 does not do 5 GHz. If the AP you're targeting is 5 GHz-only, no amount of retrying will help — switch the hotspot band.

### Finding the MAC (and learning what a MAC log isn't)

Trying to set up a DHCP reservation, I hit a wall: the device wasn't visible in the router's client list (it sits behind a WiFi **extender**, which often NATs the clients). I needed its MAC address.

First trap: the `<ba-add>` line in the boot log (e.g. `3c:84:6a:f6:3a:74`) looks like a MAC but is actually the **AP's** BSSID. Lesson: don't copy the first MAC-looking thing you see. I added `wifi_manager_get_mac()` and a `STA MAC:` log line to the boot output, plus the `/api/device/info` endpoint so the MAC is one curl away.

### Static IP: the DHCP-reservation detour

The "proper" answer to a stable IP is a DHCP reservation keyed on the MAC. But with the extender NATing clients, the main router never sees the device's MAC, and the extender's settings didn't expose a reservation UI. After enough digging, the pragmatic call won:

> Just flash a static IP. It's a desk ornament — I can always re-flash if it moves to a new network.

Four `#define`s at the top of `wifi_manager.c`:

```c
#define STA_STATIC_IP   "192.168.1.69"
#define STA_STATIC_NM   "255.255.255.0"
#define STA_STATIC_GW   "192.168.1.1"
#define STA_STATIC_DNS  "192.168.1.1"
```

Applied after netif creation with `esp_netif_dhcpc_stop()` + `esp_netif_set_ip_info()`. `IP_EVENT_STA_GOT_IP` still fires with a static IP in ESP-IDF, so NTP sync and HTTP-server startup are unaffected.

---

## Part 3 — `syncodoro`, the CLI

A webhook API begs for a one-liner. `syncodoro` is a bash script that wraps `curl` and formats the JSON:

```bash
syncodoro status              # ● FOCUS  24:58  Coding · 25 min
syncodoro start 25 Coding
syncodoro start -d 5 -t Tidy
syncodoro pause
syncodoro resume
syncodoro reset
syncodoro info                # MAC, IP, state
```

Color-coded output when attached to a TTY, plain otherwise. Two gotchas worth remembering:

- **`local` only works inside functions.** The `info` command's `local mac ip state` at top-level `case` scope dies with "local: can only be used in a function". A real bug that only shows up at runtime.
- The script falls back to raw JSON if `python3` isn't installed, so it works everywhere.

---

## Part 4 — The bug: the tag that wouldn't refresh

```bash
syncodoro start -d 20 -t Coding
syncodoro start -d 5 -t Tidy    # display still said "Coding"
```

The model changed to `Tidy`, but the screen kept showing `Coding` — until you navigated away and back. The `label_tag` was only refreshed on screen load or when the tag modal ran. Since a remote start never opens the modal, the label went stale. The ticker was already calling `update_ui_state()` every second; the label just wasn't part of it.

Fix was five lines — make `update_ui_state()` also set `label_tag` from the model on every tick. One of those "the architecture already does the right thing, it just wasn't wired up" bugs.

---

## Part 5 — `flash.sh`

All of this needs to get onto the device, and nobody wants to remember the full `idf.py` incantation. `flash.sh` finds the IDF environment, does a clean rebuild when the config is stale, auto-detects the serial port, flashes, and drops into the monitor. It made the flash-test-iterate loop one command instead of five.

---

## Part 6 — The done modal that got in the way

The webhook API made starting a session hands-free, but finishing one wasn't. When the timer hit zero, the screen popped the **done modal** with Save and Discard buttons, and saving navigated you to the history screen. That's three screen touches after a session you deliberately started from your laptop.

```bash
syncodoro start 25 Coding   # ...25 minutes later the screen demands attention
```

The fix was to remove the interaction entirely. The completion path now:

1. **Auto-appends the session to history** — the same `timerID` + `session_csv_append` logic the old Save button used.
2. **Returns the model to IDLE** — `pomo_reset_session()` does the work (the Discard button's job, but now there's nothing to discard — the session is already saved).
3. **Refreshes the display in place** — you stay on the timer screen, showing the idle `mm:ss`, ready for the next `syncodoro start` with zero screen touches.

What died:

- `show_done_modal()` and its two callbacks (`modal_save_cb`, `modal_discard_cb`) — 66 lines of modal scaffolding, gone.
- The `clear_pui_widgets()` + `ui_history_load()` navigation that yanked you off the timer screen.

The ticker's completion hook swapped from `show_done_modal()` to `auto_save_completed_session()`. `clear_pui_widgets` and `ui_history_load` survive — the nav buttons still use them — but the completion path no longer does.

The device is now a pure lap timer: start it from anywhere, and when it rings, it's just quietly done and logged.

---

## What changed

- **`main/model/pomo_model.c/.h`** — thread-safe control API with a mutex; timestamping moved in from the UI.
- **`main/network/http_server.c`** — six new API endpoints; `max_uri_handlers` 10 → 12.
- **`main/network/wifi_manager.c`** — duplicate-netif guard, reconnect path, disconnect-reason logging, `get_mac()`/`get_ip_str()`, static IP config.
- **`main/ui/ui_timer.c`** — all transitions routed through the model API; per-tick UI sync; tag label refresh; completed sessions auto-save (done modal removed).
- **`main/idf_component.yml`** — fixed invalid YAML (two dependencies on one line broke the manifest parser).
- **`flash.sh`** — one-command build/flash/monitor.
- **`syncodoro`** — LAN control CLI.

## Lessons

1. **Synchronize before you add concurrency.** The mutex-first refactor meant the webhook feature never raced the ticker.
2. **Log the failure reason.** `reason=17` vs `reason=201` turned WiFi debugging from a gut-feel exercise into a lookup table.
3. **Distrust MAC-looking log lines.** The `<ba-add>` line is the AP's BSSID, not your device.
4. **Sometimes the "right" answer is the pragmatic one.** DHCP reservation was blocked by the extender's NAT; a static IP with reflashable `#define`s got the job done.
5. **Make the feedback loop one command.** `flash.sh` made iterate-and-test actually fun.
6. **Ask what the screen is for.** Once the device gained a remote control, the "confirm your session" modal was pure friction — the remote *is* the confirmation. Deleting a feature can be as valuable as adding one.
