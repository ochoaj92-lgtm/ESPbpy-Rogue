#include <unity.h>

#include <algorithm>
#include <initializer_list>

#include <PokerGame.h>

using namespace poker;

void setUp() {}
void tearDown() {}

namespace {

Card c(uint8_t rank, Suit suit = Suit::Clubs) { return Card{rank, suit}; }

bool sameCard(Card left, Card right) {
    return left.rank == right.rank && left.suit == right.suit;
}

void assertStatus(ActionStatus expected, ActionStatus actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual));
}

void assertCard(Card expected, Card actual) {
    TEST_ASSERT_EQUAL_UINT8(expected.rank, actual.rank);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.suit), static_cast<int>(actual.suit));
}

void assertScore(const ScoreResult& expected, const ScoreResult& actual) {
    TEST_ASSERT_EQUAL(expected.valid, actual.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.type), static_cast<int>(actual.type));
    TEST_ASSERT_EQUAL_UINT8(expected.scoringMask, actual.scoringMask);
    TEST_ASSERT_EQUAL_UINT16(expected.chips, actual.chips);
    TEST_ASSERT_EQUAL_UINT16(expected.mult, actual.mult);
    TEST_ASSERT_EQUAL_UINT32(expected.total, actual.total);
}

// Compare logical fields: raw memory comparisons would include C++ padding.
void assertState(const GameState& expected, const GameState& actual) {
    TEST_ASSERT_EQUAL_UINT32(expected.seed, actual.seed);
    TEST_ASSERT_EQUAL_UINT32(expected.rngState, actual.rngState);
    TEST_ASSERT_EQUAL_UINT8(expected.nextDraw, actual.nextDraw);
    TEST_ASSERT_EQUAL_UINT8(expected.handCount, actual.handCount);
    TEST_ASSERT_EQUAL_UINT8(expected.handsRemaining, actual.handsRemaining);
    TEST_ASSERT_EQUAL_UINT8(expected.discardsRemaining, actual.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT8(expected.blindIndex, actual.blindIndex);
    TEST_ASSERT_EQUAL_UINT32(expected.roundScore, actual.roundScore);
    TEST_ASSERT_EQUAL_UINT16(expected.cash, actual.cash);
    TEST_ASSERT_EQUAL_UINT8(expected.jokerCount, actual.jokerCount);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.phase), static_cast<int>(actual.phase));
    for (unsigned i = 0; i < expected.deck.size(); ++i) assertCard(expected.deck[i], actual.deck[i]);
    for (unsigned i = 0; i < expected.hand.size(); ++i) assertCard(expected.hand[i], actual.hand[i]);
    for (unsigned i = 0; i < expected.jokers.size(); ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.jokers[i]), static_cast<int>(actual.jokers[i]));
    }
    for (unsigned i = 0; i < expected.offers.size(); ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.offers[i]), static_cast<int>(actual.offers[i]));
    }
    assertScore(expected.lastScore, actual.lastScore);
    TEST_ASSERT_EQUAL_UINT8(expected.lastPayout.reward, actual.lastPayout.reward);
    TEST_ASSERT_EQUAL_UINT8(expected.lastPayout.unusedHands, actual.lastPayout.unusedHands);
    TEST_ASSERT_EQUAL_UINT8(expected.lastPayout.interest, actual.lastPayout.interest);
    TEST_ASSERT_EQUAL_UINT16(expected.lastPayout.total, actual.lastPayout.total);
}

void assertHand(HandType type, uint16_t chips, uint16_t mult, uint8_t scoringMask,
                std::initializer_list<Card> cards,
                std::initializer_list<JokerId> jokers = {}) {
    const ScoreResult score = evaluate(cards.begin(), static_cast<uint8_t>(cards.size()),
                                       jokers.begin(), static_cast<uint8_t>(jokers.size()));
    TEST_ASSERT_TRUE(score.valid);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(type), static_cast<int>(score.type));
    TEST_ASSERT_EQUAL_UINT8(scoringMask, score.scoringMask);
    TEST_ASSERT_EQUAL_UINT16(chips, score.chips);
    TEST_ASSERT_EQUAL_UINT16(mult, score.mult);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(chips) * mult, score.total);
}

void assertUniqueDeck(const GameState& game) {
    bool seen[52] = {};
    for (Card card : game.deck) {
        TEST_ASSERT_TRUE(card.rank >= 2 && card.rank <= 14);
        TEST_ASSERT_TRUE(static_cast<unsigned>(card.suit) < 4);
        const unsigned id = static_cast<unsigned>(card.suit) * 13 + card.rank - 2;
        TEST_ASSERT_FALSE(seen[id]);
        seen[id] = true;
    }
    for (bool present : seen) TEST_ASSERT_TRUE(present);
}

void assertHandContains(const GameState& game, Card expected) {
    unsigned matches = 0;
    for (unsigned i = 0; i < game.handCount; ++i) {
        if (sameCard(expected, game.hand[i])) ++matches;
    }
    TEST_ASSERT_EQUAL_UINT32(1, matches);
}

// Move requested cards to the dealt prefix without duplicating/removing cards.
// All other state stays from startRun, making action tests valid run scenarios.
GameState fixture(std::initializer_list<Card> firstCards) {
    GameState game;
    startRun(game, 0x12345678u);
    unsigned destination = 0;
    for (Card requested : firstCards) {
        unsigned source = destination;
        while (source < game.deck.size() && !sameCard(game.deck[source], requested)) ++source;
        TEST_ASSERT_LESS_THAN_UINT32(game.deck.size(), source);
        std::swap(game.deck[destination], game.deck[source]);
        ++destination;
    }
    for (unsigned i = 0; i < game.handCount; ++i) game.hand[i] = game.deck[i];
    return game;
}

GameState winningFixture() {
    return fixture({c(10), c(11), c(12), c(13), c(14)});
}

void assertOffers(const GameState& game) {
    TEST_ASSERT_TRUE(game.offers[0] != JokerId::None);
    TEST_ASSERT_TRUE(game.offers[1] != JokerId::None);
    TEST_ASSERT_TRUE(game.offers[0] != game.offers[1]);
    for (JokerId offer : game.offers) {
        for (unsigned i = 0; i < game.jokerCount; ++i) TEST_ASSERT_TRUE(offer != game.jokers[i]);
    }
}

void test_high_card_only_highest_scores() {
    assertHand(HandType::HighCard, 16, 1, 0x04,
               {c(2), c(9, Suit::Diamonds), c(14, Suit::Hearts), c(11, Suit::Spades), c(4)});
}

void test_pair_does_not_score_ace_kicker() {
    assertHand(HandType::Pair, 30, 2, 0x03,
               {c(13), c(13, Suit::Diamonds), c(14, Suit::Hearts), c(7, Suit::Spades), c(2)});
    assertHand(HandType::Pair, 32, 2, 0x03, {c(14), c(14, Suit::Hearts)});
}

void test_two_pair_scores_both_pairs() {
    assertHand(HandType::TwoPair, 48, 2, 0x0f,
               {c(4), c(4, Suit::Hearts), c(12, Suit::Spades), c(12, Suit::Diamonds), c(14)});
}

void test_three_of_a_kind_ignores_kickers() {
    assertHand(HandType::ThreeOfAKind, 51, 3, 0x07,
               {c(7), c(7, Suit::Diamonds), c(7, Suit::Hearts), c(13, Suit::Spades), c(14)});
}

void test_straight_scores_every_card_regardless_of_order() {
    assertHand(HandType::Straight, 70, 4, 0x1f,
               {c(9, Suit::Spades), c(6), c(10), c(7, Suit::Diamonds), c(8, Suit::Hearts)});
}

void test_flush_scores_every_card_with_face_values() {
    assertHand(HandType::Flush, 71, 4, 0x1f,
               {c(2, Suit::Hearts), c(5, Suit::Hearts), c(8, Suit::Hearts),
                c(11, Suit::Hearts), c(14, Suit::Hearts)});
}

void test_full_house_scores_triple_and_pair() {
    assertHand(HandType::FullHouse, 71, 4, 0x1f,
               {c(3), c(14, Suit::Spades), c(3, Suit::Diamonds), c(14, Suit::Hearts), c(3, Suit::Hearts)});
}

void test_four_of_a_kind_ignores_kicker() {
    assertHand(HandType::FourOfAKind, 96, 7, 0x0f,
               {c(9), c(9, Suit::Diamonds), c(9, Suit::Hearts), c(9, Suit::Spades), c(14)});
}

void test_straight_flush_and_royal_flush_share_category() {
    assertHand(HandType::StraightFlush, 151, 8, 0x1f, {c(10), c(11), c(12), c(13), c(14)});
    assertHand(HandType::StraightFlush, 120, 8, 0x1f, {c(2), c(3), c(4), c(5), c(6)});
}

void test_ace_can_be_low_or_high_but_not_wrap() {
    assertHand(HandType::Straight, 55, 4, 0x1f,
               {c(14), c(2, Suit::Diamonds), c(3), c(4, Suit::Hearts), c(5)});
    assertHand(HandType::Straight, 81, 4, 0x1f,
               {c(10), c(11, Suit::Diamonds), c(12), c(13, Suit::Hearts), c(14)});
    assertHand(HandType::HighCard, 16, 1, 0x04,
               {c(12), c(13, Suit::Diamonds), c(14), c(2, Suit::Hearts), c(3)});
    assertHand(HandType::StraightFlush, 125, 8, 0x1f, {c(14), c(2), c(3), c(4), c(5)});
}

void test_four_cards_cannot_make_straight_or_flush() {
    assertHand(HandType::HighCard, 10, 1, 0x08, {c(2), c(3), c(4), c(5)});
    assertHand(HandType::Pair, 16, 2, 0x06,
               {c(2), c(3, Suit::Diamonds), c(3), c(4, Suit::Hearts), c(5)});
}

void test_evaluate_rejects_empty_and_oversized_hands() {
    const Card cards[] = {c(2), c(3), c(4), c(5), c(6), c(7)};
    TEST_ASSERT_FALSE(evaluate(cards, 0).valid);
    TEST_ASSERT_FALSE(evaluate(nullptr, 0).valid);
    TEST_ASSERT_FALSE(evaluate(cards, 6).valid);
}

void test_joker_adds_four_multiplier() {
    assertHand(HandType::HighCard, 7, 5, 0x01, {c(2)}, {JokerId::Joker});
}

void test_pair_jokers_add_independently_and_combine() {
    const auto cards = {c(14), c(14, Suit::Hearts)};
    assertHand(HandType::Pair, 32, 10, 0x03, cards, {JokerId::Jolly});
    assertHand(HandType::Pair, 82, 2, 0x03, cards, {JokerId::Sly});
    assertHand(HandType::Pair, 82, 14, 0x03, cards, {JokerId::Joker, JokerId::Jolly, JokerId::Sly});
    assertHand(HandType::HighCard, 16, 1, 0x01, {c(14)}, {JokerId::Jolly, JokerId::Sly});
}

void test_pair_jokers_also_trigger_for_trips_full_house_and_quads() {
    const auto jokers = {JokerId::Jolly, JokerId::Sly};
    assertHand(HandType::ThreeOfAKind, 101, 11, 0x07,
               {c(7), c(7, Suit::Diamonds), c(7, Suit::Hearts)}, jokers);
    assertHand(HandType::FullHouse, 121, 12, 0x1f,
               {c(3), c(3, Suit::Diamonds), c(3, Suit::Hearts), c(14), c(14, Suit::Hearts)}, jokers);
    assertHand(HandType::FourOfAKind, 146, 15, 0x0f,
               {c(9), c(9, Suit::Diamonds), c(9, Suit::Hearts), c(9, Suit::Spades), c(14)}, jokers);
    assertHand(HandType::TwoPair, 98, 10, 0x0f,
               {c(4), c(4, Suit::Hearts), c(12, Suit::Spades), c(12, Suit::Diamonds), c(14)}, jokers);
}

void test_suit_jokers_only_count_scoring_cards() {
    assertHand(HandType::Pair, 30, 2, 0x03,
               {c(13), c(13, Suit::Spades), c(14, Suit::Diamonds), c(12, Suit::Hearts)},
               {JokerId::Greedy, JokerId::Lusty});
    assertHand(HandType::Pair, 30, 8, 0x03,
               {c(12, Suit::Diamonds), c(12, Suit::Hearts), c(14), c(13, Suit::Spades)},
               {JokerId::Greedy, JokerId::Lusty});
    assertHand(HandType::Flush, 71, 19, 0x1f,
               {c(2, Suit::Diamonds), c(5, Suit::Diamonds), c(8, Suit::Diamonds),
                c(11, Suit::Diamonds), c(14, Suit::Diamonds)}, {JokerId::Greedy});
    assertHand(HandType::Flush, 71, 19, 0x1f,
               {c(2, Suit::Hearts), c(5, Suit::Hearts), c(8, Suit::Hearts),
                c(11, Suit::Hearts), c(14, Suit::Hearts)}, {JokerId::Lusty});
}

void test_half_joker_uses_played_count_not_scoring_count() {
    assertHand(HandType::HighCard, 16, 21, 0x01, {c(14)}, {JokerId::Half});
    assertHand(HandType::Pair, 30, 22, 0x03,
               {c(13), c(13, Suit::Diamonds), c(14, Suit::Hearts)}, {JokerId::Half});
    assertHand(HandType::Pair, 30, 2, 0x03,
               {c(13), c(13, Suit::Diamonds), c(14, Suit::Hearts), c(2)}, {JokerId::Half});
}

void test_seeded_run_is_reproducible_unique_and_uses_red_deck_defaults() {
    GameState first, repeat, different;
    startRun(first, 42);
    startRun(repeat, 42);
    startRun(different, 43);
    assertState(first, repeat);
    assertUniqueDeck(first);
    assertUniqueDeck(different);
    bool differs = false;
    for (unsigned i = 0; i < first.deck.size(); ++i) {
        if (!sameCard(first.deck[i], different.deck[i])) differs = true;
    }
    TEST_ASSERT_TRUE(differs);
    TEST_ASSERT_EQUAL_UINT8(8, first.handCount);
    TEST_ASSERT_EQUAL_UINT8(8, first.nextDraw);
    for (unsigned i = 0; i < first.handCount; ++i) assertHandContains(first, first.deck[i]);
    TEST_ASSERT_EQUAL_UINT8(4, first.handsRemaining);
    TEST_ASSERT_EQUAL_UINT8(4, first.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT16(4, first.cash);
    TEST_ASSERT_EQUAL_UINT8(0, first.jokerCount);
    TEST_ASSERT_EQUAL_UINT8(0, first.blindIndex);
    TEST_ASSERT_EQUAL_UINT32(0, first.roundScore);
    TEST_ASSERT_TRUE(first.phase == Phase::Playing);
    TEST_ASSERT_EQUAL_UINT32(300, blindTarget(0));
    TEST_ASSERT_EQUAL_UINT32(450, blindTarget(1));
    TEST_ASSERT_EQUAL_UINT32(600, blindTarget(2));
    startRun(first, 0);
    startRun(repeat, 0);
    assertState(first, repeat);
    assertUniqueDeck(first);
}

void test_preview_matches_committed_score_for_every_valid_selection() {
    GameState initial = fixture({c(14, Suit::Hearts), c(14, Suit::Diamonds), c(7), c(7, Suit::Diamonds), c(7, Suit::Hearts)});
    initial.jokers = {{JokerId::Joker, JokerId::Jolly, JokerId::Sly, JokerId::Greedy, JokerId::Half}};
    initial.jokerCount = 5;
    for (unsigned mask = 1; mask < 256; ++mask) {
        unsigned count = 0;
        for (unsigned bit = 0; bit < 8; ++bit) count += (mask >> bit) & 1u;
        GameState game = initial;
        const ScoreResult before = preview(game, static_cast<uint8_t>(mask));
        assertState(initial, game);
        if (count > 5) {
            TEST_ASSERT_FALSE(before.valid);
            continue;
        }
        TEST_ASSERT_TRUE(before.valid);
        assertStatus(ActionStatus::Ok, play(game, static_cast<uint8_t>(mask)));
        assertScore(before, game.lastScore);
        TEST_ASSERT_EQUAL_UINT32(before.total, game.roundScore);
        TEST_ASSERT_EQUAL_UINT8(3, game.handsRemaining);
    }
}

void test_invalid_selections_leave_state_unchanged() {
    GameState game;
    startRun(game, 17);
    const GameState original = game;
    assertStatus(ActionStatus::EmptySelection, play(game, 0));
    assertState(original, game);
    assertStatus(ActionStatus::EmptySelection, discard(game, 0));
    assertState(original, game);
    assertStatus(ActionStatus::TooManyCards, play(game, 0x3f));
    assertState(original, game);
    assertStatus(ActionStatus::TooManyCards, discard(game, 0x3f));
    assertState(original, game);
    TEST_ASSERT_FALSE(preview(game, 0).valid);
    TEST_ASSERT_FALSE(preview(game, 0x3f).valid);

    game.blindIndex = 2;
    game.handCount = 7;
    game.nextDraw = 7;
    const GameState boss = game;
    assertStatus(ActionStatus::InvalidSelection, play(game, 0x80));
    assertState(boss, game);
    assertStatus(ActionStatus::InvalidSelection, discard(game, 0x80));
    assertState(boss, game);
    TEST_ASSERT_FALSE(preview(game, 0x80).valid);
}

void test_discard_keeps_unselected_cards_and_draws_next_cards() {
    GameState game;
    startRun(game, 19);
    const GameState before = game;
    assertStatus(ActionStatus::Ok, discard(game, 0x25)); // Positions 0, 2, 5.
    const uint8_t kept[] = {1, 3, 4, 6, 7};
    for (unsigned i = 0; i < 5; ++i) assertHandContains(game, before.hand[kept[i]]);
    for (unsigned i = 0; i < 3; ++i) assertHandContains(game, before.deck[8 + i]);
    TEST_ASSERT_EQUAL_UINT8(8, game.handCount);
    TEST_ASSERT_EQUAL_UINT8(11, game.nextDraw);
    TEST_ASSERT_EQUAL_UINT8(3, game.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT8(4, game.handsRemaining);
    TEST_ASSERT_EQUAL_UINT32(0, game.roundScore);
    assertUniqueDeck(game);
}

void test_play_keeps_unselected_cards_and_draws_next_cards() {
    GameState game = fixture({c(2), c(3, Suit::Diamonds), c(8, Suit::Hearts), c(10, Suit::Spades)});
    const GameState before = game;
    const ScoreResult expected = preview(game, 0x09);
    assertStatus(ActionStatus::Ok, play(game, 0x09));
    const uint8_t kept[] = {1, 2, 4, 5, 6, 7};
    for (unsigned i = 0; i < 6; ++i) assertHandContains(game, before.hand[kept[i]]);
    for (unsigned i = 0; i < 2; ++i) assertHandContains(game, before.deck[8 + i]);
    TEST_ASSERT_EQUAL_UINT8(10, game.nextDraw);
    TEST_ASSERT_EQUAL_UINT8(3, game.handsRemaining);
    TEST_ASSERT_EQUAL_UINT8(4, game.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT32(expected.total, game.roundScore);
    assertUniqueDeck(game);
}

void test_discard_limit_rejects_fifth_discard_without_consuming_cards() {
    GameState game;
    startRun(game, 87);
    for (unsigned i = 0; i < 4; ++i) assertStatus(ActionStatus::Ok, discard(game, 0x1f));
    TEST_ASSERT_EQUAL_UINT8(0, game.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT8(28, game.nextDraw);
    const GameState before = game;
    assertStatus(ActionStatus::NoDiscards, discard(game, 1));
    assertState(before, game);
    assertStatus(ActionStatus::Ok, play(game, 1));
}

void test_actions_reject_wrong_phases_without_mutation() {
    GameState game;
    startRun(game, 9);
    const GameState playing = game;
    assertStatus(ActionStatus::WrongPhase, buyJoker(game, 0));
    assertState(playing, game);
    assertStatus(ActionStatus::WrongPhase, nextBlind(game));
    assertState(playing, game);

    const Phase locked[] = {Phase::Shop, Phase::Won, Phase::Lost};
    for (Phase phase : locked) {
        game.phase = phase;
        const GameState before = game;
        assertStatus(ActionStatus::WrongPhase, play(game, 1));
        assertState(before, game);
        assertStatus(ActionStatus::WrongPhase, discard(game, 1));
        assertState(before, game);
        if (phase != Phase::Shop) {
            assertStatus(ActionStatus::WrongPhase, buyJoker(game, 0));
            assertState(before, game);
            assertStatus(ActionStatus::WrongPhase, nextBlind(game));
            assertState(before, game);
        }
    }
}

void test_shop_offers_are_unique_exclude_owned_and_are_seeded() {
    for (uint32_t seed = 1; seed <= 32; ++seed) {
        GameState game;
        startRun(game, seed);
        game.roundScore = blindTarget(0) - 1;
        game.jokers[0] = JokerId::Jolly;
        game.jokers[1] = JokerId::Greedy;
        game.jokerCount = 2;
        GameState repeat = game;
        assertStatus(ActionStatus::Ok, play(game, 1));
        assertStatus(ActionStatus::Ok, play(repeat, 1));
        TEST_ASSERT_TRUE(game.phase == Phase::Shop);
        assertOffers(game);
        assertState(game, repeat);
    }
}

void test_purchase_charges_exact_price_fills_slot_and_leaves_empty_offer() {
    const JokerId pool[] = {JokerId::Joker, JokerId::Jolly, JokerId::Sly,
                            JokerId::Greedy, JokerId::Lusty, JokerId::Half};
    const uint8_t prices[] = {2, 3, 3, 5, 5, 5};
    for (unsigned i = 0; i < 6; ++i) {
        GameState game = winningFixture();
        assertStatus(ActionStatus::Ok, play(game, 0x1f));
        TEST_ASSERT_EQUAL_UINT8(prices[i], jokerPrice(pool[i]));
        game.offers = {{pool[i], JokerId::None}};
        game.cash = prices[i];
        assertStatus(ActionStatus::Ok, buyJoker(game, 0));
        TEST_ASSERT_EQUAL_UINT16(0, game.cash);
        TEST_ASSERT_EQUAL_UINT8(1, game.jokerCount);
        TEST_ASSERT_TRUE(game.jokers[0] == pool[i]);
        TEST_ASSERT_TRUE(game.offers[0] == JokerId::None);
        TEST_ASSERT_TRUE(game.offers[1] == JokerId::None);
        TEST_ASSERT_TRUE(game.phase == Phase::Shop);
        const GameState after = game;
        assertStatus(ActionStatus::OfferUnavailable, buyJoker(game, 0));
        assertState(after, game);
    }
}

void test_invalid_purchases_are_nonmutating() {
    GameState game = winningFixture();
    assertStatus(ActionStatus::Ok, play(game, 0x1f));
    game.offers = {{JokerId::Half, JokerId::None}};
    game.cash = 4;
    const GameState before = game;
    assertStatus(ActionStatus::InsufficientFunds, buyJoker(game, 0));
    assertState(before, game);
    assertStatus(ActionStatus::OfferUnavailable, buyJoker(game, 1));
    assertState(before, game);
    assertStatus(ActionStatus::OfferUnavailable, buyJoker(game, 2));
    assertState(before, game);
    assertStatus(ActionStatus::OfferUnavailable, buyJoker(game, 255));
    assertState(before, game);

    game.jokers = {{JokerId::Joker, JokerId::Jolly, JokerId::Sly, JokerId::Greedy, JokerId::Lusty}};
    game.jokerCount = 5;
    game.cash = 20;
    const GameState full = game;
    assertStatus(ActionStatus::NoJokerSpace, buyJoker(game, 0));
    assertState(full, game);
}

void test_payout_uses_cash_before_reward_and_caps_interest() {
    const uint16_t cashValues[] = {4, 5, 24, 25, 99};
    const uint8_t interests[] = {0, 1, 4, 5, 5};
    for (unsigned i = 0; i < 5; ++i) {
        GameState game = winningFixture();
        game.cash = cashValues[i];
        assertStatus(ActionStatus::Ok, play(game, 0x1f));
        TEST_ASSERT_TRUE(game.phase == Phase::Shop);
        TEST_ASSERT_EQUAL_UINT8(3, game.lastPayout.reward);
        TEST_ASSERT_EQUAL_UINT8(3, game.lastPayout.unusedHands);
        TEST_ASSERT_EQUAL_UINT8(interests[i], game.lastPayout.interest);
        TEST_ASSERT_EQUAL_UINT16(6 + interests[i], game.lastPayout.total);
        TEST_ASSERT_EQUAL_UINT16(cashValues[i] + 6 + interests[i], game.cash);
    }
}

void test_progression_resets_round_reshuffles_full_deck_and_keeps_jokers() {
    GameState game = winningFixture();
    assertStatus(ActionStatus::Ok, play(game, 0x1f));
    game.offers = {{JokerId::Joker, JokerId::Half}};
    assertStatus(ActionStatus::Ok, buyJoker(game, 0));
    const auto smallDeck = game.deck;
    const uint16_t shopCash = game.cash;
    assertStatus(ActionStatus::Ok, nextBlind(game));
    TEST_ASSERT_TRUE(game.phase == Phase::Playing);
    TEST_ASSERT_EQUAL_UINT8(1, game.blindIndex);
    TEST_ASSERT_EQUAL_UINT8(8, game.handCount);
    TEST_ASSERT_EQUAL_UINT8(8, game.nextDraw);
    TEST_ASSERT_EQUAL_UINT8(4, game.handsRemaining);
    TEST_ASSERT_EQUAL_UINT8(4, game.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT32(0, game.roundScore);
    TEST_ASSERT_EQUAL_UINT16(shopCash, game.cash);
    TEST_ASSERT_EQUAL_UINT8(1, game.jokerCount);
    TEST_ASSERT_TRUE(game.jokers[0] == JokerId::Joker);
    assertUniqueDeck(game);
    bool reshuffled = false;
    for (unsigned i = 0; i < 52; ++i) if (!sameCard(smallDeck[i], game.deck[i])) reshuffled = true;
    TEST_ASSERT_TRUE(reshuffled);

    game.roundScore = blindTarget(1) - 1;
    assertStatus(ActionStatus::Ok, play(game, 1));
    TEST_ASSERT_TRUE(game.phase == Phase::Shop);
    TEST_ASSERT_EQUAL_UINT8(4, game.lastPayout.reward);
    assertOffers(game);
    assertStatus(ActionStatus::Ok, nextBlind(game));
    TEST_ASSERT_EQUAL_UINT8(2, game.blindIndex);
    TEST_ASSERT_EQUAL_UINT8(7, game.handCount);
    TEST_ASSERT_EQUAL_UINT8(7, game.nextDraw);
    TEST_ASSERT_EQUAL_UINT8(4, game.handsRemaining);
    TEST_ASSERT_EQUAL_UINT8(4, game.discardsRemaining);
    TEST_ASSERT_EQUAL_UINT32(0, game.roundScore);
    assertUniqueDeck(game);
    assertStatus(ActionStatus::Ok, discard(game, 0x1f));
    TEST_ASSERT_EQUAL_UINT8(7, game.handCount);
    TEST_ASSERT_EQUAL_UINT8(12, game.nextDraw);
}

void test_victory_on_final_hand_precedes_loss_for_every_blind() {
    for (uint8_t blind = 0; blind < 3; ++blind) {
        GameState game = fixture({c(2)});
        game.blindIndex = blind;
        if (blind == 2) {
            game.handCount = 7;
            game.nextDraw = 7;
        }
        game.handsRemaining = 1;
        game.roundScore = blindTarget(blind) - 7;
        assertStatus(ActionStatus::Ok, play(game, 1));
        TEST_ASSERT_EQUAL_UINT32(blindTarget(blind), game.roundScore);
        TEST_ASSERT_EQUAL_UINT8(0, game.handsRemaining);
        TEST_ASSERT_EQUAL_UINT8(3 + blind, game.lastPayout.reward);
        TEST_ASSERT_EQUAL_UINT8(0, game.lastPayout.unusedHands);
        TEST_ASSERT_TRUE(game.phase == (blind == 2 ? Phase::Won : Phase::Shop));
    }
}

void test_low_scores_accumulate_then_fourth_hand_loses() {
    GameState game;
    startRun(game, 301);
    uint32_t total = 0;
    for (unsigned i = 0; i < 4; ++i) {
        total += preview(game, 1).total;
        assertStatus(ActionStatus::Ok, play(game, 1));
        TEST_ASSERT_EQUAL_UINT32(total, game.roundScore);
        TEST_ASSERT_EQUAL_UINT8(3 - i, game.handsRemaining);
        TEST_ASSERT_TRUE(game.phase == (i == 3 ? Phase::Lost : Phase::Playing));
    }
    TEST_ASSERT_EQUAL_UINT16(4, game.cash);
    TEST_ASSERT_EQUAL_UINT16(0, game.lastPayout.total);
    const GameState lost = game;
    assertStatus(ActionStatus::WrongPhase, play(game, 1));
    assertState(lost, game);
}

void test_restart_clears_previous_run_and_reproduces_new_seed() {
    GameState game = winningFixture();
    game.jokers[0] = JokerId::Jolly;
    game.jokerCount = 1;
    assertStatus(ActionStatus::Ok, play(game, 0x1f));
    game.phase = Phase::Won;
    game.blindIndex = 2;
    game.handsRemaining = 0;
    game.discardsRemaining = 0;
    game.cash = 81;
    startRun(game, 123);
    GameState fresh;
    startRun(fresh, 123);
    assertState(fresh, game);
}

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_high_card_only_highest_scores);
    RUN_TEST(test_pair_does_not_score_ace_kicker);
    RUN_TEST(test_two_pair_scores_both_pairs);
    RUN_TEST(test_three_of_a_kind_ignores_kickers);
    RUN_TEST(test_straight_scores_every_card_regardless_of_order);
    RUN_TEST(test_flush_scores_every_card_with_face_values);
    RUN_TEST(test_full_house_scores_triple_and_pair);
    RUN_TEST(test_four_of_a_kind_ignores_kicker);
    RUN_TEST(test_straight_flush_and_royal_flush_share_category);
    RUN_TEST(test_ace_can_be_low_or_high_but_not_wrap);
    RUN_TEST(test_four_cards_cannot_make_straight_or_flush);
    RUN_TEST(test_evaluate_rejects_empty_and_oversized_hands);
    RUN_TEST(test_joker_adds_four_multiplier);
    RUN_TEST(test_pair_jokers_add_independently_and_combine);
    RUN_TEST(test_pair_jokers_also_trigger_for_trips_full_house_and_quads);
    RUN_TEST(test_suit_jokers_only_count_scoring_cards);
    RUN_TEST(test_half_joker_uses_played_count_not_scoring_count);
    RUN_TEST(test_seeded_run_is_reproducible_unique_and_uses_red_deck_defaults);
    RUN_TEST(test_preview_matches_committed_score_for_every_valid_selection);
    RUN_TEST(test_invalid_selections_leave_state_unchanged);
    RUN_TEST(test_discard_keeps_unselected_cards_and_draws_next_cards);
    RUN_TEST(test_play_keeps_unselected_cards_and_draws_next_cards);
    RUN_TEST(test_discard_limit_rejects_fifth_discard_without_consuming_cards);
    RUN_TEST(test_actions_reject_wrong_phases_without_mutation);
    RUN_TEST(test_shop_offers_are_unique_exclude_owned_and_are_seeded);
    RUN_TEST(test_purchase_charges_exact_price_fills_slot_and_leaves_empty_offer);
    RUN_TEST(test_invalid_purchases_are_nonmutating);
    RUN_TEST(test_payout_uses_cash_before_reward_and_caps_interest);
    RUN_TEST(test_progression_resets_round_reshuffles_full_deck_and_keeps_jokers);
    RUN_TEST(test_victory_on_final_hand_precedes_loss_for_every_blind);
    RUN_TEST(test_low_scores_accumulate_then_fourth_hand_loses);
    RUN_TEST(test_restart_clears_previous_run_and_reproduces_new_seed);
    return UNITY_END();
}
