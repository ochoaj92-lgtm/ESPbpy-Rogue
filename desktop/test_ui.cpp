// Compile this translation unit instead of src/main.cpp so tests exercise the
// actual private UI state and handlers without adding a production test API.
#include <cassert>
#include <cstdio>
#include <SDL.h>
#include "../src/main.cpp"

namespace {

void pump(uint32_t duration = 20) {
    const uint32_t started = millis();
    do {
        host::pollEvents();
        loop();
    } while (millis() - started < duration);
}

void key(SDL_Keycode code, bool down, bool repeat = false) {
    SDL_Event event{};
    event.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    event.key.repeat = repeat ? 1 : 0;
    event.key.keysym.sym = code;
    event.key.keysym.scancode = SDL_GetScancodeFromKey(code);
    assert(SDL_PushEvent(&event) == 1);
    pump();
}

void tap(SDL_Keycode code) { key(code, true); key(code, false); }

void skipScore() {
    assert(screen == Screen::Score);
    pump(270);
    tap(SDLK_RETURN);
}

void winCurrentBlind() {
    assert(screen == Screen::Hand);
    game.handsRemaining = 1;
    game.roundScore = poker::blindTarget(game.blindIndex) - 1;
    selection = 0;
    tap(SDLK_z);
    tap(SDLK_e);
    assert(game.handsRemaining == 0);
    assert(game.phase == (game.blindIndex == 2 ? Phase::Won : Phase::Shop));
    skipScore();
    assert(screen == Screen::Summary);
}

} // namespace

int main() {
    assert(host::initialize());
    setup();
    pump(40);
    assert(ready && screen == Screen::Title);

    tap(SDLK_z);
    assert(screen == Screen::Intro);
    tap(SDLK_RETURN);
    assert(screen == Screen::Hand && game.handCount == 8);

    // All A aliases share one logical button, even while aliases overlap.
    key(SDLK_z, true);
    assert(selection == 1);
    key(SDLK_z, true, true); // SDL auto-repeat must not toggle the card.
    pump(400);
    assert(selection == 1);
    key(SDLK_RETURN, true);
    key(SDLK_z, false);
    assert(selection == 1 && (espboy.buttons() & (1 << Button::ACT)));
    key(SDLK_RETURN, false);
    assert(!(espboy.buttons() & (1 << Button::ACT)));
    tap(SDLK_SPACE);
    assert(selection == 0);

    // A complete tap between two game polls must retain its leading edge.
    SDL_Event quick{};
    quick.type = SDL_KEYDOWN;
    quick.key.state = SDL_PRESSED;
    quick.key.keysym.sym = SDLK_z;
    quick.key.keysym.scancode = SDL_SCANCODE_Z;
    host::handleEvent(quick);
    quick.type = SDL_KEYUP;
    quick.key.state = SDL_RELEASED;
    host::handleEvent(quick);
    pump();
    assert(selection == 1);
    tap(SDLK_SPACE);
    assert(selection == 0);

    tap(SDLK_RIGHT);
    tap(SDLK_DOWN);
    assert(cursor == 5);
    tap(SDLK_UP);
    tap(SDLK_LEFT);
    assert(cursor == 0);

    // A lost-focus event clears held keys instead of leaving navigation stuck.
    key(SDLK_RIGHT, true);
    SDL_Event focus{};
    focus.type = SDL_WINDOWEVENT;
    focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
    host::handleEvent(focus);
    pump();
    assert(espboy.buttons() == 0);
    const uint8_t focusedCursor = cursor;
    pump(400);
    assert(cursor == focusedCursor);

    tap(SDLK_SPACE);
    const uint8_t selected = selection;
    tap(SDLK_ESCAPE);
    assert(screen == Screen::Pause && selection == selected);
    tap(SDLK_x);
    assert(screen == Screen::Hand && selection == selected);

    key(SDLK_q, true);
    assert(game.discardsRemaining == 3 && selection == 0);
    pump(400);
    tap(SDLK_z);
    assert(game.discardsRemaining == 3 && selection != 0);
    key(SDLK_q, false);

    const auto expected = poker::preview(game, selection);
    key(SDLK_e, true);
    assert(screen == Screen::Score && game.handsRemaining == 3);
    assert(game.lastScore.total == expected.total);
    key(SDLK_e, true, true);
    pump(1350); // Exercise the real timed score-to-hand transition.
    assert(screen == Screen::Hand && game.handsRemaining == 3);
    tap(SDLK_SPACE);
    assert(selection != 0 && game.handsRemaining == 3);
    key(SDLK_e, false);

    // Deterministic near-target fixtures keep this UI test about transitions.
    for (uint8_t blind = 0; blind < 2; ++blind) {
        assert(game.blindIndex == blind);
        winCurrentBlind();
        tap(SDLK_RETURN);
        assert(screen == Screen::Shop);
        const auto offer = game.offers[0];
        const auto cash = game.cash;
        assert(offer != poker::JokerId::None && cash >= poker::jokerPrice(offer));
        tap(SDLK_z);
        assert(game.jokerCount == blind + 1);
        assert(game.cash == cash - poker::jokerPrice(offer));
        assert(game.offers[0] == poker::JokerId::None);
        tap(SDLK_ESCAPE);
        assert(screen == Screen::Intro && game.blindIndex == blind + 1);
        tap(SDLK_RETURN);
        assert(screen == Screen::Hand);
    }
    assert(game.handCount == 7);
    winCurrentBlind();
    assert(game.phase == Phase::Won);

    tap(SDLK_RETURN);
    assert(screen == Screen::Intro && game.jokerCount == 0 && game.cash == 4);
    tap(SDLK_z);
    game.handsRemaining = 1;
    tap(SDLK_z);
    tap(SDLK_e);
    skipScore();
    assert(screen == Screen::Summary && game.phase == Phase::Lost);
    tap(SDLK_ESCAPE);
    assert(screen == Screen::Title);
    host::shutdown();
    std::puts("Desktop UI integration checks passed.");
}
