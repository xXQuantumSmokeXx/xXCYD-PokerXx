/**
 * LovyanGFX configuration for ESP32 Cheap Yellow Display (CYD / ESP32-2432S028).
 *
 * This replaces TFT_eSPI for the 2-USB CYD variant.  LovyanGFX auto-detects
 * the display controller (ILI9341 vs ST7789) so we don't have to guess which
 * driver a particular 2-USB board shipped with.
 *
 * Provides TFT_eSPI-compatible shims (setTextFont, etc.) so the main game
 * code compiles against either backend with minimal #ifdef.
 *
 * Usage: build with -DUSE_LOVYAN_GFX in platformio.ini build_flags.
 */

#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// ── TFT_eSPI compatibility macros used by main.cpp ────────────────────────
// These are normally defined by TFT_eSPI's User_Setup.h; LovyanGFX doesn't
// need them for its own operation, but main.cpp references TFT_MADCTL / MV /
// MX / BGR in the orientation test (which is #ifdef'd out for LGFX builds).
// Defining them here avoids a cascade of #ifdef in the orientation test.
#define TFT_MADCTL  0x36
#define TFT_MAD_MV  0x20
#define TFT_MAD_MX  0x40
#define TFT_MAD_MY  0x80
#define TFT_MAD_BGR 0x08


// ── TFT_eSPI text-datum compatibility macros ─────────────────────────────
// LovyanGFX uses textdatum_t enum; the existing code uses TFT_eSPI macros.
// Define them here (with guards) so cards.h templates compile when LGFX is
// the active backend.
#ifndef TL_DATUM
#define TL_DATUM  0
#define TC_DATUM  1
#define TR_DATUM  2
#define ML_DATUM  3
#define MC_DATUM  4
#define MR_DATUM  5
#define BL_DATUM  6
#define BC_DATUM  7
#define BR_DATUM  8
#endif

// ── LGFX_CYD — LGFX_Device subclass pre-configured for the CYD pinout ─────

class LGFX_CYD : public lgfx::LGFX_Device
{
    // --- panel + peripheral instances (owned by value) ---
    lgfx::Panel_ILI9341   _panel_instance;
    lgfx::Bus_SPI         _bus_instance;
    lgfx::Light_PWM       _light_instance;
    lgfx::Touch_XPT2046   _touch_instance;

public:
    LGFX_CYD()
    {
        // ── SPI bus (VSPI) ────────────────────────────────────────────
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host    = VSPI_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 40000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = true;
            cfg.use_lock    = true;
            cfg.dma_channel = 1;
            cfg.pin_sclk    = 14;
            cfg.pin_mosi    = 13;
            cfg.pin_miso    = 12;
            cfg.pin_dc      =  2;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // ── Panel (ILI9341 240×320) ───────────────────────────────────
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs          = 15;
            cfg.pin_rst         = -1;
            cfg.pin_busy        = -1;
            cfg.memory_width    = 240;
            cfg.memory_height   = 320;
            cfg.panel_width     = 240;
            cfg.panel_height    = 320;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 0;
            cfg.rgb_order       = true;   // BGR — CYD uses BGR colour order
            _panel_instance.config(cfg);
        }

        // ── Backlight (PWM on GPIO 21) ────────────────────────────────
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl     = 21;
            cfg.invert     = false;
            cfg.freq       = 4000;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        // ── Touch (XPT2046 on HSPI) ────────────────────────────────────
        // NOTE: main.cpp still drives touch directly via the
        // XPT2046_Touchscreen library.  This block is present so LGFX
        // knows the pin wiring in case we ever switch to LGFX's built-in
        // touch API, but for now touch is handled externally.
        {
            auto cfg = _touch_instance.config();
            cfg.x_min          = 300;
            cfg.x_max          = 3900;
            cfg.y_min          = 200;
            cfg.y_max          = 3900;
            cfg.pin_int        = 36;
            cfg.bus_shared     = false;
            cfg.offset_rotation = 0;
            cfg.spi_host       = HSPI_HOST;
            cfg.freq           = 2500000;
            cfg.pin_sclk       = 25;
            cfg.pin_mosi       = 32;
            cfg.pin_miso       = 39;
            cfg.pin_cs         = 33;
            _touch_instance.config(cfg);
            _panel_instance.setTouch(&_touch_instance);
        }

        setPanel(&_panel_instance);
    }

    // ── TFT_eSPI compatibility shims ─────────────────────────────────

    /// Map TFT_eSPI setTextFont(uint8_t) → LovyanGFX setFont(IFont*).
    /// Font 0→GLCD, 1→GLCD, 2→Font2 (16 px), 4→Font4 (26 px).
    /// Others (3,5,6,7) are not used by this project and are ignored.
    void setTextFont(uint8_t font)
    {
        // Look-up table – static const so it's only built once.
        static const lgfx::IFont* const tbl[] = {
            &lgfx::fonts::Font0,   // 0 → GLCD  (6×8)
            &lgfx::fonts::Font0,   // 1 → GLCD  (closest match – TFT_eSPI font 1 is 7 px)
            &lgfx::fonts::Font2,   // 2 → Font2 (16 px)
            nullptr,                // 3 – unused
            &lgfx::fonts::Font4,   // 4 → Font4 (26 px)
            nullptr, nullptr, nullptr,
        };
        if (font < 8 && tbl[font])
            this->setFont(tbl[font]);
    }
};
