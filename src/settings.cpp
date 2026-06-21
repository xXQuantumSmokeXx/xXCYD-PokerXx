/**
 * CYD-Poker — Settings: starting credits, refill system, settings screen
 */
#include "settings.h"
#include "config.h"
#include "theme.h"
#include <Preferences.h>

// ── Preset values ────────────────────────────────────────────────────────────
const unsigned long g_creditPresets[CREDIT_PRESET_COUNT] = {50, 100, 200, 500, 1000};
const uint16_t g_intervalPresets[INTERVAL_PRESET_COUNT] = {15, 30, 60, 120};
const char*     g_intervalLabels[INTERVAL_PRESET_COUNT] = {"15 min", "30 min", "1 hour", "2 hours"};

// ── Global state ─────────────────────────────────────────────────────────────
SettingsState g_settings = {
    100,        // startingCredits
    REFILL_OFF, // refillMode
    60,         // refillMinutes
    100,        // refillAmount
    1,          // preselectIdx (100)
    1,          // refillPreselectIdx (100)
    2           // intervalIdx (1 hour)
};

unsigned long g_refillTimerStart  = 0;
bool          g_refillTimerActive = false;
bool          g_refillDoneSession = false;

// Saved game mode so we can return to the right game
uint8_t       g_prevGameMode = 0;

// Forward refs for local helpers
static void drawSettingRow(TFT_eSPI &d, int rowY, const char* label,
                           const char* value, bool active);
static const char* refillModeLabel(uint8_t mode);
static const char* refillIntervalLabel(uint16_t minutes);

// ── NVS persistence ──────────────────────────────────────────────────────────

void settingsLoad() {
    Preferences p;
    p.begin("cyd-poker", true);

    g_settings.startingCredits = p.getULong("start_cred", 100);
    g_settings.refillMode      = p.getUChar("refill_en", REFILL_OFF);
    g_settings.refillMinutes   = p.getUShort("refill_min", 60);
    g_settings.refillAmount    = p.getULong("refill_amt", g_settings.startingCredits);

    // Find preset indices
    for (int i = 0; i < CREDIT_PRESET_COUNT; i++) {
        if (g_creditPresets[i] == g_settings.startingCredits)
            g_settings.preselectIdx = i;
        if (g_creditPresets[i] == g_settings.refillAmount)
            g_settings.refillPreselectIdx = i;
    }
    for (int i = 0; i < INTERVAL_PRESET_COUNT; i++) {
        if (g_intervalPresets[i] == g_settings.refillMinutes)
            g_settings.intervalIdx = i;
    }

    p.end();
}

void settingsSave() {
    Preferences p;
    p.begin("cyd-poker", false);
    p.putULong("start_cred", g_settings.startingCredits);
    p.putUChar("refill_en",  g_settings.refillMode);
    p.putUShort("refill_min", g_settings.refillMinutes);
    p.putULong("refill_amt", g_settings.refillAmount);
    p.end();
}

void settingsInit() {
    settingsLoad();

    // Session mode: refill on boot if needed
    if (g_settings.refillMode == REFILL_SESSION) {
        g_refillDoneSession = false;  // allow one refill this session
    }
}

// ── Refill label helpers ─────────────────────────────────────────────────────

static const char* refillModeLabel(uint8_t mode) {
    switch (mode) {
        case REFILL_OFF:     return "Off";
        case REFILL_TIMER:   return "Timer";
        case REFILL_SESSION: return "Session";
        default:             return "?";
    }
}

static const char* refillIntervalLabel(uint16_t minutes) {
    for (int i = 0; i < INTERVAL_PRESET_COUNT; i++) {
        if (g_intervalPresets[i] == minutes)
            return g_intervalLabels[i];
    }
    return "1 hour";
}

// ── Setting row helper ───────────────────────────────────────────────────────

static void drawSettingRow(TFT_eSPI &d, int rowY, const char* label,
                           const char* value, bool active) {
    uint16_t labelCol = active ? g_themeColor : COL_DIM_GRAY;
    uint16_t valCol   = active ? g_themeColor : COL_DIM_GRAY;

    // Label
    d.setTextFont(2);
    d.setTextColor(labelCol, COL_BG);
    d.setTextDatum(TL_DATUM);
    d.drawString(label, SET_LABEL_X, rowY + (SET_ROW_H - 16) / 2);

    // Value box
    int vy = rowY + (SET_ROW_H - SET_VAL_H) / 2;
    d.fillRoundRect(SET_VAL_X, vy, SET_VAL_W, SET_VAL_H, 4, COL_BG);
    if (active) {
        d.drawRoundRect(SET_VAL_X, vy, SET_VAL_W, SET_VAL_H, 4, g_themeColor);
        d.drawRoundRect(SET_VAL_X + 1, vy + 1, SET_VAL_W - 2, SET_VAL_H - 2, 4, g_themeColor);
    } else {
        d.drawRoundRect(SET_VAL_X, vy, SET_VAL_W, SET_VAL_H, 4, COL_DIM_GRAY);
    }

    d.setTextColor(valCol, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString(value, SET_VAL_X + SET_VAL_W / 2, vy + SET_VAL_H / 2);
}

// ── Settings screen drawing ──────────────────────────────────────────────────

void settingsDraw(TFT_eSPI &d) {
    d.fillScreen(COL_BG);

    // ── Back button (top-left) ──
    d.fillRoundRect(SET_BACK_X, SET_BACK_Y, SET_BACK_W, SET_BACK_H, 4, COL_BG);
    d.drawRoundRect(SET_BACK_X, SET_BACK_Y, SET_BACK_W, SET_BACK_H, 4, g_themeColor);
    d.drawRoundRect(SET_BACK_X + 1, SET_BACK_Y + 1, SET_BACK_W - 2, SET_BACK_H - 2, 4, g_themeColor);
    d.setTextFont(2);
    d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(MC_DATUM);
    d.drawString("BACK", SET_BACK_X + SET_BACK_W / 2, SET_BACK_Y + SET_BACK_H / 2);

    // ── Title ──
    d.setTextFont(4);
    d.setTextColor(g_themeColor, COL_BG);
    d.setTextDatum(TC_DATUM);
    d.drawString("SETTINGS", SCREEN_W / 2, 8);

    // ── Divider line ──
    d.drawFastHLine(SET_LIST_X, SET_LIST_Y - 2, SET_LIST_W, g_themeColor);

    // ── Row 0: Starting Credits ──
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", g_settings.startingCredits);
    drawSettingRow(d, SET_LIST_Y, "Start Credits", buf, true);

    // ── Row 1: Refill Mode ──
    int row1Y = SET_LIST_Y + SET_ROW_H + SET_ROW_GAP;
    drawSettingRow(d, row1Y, "Refill Mode", refillModeLabel(g_settings.refillMode), true);

    // ── Row 2: Refill Interval (only active in Timer mode) ──
    int row2Y = row1Y + SET_ROW_H + SET_ROW_GAP;
    bool intervalActive = (g_settings.refillMode == REFILL_TIMER);
    drawSettingRow(d, row2Y, "Refill Interval",
                   refillIntervalLabel(g_settings.refillMinutes), intervalActive);

    // ── Row 3: Refill Amount ──
    int row3Y = row2Y + SET_ROW_H + SET_ROW_GAP;
    const char* amtLabel;
    char amtBuf[16];
    if (g_settings.refillPreselectIdx >= CREDIT_PRESET_COUNT) {
        amtLabel = "=Start";
    } else {
        snprintf(amtBuf, sizeof(amtBuf), "%lu", g_settings.refillAmount);
        amtLabel = amtBuf;
    }
    drawSettingRow(d, row3Y, "Refill Amount", amtLabel, true);

    // ── REFILL NOW button ──
    int btnW = 160, btnH = 26;
    int btnX = (SCREEN_W - btnW) / 2;
    d.fillRoundRect(btnX, SET_REFILL_BTN_Y, btnW, btnH, 6, COL_BG);
    d.drawRoundRect(btnX, SET_REFILL_BTN_Y, btnW, btnH, 6, g_themeColor);
    d.drawRoundRect(btnX + 1, SET_REFILL_BTN_Y + 1, btnW - 2, btnH - 2, 6, g_themeColor);
    d.setTextFont(2);
    d.setTextColor(g_themeColor, COL_BG);
    d.drawString("REFILL NOW", SCREEN_W / 2, SET_REFILL_BTN_Y + btnH / 2);
}

// ── Settings touch handling ──────────────────────────────────────────────────

bool settingsTap(int16_t tx, int16_t ty) {
    // ── Back button ──
    if (tx >= SET_BACK_X && tx <= SET_BACK_X + SET_BACK_W &&
        ty >= SET_BACK_Y && ty <= SET_BACK_Y + SET_BACK_H) {
        return true;  // caller handles mode switch
    }

    // ── Row 0: Starting Credits ──
    int row0Y = SET_LIST_Y;
    if (tx >= SET_VAL_X && tx <= SET_VAL_X + SET_VAL_W &&
        ty >= row0Y && ty <= row0Y + SET_ROW_H) {
        g_settings.preselectIdx = (g_settings.preselectIdx + 1) % CREDIT_PRESET_COUNT;
        g_settings.startingCredits = g_creditPresets[g_settings.preselectIdx];
        settingsSave();
        return true;
    }

    // ── Row 1: Refill Mode ──
    int row1Y = SET_LIST_Y + SET_ROW_H + SET_ROW_GAP;
    if (tx >= SET_VAL_X && tx <= SET_VAL_X + SET_VAL_W &&
        ty >= row1Y && ty <= row1Y + SET_ROW_H) {
        g_settings.refillMode = (g_settings.refillMode + 1) % 3;
        settingsSave();

        // Reset refill tracking when mode changes
        g_refillTimerActive = false;
        g_refillTimerStart  = 0;
        g_refillDoneSession = false;
        return true;
    }

    // ── Row 2: Refill Interval (only if Timer mode active) ──
    int row2Y = row1Y + SET_ROW_H + SET_ROW_GAP;
    if (g_settings.refillMode == REFILL_TIMER &&
        tx >= SET_VAL_X && tx <= SET_VAL_X + SET_VAL_W &&
        ty >= row2Y && ty <= row2Y + SET_ROW_H) {
        g_settings.intervalIdx = (g_settings.intervalIdx + 1) % INTERVAL_PRESET_COUNT;
        g_settings.refillMinutes = g_intervalPresets[g_settings.intervalIdx];
        settingsSave();
        return true;
    }

    // ── Row 3: Refill Amount ──
    int row3Y = row2Y + SET_ROW_H + SET_ROW_GAP;
    if (tx >= SET_VAL_X && tx <= SET_VAL_X + SET_VAL_W &&
        ty >= row3Y && ty <= row3Y + SET_ROW_H) {
        g_settings.refillPreselectIdx++;
        if (g_settings.refillPreselectIdx > CREDIT_PRESET_COUNT)
            g_settings.refillPreselectIdx = 0;
        if (g_settings.refillPreselectIdx >= CREDIT_PRESET_COUNT) {
            // "Same as Start" — refillAmount tracks startingCredits dynamically
            g_settings.refillAmount = g_settings.startingCredits;
        } else {
            g_settings.refillAmount = g_creditPresets[g_settings.refillPreselectIdx];
        }
        settingsSave();
        return true;
    }

    // ── REFILL NOW button ──
    int btnW = 160, btnH = 26;
    int btnX = (SCREEN_W - btnW) / 2;
    if (tx >= btnX && tx <= btnX + btnW &&
        ty >= SET_REFILL_BTN_Y && ty <= SET_REFILL_BTN_Y + btnH) {
        return true;  // caller handles the credit refill + save
    }

    return false;
}

// ── Refill logic ─────────────────────────────────────────────────────────────

void settingsApplyRefill(unsigned long &credits) {
    unsigned long amount = g_settings.refillAmount;
    // "Same as Start" special case
    if (g_settings.refillPreselectIdx >= CREDIT_PRESET_COUNT) {
        amount = g_settings.startingCredits;
    }
    credits = amount;
    g_refillTimerActive = false;
    g_refillTimerStart  = 0;
    g_refillDoneSession = true;
}

void settingsCheckRefill(unsigned long &credits) {
    switch (g_settings.refillMode) {

    case REFILL_OFF:
        // No automatic refill
        break;

    case REFILL_TIMER: {
        unsigned long amount = g_settings.refillAmount;
        if (g_settings.refillPreselectIdx >= CREDIT_PRESET_COUNT) {
            amount = g_settings.startingCredits;
        }

        // Start timer when credits drop below refill amount
        if (credits < amount && !g_refillTimerActive) {
            g_refillTimerStart  = millis();
            g_refillTimerActive = true;
        }

        // Cancel timer if credits are replenished by other means
        if (credits >= amount && g_refillTimerActive) {
            g_refillTimerActive = false;
            g_refillTimerStart  = 0;
        }

        // Check if timer has elapsed
        if (g_refillTimerActive) {
            unsigned long elapsedMs = millis() - g_refillTimerStart;
            unsigned long targetMs  = (unsigned long)g_settings.refillMinutes * 60000UL;

            if (elapsedMs >= targetMs) {
                credits = amount;
                g_refillTimerActive = false;
                g_refillTimerStart  = 0;
            }
        }
        break;
    }

    case REFILL_SESSION: {
        unsigned long amount = g_settings.refillAmount;
        if (g_settings.refillPreselectIdx >= CREDIT_PRESET_COUNT) {
            amount = g_settings.startingCredits;
        }

        // Refill once per boot session if below amount
        if (!g_refillDoneSession && credits < amount) {
            credits = amount;
            g_refillDoneSession = true;
        }

        // Allow re-trigger if credits drop below refill amount again
        // (user could burn through refill in same session)
        // Actually — session mode means ONE refill per boot. Keep it simple.
        break;
    }

    }
}

// ── Gear icon ────────────────────────────────────────────────────────────────

void settingsDrawGearIcon(TFT_eSPI &d) {
    // Simple ring — matches power button style
    d.drawCircle(GEAR_BTN_X, GEAR_BTN_Y, GEAR_BTN_R, g_themeColor);
    d.drawCircle(GEAR_BTN_X, GEAR_BTN_Y, GEAR_BTN_R - 1, g_themeColor);
}

bool settingsHitGearIcon(int16_t tx, int16_t ty) {
    int dx = tx - GEAR_BTN_X;
    int dy = ty - GEAR_BTN_Y;
    return (dx * dx + dy * dy <= (GEAR_BTN_R + 5) * (GEAR_BTN_R + 5));
}
