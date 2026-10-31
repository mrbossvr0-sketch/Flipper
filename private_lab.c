/* PrivateLab 3.1 Momentum -- integrated Flipper security DIAGNOSTICS.
 * No RF TX, key capture/replay, CAN writing, BLE spam, HID injection, or bypass logic.
 * All hardware interfaces default to read-only; ELM327 OBD issues only setup
 * commands and live Mode 01 PID queries through a separately isolated adapter.
 * Targets the matching Momentum uFBT SDK; hardware not yet validated.
 */
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <storage/storage.h>
#include <expansion/expansion.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "obd_parser.h"

#define LAB_DIR "/ext/apps_data/privatelab3"
#define CFG_FILE LAB_DIR "/settings.bin"
#define LOG_FILE LAB_DIR "/audit.tsv"
#define CFG_MAGIC 0x33424C50u /* PLB3 */
#define RX_CAP 512
#define RF_HISTORY 65

/* 650kHz OOK preset, adapted from the Flipper firmware CC1101 RX preset.
 * FuriHal uses the register pairs, terminator, and eight PATABLE bytes.
 * We NEVER call furi_hal_subghz_tx() or store received waveforms.
 */
static const uint8_t rf_preset[] = {
    0x02,0x0D,0x03,0x07,0x08,0x32,0x0B,0x06,
    0x14,0x00,0x13,0x00,0x12,0x30,0x11,0x32,0x10,0x17,
    0x18,0x18,0x19,0x18,0x1D,0x91,0x1C,0x00,0x1B,0x07,
    0x20,0xFB,0x22,0x11,0x21,0xB6,0x00,0x00,
    0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00
};
static const uint32_t freqs[] = {
    303875000,310000000,315000000,390000000,418000000,
    433920000,434420000,868350000,915000000,
};
#define NFREQ (sizeof(freqs)/sizeof(freqs[0]))
static const uint32_t bauds[] = {9600,19200,38400,57600,115200};
#define NBAUD (sizeof(bauds)/sizeof(bauds[0]))

typedef enum {
    ScreenHome, ScreenMenu, ScreenRF, ScreenGPIO, ScreenADC, ScreenUART,
    ScreenOBD, ScreenSystem, ScreenAudit, ScreenGuides, ScreenSettings,
    ScreenHelp, ScreenCount,
} Screen;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t frequency;
    uint8_t uart_baud;
    uint8_t obd_baud;
    uint8_t sweep;
    uint8_t logging;
    uint8_t alerts;
    uint8_t threshold; /* threshold encoded as magnitude dBm: 40..110 */
    uint16_t checksum;
} LabSettings;

typedef struct {
    FuriMessageQueue* queue;
    ViewPort* viewport;
    Gui* gui;
    Storage* storage;
    Screen screen;
    uint8_t menu_sel;
    uint8_t setting_sel;
    uint8_t guide_sel;
    uint8_t home_sel;
    bool running;
    LabSettings cfg;
    bool rf_active;
    bool rf_valid;
    uint32_t tuned_hz;
    float rssi;
    float peak;
    int8_t rf_hist[RF_HISTORY];
    uint8_t hist_next;
    uint32_t tick_count;
    uint8_t sweep_tick;
    bool gpio_on;
    bool adc_on;
    FuriHalAdcHandle* adc;
    uint16_t millivolts;
    bool digital_state;
    FuriHalSerialHandle* serial;
    bool serial_open;
    bool serial_expansion_disabled;
    volatile uint16_t rx_head;
    volatile uint16_t rx_tail;
    uint8_t rx_ring[RX_CAP];
    char serial_tail[76];
    uint8_t serial_n;
    uint32_t bytes_seen;
    char obd_raw[560];
    uint16_t obd_n;
    uint8_t obd_stage;
    uint8_t obd_tick;
    uint8_t obd_error;
    ObdLive obd;
    uint32_t logged;
    bool last_save_ok;
    uint8_t status_ticks;
} Lab;

static const char* const menu_labels[] = {
    "RF SIGNAL METER", "GPIO DIGITAL", "GPIO VOLTAGE", "UART MONITOR",
    "OBD-II LIVE", "SYSTEM CHECK", "AUDIT / SAVE", "ASSET GUIDES",
    "SETTINGS", "ABOUT / HELP"
};
static const char* const menu_sub[] = {
    "RSSI / spectrum presets", "Pin 7 input", "Pin 16 analog 0-2.5V", "3.3V UART receive",
    "External ELM327 needed", "Battery and SD", "Local logs on SD", "NFC, IR, WiFi, more",
    "Presets and controls", "Limits and wiring"
};
#define NMENU (sizeof(menu_labels)/sizeof(menu_labels[0]))
static const char* const setting_labels[] = {
    "RF FREQUENCY", "AUTO SWEEP", "RSSI ALERT", "ALERT LEVEL",
    "UART BAUD", "OBD BAUD", "SAVE LOGS", "RESET DEFAULTS",
};
#define NSET (sizeof(setting_labels)/sizeof(setting_labels[0]))
static const char* const guide_titles[] = {
    "NFC", "LOW-FREQ RFID", "IBUTTON", "INFRARED", "VEHICLE FOB",
    "WIFI / ESP32", "BLUETOOTH", "USB & GPIO", "EVIDENCE"
};
static const char* const guide_lines[][4] = {
    {"Open built-in NFC tool.", "Use only your own tags.", "Record supported tech.", "No cloning in this app."},
    {"Open built-in RFID tool.", "Use lab/test tags only.", "Record read success.", "No credential copying."},
    {"Open built-in iButton.", "Test owned iButtons.", "Inspect protocol/fit.", "No access bypass."},
    {"Use built-in Infrared.", "Test your own devices.", "Learn/verify commands", "with official IR app."},
    {"Observe frequency/RSSI", "of YOUR fob only.", "No replay, jam, start", "or immobilizer work."},
    {"WiFi radio is NOT in", "Flipper Zero. Attach", "a compatible ESP32", "with trusted firmware."},
    {"Built-in BLE supports", "approved device uses.", "No BLE spam/jamming", "or pairing bypass."},
    {"Only use 3.3V GPIO.", "Do not attach CAN,", "OBD socket or 12V.", "UART: GND and RX."},
    {"Logs stored on SD", "apps_data/privatelab3", "Export via qFlipper.", "Time = uptime seconds."},
};
#define NGUIDES (sizeof(guide_titles)/sizeof(guide_titles[0]))

static uint16_t settings_crc(const LabSettings* s) {
    /* Stable checksum over header bytes before checksum field; detects corruption,
       not cryptographic tampering. */
    uint16_t c = 0xBAD1;
    const uint8_t* bytes = (const uint8_t*)s;
    for(size_t i = 0; i < offsetof(LabSettings, checksum); i++) c = (uint16_t)((c << 3) ^ (c >> 13) ^ bytes[i]);
    return c;
}
static void settings_default(LabSettings* s) {
    memset(s, 0, sizeof(*s));
    s->magic = CFG_MAGIC;
    s->version = 3;
    s->frequency = 5; /* 433.920 MHz */
    s->uart_baud = 4; /* 115200 */
    s->obd_baud = 2; /* 38400 */
    s->logging = 1;
    s->alerts = 0;
    s->threshold = 70; /* -70 dBm */
    s->checksum = settings_crc(s);
}
static bool mkdir_lab(Lab* a) {
    /* mkdir may report AlreadyExists; the subsequent file open is definitive. */
    (void)storage_common_mkdir(a->storage, LAB_DIR);
    return true;
}
static bool save_settings(Lab* a) {
    if(!mkdir_lab(a)) return false;
    a->cfg.checksum = settings_crc(&a->cfg);
    File* f = storage_file_alloc(a->storage);
    bool ok = storage_file_open(f, CFG_FILE, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        bool written = storage_file_write(f, &a->cfg, sizeof(a->cfg)) == sizeof(a->cfg);
        (void)storage_file_close(f);
        ok = written;
    }
    storage_file_free(f);
    return ok;
}
static void load_settings(Lab* a) {
    settings_default(&a->cfg);
    File* f = storage_file_alloc(a->storage);
    LabSettings candidate;
    bool ok = storage_file_open(f, CFG_FILE, FSAM_READ, FSOM_OPEN_EXISTING);
    if(ok) {
        bool read_ok = storage_file_read(f, &candidate, sizeof(candidate)) == sizeof(candidate);
        (void)storage_file_close(f);
        ok = read_ok;
    }
    storage_file_free(f);
    if(ok && candidate.magic == CFG_MAGIC && candidate.version == 3 &&
       candidate.frequency < NFREQ && candidate.uart_baud < NBAUD &&
       candidate.obd_baud < NBAUD && candidate.threshold >= 40 &&
       candidate.threshold <= 110 && candidate.checksum == settings_crc(&candidate)) {
        a->cfg = candidate;
    }
}
static bool write_log(Lab* a, const char* event, const char* value) {
    if(!a->cfg.logging || !mkdir_lab(a)) {
        a->last_save_ok = false;
        a->status_ticks = 10;
        return false;
    }
    File* f = storage_file_alloc(a->storage);
    bool ok = storage_file_open(f, LOG_FILE, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(ok) {
        char line[144];
        int size = snprintf(line, sizeof(line), "%lu\t%s\t%s\n",
            (unsigned long)(furi_get_tick() / 1000), event, value);
        ok = size > 0 && size < (int)sizeof(line) &&
            storage_file_write(f, line, (size_t)size) == (size_t)size;
        (void)storage_file_close(f);
    }
    storage_file_free(f);
    if(ok) a->logged++;
    a->last_save_ok = ok;
    a->status_ticks = 10;
    return ok;
}
static void save_sample(Lab* a) {
    char val[98];
    switch(a->screen) {
        case ScreenRF:
            if(!a->rf_valid) return;
            snprintf(val, sizeof(val), "hz=%lu,rssi=%.1f,peak=%.1f",
                (unsigned long)a->tuned_hz, (double)a->rssi, (double)a->peak);
            write_log(a, "RF_RSSI", val);
            break;
        case ScreenGPIO:
            snprintf(val, sizeof(val), "pc3=%s", a->digital_state ? "HIGH" : "LOW");
            write_log(a, "GPIO_DIGITAL", val);
            break;
        case ScreenADC:
            snprintf(val, sizeof(val), "pc0_mv=%u", (unsigned)a->millivolts);
            write_log(a, "GPIO_VOLTAGE", val);
            break;
        case ScreenUART:
            snprintf(val, sizeof(val), "rx_bytes=%lu,baud=%lu",
                (unsigned long)a->bytes_seen, (unsigned long)bauds[a->cfg.uart_baud]);
            write_log(a, "UART_COUNT", val);
            break;
        case ScreenOBD:
            snprintf(val, sizeof(val), "rpm=%d,speed_kmh=%d,coolant_c=%d,valid=%u%u%u",
                a->obd.rpm, a->obd.speed_kmh, a->obd.coolant_c,
                a->obd.rpm_ok, a->obd.speed_ok, a->obd.coolant_ok);
            write_log(a, "OBD_MODE01", val);
            break;
        default:
            write_log(a, "MANUAL", "user_saved_marker");
            break;
    }
}

static void rf_tune(Lab* a) {
    furi_hal_subghz_idle();
    a->rf_valid = furi_hal_subghz_is_frequency_valid(freqs[a->cfg.frequency]);
    a->rssi = a->peak = -140.0f;
    a->hist_next = 0;
    memset(a->rf_hist, 0, sizeof(a->rf_hist));
    if(a->rf_valid) {
        a->tuned_hz = furi_hal_subghz_set_frequency_and_path(freqs[a->cfg.frequency]);
        furi_hal_subghz_rx();
    } else {
        a->tuned_hz = freqs[a->cfg.frequency];
    }
}
static void rf_open(Lab* a) {
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(rf_preset);
    a->rf_active = true;
    a->sweep_tick = 0;
    rf_tune(a);
}
static void rf_close(Lab* a) {
    if(!a->rf_active) return;
    furi_hal_subghz_idle();
    furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);
    furi_hal_subghz_sleep();
    a->rf_active = false;
}
static void gpio_open(Lab* a) {
    furi_hal_gpio_init(&gpio_ext_pc3, GpioModeInput, GpioPullDown, GpioSpeedLow);
    a->gpio_on = true;
}
static void gpio_close(Lab* a) {
    if(a->gpio_on) furi_hal_gpio_init(&gpio_ext_pc3, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    a->gpio_on = false;
}
static void adc_open(Lab* a) {
    furi_hal_gpio_init(&gpio_ext_pc0, GpioModeAnalog, GpioPullNo, GpioSpeedLow);
    a->adc = furi_hal_adc_acquire();
    if(a->adc) {
        furi_hal_adc_configure_ex(a->adc, FuriHalAdcScale2500, FuriHalAdcClockSync64,
            FuriHalAdcOversample64, FuriHalAdcSamplingtime247_5);
        a->adc_on = true;
    }
}
static void adc_close(Lab* a) {
    if(a->adc_on && a->adc) furi_hal_adc_release(a->adc);
    a->adc = NULL;
    a->adc_on = false;
}

static void uart_receive(FuriHalSerialHandle* h, FuriHalSerialRxEvent evt, void* context) {
    Lab* a = context;
    if(!(evt & FuriHalSerialRxEventData)) return;
    while(furi_hal_serial_async_rx_available(h)) {
        uint8_t b = furi_hal_serial_async_rx(h);
        uint16_t next = (uint16_t)((a->rx_head + 1) % RX_CAP);
        if(next != a->rx_tail) {
            a->rx_ring[a->rx_head] = b;
            a->rx_head = next;
        }
    }
}
static bool uart_pop(Lab* a, uint8_t* b) {
    if(a->rx_head == a->rx_tail) return false;
    *b = a->rx_ring[a->rx_tail];
    a->rx_tail = (uint16_t)((a->rx_tail + 1) % RX_CAP);
    return true;
}
static bool uart_open(Lab* a, uint32_t baud) {
    /* Expansion service must release the UART before we acquire it. */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    a->serial_expansion_disabled = true;
    furi_record_close(RECORD_EXPANSION);
    a->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!a->serial) {
        Expansion* e = furi_record_open(RECORD_EXPANSION);
        expansion_enable(e);
        furi_record_close(RECORD_EXPANSION);
        a->serial_expansion_disabled = false;
        return false;
    }
    a->rx_head = a->rx_tail = 0;
    a->bytes_seen = 0;
    a->serial_n = 0;
    a->serial_tail[0] = '\0';
    furi_hal_serial_init(a->serial, baud);
    furi_hal_serial_async_rx_start(a->serial, uart_receive, a, false);
    a->serial_open = true;
    return true;
}
static void uart_close(Lab* a) {
    if(a->serial_open) {
        furi_hal_serial_async_rx_stop(a->serial);
        furi_hal_serial_deinit(a->serial);
        furi_hal_serial_control_release(a->serial);
        a->serial = NULL;
        a->serial_open = false;
    }
    if(a->serial_expansion_disabled) {
        Expansion* e = furi_record_open(RECORD_EXPANSION);
        expansion_enable(e);
        furi_record_close(RECORD_EXPANSION);
        a->serial_expansion_disabled = false;
    }
}
static const char* const obd_cmds[] = {
    "ATE0\r", "ATL0\r", "ATS0\r", "ATH0\r", "ATSP0\r",
    "010C\r", "010D\r", "0105\r"
};
static void obd_send(Lab* a) {
    if(!a->serial_open || a->obd_stage >= 8) return;
    const char* cmd = obd_cmds[a->obd_stage];
    a->obd_n = 0;
    a->obd_raw[0] = '\0';
    a->obd_tick = 0;
    furi_hal_serial_tx(a->serial, (const uint8_t*)cmd, strlen(cmd));
}
static void obd_open(Lab* a) {
    memset(&a->obd, 0, sizeof(a->obd));
    a->obd_stage = 0;
    a->obd_error = 0;
    if(uart_open(a, bauds[a->cfg.obd_baud])) obd_send(a);
}
static void obd_poll(Lab* a) {
    if(!a->serial_open) return;
    uint8_t b;
    bool response_done = false;
    while(uart_pop(a, &b)) {
        if(b == '>') response_done = true;
        if(a->obd_n < sizeof(a->obd_raw) - 1) {
            a->obd_raw[a->obd_n++] = (char)b;
            a->obd_raw[a->obd_n] = '\0';
        }
    }
    a->obd_tick++;
    if(response_done || a->obd_tick >= 25) { /* ~3s timeout */
        if(!response_done) a->obd_error++;
        if(a->obd_stage == 5) (void)obd_parse_pid_response(a->obd_raw, 0x0C, &a->obd);
        if(a->obd_stage == 6) (void)obd_parse_pid_response(a->obd_raw, 0x0D, &a->obd);
        if(a->obd_stage == 7) (void)obd_parse_pid_response(a->obd_raw, 0x05, &a->obd);
        if(a->obd_stage < 7) a->obd_stage++;
        else a->obd_stage = 5;
        obd_send(a);
    }
}
static void screen_exit(Lab* a) {
    switch(a->screen) {
        case ScreenRF: rf_close(a); break;
        case ScreenGPIO: gpio_close(a); break;
        case ScreenADC: adc_close(a); break;
        case ScreenUART: case ScreenOBD: uart_close(a); break;
        default: break;
    }
}
static void screen_enter(Lab* a, Screen s) {
    screen_exit(a);
    a->screen = s;
    switch(s) {
        case ScreenRF: rf_open(a); break;
        case ScreenGPIO: gpio_open(a); break;
        case ScreenADC: adc_open(a); break;
        case ScreenUART: (void)uart_open(a, bauds[a->cfg.uart_baud]); break;
        case ScreenOBD: obd_open(a); break;
        default: break;
    }
    view_port_update(a->viewport);
}
static void draw_header(Canvas* c, const char* title, uint8_t battery) {
    canvas_set_color(c, ColorBlack);
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 4, 10, title);
    char charge[12];
    snprintf(charge, sizeof(charge), "%u%%", (unsigned)battery);
    canvas_draw_str_aligned(c, 125, 10, AlignRight, AlignBottom, charge);
    canvas_set_color(c, ColorBlack);
}
static void draw_footer(Canvas* c, const char* text) {
    canvas_draw_line(c, 0, 56, 127, 56);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 4, 63, text);
}
static void draw_meter(Canvas* c, uint8_t x, uint8_t y, uint8_t w, uint8_t h, int value) {
    if(value < 0) value = 0;
    if(value > 100) value = 100;
    canvas_draw_frame(c, x, y, w, h);
    if(value > 0) canvas_draw_box(c, x + 2, y + 2, (w - 4) * value / 100, h - 4);
}
static void draw_home(Canvas* c, Lab* a) {
    draw_header(c, "PRIVATE / LAB MNTM", furi_hal_power_get_pct());
    canvas_draw_frame(c, 2, 16, 124, 26);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 8, 28, "AUTHORIZED TEST LAB");
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 8, 39, "RX-only RF  |  Diagnostics");
    canvas_draw_box(c, 3, 45, 39, 10);
    canvas_set_color(c, ColorWhite);
    canvas_draw_str(c, 9, 53, "TOOLS");
    canvas_set_color(c, ColorBlack);
    canvas_draw_frame(c, 45, 45, 39, 10);
    canvas_draw_str(c, 50, 53, "STATUS");
    canvas_draw_frame(c, 87, 45, 38, 10);
    canvas_draw_str(c, 91, 53, "GUIDES");
    canvas_draw_line(c, 2 + a->home_sel * 42, 57, 39 + a->home_sel * 42, 57);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 18, 63, "<  OK to open  >");
}
static void draw_list(Canvas* c, Lab* a) {
    draw_header(c, "SELECT A TOOL", furi_hal_power_get_pct());
    size_t start = a->menu_sel > 2 ? a->menu_sel - 2 : 0;
    if(start + 4 > NMENU) start = NMENU - 4;
    for(size_t i = start; i < start + 4; i++) {
        uint8_t y = 16 + (i - start) * 10;
        if(i == a->menu_sel) {
            canvas_draw_box(c, 3, y - 1, 122, 10);
            canvas_set_color(c, ColorWhite);
        }
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, 8, y + 7, menu_labels[i]);
        if(i == a->menu_sel) canvas_set_color(c, ColorBlack);
    }
    draw_footer(c, menu_sub[a->menu_sel]);
}
static void draw_rf(Canvas* c, Lab* a) {
    draw_header(c, "RF METER / PASSIVE RX", furi_hal_power_get_pct());
    if(!a->rf_valid) {
        canvas_set_font(c, FontPrimary);
        canvas_draw_str(c, 8, 34, "FREQ NOT SUPPORTED");
        draw_footer(c, "LEFT / RIGHT: frequency");
        return;
    }
    char b[48];
    snprintf(b, sizeof(b), "%lu.%03lu MHz", (unsigned long)(a->tuned_hz/1000000),
        (unsigned long)((a->tuned_hz % 1000000)/1000));
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 4, 24, b);
    snprintf(b, sizeof(b), "%.0f dBm", (double)a->rssi);
    canvas_draw_str_aligned(c, 125, 24, AlignRight, AlignBottom, b);
    int strength = (int)((a->rssi + 120.0f) * 100.0f / 80.0f);
    draw_meter(c, 4, 27, 120, 9, strength);
    canvas_set_font(c, FontSecondary);
    snprintf(b, sizeof(b), "Peak %.0f dBm  %s", (double)a->peak,
             a->cfg.alerts && a->rssi >= -(float)a->cfg.threshold ? "!ALERT!" :
             (a->cfg.sweep ? "SWEEP" : "LOCK"));
    canvas_draw_str(c, 5, 44, b);
    for(int i = 1; i < RF_HISTORY; i++) {
        uint8_t v = (uint8_t)((a->hist_next + i) % RF_HISTORY);
        int ht = a->rf_hist[v] * 8 / 100;
        if(ht > 0) canvas_draw_line(c, 4 + i, 54, 4 + i, 54 - ht);
    }
    draw_footer(c, "<> FREQ  ^SWP  OK SAVE");
}
static void draw_gpio(Canvas* c, Lab* a, bool analog) {
    draw_header(c, analog ? "GPIO / VOLTAGE" : "GPIO / DIGITAL", furi_hal_power_get_pct());
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 5, 24, analog ? "PIN 16 : PC0 / ADC1 CH1" : "PIN 7 : PC3 / INPUT");
    canvas_set_font(c, FontBigNumbers);
    char number[32];
    if(analog) snprintf(number, sizeof(number), "%u", (unsigned)a->millivolts);
    else snprintf(number, sizeof(number), "%s", a->digital_state ? "HIGH" : "LOW");
    canvas_draw_str(c, 8, 49, number);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 99, 48, analog ? "mV" : "3.3V");
    draw_footer(c, analog ? "0-2.5V max | OK SAVE" : "INPUT ONLY | OK SAVE");
}
static void draw_uart(Canvas* c, Lab* a) {
    draw_header(c, "UART / RX MONITOR", furi_hal_power_get_pct());
    canvas_set_font(c, FontSecondary);
    char b[38];
    snprintf(b, sizeof(b), "USART %lu 8N1  %lu bytes",
        (unsigned long)bauds[a->cfg.uart_baud], (unsigned long)a->bytes_seen);
    canvas_draw_str(c, 4, 23, b);
    if(!a->serial_open) {
        canvas_set_font(c, FontPrimary);
        canvas_draw_str(c, 6, 42, "PORT IS BUSY");
    } else {
        canvas_draw_frame(c, 3, 27, 122, 26);
        canvas_draw_str(c, 7, 40, a->serial_tail[0] ? a->serial_tail : "Waiting for 3.3V data...");
    }
    draw_footer(c, "RX ONLY  OK: log count");
}
static void draw_obd(Canvas* c, Lab* a) {
    draw_header(c, "CAR / OBD-II MODE 01", furi_hal_power_get_pct());
    canvas_set_font(c, FontSecondary);
    if(!a->serial_open) {
        canvas_draw_str(c, 5, 27, "UART unavailable / busy");
        canvas_draw_str(c, 5, 39, "No OBD connection.");
    } else {
        char b[40];
        snprintf(b, sizeof(b), "RPM      %s", a->obd.rpm_ok ? "" : "--");
        if(a->obd.rpm_ok) snprintf(b, sizeof(b), "RPM       %d", a->obd.rpm);
        canvas_draw_str(c, 6, 23, b);
        if(a->obd.speed_ok) snprintf(b, sizeof(b), "SPEED     %d km/h", a->obd.speed_kmh);
        else snprintf(b, sizeof(b), "SPEED     -- km/h");
        canvas_draw_str(c, 6, 34, b);
        if(a->obd.coolant_ok) snprintf(b, sizeof(b), "COOLANT   %d C", a->obd.coolant_c);
        else snprintf(b, sizeof(b), "COOLANT   -- C");
        canvas_draw_str(c, 6, 45, b);
        snprintf(b, sizeof(b), "PID %u  timeout:%u", a->obd_stage, a->obd_error);
        canvas_draw_str(c, 5, 53, b);
    }
    draw_footer(c, "ISOLATED ELM327 ONLY");
}
static void draw_system(Canvas* c, Lab* a) {
    draw_header(c, "SYSTEM / SELF CHECK", furi_hal_power_get_pct());
    char b[48];
    canvas_set_font(c, FontSecondary);
    snprintf(b, sizeof(b), "Battery: %u%%  Charge:%s", furi_hal_power_get_pct(),
        furi_hal_power_is_charging() ? "YES" : "NO");
    canvas_draw_str(c, 5, 23, b);
    const char* name = furi_hal_version_get_name_ptr();
    snprintf(b, sizeof(b), "Device: %.20s", name ? name : "Flipper Zero");
    canvas_draw_str(c, 5, 33, b);
    snprintf(b, sizeof(b), "SD logging: %s", a->cfg.logging ? "ON" : "OFF");
    canvas_draw_str(c, 5, 43, b);
    snprintf(b, sizeof(b), "This session saved: %lu", (unsigned long)a->logged);
    canvas_draw_str(c, 5, 53, b);
    draw_footer(c, "OK: save health marker");
}
static void draw_audit(Canvas* c, Lab* a) {
    draw_header(c, "AUDIT / LOCAL EVIDENCE", furi_hal_power_get_pct());
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 5, 23, "LOG: /apps_data/privatelab3");
    char b[40];
    snprintf(b, sizeof(b), "Saved now: %lu markers", (unsigned long)a->logged);
    canvas_draw_str(c, 5, 34, b);
    canvas_draw_str(c, 5, 45, "TSV rows = uptime seconds");
    if(a->status_ticks) canvas_draw_str(c, 5, 53, a->last_save_ok ? "Saved to microSD" : "Not saved / SD or logs off");
    draw_footer(c, "OK: add audit marker");
}
static void draw_guides(Canvas* c, Lab* a) {
    draw_header(c, "ASSET TEST CHECKLIST", furi_hal_power_get_pct());
    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 6, 25, guide_titles[a->guide_sel]);
    canvas_set_font(c, FontSecondary);
    for(uint8_t i = 0; i < 4; i++) canvas_draw_str(c, 7, 33 + i * 7,
        guide_lines[a->guide_sel][i]);
    draw_footer(c, "LEFT/RIGHT: next guide");
}
static void draw_settings(Canvas* c, Lab* a) {
    draw_header(c, "SETTINGS / STORED", furi_hal_power_get_pct());
    size_t start = a->setting_sel > 2 ? a->setting_sel - 2 : 0;
    if(start + 4 > NSET) start = NSET - 4;
    for(size_t i = start; i < start + 4; i++) {
        uint8_t y = 16 + (i - start) * 10;
        if(i == a->setting_sel) { canvas_draw_box(c, 3, y - 1, 122, 10); canvas_set_color(c, ColorWhite); }
        canvas_set_font(c, FontSecondary);
        canvas_draw_str(c, 6, y + 7, setting_labels[i]);
        char v[30] = "";
        switch(i) {
            case 0: snprintf(v, sizeof(v), "%luMHz", (unsigned long)(freqs[a->cfg.frequency]/1000000)); break;
            case 1: snprintf(v, sizeof(v), "%s", a->cfg.sweep ? "ON" : "OFF"); break;
            case 2: snprintf(v, sizeof(v), "%s", a->cfg.alerts ? "ON" : "OFF"); break;
            case 3: snprintf(v, sizeof(v), "-%udBm", a->cfg.threshold); break;
            case 4: snprintf(v, sizeof(v), "%lu", (unsigned long)bauds[a->cfg.uart_baud]); break;
            case 5: snprintf(v, sizeof(v), "%lu", (unsigned long)bauds[a->cfg.obd_baud]); break;
            case 6: snprintf(v, sizeof(v), "%s", a->cfg.logging ? "ON" : "OFF"); break;
            case 7: snprintf(v, sizeof(v), "OK"); break;
        }
        canvas_draw_str_aligned(c, 122, y + 7, AlignRight, AlignBottom, v);
        if(i == a->setting_sel) canvas_set_color(c, ColorBlack);
    }
    draw_footer(c, "LEFT/RIGHT: change  OK");
}
static void draw_help(Canvas* c, Lab* a) {
    UNUSED(a);
    draw_header(c, "LAB MNTM / SAFETY", furi_hal_power_get_pct());
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 4, 23, "Momentum SDK / custom FAP");
    canvas_draw_str(c, 4, 33, "RF receives strength only.");
    canvas_draw_str(c, 4, 43, "Vehicles: parked, adapter.");
    canvas_draw_str(c, 4, 53, "Authorized hardware only.");
    draw_footer(c, "BACK: tool selection");
}
static void draw(Canvas* canvas, void* context) {
    Lab* a = context;
    canvas_clear(canvas);
    switch(a->screen) {
        case ScreenHome: draw_home(canvas, a); break;
        case ScreenMenu: draw_list(canvas, a); break;
        case ScreenRF: draw_rf(canvas, a); break;
        case ScreenGPIO: draw_gpio(canvas, a, false); break;
        case ScreenADC: draw_gpio(canvas, a, true); break;
        case ScreenUART: draw_uart(canvas, a); break;
        case ScreenOBD: draw_obd(canvas, a); break;
        case ScreenSystem: draw_system(canvas, a); break;
        case ScreenAudit: draw_audit(canvas, a); break;
        case ScreenGuides: draw_guides(canvas, a); break;
        case ScreenSettings: draw_settings(canvas, a); break;
        case ScreenHelp: draw_help(canvas, a); break;
        default: break;
    }
    if(a->status_ticks && a->screen != ScreenAudit) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 85, 56, 43, 8);
        canvas_set_color(canvas, ColorWhite);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 88, 63, a->last_save_ok ? "SAVED" : "NO SAVE");
        canvas_set_color(canvas, ColorBlack);
    }
}
static void input_callback(InputEvent* event, void* context) {
    Lab* a = context;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        (void)furi_message_queue_put(a->queue, event, 0);
    }
}
static void setting_change(Lab* a, int direction, bool ok) {
    switch(a->setting_sel) {
        case 0: a->cfg.frequency = (uint8_t)((a->cfg.frequency + NFREQ + direction) % NFREQ); break;
        case 1: if(direction || ok) a->cfg.sweep ^= 1; break;
        case 2: if(direction || ok) a->cfg.alerts ^= 1; break;
        case 3: {
            int t = a->cfg.threshold + direction * 5;
            if(t < 40) t = 110;
            if(t > 110) t = 40;
            a->cfg.threshold = (uint8_t)t;
            break;
        }
        case 4: a->cfg.uart_baud = (uint8_t)((a->cfg.uart_baud + NBAUD + direction) % NBAUD); break;
        case 5: a->cfg.obd_baud = (uint8_t)((a->cfg.obd_baud + NBAUD + direction) % NBAUD); break;
        case 6: if(direction || ok) a->cfg.logging ^= 1; break;
        case 7: if(ok) settings_default(&a->cfg); break;
    }
    a->last_save_ok = save_settings(a);
    a->status_ticks = 8;
}
static void select_menu(Lab* a) {
    switch(a->menu_sel) {
        case 0: screen_enter(a, ScreenRF); break;
        case 1: screen_enter(a, ScreenGPIO); break;
        case 2: screen_enter(a, ScreenADC); break;
        case 3: screen_enter(a, ScreenUART); break;
        case 4: screen_enter(a, ScreenOBD); break;
        case 5: screen_enter(a, ScreenSystem); break;
        case 6: screen_enter(a, ScreenAudit); break;
        case 7: screen_enter(a, ScreenGuides); break;
        case 8: screen_enter(a, ScreenSettings); break;
        default: screen_enter(a, ScreenHelp); break;
    }
}
static void handle_key(Lab* a, InputEvent* e) {
    InputKey k = e->key;
    if(k == InputKeyBack && e->type == InputTypeShort) {
        if(a->screen == ScreenHome) a->running = false;
        else if(a->screen == ScreenMenu) screen_enter(a, ScreenHome);
        else screen_enter(a, ScreenMenu);
        return;
    }
    switch(a->screen) {
        case ScreenHome:
            if(k == InputKeyLeft) a->home_sel = (a->home_sel + 2) % 3;
            if(k == InputKeyRight) a->home_sel = (a->home_sel + 1) % 3;
            if(k == InputKeyOk) {
                if(a->home_sel == 0) screen_enter(a, ScreenMenu);
                else if(a->home_sel == 1) screen_enter(a, ScreenSystem);
                else screen_enter(a, ScreenGuides);
            }
            break;
        case ScreenMenu:
            if(k == InputKeyUp) a->menu_sel = (a->menu_sel + NMENU - 1) % NMENU;
            if(k == InputKeyDown) a->menu_sel = (a->menu_sel + 1) % NMENU;
            if(k == InputKeyOk) select_menu(a);
            break;
        case ScreenRF:
            if(k == InputKeyLeft || k == InputKeyRight) {
                a->cfg.frequency = (uint8_t)((a->cfg.frequency + NFREQ + (k == InputKeyLeft ? -1 : 1)) % NFREQ);
                save_settings(a);
                rf_tune(a);
            }
            if(k == InputKeyUp) { a->cfg.sweep ^= 1; save_settings(a); }
            if(k == InputKeyDown) a->peak = a->rssi;
            if(k == InputKeyOk) save_sample(a);
            break;
        case ScreenGPIO: case ScreenADC: case ScreenUART: case ScreenOBD: case ScreenSystem: case ScreenAudit:
            if(k == InputKeyOk) save_sample(a);
            break;
        case ScreenGuides:
            if(k == InputKeyLeft || k == InputKeyUp) a->guide_sel = (a->guide_sel + NGUIDES - 1) % NGUIDES;
            if(k == InputKeyRight || k == InputKeyDown || k == InputKeyOk) a->guide_sel = (a->guide_sel + 1) % NGUIDES;
            break;
        case ScreenSettings:
            if(k == InputKeyUp) a->setting_sel = (a->setting_sel + NSET - 1) % NSET;
            if(k == InputKeyDown) a->setting_sel = (a->setting_sel + 1) % NSET;
            if(k == InputKeyLeft) setting_change(a, -1, false);
            if(k == InputKeyRight) setting_change(a, +1, false);
            if(k == InputKeyOk) setting_change(a, 0, true);
            break;
        default: break;
    }
}
static void tick(Lab* a) {
    a->tick_count++;
    if(a->status_ticks) a->status_ticks--;
    switch(a->screen) {
        case ScreenRF:
            if(a->rf_active && a->rf_valid) {
                a->rssi = furi_hal_subghz_get_rssi();
                if(a->rssi > a->peak) a->peak = a->rssi;
                float p = a->rssi + 120.0f;
                int s = (int)(p * 100 / 80.0f);
                if(s < 0) s = 0;
                if(s > 100) s = 100;
                a->rf_hist[a->hist_next] = (int8_t)s;
                a->hist_next = (a->hist_next + 1) % RF_HISTORY;
                /* A visual reading; no audio/LED or transmitting alert. */
            }
            if(a->cfg.sweep && ++a->sweep_tick >= 6) {
                a->sweep_tick = 0;
                a->cfg.frequency = (a->cfg.frequency + 1) % NFREQ;
                rf_tune(a);
            }
            break;
        case ScreenGPIO:
            a->digital_state = furi_hal_gpio_read(&gpio_ext_pc3);
            break;
        case ScreenADC:
            if(a->adc_on && a->adc) {
                uint16_t v = furi_hal_adc_read(a->adc, FuriHalAdcChannel1);
                a->millivolts = (uint16_t)furi_hal_adc_convert_to_voltage(a->adc, v);
            }
            break;
        case ScreenUART: {
            uint8_t b;
            while(uart_pop(a, &b)) {
                a->bytes_seen++;
                if(b == '\n' || b == '\r') {
                    a->serial_tail[a->serial_n] = '\0';
                } else if(b >= 32 && b < 127) {
                    if(a->serial_n >= sizeof(a->serial_tail) - 2) {
                        memmove(a->serial_tail, a->serial_tail + 1, sizeof(a->serial_tail) - 2);
                        a->serial_n--;
                    }
                    a->serial_tail[a->serial_n++] = (char)b;
                    a->serial_tail[a->serial_n] = '\0';
                }
            }
            break;
        }
        case ScreenOBD: obd_poll(a); break;
        default: break;
    }
    view_port_update(a->viewport);
}

int32_t privatelab_app(void* p) {
    UNUSED(p);
    Lab* a = malloc(sizeof(Lab));
    if(!a) return -1;
    memset(a, 0, sizeof(Lab));
    a->running = true;
    a->screen = ScreenHome;
    a->queue = furi_message_queue_alloc(12, sizeof(InputEvent));
    a->viewport = view_port_alloc();
    a->gui = furi_record_open(RECORD_GUI);
    a->storage = furi_record_open(RECORD_STORAGE);
    load_settings(a);
    view_port_draw_callback_set(a->viewport, draw, a);
    view_port_input_callback_set(a->viewport, input_callback, a);
    gui_add_view_port(a->gui, a->viewport, GuiLayerFullscreen);
    while(a->running) {
        InputEvent e;
        if(furi_message_queue_get(a->queue, &e, 120) == FuriStatusOk) {
            handle_key(a, &e);
            view_port_update(a->viewport);
        } else {
            tick(a);
        }
    }
    screen_exit(a);
    gui_remove_view_port(a->gui, a->viewport);
    view_port_free(a->viewport);
    furi_message_queue_free(a->queue);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(a);
    return 0;
}
