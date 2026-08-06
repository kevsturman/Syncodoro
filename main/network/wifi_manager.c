#include "wifi_manager.h"
#include "nvs_config.h"

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_mgr";

#define AP_SSID         "Syncodoro-Setup"
#define AP_CHANNEL      1
#define AP_MAX_CONN     4
#define STA_MAX_RETRY   5

// Static IP for STA mode (no DHCP). Adjust to your network and re-flash.
#define STA_STATIC_IP   "192.168.1.69"
#define STA_STATIC_NM   "255.255.255.0"
#define STA_STATIC_GW   "192.168.1.1"
#define STA_STATIC_DNS  "192.168.1.1"

static volatile wifi_state_t s_state = WIFI_STATE_IDLE;
static int s_retry_cnt = 0;
static char s_ip_str[16] = "0.0.0.0";
static wifi_connected_cb_t s_connected_cb = NULL;
static bool s_sta_netif_created = false;
static bool s_sta_started = false;

void wifi_manager_set_connected_cb(wifi_connected_cb_t cb)
{
    s_connected_cb = cb;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            s_state = WIFI_STATE_CONNECTING;
            esp_wifi_connect();
            ESP_LOGI(TAG, "STA started – connecting...");
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t *dis = (wifi_event_sta_disconnected_t *)data;
            ESP_LOGW(TAG, "Disconnected from AP, reason=%d (%s)",
                     dis->reason, esp_err_to_name(dis->reason));
            if (s_retry_cnt < STA_MAX_RETRY) {
                s_retry_cnt++;
                s_state = WIFI_STATE_CONNECTING;
                esp_wifi_connect();
                ESP_LOGI(TAG, "Retry %d/%d...", s_retry_cnt, STA_MAX_RETRY);
            } else {
                s_state = WIFI_STATE_ERROR;
                ESP_LOGW(TAG, "WiFi failed after %d retries", STA_MAX_RETRY);
            }
        } else if (id == WIFI_EVENT_STA_CONNECTED) {
            ESP_LOGI(TAG, "STA connected");
        } else if (id == WIFI_EVENT_AP_STACONNECTED) {
            ESP_LOGI(TAG, "Client connected to AP");
        } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
            ESP_LOGI(TAG, "Client disconnected from AP");
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_cnt = 0;
        s_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        if (s_connected_cb) {
            s_connected_cb();
        }
    }
}

void wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

void wifi_manager_start_ap(void)
{
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_cfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = (uint8_t)strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = "",
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    snprintf(s_ip_str, sizeof(s_ip_str), "192.168.4.1");
    s_state = WIFI_STATE_CONNECTED;
    ESP_LOGI(TAG, "AP started: SSID=%s  IP=192.168.4.1", AP_SSID);
}

void wifi_manager_start_sta(void)
{
    char ssid[64] = {0};
    char password[64] = {0};

    if (nvs_config_get_wifi_ssid(ssid, sizeof(ssid)) != ESP_OK || ssid[0] == '\0') {
        ESP_LOGW(TAG, "No WiFi credentials in NVS");
        s_state = WIFI_STATE_ERROR;
        return;
    }
    nvs_config_get_wifi_password(password, sizeof(password));

    if (!s_sta_netif_created) {
        esp_netif_t *netif = esp_netif_create_default_wifi_sta();
        s_sta_netif_created = true;

        // Use a fixed IP instead of DHCP.
        esp_netif_dhcpc_stop(netif);
        esp_netif_ip_info_t ip_info = {0};
        ip_info.ip.addr      = esp_ip4addr_aton(STA_STATIC_IP);
        ip_info.netmask.addr = esp_ip4addr_aton(STA_STATIC_NM);
        ip_info.gw.addr      = esp_ip4addr_aton(STA_STATIC_GW);
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));

        esp_netif_dns_info_t dns = {0};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = esp_ip4addr_aton(STA_STATIC_DNS);
        esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);

        snprintf(s_ip_str, sizeof(s_ip_str), "%s", STA_STATIC_IP);
        ESP_LOGI(TAG, "Using static IP: %s (gw %s)", STA_STATIC_IP, STA_STATIC_GW);
    }

    if (!s_sta_started) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t wifi_cfg = {0};
        strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
        strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());   // WIFI_EVENT_STA_START triggers the connect
        s_sta_started = true;
    } else {
        // Already started — manual reconnect after a previous failure.
        s_retry_cnt = 0;
        s_state = WIFI_STATE_CONNECTING;
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "STA connecting to SSID: %s", ssid);

    char mac[18];
    wifi_manager_get_mac(mac, sizeof(mac));
    ESP_LOGI(TAG, "STA MAC: %s", mac);
}

void wifi_manager_stop(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    s_state = WIFI_STATE_IDLE;
    s_sta_started = false;
}

wifi_state_t wifi_manager_get_state(void)
{
    return s_state;
}

void wifi_manager_get_ip_str(char *buf, size_t len)
{
    strlcpy(buf, s_ip_str, len);
}

void wifi_manager_get_mac(char *buf, size_t len)
{
    uint8_t mac[6] = {0};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(buf, len, "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        strlcpy(buf, "00:00:00:00:00:00", len);
    }
}
