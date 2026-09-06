#include "Device.h"
#include <ESP8266WiFi.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace device {
LGFX_Sprite canvas(&espboy.tft);

// Original 5x7 bitmap alphabet, row-major, held entirely in flash.
// Each byte is one five-pixel row; lowercase is drawn as uppercase.
static const uint8_t font[][7] PROGMEM = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, // 0 1
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, // A B
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, // Y Z
    {4,15,20,14,5,30,4}, // $
    {0,4,4,31,4,4,0}, {0,0,0,31,0,0,0}, // + -
    {1,2,2,4,8,8,16}, {0,4,4,0,4,4,0}, // / :
    {0,0,0,0,0,12,12}, {8,4,2,1,2,4,8}, // . >
    {2,4,8,16,8,4,2}, {0,31,0,31,0,0,0}, // < =
    {4,4,4,4,4,0,4}, {14,17,1,2,4,0,4}, // ! ?
    {0,0,0,0,0,4,8}, {2,4,8,8,8,4,2}, // , (
    {8,4,2,2,2,4,8}, {0,0,0,0,0,0,31}, // ) _
    {4,4,0,0,0,0,0}, // '
};
static const char punctuation[] PROGMEM = "$+-/:.><=!?,()_'";
static const uint32_t palette[16] PROGMEM = {
    0x101D24, 0x20343D, 0x16584D, 0xF4EACF,
    0x17272E, 0xC7354D, 0xF4BF58, 0x95ACA8,
    0x69ADE2, 0x83D48C, 0xB298DC, 0xEB9461,
    0xECA8B9, 0xFFFFFF, 0x0B1219, 0x71D6D0
};

static int glyph(char c) {
    c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    for (int i = 0; i < 16; ++i)
        if (c == static_cast<char>(pgm_read_byte(punctuation + i))) return i + 36;
    return -1;
}

bool begin() {
    Serial.begin(115200);
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    espboy.begin(F("POCKET POKER"));
    canvas.setColorDepth(4);
    if (!canvas.createSprite(128, 128) || !canvas.hasPalette()) {
        Serial.println(F("ERROR: framebuffer allocation failed"));
        espboy.tft.fillScreen(0);
        espboy.tft.setTextColor(0xffff);
        espboy.tft.setCursor(0, 20);
        espboy.tft.print(F("Not enough memory.\nPlease restart."));
        return false;
    }
    for (int i = 0; i < 16; ++i)
        canvas.setPaletteColor(i, pgm_read_dword(palette + i));
#ifdef POCKET_POKER_DESKTOP
    Serial.println(F("Pocket Poker desktop: ESP8266 memory is not measured on the PC."));
#else
    Serial.printf("Pocket Poker boot: heap=%u max_block=%u sketch=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getSketchSize());
#endif
    return true;
}

void text(int x, int y, const char* value, uint8_t color, int scale) {
    if (!value) return;
    const int origin = x;
    for (char c; (c = pgm_read_byte(value++)) != 0;) {
        if (c == '\n') { x = origin; y += 9 * scale; continue; }
        const int index = glyph(c);
        if (index >= 0) {
            for (int row = 0; row < 7; ++row) {
                const uint8_t bits = pgm_read_byte(&font[index][row]);
                for (int col = 0; col < 5; ++col)
                    if (bits & (1 << (4 - col)))
                        canvas.fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
        x += 6 * scale;
    }
}

void centered(int y, const char* value, uint8_t color, int scale) {
    text((128 - static_cast<int>(strlen_P(value)) * 6 * scale + scale) / 2,
         y, value, color, scale);
}

int wrapped(int x, int y, const char* value, int columns, uint8_t color, int maxLines) {
    char line[22];
    columns = constrain(columns, 1, 21);
    for (int row = 0; row < maxLines && pgm_read_byte(value); ++row) {
        while (pgm_read_byte(value) == ' ') ++value;
        int count = 0, lastSpace = -1;
        while (count < columns && pgm_read_byte(value + count) && pgm_read_byte(value + count) != '\n') {
            line[count] = pgm_read_byte(value + count);
            if (line[count] == ' ') lastSpace = count;
            ++count;
        }
        int consumed = count;
        if (count == columns && pgm_read_byte(value + count) &&
            pgm_read_byte(value + count) != ' ' && lastSpace > 0) {
            count = lastSpace;
            consumed = lastSpace + 1;
        }
        line[count] = 0;
        text(x, y, line, color);
        value += consumed;
        if (pgm_read_byte(value) == '\n') ++value;
        y += 10;
    }
    return y;
}

void number(int x, int y, unsigned long value, uint8_t color, int scale) {
    char buffer[12];
    snprintf(buffer, sizeof(buffer), "%lu", value);
    text(x, y, buffer, color, scale);
}

void heading(const char* value) {
    canvas.fillSprite(Background);
    canvas.fillRect(0, 0, 128, 17, Panel);
    centered(5, value, Gold);
}

void footer(const char* value) {
    canvas.fillRect(0, 116, 128, 12, Shadow);
    centered(119, value, Muted);
}

void suit(int x, int y, uint8_t kind, uint8_t color) {
    switch (kind) {
    case 0: // Club
        canvas.fillCircle(x + 4, y + 2, 2, color);
        canvas.fillCircle(x + 2, y + 5, 2, color);
        canvas.fillCircle(x + 6, y + 5, 2, color);
        canvas.fillTriangle(x + 4, y + 5, x + 2, y + 9, x + 6, y + 9, color);
        break;
    case 1: // Diamond
        canvas.fillTriangle(x + 4, y, x, y + 4, x + 4, y + 9, color);
        canvas.fillTriangle(x + 4, y, x + 8, y + 4, x + 4, y + 9, color);
        break;
    case 2: // Heart
        canvas.fillCircle(x + 2, y + 2, 2, color);
        canvas.fillCircle(x + 6, y + 2, 2, color);
        canvas.fillTriangle(x, y + 3, x + 8, y + 3, x + 4, y + 9, color);
        break;
    default: // Spade
        canvas.fillTriangle(x + 4, y, x, y + 5, x + 8, y + 5, color);
        canvas.fillCircle(x + 2, y + 5, 2, color);
        canvas.fillCircle(x + 6, y + 5, 2, color);
        canvas.fillTriangle(x + 4, y + 5, x + 2, y + 9, x + 6, y + 9, color);
        break;
    }
}

void diagnostic(uint8_t down, uint8_t seen) {
    heading(PSTR("DEVICE CHECK"));
    for (int i = 0; i < 16; ++i) canvas.fillRect(i * 8, 20, 8, 7, i);
    static const char labels[8][6] PROGMEM = {"LEFT", "UP", "DOWN", "RIGHT", "A", "B", "L TOP", "R TOP"};
    for (int i = 0; i < 8; ++i) {
        const int x = (i % 2) * 62 + 3, y = (i / 2) * 16 + 32;
        canvas.fillRect(x, y, 60, 14, down & (1 << i) ? Gold : Panel);
        text(x + 3, y + 3, labels[i], down & (1 << i) ? Ink : Cream);
        if (seen & (1 << i)) canvas.fillRect(x + 54, y + 4, 3, 5, Green);
    }
#ifdef POCKET_POKER_DESKTOP
    text(3, 101, PSTR("PC PREVIEW / NO USB"), Muted);
#else
    char buffer[22];
    snprintf(buffer, sizeof(buffer), "HEAP %u", ESP.getFreeHeap());
    text(3, 101, buffer, Muted);
#endif
    footer(seen == 255 ? PSTR("ALL SEEN! HOLD B EXIT") : PSTR("PRESS ALL 8 BUTTONS"));
}
}
