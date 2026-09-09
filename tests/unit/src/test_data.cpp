#include "gradido_blockchain_core/data/timestamp.h"
#include "memory_limit.h"
#include <climits>
#include <cstdint>
#include <gtest/gtest.h>

static size_t expected_length(const grdd_timestamp *ts) {
  char temp[64];
  snprintf(temp, sizeof(temp), "%lld.%.9d", (long long)ts->seconds, ts->nanos);
  return strlen(temp);
}

TEST(TimestampToString, Basic) {
  grdd_timestamp ts = {.seconds = 1781598032, .nanos = 32260316};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 20);
  EXPECT_STREQ(buffer, "1781598032.032260316");
}

TEST(TimestampToString, LeadingZeros) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 1234};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19); // 9 Sekunden + 1 Punkt + 9 Nanos = 19
  EXPECT_STREQ(buffer, "123456789.000001234");
}

TEST(TimestampToString, AllZeros) {
  grdd_timestamp ts = {.seconds = 0, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 11); // "0.000000000" = 11 Zeichen
  EXPECT_STREQ(buffer, "0.000000000");
}

TEST(TimestampToString, AllNines) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 999999999};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19);
  EXPECT_STREQ(buffer, "123456789.999999999");
}

TEST(TimestampToString, NegativeSeconds) {
  grdd_timestamp ts = {.seconds = -123, .nanos = 456789};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 14); // "-123.000456789" = 16 Zeichen
  EXPECT_STREQ(buffer, "-123.000456789");
}

TEST(TimestampToString, NegativeSecondsWithNanos) {
  grdd_timestamp ts = {.seconds = -1, .nanos = 500000000};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 12); // "-1.500000000"
  EXPECT_STREQ(buffer, "-1.500000000");
}

TEST(TimestampToString, MaxSeconds) {
  grdd_timestamp ts = {.seconds = INT64_MAX, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // Erwartete Länge: 19 (max. 19 Stellen für INT64_MAX) + 1 + 9 = 29
  ASSERT_EQ(written, 29);
  std::string expected = std::to_string(INT64_MAX) + ".000000000";
  EXPECT_STREQ(buffer, expected.c_str());
}

TEST(TimestampToString, MinSeconds) {
  grdd_timestamp ts = {.seconds = INT64_MIN, .nanos = 0};
  char buffer[64];
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // INT64_MIN hat 20 Zeichen (weil negativ)
  ASSERT_EQ(written, 30); // 20 + 1 + 9
  std::string expected = std::to_string(INT64_MIN) + ".000000000";
  EXPECT_STREQ(buffer, expected.c_str());
}

// nanos outside [0, 999999999] has no 9 digit representation. It used to drive the zero
// padding negative, and memset took that as a length near SIZE_MAX — a stack overflow
// reachable from a decoded transaction, since no wire type bounds the field.
TEST(TimestampToString, RejectsNanosOutOfRange) {
  char buffer[64];
  for (int32_t bad : {-1, -999999999, -1000000000, INT32_MIN, 1000000000, INT32_MAX}) {
    grdd_timestamp ts = {.seconds = 1, .nanos = bad};
    EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &ts), 0u) << "nanos " << bad;
    EXPECT_EQ(grdd_timestamp_calculate_string_size(&ts), 0u) << "nanos " << bad;
  }

  // the edges of the valid range still work
  for (int32_t good : {0, 1, 999999999}) {
    grdd_timestamp ts = {.seconds = 1, .nanos = good};
    EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &ts), 11u) << "nanos " << good;
    EXPECT_EQ(grdd_timestamp_calculate_string_size(&ts), 11u) << "nanos " << good;
  }
}

TEST(TimestampToString, BufferTooSmall) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 123456789};
  char buffer[10];
  size_t required = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  // Erwartet: 9 + 1 + 9 = 19
  ASSERT_EQ(required, 19);
  // Buffer sollte unverändert sein (oder wir prüfen, dass nichts drüber geschrieben wurde)
  // Da wir die Größe nicht prüfen können, testen wir, dass der Puffer nicht überschrieben wurde
  // Wir füllen mit 'x' und prüfen, ob die ersten bytes noch 'x' sind
  memset(buffer, 'x', sizeof(buffer));
  size_t written = grdd_timestamp_to_string(buffer, sizeof(buffer), &ts);
  ASSERT_EQ(written, 19);
  // Der Puffer sollte immer noch mit 'x' gefüllt sein, weil nichts geschrieben wurde
  for (size_t i = 0; i < sizeof(buffer); ++i) { ASSERT_EQ(buffer[i], 'x'); }
}

// promise: buffer_size counts the terminator, the way snprintf counts it. A buffer of exactly
// the character count is one byte short and is refused; one more than that is the exact fit.
// This test used to hand over 19 bytes for 19 characters and expect them written, which put the
// terminator one byte past the end of a stack array -- AddressSanitizer reports it.
TEST(TimestampToString, BufferExactlyRequired) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 0};
  const char *expected = "123456789.000000000";
  const size_t characters = strlen(expected); // 9 digits, the dot, 9 nanos

  char exact[20]; // characters + the terminator
  memset(exact, 'x', sizeof(exact));
  ASSERT_EQ(grdd_timestamp_to_string(exact, sizeof(exact), &ts), characters);
  EXPECT_STREQ(exact, expected);

  char one_short[19];
  memset(one_short, 'x', sizeof(one_short));
  EXPECT_EQ(grdd_timestamp_to_string(one_short, sizeof(one_short), &ts), characters)
      << "a buffer with no room for the terminator has to be refused, not filled";
  for (char c : one_short) { EXPECT_EQ(c, 'x') << "wrote into a buffer it had refused"; }
}

TEST(TimestampCalculateStringSize, Basic) {
  grdd_timestamp ts = {.seconds = 123456789, .nanos = 0};
  size_t size = grdd_timestamp_calculate_string_size(&ts);
  ASSERT_EQ(size, 19); // 9 + 1 + 9
}

TEST(TimestampCalculateStringSize, Negative) {
  grdd_timestamp ts = {.seconds = -123456789, .nanos = 0};
  size_t size = grdd_timestamp_calculate_string_size(&ts);
  ASSERT_EQ(size, 20); // 10 (wegen Minus) + 1 + 9
}

TEST(TimestampCalculateStringSize, Null) {
  size_t size = grdd_timestamp_calculate_string_size(nullptr);
  ASSERT_EQ(size, 0);
}

TEST(TimestampMinus, Basic) {
  grdd_timestamp t1 = {.seconds = 10, .nanos = 500000000};
  grdd_timestamp t2 = {.seconds = 3, .nanos = 200000000};
  auto result = grdd_timestamp_minus(&t1, &t2);
  ASSERT_EQ(result.seconds, 7);
  ASSERT_EQ(result.nanos, 300000000);
}

TEST(TimestampMinus, Borrow) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 100000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 500000000};
  auto result = grdd_timestamp_minus(&t1, &t2);
  ASSERT_EQ(result.seconds, 2);
  ASSERT_EQ(result.nanos, 600000000);
}

TEST(TimestampPlus, Basic) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 100000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 200000000};
  auto result = grdd_timestamp_plus(&t1, &t2);
  ASSERT_EQ(result.seconds, 7);
  ASSERT_EQ(result.nanos, 300000000);
}

TEST(TimestampPlus, Carry) {
  grdd_timestamp t1 = {.seconds = 5, .nanos = 800000000};
  grdd_timestamp t2 = {.seconds = 2, .nanos = 500000000};
  auto result = grdd_timestamp_plus(&t1, &t2);
  ASSERT_EQ(result.seconds, 8);
  ASSERT_EQ(result.nanos, 300000000);
}

// ---------------------------------------------------------------------------
// arithmetic: normalized results and no overflow
// ---------------------------------------------------------------------------

// A timestamp is seconds + nanos/1e9, so whole seconds may move between the fields. Both
// operations put the result back into nanos ∈ [0, 1e9) — the range grdd_timestamp_to_string
// can print, and the one nothing in the wire types enforces on the way in.
TEST(TimestampArithmetic, NormalizesOutOfRangeNanos) {
  char buffer[64];

  // an operand carrying more than a second's worth of nanos
  grdd_timestamp a = {.seconds = 5, .nanos = 1500000000};
  grdd_timestamp zero = {.seconds = 0, .nanos = 0};
  auto r = grdd_timestamp_plus(&a, &zero);
  EXPECT_EQ(r.seconds, 6);
  EXPECT_EQ(r.nanos, 500000000);
  EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 11u); // 1 + '.' + 9
  EXPECT_STREQ(buffer, "6.500000000");

  // and one carrying a negative amount
  grdd_timestamp b = {.seconds = 5, .nanos = -1500000000};
  r = grdd_timestamp_plus(&b, &zero);
  EXPECT_EQ(r.seconds, 3);
  EXPECT_EQ(r.nanos, 500000000);

  // a difference that borrows across the second boundary stays normalized
  grdd_timestamp t1 = {.seconds = 0, .nanos = 0};
  grdd_timestamp t2 = {.seconds = 0, .nanos = 1};
  r = grdd_timestamp_minus(&t1, &t2);
  EXPECT_EQ(r.seconds, -1);
  EXPECT_EQ(r.nanos, 999999999);
  EXPECT_EQ(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 12u);
  EXPECT_STREQ(buffer, "-1.999999999");
}

TEST(TimestampArithmetic, ResultIsAlwaysPrintable) {
  // whatever comes in, the result can be fed straight to the string functions
  const int32_t nanos[] = {INT32_MIN, -1000000001, -1, 0, 1, 999999999, 1000000000, INT32_MAX};
  const int64_t seconds[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
  char buffer[64];
  for (int64_t s1 : seconds) {
    for (int32_t n1 : nanos) {
      for (int64_t s2 : seconds) {
        for (int32_t n2 : nanos) {
          grdd_timestamp a = {.seconds = s1, .nanos = n1};
          grdd_timestamp b = {.seconds = s2, .nanos = n2};
          for (auto r : {grdd_timestamp_plus(&a, &b), grdd_timestamp_minus(&a, &b)}) {
            ASSERT_GE(r.nanos, 0);
            ASSERT_LT(r.nanos, 1000000000);
            ASSERT_NE(grdd_timestamp_to_string(buffer, sizeof(buffer), &r), 0u);
          }
        }
      }
    }
  }
}

TEST(TimestampArithmetic, SaturatesInsteadOfOverflowing) {
  grdd_timestamp max = {.seconds = INT64_MAX, .nanos = 0};
  grdd_timestamp min = {.seconds = INT64_MIN, .nanos = 0};
  grdd_timestamp one = {.seconds = 1, .nanos = 0};

  EXPECT_EQ(grdd_timestamp_plus(&max, &one).seconds, INT64_MAX);
  EXPECT_EQ(grdd_timestamp_plus(&max, &max).seconds, INT64_MAX);
  EXPECT_EQ(grdd_timestamp_minus(&min, &one).seconds, INT64_MIN);
  EXPECT_EQ(grdd_timestamp_minus(&min, &max).seconds, INT64_MIN);
  // INT64_MIN - INT64_MIN is representable and must not be pinned
  EXPECT_EQ(grdd_timestamp_minus(&min, &min).seconds, 0);
  EXPECT_EQ(grdd_timestamp_minus(&max, &max).seconds, 0);
  // the borrow at the very bottom has nowhere to go either
  grdd_timestamp min_borrow = {.seconds = INT64_MIN, .nanos = 0};
  grdd_timestamp tick = {.seconds = 0, .nanos = 1};
  EXPECT_EQ(grdd_timestamp_minus(&min_borrow, &tick).seconds, INT64_MIN);
}

TEST(TimestampArithmetic, ToleratesNullOperands) {
  grdd_timestamp t = {.seconds = 7, .nanos = 8};
  for (auto r :
       {grdd_timestamp_plus(nullptr, &t), grdd_timestamp_plus(&t, nullptr),
        grdd_timestamp_minus(nullptr, &t), grdd_timestamp_minus(&t, nullptr),
        grdd_timestamp_plus(nullptr, nullptr)}) {
    EXPECT_EQ(r.seconds, 0);
    EXPECT_EQ(r.nanos, 0);
  }
}

// ********** the enumerations of types/, read back through the names they were written by *****

#include "gradido_blockchain_core/types/address.h"
#include "gradido_blockchain_core/types/balance_derivation.h"
#include "gradido_blockchain_core/types/cross_group.h"
#include "gradido_blockchain_core/types/ledger_anchor.h"
#include "gradido_blockchain_core/types/memo_key.h"
#include "gradido_blockchain_core/types/transaction.h"

#include <cstring>

// to_string() and from_string() are each other's inverse, so every enumerator has to survive the
// trip through its own spelling. A name that maps back to something else is a value silently
// changed; a name that maps back to the fallback is a value silently lost.
#define EXPECT_ROUND_TRIP(from_string, to_string, value)                                           \
  do {                                                                                             \
    const char *spelled = to_string(value);                                                        \
    EXPECT_EQ(value, from_string(spelled, strlen(spelled))) << spelled;                            \
  } while (0)

TEST(EnumFromString, Address_RoundTrip) {
  for (int value = GRDT_ADDRESS_NONE; value <= GRDT_ADDRESS_DEFERRED_TRANSFER; ++value) {
    EXPECT_ROUND_TRIP(grdt_address_from_string, grdt_address_to_string, (grdt_address)value);
  }
}

TEST(EnumFromString, BalanceDerivation_RoundTrip) {
  for (int value = GRDT_BALANCE_DERIVATION_UNSPECIFIED; value <= GRDT_BALANCE_DERIVATION_EXTERN;
       ++value) {
    EXPECT_ROUND_TRIP(
        grdt_balance_derivation_from_string, grdt_balance_derivation_to_string,
        (grdt_balance_derivation)value
    );
  }
}

TEST(EnumFromString, CrossGroup_RoundTrip) {
  for (int value = GRDT_CROSS_GROUP_LOCAL; value <= GRDT_CROSS_GROUP_NONE; ++value) {
    EXPECT_ROUND_TRIP(
        grdt_cross_group_from_string, grdt_cross_group_to_string, (grdt_cross_group)value
    );
  }
}

TEST(EnumFromString, LedgerAnchor_RoundTrip) {
  // the enumerators are not contiguous -- 1 was an iota message id and cannot come back
  const grdt_ledger_anchor values[] = {
      GRDT_LEDGER_ANCHOR_UNSPECIFIED,
      GRDT_LEDGER_ANCHOR_HIERO_TRANSACTION_ID,
      GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_ID,
      GRDT_LEDGER_ANCHOR_NODE_TRIGGER_TRANSACTION_ID,
      GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_COMMUNITY_ID,
      GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_USER_ID,
      GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_CONTRIBUTION_ID,
      GRDT_LEDGER_ANCHOR_LEGACY_GRADIDO_DB_TRANSACTION_LINK_ID
  };
  for (grdt_ledger_anchor value : values) {
    EXPECT_ROUND_TRIP(grdt_ledger_anchor_from_string, grdt_ledger_anchor_to_string, value);
  }
}

TEST(EnumFromString, MemoKey_RoundTrip) {
  for (int value = GRDT_MEMO_KEY_SHARED_SECRET; value <= GRDT_MEMO_KEY_NONE; ++value) {
    EXPECT_ROUND_TRIP(grdt_memo_key_from_string, grdt_memo_key_to_string, (grdt_memo_key)value);
  }
}

TEST(EnumFromString, Transaction_RoundTrip) {
  for (int value = GRDT_TRANSACTION_NONE; value < GRDT_TRANSACTION_COUNT; ++value) {
    EXPECT_ROUND_TRIP(
        grdt_transaction_from_string, grdt_transaction_to_string, (grdt_transaction)value
    );
  }
}

TEST(EnumFromString, UnknownNameAnswersTheValueThatMeansNone) {
  const char unknown[] = "GRDT_SOMETHING_THAT_WAS_NEVER_WRITTEN";
  const size_t size = strlen(unknown);
  EXPECT_EQ(GRDT_ADDRESS_NONE, grdt_address_from_string(unknown, size));
  EXPECT_EQ(
      GRDT_BALANCE_DERIVATION_UNSPECIFIED, grdt_balance_derivation_from_string(unknown, size)
  );
  EXPECT_EQ(GRDT_TRANSACTION_NONE, grdt_transaction_from_string(unknown, size));
  // these two have their none past the protobuf range, because zero is a value of their own
  EXPECT_EQ(GRDT_CROSS_GROUP_NONE, grdt_cross_group_from_string(unknown, size));
  EXPECT_EQ(GRDT_MEMO_KEY_NONE, grdt_memo_key_from_string(unknown, size));
  // and this one has no none to give: "unspecified" is an anchor type a transaction may carry,
  // so an unknown name and the real thing are the same answer here
  EXPECT_EQ(GRDT_LEDGER_ANCHOR_UNSPECIFIED, grdt_ledger_anchor_from_string(unknown, size));
}

TEST(EnumFromString, TheValueThatMeansNoneIsNotTheValueThatIsZero) {
  // the whole point of adding these two: an unrecognised name must not read as a local
  // transaction or a shared secret memo, which are the enumerators the wire calls zero
  EXPECT_NE(GRDT_CROSS_GROUP_LOCAL, GRDT_CROSS_GROUP_NONE);
  EXPECT_NE(GRDT_MEMO_KEY_SHARED_SECRET, GRDT_MEMO_KEY_NONE);
  const char local[] = "GRDT_CROSS_GROUP_LOCAL";
  EXPECT_EQ(GRDT_CROSS_GROUP_LOCAL, grdt_cross_group_from_string(local, strlen(local)));
  const char shared[] = "GRDT_MEMO_KEY_SHARED_SECRET";
  EXPECT_EQ(GRDT_MEMO_KEY_SHARED_SECRET, grdt_memo_key_from_string(shared, strlen(shared)));
}

TEST(EnumFromString, PrefixOfANameIsNotThatName) {
  // GRDU_STRING_EQUALS matches the length before the bytes, so a name that merely starts like
  // another one is not it
  const char prefix[] = "GRDT_TRANSACTION_TRANSFE";
  EXPECT_EQ(GRDT_TRANSACTION_NONE, grdt_transaction_from_string(prefix, strlen(prefix)));
  const char longer[] = "GRDT_TRANSACTION_TRANSFER_AND_MORE";
  EXPECT_EQ(GRDT_TRANSACTION_NONE, grdt_transaction_from_string(longer, strlen(longer)));
}
