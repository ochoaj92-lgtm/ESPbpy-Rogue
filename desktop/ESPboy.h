#pragma once

// The desktop target supplies only the hardware API used by the shared game.
#ifdef POCKET_POKER_DESKTOP

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#define PROGMEM
#define PSTR(value) (value)
#define F(value) (value)
#define strlen_P(value) std::strlen(value)

inline uint8_t pgm_read_byte(const void* pointer) {
    return *static_cast<const uint8_t*>(pointer);
}
inline uint32_t pgm_read_dword(const void* pointer) {
    uint32_t value;
    std::memcpy(&value, pointer, sizeof(value));
    return value;
}
template <typename T> T constrain(T value, T minimum, T maximum) {
    return value < minimum ? minimum : value > maximum ? maximum : value;
}
uint32_t millis();
uint32_t micros();
void delay(uint32_t milliseconds);

class DesktopSerial {
public:
    void begin(unsigned long) {}
    int printf(const char* format, ...);
    void println(const char* value);
};
extern DesktopSerial Serial;

class DesktopESP {
public:
    unsigned getFreeHeap() const { return 0; }
    unsigned getMaxFreeBlockSize() const { return 0; }
    unsigned getSketchSize() const { return 0; }
    uint32_t getChipId() const { return 0; }
};
extern DesktopESP ESP;

class Button {
public:
    static constexpr uint8_t LEFT = 0, UP = 1, DOWN = 2, RIGHT = 3;
    static constexpr uint8_t ACT = 4, ESC = 5, TOP_LEFT = 6, TOP_RIGHT = 7;
    void read(uint8_t input);
    void clear();
    bool pressed(uint8_t button) const;
    bool released(uint8_t button) const;
    bool held(uint8_t button, uint32_t delayMilliseconds = 0) const;

private:
    enum class State : uint8_t { Free, Pressed, Held, Released };
    std::array<State, 8> states_{};
    std::array<uint32_t, 8> heldSince_{};
};

class DesktopTFT {
public:
    void fillScreen(uint16_t) {}
    void setTextColor(uint16_t) {}
    void setCursor(int, int) {}
    void print(const char* value);
};

class ESPboy {
public:
    DesktopTFT tft;
    Button button;
    void begin(const char*) {}
    void update();
    uint8_t buttons() const;
};
extern ESPboy espboy;

class LGFX_Sprite {
public:
    explicit LGFX_Sprite(DesktopTFT*) {}
    void setColorDepth(int depth) { depth_ = depth; }
    void* createSprite(int width, int height);
    bool hasPalette() const { return created_; }
    void setPaletteColor(std::size_t index, uint32_t color);
    void fillSprite(uint8_t color);
    void fillRect(int x, int y, int width, int height, uint8_t color);
    void fillRoundRect(int x, int y, int width, int height, int radius, uint8_t color);
    void drawRoundRect(int x, int y, int width, int height, int radius, uint8_t color);
    void drawFastHLine(int x, int y, int width, uint8_t color);
    void fillCircle(int x, int y, int radius, uint8_t color);
    void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color);
    void pushSprite(int x, int y);
    uint32_t rgbAt(int x, int y) const;
    uint64_t hash() const;

private:
    void pixel(int x, int y, uint8_t color);
    std::array<uint8_t, 8192> pixels_{};
    std::array<uint32_t, 16> palette_{};
    int depth_ = 4;
    bool created_ = false;
};

namespace host {
// Public event hooks also allow finite keyboard smoke runs without a device.
bool initialize();
void shutdown();
void handleEvent(const SDL_Event& event);
void pollEvents();
bool running();
void quit();
bool saveScreenshot(const char* path);
uint64_t framebufferHash();
}

#endif  // POCKET_POKER_DESKTOP
