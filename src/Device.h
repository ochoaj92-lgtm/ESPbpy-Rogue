#pragma once

#include <ESPboy.h>

namespace device {
enum Color : uint8_t {
    Background, Panel, Felt, Cream, Ink, Red, Gold, Muted,
    Blue, Green, Violet, Orange, Pink, White, Shadow, Cyan
};
extern LGFX_Sprite canvas;
bool begin();
void text(int x, int y, const char* value, uint8_t color = Cream, int scale = 1);
void centered(int y, const char* value, uint8_t color = Cream, int scale = 1);
int wrapped(int x, int y, const char* value, int columns = 20,
            uint8_t color = Cream, int maxLines = 5);
void number(int x, int y, unsigned long value, uint8_t color = Cream, int scale = 1);
void heading(const char* value);
void footer(const char* value);
void suit(int x, int y, uint8_t kind, uint8_t color);
void diagnostic(uint8_t down, uint8_t seen);
}
