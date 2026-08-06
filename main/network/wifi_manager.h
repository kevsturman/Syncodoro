#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_ERROR,
} wifi_state_t;

// Register a callback that fires when STA gets an IP (WiFi connected).
// Call before wifi_manager_start_sta().
typedef void (*wifi_connected_cb_t)(void);
void wifi_manager_set_connected_cb(wifi_connected_cb_t cb);

// Must be called once before start_ap / start_sta.
void wifi_manager_init(void);

// Start as Access Point (open SSID "Syncodoro-Setup") for the setup captive portal.
void wifi_manager_start_ap(void);

// Read credentials from NVS and start STA connection. Non-blocking.
void wifi_manager_start_sta(void);

void wifi_manager_stop(void);
wifi_state_t wifi_manager_get_state(void);

// Fills buf with the current IP string (AP: "192.168.4.1"; STA: assigned IP).
void wifi_manager_get_ip_str(char *buf, size_t len);

// Fills buf with the STA interface MAC address (e.g. "3c:84:6a:f6:3a:74").
void wifi_manager_get_mac(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
