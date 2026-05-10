#pragma once

#include <Arduino.h>
#include <M5GFX.h>

namespace UiTheme {
constexpr uint16_t BG = 0x0000;
constexpr uint16_t BG_DEEP = 0x0000;
constexpr uint16_t PANEL = 0x2124;
constexpr uint16_t PANEL_2 = 0x2945;
constexpr uint16_t PANEL_LIGHT = 0x3186;
constexpr uint16_t TEXT = 0xEF7D;
constexpr uint16_t TEXT_DIM = 0x7BEF;
constexpr uint16_t CYAN = 0x4B5F;
constexpr uint16_t CYAN_DIM = 0x2D1B;
constexpr uint16_t AMBER = 0xFD40;
constexpr uint16_t PINK = 0xF980;
constexpr uint16_t GREEN = 0x77E0;
constexpr uint16_t RED = 0xF980;
constexpr uint16_t BLUE = 0x5D3F;

inline void drawPanel(M5Canvas& c, int x, int y, int w, int h, uint16_t accent = CYAN) {
    c.fillRoundRect(x, y, w, h, 7, PANEL);
    c.drawRoundRect(x, y, w, h, 7, PANEL_LIGHT);
    c.drawFastHLine(x + 8, y, w - 16, accent);
}

inline void drawStatusPill(M5Canvas& c, int x, int y, int w, const char* label,
                           uint16_t color, uint16_t bg = PANEL) {
    c.fillRoundRect(x, y, w, 20, 8, bg);
    c.drawRoundRect(x, y, w, 20, 8, color);
    c.setTextDatum(MC_DATUM);
    c.setTextSize(1);
    c.setTextColor(color, bg);
    c.drawString(label, x + w / 2, y + 10);
    c.setTextDatum(TL_DATUM);
}

inline void drawBackButton(M5Canvas& c, int x, int y, int w, int h) {
    c.fillRoundRect(x, y, w, h, 6, PANEL);
    c.drawRoundRect(x, y, w, h, 6, PANEL_LIGHT);
    c.setTextDatum(MC_DATUM);
    c.setTextSize(1);
    c.setTextColor(TEXT_DIM, PANEL);
    c.drawString("< Back", x + w / 2, y + h / 2 + 1);
    c.setTextDatum(TL_DATUM);
}

inline void drawTitle(M5Canvas& c, const char* title, const char* sub, uint16_t accent = CYAN) {
    c.setTextDatum(TC_DATUM);
    c.setTextSize(1);
    c.setTextColor(accent, BG);
    c.drawString(title, c.width() / 2, 8);
    if (sub && sub[0] != '\0') {
        c.setTextColor(TEXT_DIM, BG);
        c.drawString(sub, c.width() / 2, 22);
    }
    c.setTextDatum(TL_DATUM);
}
}
