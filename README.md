# Pocket Poker — ESPBoy demo

A native C++ poker roguelike inspired by Balatro, built for a standard ESPBoy with PlatformIO. Demo 0.1 contains one ante: three blinds, two joker shops, and victory/defeat screens. It uses an independent rules implementation and custom pixel graphics.

**Hardware testing is still pending.** No USB serial device was detected during development, so the display, physical controls, upload, and runtime stability have not yet been verified on an ESPBoy.

## Build and upload

The target is a stock ESP8266 ESPBoy: Wemos/LOLIN D1 mini, 80 MHz CPU, 4 MB flash, and a 128 × 128 display. The project follows the [ESPboy library’s PlatformIO setup](https://m1cr0lab-espboy.github.io/ESPboy/).

### VS Code

1. Open this entire folder in VS Code using **File → Open Folder**. PlatformIO should discover `platformio.ini`; there is no need to create another project.
2. Let PlatformIO install the pinned platform and libraries. The first build requires an internet connection.
3. Open **PlatformIO → Project Tasks → espboy → General → Build**. The default environment is `espboy`.
4. Connect the ESPBoy using a **USB data cable** and turn it on.
5. Choose **espboy → General → Upload**. Uploading replaces the currently installed application; a full flash erase is not needed for this demo.
6. After upload/reset, choose **NEW RUN** with A. A second press of A starts the first blind.

### Terminal

Run these commands from the project folder, using PlatformIO’s terminal:

```sh
pio test -e native
pio run -e espboy
pio device list
pio run -e espboy -t upload
```

PlatformIO Core is installed on the development machine at `~/.platformio/penv/bin/pio`. If `pio` is not on your shell’s PATH, use that executable directly, for example:

```sh
~/.platformio/penv/bin/pio run -e espboy
```

To select a port explicitly, use the actual port reported by `pio device list`. For example, on Linux:

```sh
pio run -e espboy -t upload --upload-port /dev/ttyUSB0
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

Windows ports look like `COM3`; macOS ports commonly begin `/dev/cu.`. These are examples, not a detected device. If no port appears, check that the cable carries data and the ESPBoy is powered on. If the port is busy, close any other serial monitor before uploading.

The game binary is generated at `.pio/build/espboy/firmware.bin`; the delivered copy is `dist/firmware.bin`. Rebuild after source changes before uploading. Use the `espboy` environment for the normal title-screen startup.

The copies in `dist/` are the verified demo snapshot. Normal PlatformIO Upload uses the current build in `.pio/`; it does not refresh the copies in `dist/`.

## Controls and screens

| Control | During a hand |
|---|---|
| D-pad | Move the cursor between cards; up/down moves between rows |
| A | Select or deselect the focused card |
| B | Open the pause menu |
| Upper-left / L TOP | Discard selected cards |
| Upper-right / R TOP | Play selected cards |

Select **one to five** cards. Gold cards with a `+` are selected; the cyan outline marks the cursor. The bottom panel previews the selected poker hand and its complete chips × multiplier score, including owned jokers. The top panel shows the current score/target, remaining hands/discards, cash, and joker count. Cards are sorted by rank after dealing/refilling.

Action buttons trigger once per press; holding play or discard does not consume multiple actions. D-pad navigation repeats when held. A score animation runs after playing a hand and advances automatically; A skips ahead after its brief initial delay.

| Screen | Controls |
|---|---|
| Title | Up/down to choose NEW RUN, CONTROLS, or DEVICE CHECK; A confirms |
| Blind introduction | A enters the hand; B opens pause |
| Blind cleared | A opens the shop |
| Shop | D-pad chooses between offers; A buys; B starts the next blind |
| Pause | Up/down and A choose RESUME, JOKERS, CONTROLS, RESTART, or TITLE; B resumes |
| Owned jokers | Left/right browses names and effects; B returns to pause |
| Restart/title confirmation | A confirms; B cancels |
| Victory or defeat | A starts a new run; B returns to title |

Restart and return-to-title discard the current run after confirmation. Booting starts at the title screen with no saved run. The demo is offline and silent: Wi-Fi is disabled, and there is no save/resume, audio, or desktop graphical emulator.

## Demo rules

- Start with a standard 52-card deck, eight cards held, $4, and five joker slots.
- Each blind starts with four hands and four discards, matching the demo’s Red Deck rules. Unselected cards stay in your hand; played/discarded cards are replaced from the draw pile and stay out for the rest of that blind.
- Each new blind rebuilds and shuffles all 52 cards. Cash and purchased jokers persist between blinds.
- Scores accumulate within a blind. Reach the target to win immediately, including on your fourth/final hand. Use all four hands below the target and the run ends.

| Blind | Target | Reward | Hand size |
|---|---:|---:|---:|
| Small Blind | 300 | $3 | 8 |
| Big Blind | 450 | $4 | 8 |
| The Manacle | 600 | $5 | 7 |

Clearing a blind pays its reward, $1 per unused hand, and interest of `min(5, cash_before_payout / 5)` with integer division. For example, clearing Small Blind on the first hand while holding $4 pays $6, leaving $10. Clearing The Manacle completes the demo.

### Poker scoring

The highest qualifying poker category determines base chips and multiplier. Add chips from contributing cards, apply joker effects, then multiply. Number cards contribute their rank, J/Q/K contribute 10, and aces contribute 11.

| Hand | Base chips | Base multiplier | Contributing cards |
|---|---:|---:|---|
| High Card | 5 | 1 | Highest card |
| Pair | 10 | 2 | Pair |
| Two Pair | 20 | 2 | Both pairs |
| Three of a Kind | 30 | 3 | Triple |
| Straight | 30 | 4 | All five |
| Flush | 35 | 4 | All five |
| Full House | 40 | 4 | All five |
| Four of a Kind | 60 | 7 | Four matching cards |
| Straight Flush | 100 | 8 | All five |

Aces may be low in A-2-3-4-5 or high in 10-J-Q-K-A; Q-K-A-2-3 is not a straight. A royal flush uses Straight Flush scoring. Straight and Flush both require five cards. Unscored kickers do not add chips or activate suit jokers. For example, two kings and an unrelated ace score `(10 + 10 + 10) × 2 = 60` without jokers.

### Jokers and shops

The first two blinds lead to shops with two randomly selected jokers. Offers exclude owned jokers and duplicate offers. A purchase fills one joker slot and leaves the offer marked SOLD. There are no rerolls, selling, consumables, upgrades, or blind skipping in this demo.

| Joker | Price | Effect |
|---|---:|---|
| Joker | $2 | +4 multiplier |
| Jolly Joker | $3 | +8 multiplier when played cards contain a pair |
| Sly Joker | $3 | +50 chips when played cards contain a pair |
| Greedy Joker | $5 | +3 multiplier per scoring diamond |
| Lusty Joker | $5 | +3 multiplier per scoring heart |
| Half Joker | $5 | +20 multiplier when playing three or fewer cards |

“Contains a pair” includes Two Pair, Three of a Kind, Full House, and Four of a Kind. Half Joker counts **played cards**, including kickers, rather than only scoring cards. All six effects are additive and combine with each other.

## Device check and serial diagnostics

Choose **DEVICE CHECK** from the title to inspect a 16-color strip and all eight buttons. A held button highlights its label; a green marker remains once its press has been observed. Press each button and confirm the correct label reacts. **Hold B for 1.5 seconds to return to the title.** The screen also reports free heap.

For a diagnostic-first build, use the separate environment:

```sh
pio run -e diagnostic
pio run -e diagnostic -t upload
```

This firmware starts directly in DEVICE CHECK and still allows returning to the game title. Its binary is `.pio/build/diagnostic/firmware.bin`; the delivered copy is `dist/diagnostic.bin`.

Serial logging uses **115200 baud**. Boot output reports free heap, largest free block, and sketch size. Gameplay events record the run seed, blind, score, cash, and heap; health output appears every five seconds. A recorded seed can reproduce a run through the portable engine’s `startRun(game, seed)` API. If framebuffer allocation fails, the device displays an error and logs it over serial.

When a device is connected, complete these hardware checks:

- Confirm text, all four suit symbols, card selection, and the color strip are readable.
- Check all eight buttons, both card rows, and the seven-card boss hand; hold play/discard to confirm one action per press.
- Exercise a purchase, an insufficient-funds message, and continuation to the next blind.
- Reach victory and defeat; test pause, resume, restart, and return-to-title confirmation.
- Repeat runs/restarts and watch free heap and largest free block for sustained decline or resets.

These checks have **not been performed on hardware**. Passing desktop tests and compiling firmware do not establish physical-device behavior.

## Development and verification

The portable engine is in `lib/PokerGame/src`, hardware drawing/setup is in `src/Device.*`, and screen/input orchestration is in `src/main.cpp`. The engine exposes seeded initialization, readable game state, play/discard/purchase/next-blind actions, and a shared score evaluator used for previews and committed plays. It has no Arduino dependency.

Cards and jokers use fixed-size storage. Rendering reuses one 128 × 128 4-bit framebuffer, approximately 8 KB plus palette/driver overhead. The custom bitmap font, palette, and static text are stored in flash. Input is polled on a 5 ms schedule, rendering is limited to approximately 30 FPS, and score animation does not block the loop.

[Screen previews](docs/screen-preview.png) show the actual drawing code rendered with software test fixtures, not photos of the device. A temporary host harness checked 16 screens, single-press/held-button behavior, selection preservation through pause, and the score → summary → shop → next-blind sequence. It is not part of the shipped firmware or a desktop emulator.

`pio test -e native` builds desktop Unity tests using a host C++ compiler. **32 tests passed** during development, including:

- All nine hand categories, exact chips/multiplier values, ace-low/high straights, kickers, and all six joker effects.
- Preview versus committed scoring for all 218 valid selections of one to five cards from an eight-card hand.
- Deterministic shuffles, deck uniqueness, retained cards/refills, selection errors, discard limits, and nonmutating rejected actions.
- Shop uniqueness/purchases, payouts and interest cap, blind transitions, seven-card boss hands, final-hand victory, defeat, and clean restart.

Both `pio run -e espboy` and `pio run -e diagnostic` compile successfully. The game build uses **30,696 bytes of static RAM (37.5% of 81,920)** and **324,807 bytes of program space (31.1% of the 1,044,464-byte application limit)**. These static figures exclude the roughly 8 KB framebuffer allocation and other runtime heap use. Actual free heap and runtime stability require the connected-device checks above. The delivered binaries and checksums are recorded in `dist/build-info.json`.

### Pinned dependencies and sources

| Dependency | Pinned version | Source |
|---|---|---|
| Espressif 8266 PlatformIO platform | 4.2.1 | [platform-espressif8266](https://github.com/platformio/platform-espressif8266) |
| ESPboy | 1.2.1 | [ESPboy](https://github.com/m1cr0lab-espboy/ESPboy) |
| LovyanGFX | 0.4.18 | [LovyanGFX](https://github.com/lovyan03/LovyanGFX) |
| Adafruit MCP4725 | 2.0.2 | [Adafruit MCP4725](https://github.com/adafruit/Adafruit_MCP4725) |
| Adafruit MCP23017 Arduino Library | 2.3.2 | [Adafruit MCP23017](https://github.com/adafruit/Adafruit-MCP23017-Arduino-Library) |
| Adafruit BusIO | 1.17.4 | [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) |
| Native test platform | 1.2.1 | [platform-native](https://github.com/platformio/platform-native) |

The verified build uses PlatformIO Core 6.1.19, Arduino ESP8266 core 3.1.2, and Xtensa GCC 10.3.0 (`toolchain-xtensa@2.100300.220621`). The hardware libraries, including Adafruit dependencies, are pinned in `platformio.ini`. Run `pio pkg list -e espboy` to inspect installed versions.

Upstream dependency source and license notices are retained in PlatformIO’s installed packages; keep the applicable upstream notices with any redistributed dependency code or firmware. The game’s graphics are custom and no Balatro art, music, or game source is included. Balatro is the inspiration for these demo mechanics; its [official FAQ](https://www.playbalatro.com/faq) describes the three-blind progression.

## Next milestones

1. Refine controls, readability, and timing using feedback from a real ESPBoy.
2. Add more antes and boss rules.
3. Extend joker effects and shops.
4. Add card upgrades and consumables.
5. Add save/resume and sound.
