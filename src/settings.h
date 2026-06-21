#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

// ── Refill mode ─────────────────────────────────────────────────────────────
enum RefillMode : uint8_t {
    REFILL_OFF     = 0,   // no automatic refill
    REFILL_TIMER   = 1,   // refill after N minutes of awake time
    REFILL_SESSION = 2    // refill once per boot session
};

// ── Preset arrays ───────────────────────────────────────────────────────────
#define CREDIT_PRESET_COUNT  5
extern const unsigned long g_creditPresets[CREDIT_PRESET_COUNT];

#define INTERVAL_PRESET_COUNT  4
extern const uint16_t g_intervalPresets[INTERVAL_PRESET_COUNT];
extern const char*     g_intervalLabels[INTERVAL_PRESET_COUNT];

// ── Settings state ──────────────────────────────────────────────────────────
struct SettingsState {
    unsigned long startingCredits;   // initial credits for new/reset
    uint8_t       refillMode;        // RefillMode enum
    uint16_t      refillMinutes;     // timer interval (minutes)
    unsigned long refillAmount;      // amount to refill to
    int8_t        preselectIdx;      // index into presets for starting credits
    int8_t        refillPreselectIdx;// index into presets for refill amount
    int8_t        intervalIdx;       // index into interval presets
};

extern SettingsState g_settings;

// ── Runtime refill tracking (not persisted) ────────────────────────────────
extern unsigned long g_refillTimerStart;   // millis() when countdown started
extern bool          g_refillTimerActive;  // true = countdown in progress
extern bool          g_refillDoneSession;  // true = already refilled this boot
extern uint8_t       g_prevGameMode;       // game mode to return to from settings

// ── API ────────────────────────────────────────────────────────────────────
void settingsInit();
void settingsLoad();
void settingsSave();
void settingsDraw(TFT_eSPI &d);
bool settingsTap(int16_t tx, int16_t ty);   // returns true if screen needs redraw
void settingsCheckRefill(unsigned long &credits);
void settingsApplyRefill(unsigned long &credits);
void settingsDrawGearIcon(TFT_eSPI &d);
bool settingsHitGearIcon(int16_t tx, int16_t ty);
