#include "http_server.h"
#include "nvs_config.h"
#include "pomo_model.h"
#include "wifi_manager.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "http_srv";
static httpd_handle_t s_server = NULL;

// =========================================================
// EMBEDDED HTML CONTENT
// =========================================================

static const char SETUP_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Syncodoro Setup</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui,sans-serif;background:#1c1c1c;color:#ebeceq;"
"min-height:100vh;display:flex;align-items:center;justify-content:center;padding:20px}"
".card{background:#2d2d2d;border-radius:12px;padding:32px;width:100%;max-width:400px}"
"h1{color:#d6d8c9;font-size:1.6rem;margin-bottom:6px}"
"p{color:#8a8a8a;margin-bottom:24px;font-size:.9rem}"
"label{display:block;color:#d6d8c9;margin-bottom:6px;font-size:.9rem;margin-top:12px}"
"input{width:100%;padding:12px;background:#1c1c1c;border:1px solid #444;"
"color:#ebeceq;border-radius:8px;font-size:1rem;outline:none}"
"input:focus{border-color:#5e9c60}"
"button{width:100%;padding:14px;background:#5e9c60;color:#fff;border:none;"
"border-radius:8px;font-size:1rem;cursor:pointer;font-weight:600;margin-top:24px}"
"</style></head><body><div class=\"card\">"
"<h1>Syncodoro</h1>"
"<p>Connect to your WiFi so the device can sync your sessions.</p>"
"<form action=\"/save\" method=\"POST\">"
"<label>WiFi Network (SSID)</label>"
"<input type=\"text\" name=\"ssid\" placeholder=\"Your WiFi name\" required autocomplete=\"off\">"
"<label>Password</label>"
"<input type=\"password\" name=\"password\" placeholder=\"Leave blank if open network\" autocomplete=\"off\">"
"<button type=\"submit\">Save &amp; Connect</button>"
"</form></div></body></html>";

static const char SAVED_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Syncodoro</title><style>"
"body{font-family:system-ui,sans-serif;background:#1c1c1c;color:#d6d8c9;"
"display:flex;align-items:center;justify-content:center;min-height:100vh}"
".card{background:#2d2d2d;border-radius:12px;padding:40px;text-align:center;max-width:360px}"
"h1{font-size:2rem;margin-bottom:12px;color:#5e9c60}"
"p{color:#8a8a8a;line-height:1.6}"
"</style></head><body><div class=\"card\">"
"<h1>&#10003; Saved!</h1>"
"<p>Credentials saved. The device will now reboot and connect to your WiFi network.<br><br>You can close this page.</p>"
"</div></body></html>";

static const char DASHBOARD_HTML[] =
"<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Syncodoro Dashboard</title><style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:system-ui,sans-serif;background:#1c1c1c;color:#d6d8c9;padding:24px;max-width:860px;margin:0 auto}"
"h1{color:#d6d8c9;font-size:1.5rem;margin-bottom:4px}"
"h2{color:#d6d8c9;font-size:1.1rem;margin:28px 0 12px}"
".sub{color:#8a8a8a;font-size:.9rem;margin-bottom:24px}"
".btn{display:inline-block;padding:10px 20px;background:#5e9c60;color:#fff;"
"text-decoration:none;border-radius:8px;margin-bottom:24px;font-weight:600;font-size:.9rem;border:none;cursor:pointer}"
".btn-sm{padding:7px 14px;font-size:.85rem;font-weight:600;border:none;border-radius:6px;cursor:pointer}"
".btn-add{background:#5e9c60;color:#fff}"
".btn-save{background:#5e9c60;color:#fff;margin-top:14px;width:100%}"
".btn-del{background:#6b3030;color:#d6d8c9;margin-left:6px;padding:3px 9px;font-size:.8rem;border-radius:4px;border:none;cursor:pointer}"
"table{width:100%;border-collapse:collapse;background:#2d2d2d;border-radius:8px;overflow:hidden}"
"th{background:#3a3a3a;color:#d6d8c9;padding:12px 16px;text-align:left;font-size:.8rem;font-weight:600}"
"td{padding:12px 16px;border-bottom:1px solid #3a3a3a;color:#d6d8c9;font-size:.9rem}"
"tr:last-child td{border-bottom:none}"
".tag{display:inline-block;padding:2px 10px;background:#444;border-radius:12px;font-size:.8rem}"
".empty{color:#8a8a8a;text-align:center;padding:40px;font-size:.9rem}"
".card{background:#2d2d2d;border-radius:10px;padding:20px;margin-top:8px}"
".tag-row{display:flex;align-items:center;padding:7px 0;border-bottom:1px solid #3a3a3a}"
".tag-row:last-child{border-bottom:none}"
".tag-name{flex:1;font-size:.95rem}"
".add-row{display:flex;gap:10px;margin-top:14px}"
".add-row input{flex:1;padding:9px 12px;background:#1c1c1c;border:1px solid #444;"
"color:#d6d8c9;border-radius:7px;font-size:.9rem;outline:none}"
".add-row input:focus{border-color:#5e9c60}"
".toast{position:fixed;bottom:24px;right:24px;padding:12px 20px;border-radius:8px;"
"font-size:.9rem;font-weight:600;opacity:0;transition:opacity .3s;pointer-events:none}"
".toast.show{opacity:1}"
".toast.ok{background:#2d4a2e;color:#7ecb82}"
".toast.err{background:#4a2d2d;color:#cb7e7e}"
"</style></head><body>"
"<h1>Syncodoro</h1><p class=\"sub\">Pomodoro Session History</p>"
"<a href=\"/download\" class=\"btn\">&#11123; Download CSV</a>"
"<table><thead><tr><th>#</th><th>Date</th><th>Time (UTC)</th><th>Tag</th><th>Duration (min)</th><th>Pomodoros</th><th>Completed</th></tr></thead>"
"<tbody id=\"b\"><tr><td colspan=\"7\" class=\"empty\">Loading...</td></tr></tbody></table>"
"<h2>Manage Tags</h2>"
"<div class=\"card\">"
"<div id=\"tl\"><p class=\"empty\">Loading...</p></div>"
"<div class=\"add-row\">"
"<input type=\"text\" id=\"nt\" placeholder=\"New tag name...\" maxlength=\"31\">"
"<button class=\"btn-sm btn-add\" onclick=\"addTag()\">Add</button>"
"</div>"
"<button class=\"btn btn-save\" onclick=\"saveTags()\">Save Tags to Device</button>"
"</div>"
"<div class=\"toast\" id=\"toast\"></div>"
"<script>"
"var tags=[];"
/* Load sessions */
"fetch('/api/sessions').then(function(r){return r.json();}).then(function(d){"
"var b=document.getElementById('b');"
"if(!d||!d.length){b.innerHTML='<tr><td colspan=\"7\" class=\"empty\">No sessions recorded yet.</td></tr>';return;}"
"b.innerHTML=d.map(function(s,i){"
"return'<tr><td>'+(i+1)+'</td><td>'+s.date+'</td><td>'+s.time+'</td>'"
"+'<td><span class=\"tag\">'+s.classTag+'</span></td>'"
"+'<td>'+s.focusDuration+'</td><td>'+Math.round(s.pomoCount)+'</td>'"
"+'<td>'+s.completed+'</td></tr>';"
"}).join('');}).catch(function(){"
"document.getElementById('b').innerHTML='<tr><td colspan=\"7\" class=\"empty\">Could not load sessions.</td></tr>';"
"});"
/* Load tags */
"fetch('/api/tags').then(function(r){return r.json();}).then(function(d){"
"tags=d||[];renderTags();"
"}).catch(function(){tags=[];renderTags();});"
"function renderTags(){"
"var el=document.getElementById('tl');"
"if(!tags.length){el.innerHTML='<p class=\"empty\">No tags yet. Add one below.</p>';return;}"
"el.innerHTML=tags.map(function(t,i){"
"return'<div class=\"tag-row\"><span class=\"tag-name\">'+t+'</span>'"
"+'<button class=\"btn-del\" onclick=\"removeTag('+i+')\">&#10005;</button></div>';"
"}).join('');}"
"function addTag(){"
"var v=document.getElementById('nt').value.trim();"
"if(!v)return;"
"if(tags.indexOf(v)>=0){showToast('Tag already exists','err');return;}"
"tags.push(v);renderTags();document.getElementById('nt').value='';}"
"function removeTag(i){tags.splice(i,1);renderTags();}"
"function saveTags(){"
"fetch('/api/tags',{method:'POST',"
"headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"body:'tags='+encodeURIComponent(tags.join('\\n'))})"
".then(function(r){if(r.ok){showToast('Tags saved!','ok');}else{showToast('Save failed','err');}})"
".catch(function(){showToast('Save failed','err');});}"
"function showToast(msg,type){"
"var t=document.getElementById('toast');"
"t.textContent=msg;t.className='toast '+type+' show';"
"setTimeout(function(){t.className='toast';},2500);}"
"document.getElementById('nt').addEventListener('keydown',function(e){if(e.key==='Enter')addTag();});"
"</script></body></html>";

// =========================================================
// HELPERS
// =========================================================

// Simple URL-decode into dst. dst_len includes space for null terminator.
static void url_decode(char *dst, const char *src, size_t dst_len)
{
    char *d = dst;
    const char *s = src;
    size_t rem = dst_len - 1;
    while (*s && rem > 0) {
        if (*s == '%' && s[1] && s[2]) {
            char hex[3] = {s[1], s[2], '\0'};
            *d++ = (char)strtol(hex, NULL, 16);
            s += 3;
        } else if (*s == '+') {
            *d++ = ' ';
            s++;
        } else {
            *d++ = *s++;
        }
        rem--;
    }
    *d = '\0';
}

// Extract value for key from URL-encoded form body (key=value&key2=value2).
static void form_get_value(const char *body, const char *key, char *out, size_t out_len)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    const char *p = body;
    while (*p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            const char *end = strchr(p, '&');
            size_t vlen = end ? (size_t)(end - p) : strlen(p);
            if (vlen >= out_len) vlen = out_len - 1;
            char encoded[128] = {0};
            if (vlen < sizeof(encoded)) {
                memcpy(encoded, p, vlen);
                encoded[vlen] = '\0';
            }
            url_decode(out, encoded, out_len);
            return;
        }
        // Advance past this key=value pair
        p = strchr(p, '&');
        if (!p) break;
        p++;
    }
}

// =========================================================
// SETUP MODE HANDLERS
// =========================================================

static esp_err_t setup_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SETUP_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// One-shot timer callback: fires 2 s after the HTTP handler returns,
// giving the TCP stack time to flush and close the connection cleanly.
static void reboot_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "Rebooting now...");
    esp_restart();
}

static esp_err_t setup_save_handler(httpd_req_t *req)
{
    // Read the full POST body.  Loop to handle TCP fragmentation.
    char body[384] = {0};
    int remaining = (int)req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(body)) {
        // No body or suspiciously large – fall back to one blocking read
        remaining = (int)sizeof(body) - 1;
    }

    int total = 0;
    while (total < remaining) {
        int n = httpd_req_recv(req, body + total, (size_t)(remaining - total));
        if (n <= 0) {
            ESP_LOGW(TAG, "recv error or connection closed (n=%d)", n);
            break;
        }
        total += n;
    }
    body[total] = '\0';

    if (total == 0) {
        ESP_LOGW(TAG, "Empty POST body received");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "POST body (%d bytes): %s", total, body);

    char ssid[64]     = {0};
    char password[64] = {0};
    form_get_value(body, "ssid",     ssid,     sizeof(ssid));
    form_get_value(body, "password", password, sizeof(password));

    if (ssid[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saving credentials for SSID: %s", ssid);
    nvs_config_save_wifi_credentials(ssid, password);
    nvs_config_set_setup_mode(false);

    // Send the "saved" confirmation page first.
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVED_HTML, HTTPD_RESP_USE_STRLEN);

    // Schedule a reboot 2 s from now via esp_timer so the HTTP server task
    // can return cleanly and the TCP stack can flush/close the connection
    // before the device restarts.  Do NOT block here with vTaskDelay.
    esp_timer_handle_t timer;
    const esp_timer_create_args_t timer_args = {
        .callback = reboot_timer_cb,
        .arg      = NULL,
        .name     = "reboot_tmr",
    };
    if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 2000000ULL); // 2 s in microseconds
    } else {
        // Fallback: restart immediately (response may not reach browser)
        esp_restart();
    }

    return ESP_OK;
}

// =========================================================
// DASHBOARD HANDLERS
// =========================================================

static esp_err_t dash_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, DASHBOARD_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Stream CSV file from SD as a JSON array, one object per line.
static esp_err_t api_sessions_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    FILE *f = fopen("/data/sessions.csv", "r");
    if (!f) {
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    httpd_resp_sendstr_chunk(req, "[");

    char line[256];
    bool first = true;
    // Skip header row
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        httpd_resp_sendstr_chunk(req, "]");
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }

    // Format: Session ID,Date,Time (UTC),Tag,Duration (min),Pomodoros,Completed
    char timerID[32], classTag[32], date_str[16], time_str[8], completed_str[4];
    int focusDuration;
    double pomoCount;
    char chunk[400];

    while (fgets(line, sizeof(line), f)) {
        // Strip trailing newline
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        timerID[0] = classTag[0] = date_str[0] = time_str[0] = completed_str[0] = '\0';
        focusDuration = 0;
        pomoCount = 0.0;

        int parsed = sscanf(line, "%31[^,],%15[^,],%7[^,],%31[^,],%d,%lf,%3[^\n]",
                            timerID, date_str, time_str, classTag,
                            &focusDuration, &pomoCount, completed_str);
        if (parsed < 6) continue;

        snprintf(chunk, sizeof(chunk),
                 "%s{\"timerID\":\"%s\",\"date\":\"%s\",\"time\":\"%s\","
                 "\"dateAndTime\":\"%s %s\",\"classTag\":\"%s\","
                 "\"focusDuration\":%d,\"pomoCount\":%.1f,\"completed\":\"%s\"}",
                 first ? "" : ",",
                 timerID, date_str, time_str, date_str, time_str,
                 classTag, focusDuration, pomoCount, completed_str);

        httpd_resp_sendstr_chunk(req, chunk);
        first = false;
    }

    fclose(f);
    httpd_resp_sendstr_chunk(req, "]");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Serve the raw sessions.csv file as a downloadable attachment.
static esp_err_t download_handler(httpd_req_t *req)
{
    FILE *f = fopen("/data/sessions.csv", "r");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No sessions file found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"sessions.csv\"");

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) {
            break;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// =========================================================
// TAG API HANDLERS
// =========================================================

#define DEFAULT_TAGS "Coding\nReading\nWriting\nDesign\nGaming\nStudying"

/**
 * @brief GET /api/tags — return current tag list as a JSON string array.
 *
 * Reads the '\n'-separated tag string from NVS and converts it to a JSON
 * array.  Falls back to the built-in default list when no custom tags have
 * been saved yet.
 *
 * @param req  Incoming HTTP request handle.
 * @return ESP_OK always (errors produce an empty array response).
 */
static esp_err_t api_tags_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char tag_buf[512];
    const char *tag_str = DEFAULT_TAGS;
    if (nvs_config_get_tags(tag_buf, sizeof(tag_buf)) == ESP_OK && tag_buf[0] != '\0') {
        tag_str = tag_buf;
    }

    /* Convert '\n'-separated string to a JSON array */
    char json[640];
    int pos = 0;
    pos += snprintf(json + pos, sizeof(json) - (size_t)pos, "[");

    const char *p = tag_str;
    bool first = true;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t tlen = nl ? (size_t)(nl - p) : strlen(p);
        if (tlen > 0 && pos < (int)sizeof(json) - 4) {
            pos += snprintf(json + pos, sizeof(json) - (size_t)pos,
                            "%s\"%.*s\"", first ? "" : ",", (int)tlen, p);
            first = false;
        }
        if (!nl) break;
        p = nl + 1;
    }

    /* If list was empty or only newlines, return a single default to avoid NULL/empty UI */
    if (first) {
        pos += snprintf(json + pos, sizeof(json) - (size_t)pos, "\"Focus\"");
    }
    snprintf(json + pos, sizeof(json) - (size_t)pos, "]");
    httpd_resp_sendstr(req, json);
    return ESP_OK;
}

/**
 * @brief POST /api/tags — replace the stored tag list from a form-encoded body.
 *
 * Expects body: tags=Tag1%0ATag2%0ATag3 (URL-encoded newline-separated).
 * The decoded string is written directly to NVS via nvs_config_set_tags().
 *
 * @param req  Incoming HTTP request handle.
 * @return ESP_OK on success, ESP_FAIL on body read or NVS error.
 */
static esp_err_t api_tags_post_handler(httpd_req_t *req)
{
    char body[640] = {0};
    int remaining = (int)req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(body)) {
        remaining = (int)sizeof(body) - 1;
    }

    int total = 0;
    while (total < remaining) {
        int n = httpd_req_recv(req, body + total, (size_t)(remaining - total));
        if (n <= 0) break;
        total += n;
    }
    body[total] = '\0';

    if (total == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }

    char tags_decoded[512] = {0};
    form_get_value(body, "tags", tags_decoded, sizeof(tags_decoded));

    esp_err_t err = nvs_config_set_tags(tags_decoded);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_config_set_tags failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "NVS write failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

// =========================================================
// TIMER WEBHOOK HANDLERS
// =========================================================

static const char *timer_state_str(pomotimerState_t state)
{
    switch (state) {
        case pomoTimer_IDLE:      return "IDLE";
        case pomoTimer_COUNTING:  return "COUNTING";
        case pomoTimer_STOP:      return "PAUSED";
        case pomoTimer_COMPLETED: return "COMPLETED";
    }
    return "UNKNOWN";
}

// Respond with a JSON snapshot of the current session state.
static esp_err_t send_timer_response(httpd_req_t *req)
{
    pomoTimer_t snap;
    pomo_get_state(&snap);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"state\":\"%s\",\"remaining_secs\":%d,"
             "\"focusDuration\":%d,\"tag\":\"%s\",\"isCompleted\":%s}",
             timer_state_str(snap.timerState), snap.remaining_secs,
             snap.focusDuration, snap.classTag,
             snap.isCompleted ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// POST /api/timer/start — begin a fresh focus session.
// Optional form body: duration=<minutes>, tag=<name>.
static esp_err_t api_timer_start_handler(httpd_req_t *req)
{
    char body[128] = {0};
    int remaining = (int)req->content_len;
    if (remaining <= 0 || remaining >= (int)sizeof(body)) {
        remaining = (int)sizeof(body) - 1;
    }

    int total = 0;
    while (total < remaining) {
        int n = httpd_req_recv(req, body + total, (size_t)(remaining - total));
        if (n <= 0) break;
        total += n;
    }
    body[total] = '\0';

    char dur_str[16] = {0};
    char tag[32] = {0};
    form_get_value(body, "duration", dur_str, sizeof(dur_str));
    form_get_value(body, "tag", tag, sizeof(tag));

    int dur = dur_str[0] != '\0' ? atoi(dur_str) : 0;
    pomo_start_session(tag[0] != '\0' ? tag : NULL, dur);
    return send_timer_response(req);
}

static esp_err_t api_timer_pause_handler(httpd_req_t *req)
{
    pomo_pause_session();
    return send_timer_response(req);
}

static esp_err_t api_timer_resume_handler(httpd_req_t *req)
{
    pomo_resume_session();
    return send_timer_response(req);
}

static esp_err_t api_timer_reset_handler(httpd_req_t *req)
{
    pomo_reset_session();
    return send_timer_response(req);
}

static esp_err_t api_timer_status_handler(httpd_req_t *req)
{
    return send_timer_response(req);
}

// GET /api/device/info — report the device MAC, IP and current timer state.
static esp_err_t api_device_info_handler(httpd_req_t *req)
{
    char mac[18] = {0};
    char ip[16] = {0};
    wifi_manager_get_mac(mac, sizeof(mac));
    wifi_manager_get_ip_str(ip, sizeof(ip));

    pomoTimer_t snap;
    pomo_get_state(&snap);

    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"ok\":true,\"mac\":\"%s\",\"ip\":\"%s\",\"state\":\"%s\"}",
             mac, ip, timer_state_str(snap.timerState));

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// =========================================================
// PUBLIC API
// =========================================================

esp_err_t http_server_start_setup_mode(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets  = 4;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t setup_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = setup_get_handler,
    };
    httpd_register_uri_handler(s_server, &setup_get);

    httpd_uri_t setup_save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = setup_save_handler,
    };
    httpd_register_uri_handler(s_server, &setup_save);

    ESP_LOGI(TAG, "Setup HTTP server started at http://192.168.4.1");
    return ESP_OK;
}

esp_err_t http_server_start_dashboard(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets  = 4;
    config.max_uri_handlers  = 12;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start dashboard HTTP server");
        return ESP_FAIL;
    }

    httpd_uri_t dash      = { .uri = "/",             .method = HTTP_GET,  .handler = dash_get_handler };
    httpd_uri_t api       = { .uri = "/api/sessions", .method = HTTP_GET,  .handler = api_sessions_handler };
    httpd_uri_t dl        = { .uri = "/download",     .method = HTTP_GET,  .handler = download_handler };
    httpd_uri_t tags_get  = { .uri = "/api/tags",     .method = HTTP_GET,  .handler = api_tags_get_handler };
    httpd_uri_t tags_post = { .uri = "/api/tags",     .method = HTTP_POST, .handler = api_tags_post_handler };

    httpd_uri_t tmr_status = { .uri = "/api/timer/status", .method = HTTP_GET,  .handler = api_timer_status_handler };
    httpd_uri_t tmr_start  = { .uri = "/api/timer/start",  .method = HTTP_POST, .handler = api_timer_start_handler };
    httpd_uri_t tmr_pause  = { .uri = "/api/timer/pause",  .method = HTTP_POST, .handler = api_timer_pause_handler };
    httpd_uri_t tmr_resume = { .uri = "/api/timer/resume", .method = HTTP_POST, .handler = api_timer_resume_handler };
    httpd_uri_t tmr_reset  = { .uri = "/api/timer/reset",  .method = HTTP_POST, .handler = api_timer_reset_handler };
    httpd_uri_t dev_info   = { .uri = "/api/device/info",  .method = HTTP_GET,  .handler = api_device_info_handler };

    httpd_register_uri_handler(s_server, &dash);
    httpd_register_uri_handler(s_server, &api);
    httpd_register_uri_handler(s_server, &dl);
    httpd_register_uri_handler(s_server, &tags_get);
    httpd_register_uri_handler(s_server, &tags_post);
    httpd_register_uri_handler(s_server, &tmr_status);
    httpd_register_uri_handler(s_server, &tmr_start);
    httpd_register_uri_handler(s_server, &tmr_pause);
    httpd_register_uri_handler(s_server, &tmr_resume);
    httpd_register_uri_handler(s_server, &tmr_reset);
    httpd_register_uri_handler(s_server, &dev_info);

    ESP_LOGI(TAG, "Dashboard HTTP server started");
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
