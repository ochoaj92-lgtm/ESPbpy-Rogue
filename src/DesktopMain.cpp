#ifdef POCKET_POKER_DESKTOP

#include <ESPboy.h>
#include <ESP8266WiFi.h>
#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void setup();
void loop();

DesktopSerial Serial;
DesktopESP ESP;
DesktopWiFi WiFi;
ESPboy espboy;

namespace {
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;
const LGFX_Sprite* latestCanvas = nullptr;
std::array<bool, SDL_NUM_SCANCODES> keys{};
#ifdef __EMSCRIPTEN__
uint8_t pointerButtons = 0;
#endif
uint8_t pendingPresses = 0;
bool active = true;
bool repaint = false;
bool videoFailed = false;
uint64_t counterOrigin = 0;

uint8_t heldButtons() {
    uint8_t mask = 0;
    if (keys[SDL_SCANCODE_LEFT]) mask |= 1u << Button::LEFT;
    if (keys[SDL_SCANCODE_UP]) mask |= 1u << Button::UP;
    if (keys[SDL_SCANCODE_DOWN]) mask |= 1u << Button::DOWN;
    if (keys[SDL_SCANCODE_RIGHT]) mask |= 1u << Button::RIGHT;
    if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE])
        mask |= 1u << Button::ACT;
    if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_ESCAPE]) mask |= 1u << Button::ESC;
    if (keys[SDL_SCANCODE_Q]) mask |= 1u << Button::TOP_LEFT;
    if (keys[SDL_SCANCODE_E]) mask |= 1u << Button::TOP_RIGHT;
#ifdef __EMSCRIPTEN__
    mask |= pointerButtons;
#endif
    return mask;
}

void clearKeys() {
    keys.fill(false);
#ifdef __EMSCRIPTEN__
    pointerButtons = 0;
#endif
    pendingPresses = 0;
    espboy.button.clear();
}

void displayFailed(const char* operation) {
    std::fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
    videoFailed = true;
    active = false;
}

void present() {
    if (!renderer || !texture || !latestCanvas) return;
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0) {
        displayFailed("Unable to read display size");
        return;
    }
    const int scale = std::max(1, std::min(width, height) / 128);
    const SDL_Rect destination{(width - 128 * scale) / 2, (height - 128 * scale) / 2,
                               128 * scale, 128 * scale};
    if (SDL_SetRenderDrawColor(renderer, 11, 18, 25, 255) != 0 ||
        SDL_RenderClear(renderer) != 0 ||
        SDL_RenderCopy(renderer, texture, nullptr, &destination) != 0) {
        displayFailed("Unable to render display");
        return;
    }
    SDL_RenderPresent(renderer);
    repaint = false;
}

void submit(const LGFX_Sprite& canvas) {
    latestCanvas = &canvas;
    if (!texture) return;
    void* data = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture, nullptr, &data, &pitch) != 0) {
        displayFailed("Unable to update display");
        return;
    }
    for (int y = 0; y < 128; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(data) + y * pitch);
        for (int x = 0; x < 128; ++x) row[x] = 0xff000000u | canvas.rgbAt(x, y);
    }
    SDL_UnlockTexture(texture);
    present();
}

bool initializeVideo() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "Unable to initialize SDL: %s\n", SDL_GetError());
        return false;
    }
    counterOrigin = SDL_GetPerformanceCounter();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window = SDL_CreateWindow(
        "Pocket Poker | Arrows move | Z/Enter/Space select | X/Esc back | Q discard | E play",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 640,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window, 128, 128);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
#ifdef __EMSCRIPTEN__
    // SDL's software renderer can report success before its first Canvas 2D
    // access. Probe it now so an unavailable context becomes a handled startup
    // error instead of a JavaScript exception during the first presentation.
    if (!renderer && EM_ASM_INT({
        try { return Module['canvas'].getContext('2d') ? 1 : 0; }
        catch (error) { return 0; }
    })) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#else
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
#endif
    if (!renderer) {
        std::fprintf(stderr, "Unable to create renderer: %s\n", SDL_GetError());
        return false;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 128, 128);
    if (!texture) {
        std::fprintf(stderr, "Unable to create display texture: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    return true;
}

void shutdownVideo() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
    SDL_Quit();
}

bool insideRoundRect(int x, int y, int width, int height, int radius) {
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    radius = std::max(0, std::min(radius, std::min(width, height) / 2));
    if (radius == 0) return true;
    const int cx = x < radius ? radius : x >= width - radius ? width - radius - 1 : x;
    const int cy = y < radius ? radius : y >= height - radius ? height - radius - 1 : y;
    const int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

#if !defined(POCKET_POKER_DESKTOP_TEST) && !defined(__EMSCRIPTEN__)
struct SmokeEvent { uint32_t at; SDL_Scancode key; bool down; };
const SmokeEvent smokeEvents[] = {
    {120, SDL_SCANCODE_Z, true}, {200, SDL_SCANCODE_Z, false},
    {320, SDL_SCANCODE_RETURN, true}, {400, SDL_SCANCODE_RETURN, false},
    {520, SDL_SCANCODE_Z, true}, {600, SDL_SCANCODE_Z, false},
    {720, SDL_SCANCODE_RIGHT, true}, {800, SDL_SCANCODE_RIGHT, false},
    {920, SDL_SCANCODE_SPACE, true}, {1000, SDL_SCANCODE_SPACE, false},
    {1120, SDL_SCANCODE_ESCAPE, true}, {1200, SDL_SCANCODE_ESCAPE, false},
    {1320, SDL_SCANCODE_ESCAPE, true}, {1400, SDL_SCANCODE_ESCAPE, false},
};
const uint32_t smokeSamples[] = {80, 280, 480, 1080, 1280, 1520};

void injectSmokeEvents(uint32_t elapsed, std::size_t& index) {
    while (index < sizeof(smokeEvents) / sizeof(smokeEvents[0]) && smokeEvents[index].at <= elapsed) {
        const auto& step = smokeEvents[index++];
        SDL_Event event{};
        event.type = step.down ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.windowID = SDL_GetWindowID(window);
        event.key.state = step.down ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.scancode = step.key;
        event.key.keysym.sym = SDL_GetKeyFromScancode(step.key);
        SDL_PushEvent(&event);
    }
}

bool parseFrames(const char* value, uint32_t& frames) {
    if (!value || value[0] < '0' || value[0] > '9') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long result = std::strtoul(value, &end, 10);
    if (errno || *end || result == 0 || result > std::numeric_limits<uint32_t>::max()) return false;
    frames = static_cast<uint32_t>(result);
    return true;
}
#endif
}  // namespace

uint32_t millis() { return static_cast<uint32_t>(SDL_GetTicks64()); }
uint32_t micros() {
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0) return 0;
    const uint64_t elapsed = SDL_GetPerformanceCounter() - counterOrigin;
    return static_cast<uint32_t>((elapsed / frequency) * 1000000u +
                                 (elapsed % frequency) * 1000000u / frequency);
}
void delay(uint32_t milliseconds) {
#ifdef __EMSCRIPTEN__
    // Each browser animation callback returns to JavaScript instead of sleeping
    // on its main thread. The shared game's millis() checks still pace work.
    (void)milliseconds;
#else
    SDL_Delay(milliseconds);
#endif
}

int DesktopSerial::printf(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result = std::vprintf(format, arguments);
    va_end(arguments);
    std::fflush(stdout);
    return result;
}
void DesktopSerial::println(const char* value) { std::puts(value); std::fflush(stdout); }
void DesktopTFT::print(const char* value) { std::fputs(value, stderr); }

void Button::read(uint8_t input) {
    // SDL key events need no electrical debounce. Keep ESPboy's one-poll edge
    // states and begin held timing on the poll after the initial press.
    for (uint8_t i = 0; i < 8; ++i) {
        const bool down = (input & (1u << i)) != 0;
        switch (states_[i]) {
            case State::Free: if (down) states_[i] = State::Pressed; break;
            case State::Pressed:
                states_[i] = down ? State::Held : State::Released;
                if (down) heldSince_[i] = millis();
                break;
            case State::Held: if (!down) states_[i] = State::Released; break;
            case State::Released: states_[i] = State::Free; break;
        }
    }
}
void Button::clear() { states_.fill(State::Free); heldSince_.fill(0); }
bool Button::pressed(uint8_t button) const { return states_[button & 7] == State::Pressed; }
bool Button::released(uint8_t button) const { return states_[button & 7] == State::Released; }
bool Button::held(uint8_t button, uint32_t delayMilliseconds) const {
    return states_[button & 7] == State::Held && millis() - heldSince_[button & 7] >= delayMilliseconds;
}
void ESPboy::update() {
    // A press/release can arrive between the game's 5 ms polls. Latch its
    // leading edge until a poll observes it, then resume the live key state.
    button.read(heldButtons() | pendingPresses);
    for (uint8_t i = 0; i < 8; ++i)
        if (button.pressed(i) || button.held(i)) pendingPresses &= ~(1u << i);
}
uint8_t ESPboy::buttons() const { return heldButtons(); }

void* LGFX_Sprite::createSprite(int width, int height) {
    created_ = width == 128 && height == 128 && depth_ == 4;
    if (created_) pixels_.fill(0);
    return created_ ? pixels_.data() : nullptr;
}
void LGFX_Sprite::setPaletteColor(std::size_t index, uint32_t color) {
    if (index < palette_.size()) palette_[index] = color & 0xffffffu;
}
void LGFX_Sprite::pixel(int x, int y, uint8_t color) {
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return;
    const int index = y * 128 + x;
    auto& byte = pixels_[index / 2];
    color &= 15;
    byte = index & 1 ? (byte & 0xf0) | color : (byte & 0x0f) | (color << 4);
}
uint32_t LGFX_Sprite::rgbAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= 128 || y >= 128) return 0;
    const int index = y * 128 + x;
    const uint8_t byte = pixels_[index / 2];
    return palette_[index & 1 ? byte & 15 : byte >> 4];
}
uint64_t LGFX_Sprite::hash() const {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : pixels_) { hash ^= byte; hash *= 1099511628211ull; }
    return hash;
}
void LGFX_Sprite::fillSprite(uint8_t color) {
    color &= 15;
    pixels_.fill(static_cast<uint8_t>((color << 4) | color));
}
void LGFX_Sprite::fillRect(int x, int y, int width, int height, uint8_t color) {
    if (width <= 0 || height <= 0) return;
    const int right = std::min(128, x + width), bottom = std::min(128, y + height);
    for (int row = std::max(0, y); row < bottom; ++row)
        for (int column = std::max(0, x); column < right; ++column) pixel(column, row, color);
}
void LGFX_Sprite::fillRoundRect(int x, int y, int width, int height, int radius, uint8_t color) {
    const int right = std::min(128, x + width), bottom = std::min(128, y + height);
    for (int row = std::max(0, y); row < bottom; ++row)
        for (int column = std::max(0, x); column < right; ++column)
            if (insideRoundRect(column - x, row - y, width, height, radius)) pixel(column, row, color);
}
void LGFX_Sprite::drawRoundRect(int x, int y, int width, int height, int radius, uint8_t color) {
    const int right = std::min(128, x + width), bottom = std::min(128, y + height);
    for (int row = std::max(0, y); row < bottom; ++row)
        for (int column = std::max(0, x); column < right; ++column)
            if (insideRoundRect(column - x, row - y, width, height, radius) &&
                !insideRoundRect(column - x - 1, row - y - 1, width - 2, height - 2, std::max(0, radius - 1)))
                pixel(column, row, color);
}
void LGFX_Sprite::drawFastHLine(int x, int y, int width, uint8_t color) { fillRect(x, y, width, 1, color); }
void LGFX_Sprite::fillCircle(int x, int y, int radius, uint8_t color) {
    if (radius < 0) return;
    for (int row = std::max(0, y - radius); row <= std::min(127, y + radius); ++row)
        for (int column = std::max(0, x - radius); column <= std::min(127, x + radius); ++column) {
            const int dx = column - x, dy = row - y;
            if (dx * dx + dy * dy <= radius * radius) pixel(column, row, color);
        }
}
void LGFX_Sprite::fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color) {
    const int left = std::max(0, std::min(x1, std::min(x2, x3)));
    const int right = std::min(127, std::max(x1, std::max(x2, x3)));
    const int top = std::max(0, std::min(y1, std::min(y2, y3)));
    const int bottom = std::min(127, std::max(y1, std::max(y2, y3)));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const int a = (x - x1) * (y2 - y1) - (y - y1) * (x2 - x1);
        const int b = (x - x2) * (y3 - y2) - (y - y2) * (x3 - x2);
        const int c = (x - x3) * (y1 - y3) - (y - y3) * (x1 - x3);
        if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) pixel(x, y, color);
    }
}
void LGFX_Sprite::pushSprite(int, int) { submit(*this); }

namespace host {
bool initialize() {
    active = true;
    videoFailed = false;
    repaint = false;
    latestCanvas = nullptr;
    clearKeys();
    return initializeVideo();
}
void shutdown() { clearKeys(); latestCanvas = nullptr; active = false; shutdownVideo(); }
void handleEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) { active = false; clearKeys(); }
    else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_CLOSE) { active = false; clearKeys(); }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) clearKeys();
        if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            event.window.event == SDL_WINDOWEVENT_EXPOSED) repaint = true;
    } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        if (event.key.repeat) return;
        const int code = event.key.keysym.scancode;
        if (code >= 0 && code < SDL_NUM_SCANCODES) {
            const uint8_t previous = heldButtons();
            keys[code] = event.type == SDL_KEYDOWN;
            pendingPresses |= heldButtons() & ~previous;
        }
    }
}
void pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) handleEvent(event);
    if (repaint) present();
}
bool running() { return active; }
void quit() { active = false; clearKeys(); }
uint64_t framebufferHash() { return latestCanvas ? latestCanvas->hash() : 0; }
bool saveScreenshot(const char* path) {
    if (!latestCanvas) { std::fprintf(stderr, "No rendered frame to save.\n"); return false; }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 128, 128, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) { std::fprintf(stderr, "Screenshot allocation failed: %s\n", SDL_GetError()); return false; }
    for (int y = 0; y < 128; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels) + y * surface->pitch);
        for (int x = 0; x < 128; ++x) row[x] = 0xff000000u | latestCanvas->rgbAt(x, y);
    }
    const bool ok = SDL_SaveBMP(surface, path) == 0;
    if (!ok) std::fprintf(stderr, "Unable to save screenshot: %s\n", SDL_GetError());
    SDL_FreeSurface(surface);
    return ok;
}
}  // namespace host

#ifdef __EMSCRIPTEN__
// Pointer controls supply logical held states, independently of SDL's keyboard
// aliases. The page aggregates pointer IDs before changing each button state.
extern "C" EMSCRIPTEN_KEEPALIVE void pocket_key(int button, int down) {
    if (button < 0 || button >= 8 || !host::running()) return;
    const uint8_t previous = heldButtons();
    const uint8_t bit = static_cast<uint8_t>(1u << button);
    if (down) pointerButtons |= bit;
    else pointerButtons &= ~bit;
    pendingPresses |= heldButtons() & ~previous;
}

extern "C" EMSCRIPTEN_KEEPALIVE void pocket_release() { clearKeys(); }

#ifndef POCKET_POKER_DESKTOP_TEST
namespace {
void browserFailure(const char* message) {
    // Notify the page after stopping callbacks and releasing the SDL resources.
    // Keep the message separate from SDL_GetError(), whose storage SDL owns.
    emscripten_cancel_main_loop();
    host::shutdown();
    EM_ASM({
        if (Module['onAbort']) Module['onAbort'](UTF8ToString($0));
    }, message);
}

void browserFrame() {
    static bool initialized = false;
    if (!initialized) {
        // SDL's GLES renderer sets the swap interval during construction.
        // Emscripten requires a registered main loop before that timing call.
        if (!host::initialize()) {
            browserFailure("Unable to initialize the game display.");
            return;
        }
        setup();
        initialized = true;
        // SDL may request zero-delay timers while disabling swap-interval
        // synchronization. Browser presentation should still follow RAF.
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
    }
    host::pollEvents();
    if (host::running()) loop();
    if (!host::running()) {
        if (videoFailed) browserFailure("Unable to update the game display.");
        else {
            emscripten_cancel_main_loop();
            host::shutdown();
        }
    }
}
}  // namespace
#endif
#endif

#ifndef POCKET_POKER_DESKTOP_TEST
#ifdef __EMSCRIPTEN__
int main() {
    std::puts("Pocket Poker browser: arrows move; Z/Enter/Space select; X/Escape back; Q discard; E play.");
    // Zero FPS uses requestAnimationFrame; all game/UI state stays in the
    // shared source files. Register before SDL initializes in the first frame.
    emscripten_set_main_loop(browserFrame, 0, 1);
    return 0;
}
#else
int main(int argc, char** argv) {
    uint32_t frameLimit = 0;
    const char* screenshot = nullptr;
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc && parseFrames(argv[i + 1], frameLimit)) ++i;
        else if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (std::strcmp(argv[i], "--smoke-test") == 0) smoke = true;
        else {
            std::fprintf(stderr, "Usage: %s [--frames N] [--screenshot PATH.bmp] [--smoke-test]\n", argv[0]);
            return std::strcmp(argv[i], "--help") == 0 ? 0 : 2;
        }
    }
    if (smoke && frameLimit == 0) frameLimit = 120;
    if (!host::initialize()) { host::shutdown(); return 1; }
    std::puts("Pocket Poker desktop: arrows move; Z/Enter/Space select; X/Escape back; Q discard; E play. Close the window to quit.");
    setup();
    const uint32_t started = millis();
    uint32_t frames = 0;
    std::size_t eventIndex = 0, sampleIndex = 0;
    std::array<uint64_t, 6> samples{};
    while (host::running() && (frameLimit == 0 || frames < frameLimit || latestCanvas == nullptr)) {
        const uint32_t elapsed = millis() - started;
        if (smoke) injectSmokeEvents(elapsed, eventIndex);
        host::pollEvents();
        if (!host::running()) break;
        loop();
        // Count 60 Hz wall-clock ticks even while the game shows a static screen.
        frames = static_cast<uint32_t>((static_cast<uint64_t>(millis() - started) * 60) / 1000);
        if (smoke && sampleIndex < samples.size() && elapsed >= smokeSamples[sampleIndex]) {
            samples[sampleIndex++] = host::framebufferHash();
        }
    }
    int status = videoFailed ? 1 : 0;
    if (smoke) {
        const bool passed = sampleIndex == samples.size() && samples[0] != 0 &&
            samples[0] != samples[1] && samples[1] != samples[2] &&
            samples[2] != samples[3] && samples[3] != samples[4] && samples[3] == samples[5];
        std::printf("Keyboard smoke test: %s (title, intro, hand, selection, pause, resume).\n", passed ? "PASS" : "FAIL");
        if (!passed) status = 1;
    }
    if (screenshot && !host::saveScreenshot(screenshot)) status = 1;
    host::shutdown();
    return status;
}
#endif
#endif

#endif  // POCKET_POKER_DESKTOP
