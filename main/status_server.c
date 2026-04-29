/**
 * @file status_server.c
 * @brief HTTP status page — serves live sensor data over Wi-Fi.
 *
 * Routes:
 *   GET /           HTML dashboard; JavaScript polls /api/status every 2 s.
 *   GET /api/status JSON snapshot of all sensor readings.
 *
 * @author William Crow
 * @date 2026-04-29
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 */

#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "peripherals.h"
#include "tmp102.h"
#include "mcp9701a.h"
#include "max17049.h"
#include "bq25731.h"

#include "status_server.h"

static const char *TAG = "status_server";

// Thermistor indices — must match the order in init_thermistors() in main.c.
#define THERM_BAT0    0
#define THERM_BAT1    1
#define THERM_FRONT   2
#define THERM_BACK    3
#define THERM_TOP     4
#define THERM_BOTTOM  5
#define THERM_LEFT    6
#define THERM_RIGHT   7

// ── HTML page ────────────────────────────────────────────────────────────────
// Served once; JavaScript fetches /api/status every 2 s and updates the DOM.

static const char STATUS_HTML[] =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>LED Cube</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:monospace;background:#0d0d1a;color:#ccc;padding:20px}"
    "h1{color:#00d4ff;margin-bottom:4px;font-size:1.6em}"
    "h2{color:#7eb8f7;font-size:.9em;letter-spacing:.1em;text-transform:uppercase;"
    "   margin:20px 0 8px;border-bottom:1px solid #222;padding-bottom:4px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(130px,1fr));gap:8px}"
    ".card{background:#16213e;border-radius:6px;padding:10px 12px}"
    ".lbl{color:#555;font-size:.75em;margin-bottom:2px}"
    ".val{font-size:1.25em;color:#00d4ff}"
    ".ok{color:#4caf50}.warn{color:#ff9800}.err{color:#f44336}"
    "#ts{color:#444;font-size:.72em;margin-top:20px}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>LED Cube</h1>"
    "<h2>Battery</h2><div class=\"grid\" id=\"s-bat\"></div>"
    "<h2>Charger</h2><div class=\"grid\" id=\"s-chg\"></div>"
    "<h2>Temperatures</h2><div class=\"grid\" id=\"s-tmp\"></div>"
    "<p id=\"ts\"></p>"
    "<script>"
    "function c(l,v,k){"
    "  return '<div class=\"card\"><div class=\"lbl\">'+l+'</div>"
             "<div class=\"val '+(k||'')+'\">'+v+'</div></div>';"
    "}"
    "async function poll(){"
    "  try{"
    "    const d=await(await fetch('/api/status')).json();"
    "    const b=d.battery;"
    "    document.getElementById('s-bat').innerHTML="
    "      c('State of Charge',b.soc_pct.toFixed(1)+' %',"
    "        b.soc_pct<15?'err':b.soc_pct<30?'warn':'ok')+"
    "      c('Voltage',b.voltage_v.toFixed(2)+' V')+"
    "      c('Rate',b.crate_pct_hr.toFixed(1)+' %/hr',"
    "        b.crate_pct_hr>0?'ok':b.crate_pct_hr<0?'warn':'');"
    "    const g=d.charger;"
    "    document.getElementById('s-chg').innerHTML="
    "      c('AC Input',g.ac_present?'Present':'Absent',g.ac_present?'ok':'')+"
    "      c('Charging',g.charging?'Active':'Idle',g.charging?'ok':'')+"
    "      c('Fault',g.fault?'YES':'None',g.fault?'err':'ok')+"
    "      c('V<sub>BUS</sub>',g.vbus_v.toFixed(2)+' V')+"
    "      c('V<sub>BAT</sub>',g.vbat_v.toFixed(2)+' V')+"
    "      c('V<sub>SYS</sub>',g.vsys_v.toFixed(2)+' V')+"
    "      c('I<sub>CHG</sub>',g.ichg_a.toFixed(2)+' A')+"
    "      c('I<sub>IN</sub>',g.iin_a.toFixed(2)+' A');"
    "    const t=d.therm;"
    "    document.getElementById('s-tmp').innerHTML="
    "      c('PCB',d.pcb_temp_c.toFixed(2)+' &#8451;')+"
    "      c('Battery 0',t.bat0_c.toFixed(1)+' &#8451;')+"
    "      c('Battery 1',t.bat1_c.toFixed(1)+' &#8451;')+"
    "      c('Top',t.top_c.toFixed(1)+' &#8451;')+"
    "      c('Bottom',t.bottom_c.toFixed(1)+' &#8451;')+"
    "      c('Front',t.front_c.toFixed(1)+' &#8451;')+"
    "      c('Back',t.back_c.toFixed(1)+' &#8451;')+"
    "      c('Left',t.left_c.toFixed(1)+' &#8451;')+"
    "      c('Right',t.right_c.toFixed(1)+' &#8451;');"
    "    document.getElementById('ts').textContent="
    "      'Updated '+new Date().toLocaleTimeString();"
    "  }catch(e){"
    "    document.getElementById('ts').textContent='Error: '+e;"
    "  }"
    "}"
    "poll();setInterval(poll,2000);"
    "</script>"
    "</body>"
    "</html>";

// ── /api/status handler ───────────────────────────────────────────────────────

static esp_err_t api_status_handler(httpd_req_t *req)
{
    // Read all sensors; substitute 0 on error so the JSON is always valid.
    tmp102_data_t pcb = {0};
    tmp102_read(&pcb);

    mcp9701a_data_t therm[PERIPH_THERM_COUNT] = {0};
    mcp9701a_read_all(therm, PERIPH_THERM_COUNT);

    max17049_data_t bat = {0};
    max17049_read(&bat);

    bq25731_status_t chg_status = {0};
    bq25731_read_status(&chg_status);

    bq25731_adc_t chg_adc = {0};
    bq25731_read_adc(&chg_adc);

    char buf[768];
    int len = snprintf(buf, sizeof(buf),
        "{"
          "\"pcb_temp_c\":%.2f,"
          "\"therm\":{"
            "\"bat0_c\":%.1f,\"bat1_c\":%.1f,"
            "\"front_c\":%.1f,\"back_c\":%.1f,"
            "\"top_c\":%.1f,\"bottom_c\":%.1f,"
            "\"left_c\":%.1f,\"right_c\":%.1f"
          "},"
          "\"battery\":{"
            "\"soc_pct\":%.1f,\"voltage_v\":%.2f,\"crate_pct_hr\":%.1f"
          "},"
          "\"charger\":{"
            "\"ac_present\":%d,\"charging\":%d,\"fault\":%d,"
            "\"vbus_v\":%.2f,\"vbat_v\":%.2f,\"vsys_v\":%.2f,"
            "\"ichg_a\":%.2f,\"idchg_a\":%.2f,\"iin_a\":%.2f,\"psys_v\":%.2f"
          "}"
        "}",
        pcb.temperature_c,
        therm[THERM_BAT0].temperature_c,   therm[THERM_BAT1].temperature_c,
        therm[THERM_FRONT].temperature_c,  therm[THERM_BACK].temperature_c,
        therm[THERM_TOP].temperature_c,    therm[THERM_BOTTOM].temperature_c,
        therm[THERM_LEFT].temperature_c,   therm[THERM_RIGHT].temperature_c,
        bat.soc_pct, bat.voltage_v, bat.crate_pct_hr,
        (int)chg_status.ac_present, (int)chg_status.in_fchrg, (int)chg_status.fault,
        chg_adc.vbus_v, chg_adc.vbat_v, chg_adc.vsys_v,
        chg_adc.ichg_a, chg_adc.idchg_a, chg_adc.iin_a, chg_adc.psys_v);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, buf, len);
    return ESP_OK;
}

// ── / handler ─────────────────────────────────────────────────────────────────

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, STATUS_HTML);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

void status_server_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;

    httpd_handle_t server;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "failed to start HTTP server");
        return;
    }

    static const httpd_uri_t route_root = {
        .uri     = "/",
        .method  = HTTP_GET,
        .handler = root_handler,
    };
    static const httpd_uri_t route_api = {
        .uri     = "/api/status",
        .method  = HTTP_GET,
        .handler = api_status_handler,
    };

    httpd_register_uri_handler(server, &route_root);
    httpd_register_uri_handler(server, &route_api);

    ESP_LOGI(TAG, "HTTP server started — http://<device-ip>/");
}