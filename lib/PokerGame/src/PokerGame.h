#pragma once

#include <array>
#include <cstdint>

namespace poker {

enum class Suit : uint8_t { Clubs, Diamonds, Hearts, Spades };
struct Card {
    uint8_t rank;  // 2..14; Ace is 14
    Suit suit;
};

enum class HandType : uint8_t {
    HighCard, Pair, TwoPair, ThreeOfAKind, Straight, Flush,
    FullHouse, FourOfAKind, StraightFlush
};
enum class JokerId : uint8_t { None, Joker, Jolly, Sly, Greedy, Lusty, Half };
enum class Phase : uint8_t { Playing, Shop, Won, Lost };
enum class ActionStatus : uint8_t {
    Ok, WrongPhase, EmptySelection, TooManyCards, InvalidSelection,
    NoDiscards, NoHands, InsufficientFunds, NoJokerSpace, OfferUnavailable
};

struct ScoreResult {
    bool valid = false;
    HandType type = HandType::HighCard;
    uint8_t scoringMask = 0;  // Bits refer to the supplied selected-card array.
    uint16_t chips = 0;
    uint16_t mult = 0;
    uint32_t total = 0;
};
struct Payout {
    uint8_t reward = 0;
    uint8_t unusedHands = 0;
    uint8_t interest = 0;
    uint16_t total = 0;
};
struct GameState {
    uint32_t seed = 1;
    uint32_t rngState = 1;
    std::array<Card, 52> deck{};
    uint8_t nextDraw = 0;
    std::array<Card, 8> hand{};
    uint8_t handCount = 0;
    uint8_t handsRemaining = 4;
    uint8_t discardsRemaining = 4;
    uint8_t blindIndex = 0;
    uint32_t roundScore = 0;
    uint16_t cash = 4;
    std::array<JokerId, 5> jokers{};
    uint8_t jokerCount = 0;
    std::array<JokerId, 2> offers{};
    Phase phase = Phase::Playing;
    ScoreResult lastScore{};
    Payout lastPayout{};
};

// Device-independent rules. evaluate() never changes cards or game state.
ScoreResult evaluate(const Card* cards, uint8_t count,
                     const JokerId* jokers = nullptr, uint8_t jokerCount = 0);
ScoreResult preview(const GameState& game, uint8_t selectionMask);
void startRun(GameState& game, uint32_t seed);
ActionStatus play(GameState& game, uint8_t selectionMask);
ActionStatus discard(GameState& game, uint8_t selectionMask);
ActionStatus buyJoker(GameState& game, uint8_t offerIndex);
ActionStatus nextBlind(GameState& game);

uint32_t blindTarget(uint8_t blindIndex);
// Display strings reside in flash on ESP8266; use pgm_read_byte/strlen_P there.
const char* blindName(uint8_t blindIndex);
const char* handName(HandType type);
const char* jokerName(JokerId joker);
const char* jokerDescription(JokerId joker);
uint8_t jokerPrice(JokerId joker);
const char* actionMessage(ActionStatus status);

}  // namespace poker
