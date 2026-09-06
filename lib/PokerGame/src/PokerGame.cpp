#include "PokerGame.h"

#include <cstddef>

#if defined(ARDUINO_ARCH_ESP8266)
#include <pgmspace.h>
#define POKER_TEXT(value) PSTR(value)
#else
#define POKER_TEXT(value) value
#endif

namespace poker {
namespace {

const uint16_t kBaseChips[] = {5, 10, 20, 30, 30, 35, 40, 60, 100};
const uint8_t kBaseMult[] = {1, 2, 2, 3, 4, 4, 4, 7, 8};
const uint32_t kTargets[] = {300, 450, 600};
const uint8_t kRewards[] = {3, 4, 5};

uint32_t randomWord(GameState& game) {
    uint32_t value = game.rngState;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game.rngState = value;
    return value;
}

uint32_t randomBelow(GameState& game, uint32_t bound) {
    // xorshift visits 1..UINT32_MAX: reject the incomplete upper bucket.
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t value;
    do {
        value = randomWord(game);
    } while (value > limit);
    return (value - 1u) % bound;
}

uint8_t bitCount(uint8_t bits) {
    uint8_t count = 0;
    while (bits != 0) {
        count += bits & 1u;
        bits >>= 1;
    }
    return count;
}

uint8_t cardChips(Card card) {
    return card.rank == 14 ? 11 : (card.rank > 10 ? 10 : card.rank);
}

bool comesBefore(Card a, Card b) {
    return a.rank < b.rank ||
        (a.rank == b.rank && static_cast<uint8_t>(a.suit) < static_cast<uint8_t>(b.suit));
}

void sortHand(GameState& game) {
    // Eight cards do not need a heap or a heavyweight sorting abstraction.
    for (uint8_t i = 1; i < game.handCount; ++i) {
        const Card card = game.hand[i];
        uint8_t position = i;
        while (position > 0 && comesBefore(card, game.hand[position - 1])) {
            game.hand[position] = game.hand[position - 1];
            --position;
        }
        game.hand[position] = card;
    }
}

void refillHand(GameState& game) {
    const uint8_t capacity = game.blindIndex == 2 ? 7 : 8;
    while (game.handCount < capacity && game.nextDraw < game.deck.size()) {
        game.hand[game.handCount++] = game.deck[game.nextDraw++];
    }
    sortHand(game);
}

void removeSelected(GameState& game, uint8_t selectionMask) {
    uint8_t retained = 0;
    for (uint8_t i = 0; i < game.handCount; ++i) {
        if ((selectionMask & (1u << i)) == 0) {
            game.hand[retained++] = game.hand[i];
        }
    }
    game.handCount = retained;
    refillHand(game);
}

void beginBlind(GameState& game) {
    game.phase = Phase::Playing;
    game.handCount = 0;
    game.nextDraw = 0;
    game.roundScore = 0;
    game.handsRemaining = 4;
    game.discardsRemaining = 4;
    game.offers.fill(JokerId::None);
    game.lastScore = ScoreResult{};
    // Preserve the previous payout so the blind transition can display it.
    for (uint8_t suit = 0; suit < 4; ++suit) {
        for (uint8_t rank = 2; rank <= 14; ++rank) {
            game.deck[suit * 13 + rank - 2] = Card{rank, static_cast<Suit>(suit)};
        }
    }
    for (uint8_t i = 51; i > 0; --i) {
        const uint8_t other = static_cast<uint8_t>(randomBelow(game, i + 1));
        const Card card = game.deck[i];
        game.deck[i] = game.deck[other];
        game.deck[other] = card;
    }
    refillHand(game);
}

ActionStatus validateSelection(const GameState& game, uint8_t selectionMask) {
    if (game.phase != Phase::Playing) return ActionStatus::WrongPhase;
    if (selectionMask == 0) return ActionStatus::EmptySelection;
    if (game.handCount > game.hand.size()) return ActionStatus::InvalidSelection;
    const uint16_t allowed = (1u << game.handCount) - 1u;
    if ((selectionMask & ~allowed) != 0) return ActionStatus::InvalidSelection;
    if (bitCount(selectionMask) > 5) return ActionStatus::TooManyCards;
    return ActionStatus::Ok;
}

bool owns(const GameState& game, JokerId joker) {
    for (uint8_t i = 0; i < game.jokerCount; ++i) {
        if (game.jokers[i] == joker) return true;
    }
    return false;
}

void dealOffers(GameState& game) {
    std::array<JokerId, 6> available{};
    uint8_t availableCount = 0;
    for (uint8_t id = 1; id <= 6; ++id) {
        const JokerId joker = static_cast<JokerId>(id);
        if (!owns(game, joker)) available[availableCount++] = joker;
    }
    game.offers.fill(JokerId::None);
    for (uint8_t i = 0; i < game.offers.size() && availableCount > 0; ++i) {
        const uint8_t choice = static_cast<uint8_t>(randomBelow(game, availableCount));
        game.offers[i] = available[choice];
        available[choice] = available[--availableCount];
    }
}

void winBlind(GameState& game) {
    Payout payout;
    payout.reward = kRewards[game.blindIndex];
    payout.unusedHands = game.handsRemaining;
    payout.interest = static_cast<uint8_t>(game.cash / 5 > 5 ? 5 : game.cash / 5);
    payout.total = payout.reward + payout.unusedHands + payout.interest;
    game.cash += payout.total;
    game.lastPayout = payout;
    if (game.blindIndex == 2) {
        game.phase = Phase::Won;
    } else {
        game.phase = Phase::Shop;
        dealOffers(game);
    }
}

}  // namespace

ScoreResult evaluate(const Card* cards, uint8_t count,
                     const JokerId* jokers, uint8_t jokerCount) {
    ScoreResult result;
    if (cards == nullptr || count == 0 || count > 5 || jokerCount > 5 ||
        (jokerCount > 0 && jokers == nullptr)) return result;

    uint8_t rankCounts[15] = {};
    uint8_t highRank = 0;
    uint8_t lowRank = 14;
    bool flush = count == 5;
    for (uint8_t i = 0; i < count; ++i) {
        const Card card = cards[i];
        if (card.rank < 2 || card.rank > 14 || static_cast<uint8_t>(card.suit) > 3) {
            return result;
        }
        for (uint8_t j = 0; j < i; ++j) {
            if (cards[j].rank == card.rank && cards[j].suit == card.suit) return result;
        }
        ++rankCounts[card.rank];
        if (card.rank > highRank) highRank = card.rank;
        if (card.rank < lowRank) lowRank = card.rank;
        flush = flush && card.suit == cards[0].suit;
    }
    for (uint8_t i = 0; i < jokerCount; ++i) {
        if (static_cast<uint8_t>(jokers[i]) > static_cast<uint8_t>(JokerId::Half)) {
            return result;
        }
    }

    uint8_t pairs = 0;
    uint8_t tripleRank = 0;
    uint8_t quadRank = 0;
    uint8_t distinctRanks = 0;
    bool containsPair = false;
    for (uint8_t rank = 2; rank <= 14; ++rank) {
        if (rankCounts[rank] > 0) ++distinctRanks;
        if (rankCounts[rank] >= 2) containsPair = true;
        if (rankCounts[rank] == 2) ++pairs;
        if (rankCounts[rank] == 3) tripleRank = rank;
        if (rankCounts[rank] == 4) quadRank = rank;
    }
    const bool wheel = rankCounts[14] == 1 && rankCounts[2] == 1 &&
        rankCounts[3] == 1 && rankCounts[4] == 1 && rankCounts[5] == 1;
    const bool straight = distinctRanks == 5 && (highRank - lowRank == 4 || wheel);
    if (straight && flush) result.type = HandType::StraightFlush;
    else if (quadRank != 0) result.type = HandType::FourOfAKind;
    else if (tripleRank != 0 && pairs == 1) result.type = HandType::FullHouse;
    else if (flush) result.type = HandType::Flush;
    else if (straight) result.type = HandType::Straight;
    else if (tripleRank != 0) result.type = HandType::ThreeOfAKind;
    else if (pairs == 2) result.type = HandType::TwoPair;
    else if (pairs == 1) result.type = HandType::Pair;
    else result.type = HandType::HighCard;

    const uint8_t typeIndex = static_cast<uint8_t>(result.type);
    result.chips = kBaseChips[typeIndex];
    result.mult = kBaseMult[typeIndex];
    for (uint8_t i = 0; i < count; ++i) {
        bool scores = false;
        switch (result.type) {
            case HandType::HighCard: scores = cards[i].rank == highRank; break;
            case HandType::Pair:
            case HandType::TwoPair: scores = rankCounts[cards[i].rank] == 2; break;
            case HandType::ThreeOfAKind: scores = cards[i].rank == tripleRank; break;
            case HandType::FourOfAKind: scores = cards[i].rank == quadRank; break;
            default: scores = true; break;
        }
        if (scores) {
            result.scoringMask |= 1u << i;
            result.chips += cardChips(cards[i]);
        }
    }

    for (uint8_t i = 0; i < jokerCount; ++i) {
        switch (jokers[i]) {
            case JokerId::Joker: result.mult += 4; break;
            case JokerId::Jolly: if (containsPair) result.mult += 8; break;
            case JokerId::Sly: if (containsPair) result.chips += 50; break;
            case JokerId::Half: if (count <= 3) result.mult += 20; break;
            case JokerId::Greedy:
            case JokerId::Lusty: {
                const Suit suit = jokers[i] == JokerId::Greedy ? Suit::Diamonds : Suit::Hearts;
                for (uint8_t card = 0; card < count; ++card) {
                    if ((result.scoringMask & (1u << card)) != 0 && cards[card].suit == suit) {
                        result.mult += 3;
                    }
                }
                break;
            }
            case JokerId::None: break;
        }
    }
    result.total = static_cast<uint32_t>(result.chips) * result.mult;
    result.valid = true;
    return result;
}

ScoreResult preview(const GameState& game, uint8_t selectionMask) {
    if (validateSelection(game, selectionMask) != ActionStatus::Ok) return ScoreResult{};
    std::array<Card, 5> selected{};
    uint8_t count = 0;
    for (uint8_t i = 0; i < game.handCount; ++i) {
        if ((selectionMask & (1u << i)) != 0) selected[count++] = game.hand[i];
    }
    return evaluate(selected.data(), count, game.jokers.data(), game.jokerCount);
}

void startRun(GameState& game, uint32_t seed) {
    game = GameState{};
    game.seed = seed;
    game.rngState = seed == 0 ? 0x9e3779b9u : seed;
    beginBlind(game);
}

ActionStatus play(GameState& game, uint8_t selectionMask) {
    const ActionStatus status = validateSelection(game, selectionMask);
    if (status != ActionStatus::Ok) return status;
    if (game.handsRemaining == 0) return ActionStatus::NoHands;
    const ScoreResult score = preview(game, selectionMask);
    if (!score.valid) return ActionStatus::InvalidSelection;
    game.lastScore = score;
    game.roundScore += score.total;
    --game.handsRemaining;
    removeSelected(game, selectionMask);
    // A winning fourth play succeeds even though no hands remain afterward.
    if (game.roundScore >= blindTarget(game.blindIndex)) winBlind(game);
    else if (game.handsRemaining == 0) game.phase = Phase::Lost;
    return ActionStatus::Ok;
}

ActionStatus discard(GameState& game, uint8_t selectionMask) {
    const ActionStatus status = validateSelection(game, selectionMask);
    if (status != ActionStatus::Ok) return status;
    if (game.discardsRemaining == 0) return ActionStatus::NoDiscards;
    --game.discardsRemaining;
    removeSelected(game, selectionMask);
    return ActionStatus::Ok;
}

ActionStatus buyJoker(GameState& game, uint8_t offerIndex) {
    if (game.phase != Phase::Shop) return ActionStatus::WrongPhase;
    if (offerIndex >= game.offers.size() || game.offers[offerIndex] == JokerId::None ||
        jokerPrice(game.offers[offerIndex]) == 0) return ActionStatus::OfferUnavailable;
    if (game.jokerCount >= game.jokers.size()) return ActionStatus::NoJokerSpace;
    const JokerId joker = game.offers[offerIndex];
    if (owns(game, joker)) return ActionStatus::OfferUnavailable;
    const uint8_t price = jokerPrice(joker);
    if (game.cash < price) return ActionStatus::InsufficientFunds;
    game.cash -= price;
    game.jokers[game.jokerCount++] = joker;
    game.offers[offerIndex] = JokerId::None;
    return ActionStatus::Ok;
}

ActionStatus nextBlind(GameState& game) {
    if (game.phase != Phase::Shop || game.blindIndex >= 2) return ActionStatus::WrongPhase;
    ++game.blindIndex;
    beginBlind(game);
    return ActionStatus::Ok;
}

uint32_t blindTarget(uint8_t blindIndex) {
    return blindIndex < 3 ? kTargets[blindIndex] : 0;
}

const char* blindName(uint8_t blindIndex) {
    switch (blindIndex) {
        case 0: return POKER_TEXT("Small Blind");
        case 1: return POKER_TEXT("Big Blind");
        case 2: return POKER_TEXT("The Manacle");
        default: return POKER_TEXT("Unknown Blind");
    }
}

const char* handName(HandType type) {
    switch (type) {
        case HandType::HighCard: return POKER_TEXT("High Card");
        case HandType::Pair: return POKER_TEXT("Pair");
        case HandType::TwoPair: return POKER_TEXT("Two Pair");
        case HandType::ThreeOfAKind: return POKER_TEXT("Three of a Kind");
        case HandType::Straight: return POKER_TEXT("Straight");
        case HandType::Flush: return POKER_TEXT("Flush");
        case HandType::FullHouse: return POKER_TEXT("Full House");
        case HandType::FourOfAKind: return POKER_TEXT("Four of a Kind");
        case HandType::StraightFlush: return POKER_TEXT("Straight Flush");
        default: return POKER_TEXT("Unknown Hand");
    }
}

const char* jokerName(JokerId joker) {
    switch (joker) {
        case JokerId::Joker: return POKER_TEXT("Joker");
        case JokerId::Jolly: return POKER_TEXT("Jolly Joker");
        case JokerId::Sly: return POKER_TEXT("Sly Joker");
        case JokerId::Greedy: return POKER_TEXT("Greedy Joker");
        case JokerId::Lusty: return POKER_TEXT("Lusty Joker");
        case JokerId::Half: return POKER_TEXT("Half Joker");
        default: return POKER_TEXT("Empty");
    }
}

const char* jokerDescription(JokerId joker) {
    switch (joker) {
        case JokerId::Joker: return POKER_TEXT("+4 mult");
        case JokerId::Jolly: return POKER_TEXT("+8 mult with a pair");
        case JokerId::Sly: return POKER_TEXT("+50 chips with a pair");
        case JokerId::Greedy: return POKER_TEXT("+3 mult per scoring diamond");
        case JokerId::Lusty: return POKER_TEXT("+3 mult per scoring heart");
        case JokerId::Half: return POKER_TEXT("+20 mult with 1-3 cards");
        default: return POKER_TEXT("No joker");
    }
}

uint8_t jokerPrice(JokerId joker) {
    switch (joker) {
        case JokerId::Joker: return 2;
        case JokerId::Jolly:
        case JokerId::Sly: return 3;
        case JokerId::Greedy:
        case JokerId::Lusty:
        case JokerId::Half: return 5;
        default: return 0;
    }
}

const char* actionMessage(ActionStatus status) {
    switch (status) {
        case ActionStatus::Ok: return POKER_TEXT("OK");
        case ActionStatus::WrongPhase: return POKER_TEXT("Not available now");
        case ActionStatus::EmptySelection: return POKER_TEXT("Select 1-5 cards");
        case ActionStatus::TooManyCards: return POKER_TEXT("Select at most 5");
        case ActionStatus::InvalidSelection: return POKER_TEXT("Invalid selection");
        case ActionStatus::NoDiscards: return POKER_TEXT("No discards left");
        case ActionStatus::NoHands: return POKER_TEXT("No hands left");
        case ActionStatus::InsufficientFunds: return POKER_TEXT("Not enough cash");
        case ActionStatus::NoJokerSpace: return POKER_TEXT("Joker slots full");
        case ActionStatus::OfferUnavailable: return POKER_TEXT("Offer unavailable");
        default: return POKER_TEXT("Unknown action");
    }
}

}  // namespace poker

#undef POKER_TEXT
