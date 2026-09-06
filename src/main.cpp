#include "Device.h"
#include <PokerGame.h>
#include <cstdio>

namespace {
using namespace device;
using poker::ActionStatus;
using poker::Phase;

enum class Screen : uint8_t {
    Title, Intro, Hand, Score, Summary, Shop, Pause, Help, Jokers,
    ConfirmRestart, ConfirmTitle, Diagnostics
};

poker::GameState game;
Screen screen = Screen::Title, returnScreen = Screen::Title, resumeScreen = Screen::Hand;
bool ready = false, dirty = true;
uint8_t selection = 0, cursor = 0, menuItem = 0, jokerIndex = 0;
uint8_t diagnosticSeen = 0, diagnosticDown = 0;
uint32_t lastPoll = 0, lastFrame = 0, lastStats = 0, screenSince = 0;
uint32_t repeatAt[4] = {};
uint32_t toastSince = 0, runs = 0, minHeap = 0;
const char* toast = nullptr;

void changeScreen(Screen next) {
    screen = next;
    screenSince = millis();
    toast = nullptr;
    dirty = true;
}

void message(const char* value) {
    toast = value;
    toastSince = millis();
    dirty = true;
}

void report(const char* event) {
    Serial.printf("%s seed=%lu blind=%u phase=%u score=%lu cash=%u heap=%u min_heap=%lu\n",
                  event, static_cast<unsigned long>(game.seed), game.blindIndex + 1,
                  static_cast<unsigned>(game.phase), static_cast<unsigned long>(game.roundScore),
                  game.cash, ESP.getFreeHeap(), static_cast<unsigned long>(minHeap));
}

void newRun() {
    // Human input timing varies between runs; the logged seed reproduces engine tests.
    const uint32_t seed = micros() ^ ESP.getChipId() ^ (++runs * 0x9e3779b9u);
    poker::startRun(game, seed);
    selection = cursor = menuItem = 0;
    changeScreen(Screen::Intro);
    report("new_run");
}

uint8_t selectedCount() {
    uint8_t count = 0;
    for (uint8_t mask = selection; mask; mask >>= 1) count += mask & 1;
    return count;
}

void drawCard(int x, int y, const poker::Card& card, bool selected, bool focused) {
    canvas.fillRoundRect(x, y, 28, 25, 2, selected ? Gold : Cream);
    const uint8_t ink = card.suit == poker::Suit::Diamonds || card.suit == poker::Suit::Hearts ? Red : Ink;
    char rank[3] = {};
    if (card.rank <= 10) snprintf(rank, sizeof(rank), "%u", card.rank);
    else rank[0] = card.rank == 11 ? 'J' : card.rank == 12 ? 'Q' : card.rank == 13 ? 'K' : 'A';
    text(x + 3, y + 3, rank, ink);
    suit(x + 16, y + 12, static_cast<uint8_t>(card.suit), ink);
    if (selected) text(x + 3, y + 15, PSTR("+"), Ink);
    if (focused) {
        canvas.drawRoundRect(x - 1, y - 1, 30, 27, 2, Cyan);
        canvas.drawFastHLine(x + 3, y + 26, 22, Cyan);
    }
}

void row(int y, const char* value, bool active) {
    if (active) canvas.fillRoundRect(5, y - 3, 118, 14, 2, Gold);
    text(11, y, value, active ? Ink : Cream);
}

void drawTitle() {
    heading(PSTR("ESPBOY DEMO 0.1"));
    centered(24, PSTR("POCKET"), Cream, 2);
    centered(41, PSTR("POKER"), Gold, 2);
    row(66, PSTR("NEW RUN"), menuItem == 0);
    row(83, PSTR("CONTROLS"), menuItem == 1);
    row(100, PSTR("DEVICE CHECK"), menuItem == 2);
    footer(PSTR("UP/DOWN  A CONFIRM"));
}

void drawIntro() {
    heading(PSTR("ANTE 1 / RED DECK"));
    centered(25, poker::blindName(game.blindIndex));
    centered(42, PSTR("BEAT"), Muted);
    char value[12];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(poker::blindTarget(game.blindIndex)));
    centered(54, value, Gold, 2);
    if (game.blindIndex == 2) {
        centered(78, PSTR("BOSS: HAND SIZE -1"), Pink);
        centered(89, PSTR("HOLD 7 CARDS"), Muted);
    } else {
        centered(79, PSTR("4 HANDS / 4 DISCARDS"), Muted);
        centered(91, PSTR("SELECT UP TO 5 CARDS"), Muted);
    }
    footer(PSTR("A DEAL  B PAUSE"));
}

void drawHand() {
    canvas.fillSprite(Felt);
    canvas.fillRect(0, 0, 128, 32, Background);
    char buffer[24];
    const char* blind = game.blindIndex == 0 ? PSTR("SMALL") : game.blindIndex == 1 ? PSTR("BIG") : PSTR("MANACLE");
    text(3, 2, blind, Gold);
    snprintf(buffer, sizeof(buffer), "$%u J%u", game.cash, game.jokerCount);
    text(74, 2, buffer, Cream);
    snprintf(buffer, sizeof(buffer), "%lu / %lu", static_cast<unsigned long>(game.roundScore),
             static_cast<unsigned long>(poker::blindTarget(game.blindIndex)));
    text(3, 12, buffer, Cream);
    snprintf(buffer, sizeof(buffer), "HANDS %u  DISCARDS %u", game.handsRemaining, game.discardsRemaining);
    text(3, 23, buffer, Muted);
    for (uint8_t i = 0; i < game.handCount; ++i)
        drawCard(3 + (i % 4) * 31, 35 + (i / 4) * 29, game.hand[i], selection & (1 << i), cursor == i);
    canvas.fillRect(0, 93, 128, 23, Background);
    const poker::ScoreResult score = poker::preview(game, selection);
    if (score.valid) {
        text(3, 95, poker::handName(score.type), Gold);
        snprintf(buffer, sizeof(buffer), "%u X %u = %lu", score.chips, score.mult, static_cast<unsigned long>(score.total));
        text(3, 106, buffer, Cream);
    } else {
        centered(95, PSTR("A SELECT / B PAUSE"), Muted);
        centered(106, PSTR("MAKE A POKER HAND"), Muted);
    }
    footer(PSTR("L DISCARD  R PLAY"));
}

void drawScore(uint32_t now) {
    heading(PSTR("HAND SCORED"));
    centered(27, poker::handName(game.lastScore.type), Gold);
    char buffer[22];
    snprintf(buffer, sizeof(buffer), "%u CHIPS", game.lastScore.chips);
    centered(43, buffer, Blue);
    snprintf(buffer, sizeof(buffer), "X %u MULT", game.lastScore.mult);
    centered(55, buffer, Red);
    const uint32_t elapsed = now - screenSince;
    const uint32_t amount = game.lastScore.total * (elapsed < 700 ? elapsed : 700) / 700;
    snprintf(buffer, sizeof(buffer), "+%lu", static_cast<unsigned long>(amount));
    centered(75, buffer, Gold, 2);
    snprintf(buffer, sizeof(buffer), "ROUND %lu / %lu", static_cast<unsigned long>(game.roundScore),
             static_cast<unsigned long>(poker::blindTarget(game.blindIndex)));
    centered(101, buffer, Muted);
    footer(PSTR("A CONTINUE"));
}

void drawSummary() {
    const bool lost = game.phase == Phase::Lost;
    heading(lost ? PSTR("RUN OVER") : game.phase == Phase::Won ? PSTR("DEMO COMPLETE!") : PSTR("BLIND CLEARED!"));
    centered(25, poker::blindName(game.blindIndex), Gold);
    char buffer[22];
    snprintf(buffer, sizeof(buffer), "%lu / %lu", static_cast<unsigned long>(game.roundScore),
             static_cast<unsigned long>(poker::blindTarget(game.blindIndex)));
    centered(39, buffer, Cream);
    if (lost) {
        centered(59, PSTR("OUT OF HANDS"), Pink);
        centered(77, PSTR("TRY A NEW DECK!"), Muted);
    } else {
        snprintf(buffer, sizeof(buffer), "BLIND REWARD  $%u", game.lastPayout.reward);
        text(9, 56, buffer);
        snprintf(buffer, sizeof(buffer), "UNUSED HANDS  $%u", game.lastPayout.unusedHands);
        text(9, 68, buffer);
        snprintf(buffer, sizeof(buffer), "INTEREST      $%u", game.lastPayout.interest);
        text(9, 80, buffer);
        snprintf(buffer, sizeof(buffer), "CASH $%u (+%u)", game.cash, game.lastPayout.total);
        centered(99, buffer, Gold);
    }
    footer(game.phase == Phase::Shop ? PSTR("A VISIT SHOP") : PSTR("A NEW RUN  B TITLE"));
}

void drawShop() {
    char buffer[22];
    snprintf(buffer, sizeof(buffer), "SHOP $%u  J%u/5", game.cash, game.jokerCount);
    heading(buffer);
    for (uint8_t i = 0; i < 2; ++i) {
        const int y = 27 + i * 24;
        const auto id = game.offers[i];
        if (menuItem == i) canvas.fillRoundRect(3, y - 4, 122, 22, 2, Gold);
        text(7, y, id == poker::JokerId::None ? PSTR("SOLD") : poker::jokerName(id), menuItem == i ? Ink : Cream);
        if (id != poker::JokerId::None) {
            snprintf(buffer, sizeof(buffer), "$%u", poker::jokerPrice(id));
            text(103, y + 9, buffer, menuItem == i ? Ink : Muted);
        }
    }
    const auto id = game.offers[menuItem];
    wrapped(5, 77, id == poker::JokerId::None ? PSTR("Choose the other offer or continue.") : poker::jokerDescription(id), 20, Cream, 3);
    footer(PSTR("A BUY  B NEXT BLIND"));
}

void drawPause() {
    heading(PSTR("PAUSED"));
    static const char items[5][10] PROGMEM = {"RESUME", "JOKERS", "CONTROLS", "RESTART", "TITLE"};
    for (uint8_t i = 0; i < 5; ++i) row(29 + i * 17, items[i], menuItem == i);
    footer(PSTR("A CONFIRM  B RESUME"));
}

void drawHelp() {
    heading(PSTR("CONTROLS"));
    text(4, 24, PSTR("D-PAD  MOVE CURSOR"));
    text(4, 37, PSTR("A      SELECT / BUY"));
    text(4, 50, PSTR("B      PAUSE / BACK"));
    text(4, 63, PSTR("L TOP  DISCARD"));
    text(4, 76, PSTR("R TOP  PLAY HAND"));
    wrapped(4, 93, PSTR("Select 1-5 cards. Beat the target."), 20, Gold, 2);
    footer(PSTR("A OR B BACK"));
}

void drawJokers() {
    heading(PSTR("YOUR JOKERS"));
    if (game.jokerCount == 0) {
        centered(42, PSTR("NO JOKERS YET"), Muted);
        wrapped(5, 62, PSTR("Clear a blind to buy jokers in the shop."), 20, Cream, 4);
    } else {
        char buffer[12];
        snprintf(buffer, sizeof(buffer), "%u / %u", jokerIndex + 1, game.jokerCount);
        centered(26, buffer, Muted);
        centered(44, poker::jokerName(game.jokers[jokerIndex]), Gold);
        wrapped(5, 64, poker::jokerDescription(game.jokers[jokerIndex]), 20, Cream, 4);
    }
    footer(PSTR("LEFT/RIGHT  B BACK"));
}

void drawConfirm() {
    heading(screen == Screen::ConfirmRestart ? PSTR("RESTART RUN?") : PSTR("RETURN TO TITLE?"));
    wrapped(9, 41, PSTR("This run will be lost. There are no saves in this demo."), 18, Cream, 5);
    footer(PSTR("A YES  B CANCEL"));
}

void render(uint32_t now) {
    switch (screen) {
    case Screen::Title: drawTitle(); break;
    case Screen::Intro: drawIntro(); break;
    case Screen::Hand: drawHand(); break;
    case Screen::Score: drawScore(now); break;
    case Screen::Summary: drawSummary(); break;
    case Screen::Shop: drawShop(); break;
    case Screen::Pause: drawPause(); break;
    case Screen::Help: drawHelp(); break;
    case Screen::Jokers: drawJokers(); break;
    case Screen::ConfirmRestart:
    case Screen::ConfirmTitle: drawConfirm(); break;
    case Screen::Diagnostics: diagnostic(diagnosticDown, diagnosticSeen); break;
    }
    if (toast) {
        canvas.fillRoundRect(2, 47, 124, 37, 3, Shadow);
        canvas.drawRoundRect(2, 47, 124, 37, 3, Gold);
        wrapped(7, 54, toast, 19, Gold, 3);
    }
    canvas.pushSprite(0, 0);
    dirty = false;
}

bool navigation(uint8_t button, uint32_t now) {
    if (espboy.button.pressed(button)) { repeatAt[button] = now; return true; }
    if (espboy.button.held(button, 350) && now - repeatAt[button] >= 120) {
        repeatAt[button] = now;
        return true;
    }
    return false;
}

void pause() {
    resumeScreen = screen;
    menuItem = 0;
    changeScreen(Screen::Pause);
}

void finishScore() {
    changeScreen(game.phase == Phase::Playing ? Screen::Hand : Screen::Summary);
}

void input(uint32_t now) {
    const bool left = navigation(Button::LEFT, now), right = navigation(Button::RIGHT, now);
    const bool up = navigation(Button::UP, now), down = navigation(Button::DOWN, now);
    const bool a = espboy.button.pressed(Button::ACT), b = espboy.button.pressed(Button::ESC);
    switch (screen) {
    case Screen::Title:
        if (up) menuItem = (menuItem + 2) % 3;
        if (down) menuItem = (menuItem + 1) % 3;
        if (a) {
            if (menuItem == 0) newRun();
            else if (menuItem == 1) { returnScreen = Screen::Title; changeScreen(Screen::Help); }
            else { diagnosticSeen = diagnosticDown = 0; changeScreen(Screen::Diagnostics); }
        }
        break;
    case Screen::Intro:
        if (b) pause();
        else if (a) changeScreen(Screen::Hand);
        break;
    case Screen::Hand: {
        if (b) { pause(); break; }
        if (left && cursor > 0) --cursor;
        if (right && cursor + 1 < game.handCount) ++cursor;
        if (up && cursor >= 4) cursor -= 4;
        if (down && cursor < 4 && game.handCount > 4)
            cursor = cursor + 4 < game.handCount ? cursor + 4 : game.handCount - 1;
        if (a) {
            if ((selection & (1 << cursor)) || selectedCount() < 5) selection ^= 1 << cursor;
            else message(PSTR("Select at most 5 cards."));
        }
        const bool play = espboy.button.pressed(Button::TOP_RIGHT);
        const bool discard = espboy.button.pressed(Button::TOP_LEFT);
        if (play || discard) {
            const ActionStatus status = play ? poker::play(game, selection) : poker::discard(game, selection);
            if (status == ActionStatus::Ok) {
                selection = 0;
                if (cursor >= game.handCount) cursor = game.handCount ? game.handCount - 1 : 0;
                if (play) {
                    changeScreen(Screen::Score);
                    report("play");
                } else {
                    dirty = true;
                    report("discard");
                }
            } else message(poker::actionMessage(status));
        }
        break;
    }
    case Screen::Score:
        if (a && now - screenSince >= 250) finishScore();
        break;
    case Screen::Summary:
        if (game.phase == Phase::Shop) {
            if (a) { menuItem = 0; changeScreen(Screen::Shop); }
        } else if (b) { menuItem = 0; changeScreen(Screen::Title); }
        else if (a) newRun();
        break;
    case Screen::Shop:
        if (up || down || left || right) menuItem ^= 1;
        if (b) {
            const ActionStatus status = poker::nextBlind(game);
            if (status == ActionStatus::Ok) {
                selection = cursor = 0;
                changeScreen(Screen::Intro);
                report("next_blind");
            } else message(poker::actionMessage(status));
        } else if (a) {
            const ActionStatus status = poker::buyJoker(game, menuItem);
            message(status == ActionStatus::Ok ? PSTR("Joker added!") : poker::actionMessage(status));
            if (status == ActionStatus::Ok) report("buy_joker");
        }
        break;
    case Screen::Pause:
        if (up) menuItem = (menuItem + 4) % 5;
        if (down) menuItem = (menuItem + 1) % 5;
        if (b) changeScreen(resumeScreen);
        else if (a) {
            if (menuItem == 0) changeScreen(resumeScreen);
            else if (menuItem == 1) { jokerIndex = 0; changeScreen(Screen::Jokers); }
            else if (menuItem == 2) { returnScreen = Screen::Pause; changeScreen(Screen::Help); }
            else changeScreen(menuItem == 3 ? Screen::ConfirmRestart : Screen::ConfirmTitle);
        }
        break;
    case Screen::Help:
        if (a || b) changeScreen(returnScreen);
        break;
    case Screen::Jokers:
        if (game.jokerCount) {
            if (left || up) jokerIndex = (jokerIndex + game.jokerCount - 1) % game.jokerCount;
            if (right || down) jokerIndex = (jokerIndex + 1) % game.jokerCount;
        }
        if (b) changeScreen(Screen::Pause);
        break;
    case Screen::ConfirmRestart:
    case Screen::ConfirmTitle:
        if (b) changeScreen(Screen::Pause);
        else if (a) {
            if (screen == Screen::ConfirmRestart) newRun();
            else { menuItem = 0; changeScreen(Screen::Title); }
        }
        break;
    case Screen::Diagnostics: {
        const uint8_t buttons = espboy.buttons();
        if (buttons != diagnosticDown) { diagnosticDown = buttons; dirty = true; }
        for (uint8_t i = 0; i < 8; ++i)
            if (espboy.button.pressed(i)) { diagnosticSeen |= 1 << i; dirty = true; }
        if (espboy.button.held(Button::ESC, 1500)) { menuItem = 2; changeScreen(Screen::Title); }
        break;
    }
    }
    if (left || right || up || down || a || b) dirty = true;
}
}

void setup() {
    ready = device::begin();
    minHeap = ESP.getFreeHeap();
#ifdef ESPBOY_DIAGNOSTIC
    changeScreen(Screen::Diagnostics);
#else
    changeScreen(Screen::Title);
#endif
}

void loop() {
    uint32_t now = millis();
    if (now - lastPoll >= 5) {
        lastPoll = now;
        espboy.update();
        if (ready) input(millis());
    }
    // Input handlers timestamp new screens after I2C polling; do not compare
    // those timestamps with the older loop-entry time (unsigned underflow).
    now = millis();
    if (toast && now - toastSince >= 1500) { toast = nullptr; dirty = true; }
    if (screen == Screen::Score) {
        dirty = true;
        if (now - screenSince >= 1300) finishScore();
    }
    if (now - lastStats >= 5000) {
        lastStats = now;
        const uint32_t heap = ESP.getFreeHeap();
        if (heap < minHeap) minHeap = heap;
        Serial.printf("health heap=%u min_heap=%lu max_block=%u screen=%u\n", heap,
                      static_cast<unsigned long>(minHeap), ESP.getMaxFreeBlockSize(), static_cast<unsigned>(screen));
        if (screen == Screen::Diagnostics) dirty = true;
    }
    if (ready && dirty && now - lastFrame >= 33) {
        lastFrame = now;
        render(now);
    }
    delay(1);  // Yield to ESP8266 housekeeping/watchdog even on static screens.
}
