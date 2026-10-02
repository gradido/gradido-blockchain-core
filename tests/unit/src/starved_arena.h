#ifndef GRADIDO_BLOCKCHAIN_CORE_TESTS_STARVED_ARENA_H
#define GRADIDO_BLOCKCHAIN_CORE_TESTS_STARVED_ARENA_H

/*
 * An arena a test can run dry at one exact step, and give its room back afterwards -- for the
 * tests that check an add refused part way is finished, not repeated, by the add that follows.
 *
 * arnm has no allocator that can be told to fail on a given call, but a borrowed arena is just
 * as precise: it refuses what does not fit, and arnm_arena_remaining() says what fits. The test
 * takes all but a few bytes as one block, lets the index run into the wall, and frees the block
 * again. It is the arena's newest block, so the bytes really come back; everything the index
 * allocated before sits below it and stays where it is.
 *
 * That only holds while nothing is allocated between starve() and feed(). A test that lets the
 * index allocate in between -- a map growing a bucket before the step that fails, say -- buries
 * the block, and feed() fails loudly rather than letting the retry run out of room a second time.
 */

#include "arnm/arena.h"
#include "arnm/memory.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

/** A borrowed arena that can be run dry to within a few bytes, and given its room back. */
class Starved {
public:
  explicit Starved(uint32_t bytes) : buffer_(bytes / 8u, 0u) {
    EXPECT_EQ(
        ARNM_SUCCESS, arnm_init_arena_borrow(
                          &arena, reinterpret_cast<uint8_t *>(buffer_.data()),
                          static_cast<uint32_t>(buffer_.size() * sizeof(uint64_t))
                      )
    );
  }
  ~Starved() {
    arnm_release(&arena);
  }
  Starved(const Starved &) = delete;
  Starved &operator=(const Starved &) = delete;

  /** Takes all but @p leave bytes, as the newest block of the arena. */
  void starve(uint32_t leave) {
    const uint32_t remaining = arnm_arena_remaining(&arena);
    ASSERT_GT(remaining, leave + 8u);
    hog_size_ = (remaining - leave) & ~7u;
    ASSERT_EQ(ARNM_SUCCESS, arnm_alloc(&hog_, hog_size_, &arena));
  }

  /** Gives it back. Fails when the block is no longer the newest; see the note above. */
  void feed() {
    ASSERT_EQ(ARNM_SUCCESS, arnm_free(hog_, hog_size_, &arena));
    hog_ = nullptr;
  }

  arnm arena{};

private:
  std::vector<uint64_t> buffer_;
  uint8_t *hog_ = nullptr;
  uint32_t hog_size_ = 0;
};

} // namespace

#endif // GRADIDO_BLOCKCHAIN_CORE_TESTS_STARVED_ARENA_H
