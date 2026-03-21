# Syncodoro

A standalone **Pomodoro focus timer** for the ESP32-2432S028R ("Cheap Yellow Display") — a 2.8" resistive touchscreen device that runs focus sessions, logs them to internal SPIFFS, syncs time over NTP, and serves a web dashboard on your local WiFi. Manage tags in the browser; the device shows the same list and uses the first tag on boot.

---

## Features

| Feature | Details |
|---|---|
| **Touchscreen UI** | LVGL dark theme with circular arc progress, large countdown, tap-to-start/pause; four colour themes (Dark, Forest, Ocean, Warm) in Settings |
| **Focus sessions** | Duration 5–120 min (5-min steps); tag chosen from a list synced with the web dashboard (add/edit tags in browser, select on device) |
| **Session logging** | Completed sessions appended to `/data/sessions.csv` on internal SPIFFS (no SD card required) |
| **Session timestamps** | NTP time sync after WiFi connect; session start time stored in UTC (YYYY-MM-DD HH:MM) |
| **On-device history** | Scrollable session list: tag, duration, date/time |
| **Web dashboard** | Session history table, CSV download, and **Manage Tags** (add/remove tags, save to device); all HTML/JS embedded in firmware |
| **WiFi modes** | **AP mode** (first boot): device hosts `Syncodoro-Setup`, you join and open 192.168.4.1 to enter home WiFi. **STA mode** (normal): device joins your network and serves the dashboard at its LAN IP |
| **Dual-boot path** | NVS `setup_mode` separates first-boot setup from normal timer operation; config (duration) in `/data/config.json` on SPIFFS |

---

## Hardware

**Target board:** [ESP32-2432S028R](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) — commonly called the "CYD" (Cheap Yellow Display)

```
┌──────────────────────────────┐
│  ESP32-2432S028R             │
│                              │
│  LCD   ST7789/ILI9341 2.8"   │
│  Touch XPT2046  (resistive)  │
│  Flash 4 MB                  │
│  SD    microSD (optional)    │
└──────────────────────────────┘
```

**Display driver:** The project supports both **ST7789** (default) and **ILI9341** LCD controllers. Use `idf.py menuconfig` → Syncodoro Configuration → Display driver to select your board's controller. For web flashing, choose the driver in [docs/index.html](docs/index.html) before installing.

### Pin Map

| Peripheral | Signal | GPIO |
|---|---|---|
| **LCD (SPI2)** | MOSI | 13 |
| | MISO | 12 |
| | SCLK | 14 |
| | CS | 15 |
| | DC | 2 |
| | Backlight | 21 |
| **Touch (SPI3)** | MOSI | 32 |
| | MISO | 39 |
| | SCLK | 25 |
| | CS | 33 |
| **SD (shared SPI2)** | CS | 4 |

### 3D-Printed Case

An optional snap-fit enclosure sized for the ESP32-2432S028R is included in the [`mechanical/`](mechanical/) folder. All parts were modelled in [FreeCAD](https://www.freecad.org/) and exported as STEP (`.stp`) so they can be opened in any CAD tool and re-sliced for any printer.

| File | Description |
|---|---|
| `syncodoro_case_bottom.stp` | Base shell — holds the PCB and provides a flat stand |
| `syncodoro_case_top.stp` | Top frame — exposes the 2.8" display and USB port |
| `syncodoro_case_button.stp` | Optional tactile-button surround (if used) |

**Suggested print settings**

| Setting | Value |
|---|---|
| Material | PLA or PETG |
| Layer height | 0.2 mm |
| Infill | 20 % |
| Supports | Required for the top frame (display cutout overhang) |
| Orientation | Print bottom shell flat; top frame upside-down to minimise support use |

---

## Architecture

### Boot Flow

```
Power on
   │
   ├─ nvs_flash_init()
   ├─ cyd_init()  (LCD + Touch + LVGL)
   │
   ├─ Read NVS: setup_mode?
   │
   ├── YES (first boot / reset) ──────────────────────────┐
   │    WiFi AP "Syncodoro-Setup"                         │
   │    HTTP server at 192.168.4.1 (setup form)           │
   │    User enters SSID + password → save → reboot        │
   │                                                      │
   └── NO (normal boot) ───────────────────────────────────┘
        Mount SPIFFS (/data)
        Load /data/config.json → apply duration
        Set tag = first tag from NVS list (or built-in list)
        WiFi STA connect (non-blocking)
        Show Timer UI
        On IP assigned → HTTP dashboard + SNTP time sync
```

### Screen Navigation

```
Timer Screen
  ├─ [⚙] Settings Screen
  │        ├─ WiFi status + Connect button
  │        ├─ [Timer] → Timer Screen
  │        └─ [History] → History Screen
  └─ [≡] History Screen
           └─ [Back] → Timer Screen
```

### Project Structure

```
syncodoro/
├── mechanical/             # 3D-print case STEP files (FreeCAD export)
├── partitions.csv          # Custom 4 MB flash layout
├── sdkconfig.defaults      # Flash size, partition table, LVGL fonts, HTTP buffer
└── main/
    ├── main.c              # Boot entry — NVS read, BSP init, mode branch
    ├── bsp/
    │   ├── cyd.c           # ST7789/ILI9341 LCD, XPT2046 touch, SD card init
    │   └── cyd.h
    ├── model/
    │   ├── pomo_model.c    # Global active_session (single source of truth)
    │   └── pomo_model.h    # pomoTimer_t struct + pomotimerState_t enum
    ├── config/
    │   ├── nvs_config.c   # NVS: setup_mode, wifi, theme, user_tags; get_first_tag for boot
    │   └── nvs_config.h
    ├── network/
    │   ├── wifi_manager.c  # AP/STA state machine, 5-retry policy, IP callback
    │   ├── wifi_manager.h
    │   ├── http_server.c   # Captive portal (setup) + dashboard (normal mode)
    │   └── http_server.h
    ├── storage/
    │   ├── session_csv.c   # Append/read /data/sessions.csv (SPIFFS)
    │   ├── session_csv.h
    │   ├── config_json.c   # Read/write /data/config.json (SPIFFS)
    │   └── config_json.h
    └── ui/
        ├── ui_theme.h      # Overtec dark theme — LVGL color constants
        ├── ui_timer.c/h    # Main timer screen (arc, countdown, modals, nav icons)
        ├── ui_setup.c/h    # First-boot AP instructions screen
        ├── ui_settings.c/h # WiFi status + navigation hub
        └── ui_history.c/h  # Scrollable session list from SPIFFS CSV
```

---

## Data Model

```c
typedef struct {
    char             timerID[32];      // Unique session identifier
    int              focusDuration;    // Target duration in minutes
    int              remaining_secs;   // Live countdown (persists across screen nav)
    double           pomoCount;        // Accumulated completed sessions
    char             classTag[32];     // "Coding", "Reading", etc.
    char             dateAndTime[32];  // Timestamp string
    bool             isCompleted;      // True when timer reached zero
    pomotimerState_t timerState;       // IDLE → COUNTING → STOP → COMPLETED
} pomoTimer_t;
```

A single global `active_session` is the shared state across all UI screens, storage modules, and the HTTP API — no dynamic allocation required.

### Session CSV Format

Sessions are stored in `/data/sessions.csv` on SPIFFS. Headers and separate Date/Time columns make pasting into Notion or Google Sheets straightforward.

```
Session ID,Date,Time (UTC),Tag,Duration (min),Pomodoros,Completed
s1709123456,2025-03-08,14:30,Coding,25,1,Yes
s1709127890,2025-03-08,15:00,Reading,20,2,Yes
```

---

## Web Dashboard

When the device is in **STA (station) mode** and connected to your WiFi, open `http://<device-ip>` in a browser on the same network (the Settings screen on the device shows the IP).

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Session history table + **Manage Tags** card (add/remove tags, Save to device) |
| `/api/sessions` | GET | All sessions as a JSON array (from SPIFFS CSV) |
| `/api/tags` | GET | Current tag list as JSON array |
| `/api/tags` | POST | Save tag list (form body: `tags=Tag1%0ATag2`) |
| `/download` | GET | Download `sessions.csv` as attachment |

The tag list is stored in NVS; the device uses the **first tag** in that list as the default on boot, and the tag picker on the timer screen shows the same list.

---

## Manual setup: AP mode and STA (host) mode

The device has two WiFi roles. Understanding them makes setup and recovery straightforward.

### Modes at a glance

| Mode | When it’s used | What the device does | How you access it |
|------|----------------|----------------------|-------------------|
| **AP (Access Point)** | First boot, or after NVS is erased | Creates WiFi network **`Syncodoro-Setup`** (open). Serves a setup page at **192.168.4.1** | Join `Syncodoro-Setup`, open browser → `http://192.168.4.1` |
| **STA (Station)** | Normal use after WiFi is configured | Joins your home/office WiFi. Gets an IP from your router. Serves the **dashboard** at that IP | Open `http://<device-ip>` from any device on the same network |

### Going from AP mode to STA mode (first-time setup)

1. **Power on** the device. If it has never had WiFi saved, it starts in **AP mode**.
2. The screen shows instructions to connect to **`Syncodoro-Setup`** and open **192.168.4.1**.
3. On a phone or PC, connect to the WiFi network **`Syncodoro-Setup`** (no password).
4. Open a browser and go to **`http://192.168.4.1`** (or `http://192.168.4.1/`).
5. Enter your **home/office WiFi SSID** and **password**, then tap **Save & Connect**.
6. The device writes credentials to NVS, shows “Saved!”, and **reboots after about 2 seconds**.
7. After reboot it starts in **STA mode**: it joins your WiFi, gets an IP, and starts the web dashboard and NTP. The **Settings** screen on the device shows the current IP (e.g. `192.168.1.42`). Open **`http://<that-ip>`** in a browser to use the session history and **Manage Tags**.

You are now in STA (host) mode. Every later power-on will also start in STA mode until NVS is cleared.

### Staying in STA mode (normal use)

- Power on → NVS has WiFi credentials and `setup_mode` off → device goes straight to **STA mode**, connects to your network, and runs the timer + dashboard. No need to touch AP mode again unless you reset WiFi.

### Going back to AP mode (reset WiFi)

- If you need to **re-enter WiFi credentials** (new router, new network, wrong password):
  - **Erase NVS** so the device “forgets” it was set up. After that it will boot in AP mode again.
  - From the project directory:  
    `idf.py -p COM<PORT> erase-flash`  
    then flash again:  
    `idf.py -p COM<PORT> flash`
  - **Warning:** `erase-flash` wipes the whole flash (including app). So re-flash the firmware after erase. Alternatively, only erase the NVS partition if you have a partition table that exposes it (e.g. `idf.py -p COMx erase-region 0x9000 0x6000` for a 24 KB NVS at 0x9000).

After erasing and re-flashing, the device will boot in **AP mode** again; repeat the “Going from AP mode to STA mode” steps to configure WiFi and return to STA (host) mode.

---

## Building & Flashing

### Requirements

- [ESP-IDF v5.4.x](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32/get-started/)
- Python 3.x
- USB cable connected to the ESP32-2432S028R

### Build

```bash
# Clone and enter the project
cd syncodoro

# Set up IDF environment (Windows PowerShell)
. $env:IDF_PATH\export.ps1

# Build (default: ST7789 driver)
idf.py build

# For web installer: copy build/syncodoro.bin to docs/syncodoro-st7789.bin

# Build ILI9341 variant (for web installer / releases)
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.ili9341" fullclean build
# Then copy build/syncodoro.bin to docs/syncodoro-ili9341.bin
```

### Flash

```bash
idf.py -p COM<PORT> flash monitor
```

Replace `COM<PORT>` with your serial port (e.g. `COM3` on Windows, `/dev/ttyUSB0` on Linux).

### First-time Flash Size Note

The project uses a **custom 4 MB partition table**. If flashing over a previous project, run a full erase first:

```bash
idf.py -p COM<PORT> erase-flash
idf.py -p COM<PORT> flash
```

---

## Flash Layout

```
0x00001000  Bootloader        (from ESP-IDF)
0x00008000  Partition table   (partitions.csv)
0x00009000  NVS               24 KB   — WiFi credentials, setup_mode flag
0x0000F000  PHY init data     4 KB
0x00010000  App (factory)     1.94 MB — firmware binary (~1.35 MB used, 33% free)
0x00200000  Storage (SPIFFS)  2 MB    — /data (sessions.csv, config.json)
```

---

## Dependencies

| Component | Version | Purpose |
|---|---|---|
| `esp-idf` | `>=5.4` | Core framework, FreeRTOS, WiFi, NVS, HTTP server |
| `espressif/esp_lvgl_port` | `^2.7.1` | LVGL ↔ ESP-IDF display/touch integration |
| `atanisoft/esp_lcd_touch_xpt2046` | `^1.0.6` | XPT2046 resistive touch driver |
| `lvgl/lvgl` | `9.5.0` | UI framework (pulled in by esp_lvgl_port) |

---

## Roadmap

- [x] NTP time sync for session timestamps (no RTC hardware needed)
- [x] Colour theme selection in Settings (Dark, Forest, Ocean, Warm)
- [x] Web-managed tags (add/remove on dashboard, first tag on device at boot)
- [ ] Notion API sync — post sessions to a Notion database
- [ ] Break timer mode (short / long break)
- [ ] Daily session goal and streak tracking
- [ ] OTA firmware update via web dashboard

---

## License

MIT License — see [LICENSE](LICENSE) for details.
