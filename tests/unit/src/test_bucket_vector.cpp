#include "gradido_blockchain_core/utils/bucket_vector.h"
#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <set>
#include <vector>

namespace {
struct payload {
  uint64_t id;
  uint8_t blob[24];
};
} // namespace

// tiny buckets on purpose: every test crosses bucket boundaries again and again
GRDU_BVEC_STATIC(u32_vec, uint32_t, 3)
GRDU_BVEC_STATIC(pay_vec, payload, 4)
// degenerate bucket size: one element per bucket, every push allocates
GRDU_BVEC_STATIC(one_vec, uint32_t, 0)

namespace {

/**
 * Verify the internal state a generated vector must always be in.
 *
 * The generated types all carry the same field names, so one template covers every payload.
 * @p capacity is the BUCKET_CAPACITY the type was instantiated with.
 */
template <typename V> void CheckInvariants(const V &v, size_t capacity) {
  ASSERT_LE(v.bucket_count, v.bucket_capacity);
  if (v.bucket_capacity > 0 || v.bucket_count > 0) { ASSERT_NE(v.buckets, nullptr); }

  std::set<const void *> distinct;
  for (size_t i = 0; i < v.bucket_count; ++i) {
    ASSERT_NE(v.buckets[i], nullptr) << "bucket " << i;
    ASSERT_TRUE(distinct.insert(v.buckets[i]).second) << "bucket " << i << " listed twice";
  }

  if (v.size == 0) {
    // the empty marker: no tail, and tail_used parked at capacity so the next push grows
    EXPECT_EQ(v.tail, nullptr);
    EXPECT_EQ(v.tail_index, 0u);
    EXPECT_EQ(v.tail_used, capacity);
    return;
  }
  ASSERT_NE(v.tail, nullptr);
  ASSERT_LT(v.tail_index, v.bucket_count);
  EXPECT_EQ(v.tail, v.buckets[v.tail_index]);
  EXPECT_GE(v.tail_used, 1u);
  EXPECT_LE(v.tail_used, capacity);
  // the one equation tying the three counters together
  EXPECT_EQ(v.size, v.tail_index * capacity + v.tail_used);
}

} // namespace

TEST(BucketVector, EmptyState) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  EXPECT_EQ(u32_vec_size(&v), 0u);
  EXPECT_EQ(u32_vec_bucket_count(&v), 0u);
  EXPECT_EQ(u32_vec_front(&v), nullptr);
  EXPECT_EQ(u32_vec_back(&v), nullptr);
  EXPECT_EQ(u32_vec_at(&v, 0), nullptr);
  EXPECT_EQ(u32_vec_pop(&v), GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS);
  u32_vec_free(&v);
}

TEST(BucketVector, InitNullPointer) {
  EXPECT_EQ(u32_vec_init(nullptr, nullptr), GRD_ERROR_NULL_POINTER);
  EXPECT_EQ(u32_vec_reserve(nullptr, 10), GRD_ERROR_NULL_POINTER);
}

TEST(BucketVector, PushAndRandomAccess) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < 1000; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i * 3)), GRD_SUCCESS);
    ASSERT_EQ(u32_vec_size(&v), i + 1);
    EXPECT_EQ(*u32_vec_back(&v), static_cast<uint32_t>(i * 3));
    EXPECT_EQ(*u32_vec_front(&v), 0u);
  }
  for (size_t i = 0; i < 1000; ++i) {
    ASSERT_EQ(*u32_vec_at(&v, i), static_cast<uint32_t>(i * 3));
    ASSERT_EQ(*u32_vec_get(&v, i), static_cast<uint32_t>(i * 3));
  }
  EXPECT_EQ(u32_vec_at(&v, 1000), nullptr);
  EXPECT_EQ(u32_vec_bucket_count(&v), 1000u / u32_vec_BUCKET_CAPACITY);
  u32_vec_free(&v);
}

TEST(BucketVector, PointerStabilityAcrossGrowth) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  ASSERT_EQ(u32_vec_push(&v, 11u), GRD_SUCCESS);
  uint32_t *first = u32_vec_at(&v, 0);
  for (size_t i = 0; i < 5000; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  EXPECT_EQ(first, u32_vec_at(&v, 0));
  EXPECT_EQ(*first, 11u);
  u32_vec_free(&v);
}

TEST(BucketVector, IterationInOrder) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < 300; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i * 7)), GRD_SUCCESS);
  }

  uint32_t *item = nullptr;
  size_t seen = 0;
  GRDU_BVEC_FOREACH(u32_vec, &v, item, index) {
    ASSERT_EQ(*item, static_cast<uint32_t>(index * 7));
    ++seen;
  }
  EXPECT_EQ(seen, 300u);

  size_t total = 0;
  for (size_t b = 0, buckets = u32_vec_bucket_count(&v); b < buckets; ++b) {
    const uint32_t *data = u32_vec_bucket_data(&v, b);
    const size_t count = u32_vec_bucket_size(&v, b);
    for (size_t k = 0; k < count; ++k) {
      ASSERT_EQ(data[k], static_cast<uint32_t>((total + k) * 7));
    }
    total += count;
  }
  EXPECT_EQ(total, 300u);
  u32_vec_free(&v);
}

TEST(BucketVector, PopDrainsAndRefills) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < 500; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  for (size_t i = 500; i > 0; --i) {
    ASSERT_EQ(*u32_vec_back(&v), static_cast<uint32_t>(i - 1));
    ASSERT_EQ(u32_vec_size(&v), i);
    ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
  }
  EXPECT_EQ(u32_vec_size(&v), 0u);
  EXPECT_EQ(u32_vec_back(&v), nullptr);
  EXPECT_EQ(u32_vec_bucket_count(&v), 0u);
  EXPECT_EQ(u32_vec_pop(&v), GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS);

  // the drained buckets stay allocated and are taken up again
  const size_t buckets_before = v.bucket_count;
  for (size_t i = 0; i < 500; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  EXPECT_EQ(v.bucket_count, buckets_before);
  for (size_t i = 0; i < 500; ++i) ASSERT_EQ(*u32_vec_at(&v, i), static_cast<uint32_t>(i));
  u32_vec_free(&v);
}

TEST(BucketVector, PopOnBucketBoundary) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < u32_vec_BUCKET_CAPACITY; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  EXPECT_EQ(u32_vec_bucket_count(&v), 1u);

  ASSERT_EQ(u32_vec_push(&v, 99u), GRD_SUCCESS); // opens the second bucket
  EXPECT_EQ(u32_vec_bucket_count(&v), 2u);
  ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS); // and falls back into the first
  EXPECT_EQ(u32_vec_bucket_count(&v), 1u);
  EXPECT_EQ(u32_vec_size(&v), static_cast<size_t>(u32_vec_BUCKET_CAPACITY));
  EXPECT_EQ(*u32_vec_back(&v), static_cast<uint32_t>(u32_vec_BUCKET_CAPACITY - 1));

  ASSERT_EQ(u32_vec_push(&v, 123u), GRD_SUCCESS);
  EXPECT_EQ(*u32_vec_back(&v), 123u);
  EXPECT_EQ(u32_vec_bucket_count(&v), 2u);
  u32_vec_free(&v);
}

TEST(BucketVector, ClearKeepsBuckets) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < 100; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  const size_t buckets_before = v.bucket_count;
  u32_vec_clear(&v);
  EXPECT_EQ(u32_vec_size(&v), 0u);
  EXPECT_EQ(u32_vec_bucket_count(&v), 0u);
  EXPECT_EQ(v.bucket_count, buckets_before);
  ASSERT_EQ(u32_vec_push(&v, 5u), GRD_SUCCESS);
  EXPECT_EQ(*u32_vec_front(&v), 5u);
  EXPECT_EQ(*u32_vec_back(&v), 5u);
  EXPECT_EQ(v.bucket_count, buckets_before);
  u32_vec_free(&v);
}

TEST(BucketVector, FreeIsIdempotent) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  ASSERT_EQ(u32_vec_push(&v, 1u), GRD_SUCCESS);
  u32_vec_free(&v);
  u32_vec_free(&v);
  EXPECT_EQ(u32_vec_size(&v), 0u);
  ASSERT_EQ(u32_vec_push(&v, 2u), GRD_SUCCESS); // usable again after free
  EXPECT_EQ(*u32_vec_front(&v), 2u);
  u32_vec_free(&v);
}

TEST(BucketVector, ReserveAllocatesUpFront) {
  pay_vec v;
  ASSERT_EQ(pay_vec_init(&v, nullptr), GRD_SUCCESS);
  ASSERT_EQ(pay_vec_reserve(&v, 500), GRD_SUCCESS);
  const size_t expected = (500 + pay_vec_BUCKET_MASK) / pay_vec_BUCKET_CAPACITY;
  EXPECT_EQ(v.bucket_count, expected);
  EXPECT_EQ(pay_vec_size(&v), 0u);

  ASSERT_EQ(pay_vec_reserve(&v, 10), GRD_SUCCESS); // never shrinks
  EXPECT_EQ(v.bucket_count, expected);

  for (size_t i = 0; i < 500; ++i) {
    payload *slot = nullptr;
    ASSERT_EQ(pay_vec_emplace(&v, &slot), GRD_SUCCESS);
    slot->id = i;
    std::memset(slot->blob, static_cast<int>(i & 0xff), sizeof(slot->blob));
  }
  EXPECT_EQ(v.bucket_count, expected); // reserved buckets covered every push
  for (size_t i = 0; i < 500; ++i) {
    ASSERT_EQ(pay_vec_at(&v, i)->id, i);
    ASSERT_EQ(pay_vec_at(&v, i)->blob[0], static_cast<uint8_t>(i & 0xff));
  }
  pay_vec_free(&v);
}

TEST(BucketVector, PushPtrCopiesPayload) {
  pay_vec v;
  ASSERT_EQ(pay_vec_init(&v, nullptr), GRD_SUCCESS);
  payload p{};
  p.id = 777;
  std::memset(p.blob, 3, sizeof(p.blob));
  ASSERT_EQ(pay_vec_push_ptr(&v, &p), GRD_SUCCESS);
  ASSERT_EQ(pay_vec_push(&v, p), GRD_SUCCESS);
  p.id = 0; // source may change, the stored copies must not
  EXPECT_EQ(pay_vec_at(&v, 0)->id, 777u);
  EXPECT_EQ(pay_vec_at(&v, 1)->id, 777u);
  EXPECT_EQ(pay_vec_at(&v, 1)->blob[23], 3u);
  pay_vec_free(&v);
}

TEST(BucketVector, CopyTo) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (size_t i = 0; i < 100; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i * 2)), GRD_SUCCESS);
  }
  uint32_t flat[100];
  EXPECT_EQ(u32_vec_copy_to(&v, flat, 99), GRD_ERROR_DESTINATION_BUFFER_TO_SMALL);
  EXPECT_EQ(u32_vec_copy_to(&v, nullptr, 100), GRD_ERROR_NULL_POINTER);
  ASSERT_EQ(u32_vec_copy_to(&v, flat, 100), GRD_SUCCESS);
  for (size_t i = 0; i < 100; ++i) ASSERT_EQ(flat[i], static_cast<uint32_t>(i * 2));
  u32_vec_free(&v);
}

TEST(BucketVector, ArenaAllocator) {
  grd_memory arena;
  ASSERT_EQ(grd_memory_init_arena(&arena, 1024 * 1024), GRD_SUCCESS);

  pay_vec v;
  ASSERT_EQ(pay_vec_init(&v, &arena), GRD_SUCCESS);
  ASSERT_EQ(pay_vec_reserve(&v, 2000), GRD_SUCCESS);
  for (size_t i = 0; i < 2000; ++i) {
    payload p{};
    p.id = i * 2;
    ASSERT_EQ(pay_vec_push_ptr(&v, &p), GRD_SUCCESS);
  }
  for (size_t i = 0; i < 2000; ++i) ASSERT_EQ(pay_vec_at(&v, i)->id, i * 2);
  pay_vec_free(&v);
  grd_memory_free(&arena);
}

TEST(BucketVector, ExhaustedArenaReportsOutOfMemory) {
  uint8_t buffer[256];
  grd_memory small;
  ASSERT_EQ(grd_memory_init_arena_static(&small, buffer, sizeof(buffer)), GRD_SUCCESS);

  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, &small), GRD_SUCCESS);
  size_t pushed = 0;
  grd_result result = GRD_SUCCESS;
  while ((result = u32_vec_push(&v, static_cast<uint32_t>(pushed))) == GRD_SUCCESS) ++pushed;
  EXPECT_EQ(result, GRD_ERROR_OUT_OF_MEMORY);
  EXPECT_GT(pushed, 0u);
  EXPECT_EQ(u32_vec_size(&v), pushed); // the failed push left the vector untouched
  for (size_t i = 0; i < pushed; ++i) ASSERT_EQ(*u32_vec_at(&v, i), static_cast<uint32_t>(i));
  u32_vec_free(&v);
  grd_memory_free(&small);
}

// --- reference tests against std::vector ---------------------------------------------------

namespace {

/** Compare the whole sequence — through every read path the API offers. */
void ExpectMatches(const u32_vec &v, const std::vector<uint32_t> &ref) {
  ASSERT_EQ(u32_vec_size(&v), ref.size());
  if (ref.empty()) {
    EXPECT_EQ(u32_vec_front(&v), nullptr);
    EXPECT_EQ(u32_vec_back(&v), nullptr);
    EXPECT_EQ(u32_vec_bucket_count(&v), 0u);
    return;
  }
  EXPECT_EQ(*u32_vec_front(&v), ref.front());
  EXPECT_EQ(*u32_vec_back(&v), ref.back());

  for (size_t i = 0; i < ref.size(); ++i) {
    ASSERT_EQ(*u32_vec_at(&v, i), ref[i]) << "at index " << i;
    ASSERT_EQ(*u32_vec_get(&v, i), ref[i]) << "at index " << i;
  }
  EXPECT_EQ(u32_vec_at(&v, ref.size()), nullptr);

  // bucket-wise traversal must yield the same sequence, and cover it exactly once
  std::vector<uint32_t> walked;
  walked.reserve(ref.size());
  for (size_t b = 0, buckets = u32_vec_bucket_count(&v); b < buckets; ++b) {
    const uint32_t *data = u32_vec_bucket_data(&v, b);
    const size_t count = u32_vec_bucket_size(&v, b);
    ASSERT_GE(count, 1u);
    ASSERT_LE(count, static_cast<size_t>(u32_vec_BUCKET_CAPACITY));
    walked.insert(walked.end(), data, data + count);
  }
  EXPECT_EQ(walked, ref);

  std::vector<uint32_t> flat(ref.size());
  ASSERT_EQ(u32_vec_copy_to(&v, flat.data(), flat.size()), GRD_SUCCESS);
  EXPECT_EQ(flat, ref);
}

} // namespace

TEST(BucketVectorReference, RandomOperationSequence) {
  std::mt19937 rng(20260725u);
  std::uniform_int_distribution<int> pick(0, 99);

  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  std::vector<uint32_t> ref;

  for (int step = 0; step < 60000; ++step) {
    const int roll = pick(rng);
    if (roll < 55) { // push
      const uint32_t value = static_cast<uint32_t>(rng());
      ASSERT_EQ(u32_vec_push(&v, value), GRD_SUCCESS);
      ref.push_back(value);
    } else if (roll < 85) { // pop
      if (ref.empty()) {
        ASSERT_EQ(u32_vec_pop(&v), GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS);
      } else {
        ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
        ref.pop_back();
      }
    } else if (roll < 95) { // spot check a random index
      if (!ref.empty()) {
        const size_t index = rng() % ref.size();
        ASSERT_EQ(*u32_vec_at(&v, index), ref[index]) << "step " << step;
      }
      ASSERT_EQ(u32_vec_at(&v, ref.size()), nullptr);
    } else if (roll < 98) { // reserve, must never disturb the content
      ASSERT_EQ(u32_vec_reserve(&v, ref.size() + (rng() % 500)), GRD_SUCCESS);
    } else { // clear
      u32_vec_clear(&v);
      ref.clear();
    }

    ASSERT_EQ(u32_vec_size(&v), ref.size()) << "step " << step;
    if (step % 250 == 0) {
      ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY)) << "step " << step;
    }
    if (step % 2000 == 0) { ASSERT_NO_FATAL_FAILURE(ExpectMatches(v, ref)) << "step " << step; }
  }

  ASSERT_NO_FATAL_FAILURE(ExpectMatches(v, ref));
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorReference, PushHeavySequenceStaysInSync) {
  // push-dominated: drives the index array through many doublings
  std::mt19937 rng(4711u);
  std::uniform_int_distribution<int> pick(0, 99);

  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  std::vector<uint32_t> ref;

  for (int step = 0; step < 40000; ++step) {
    if (pick(rng) < 90) {
      const uint32_t value = static_cast<uint32_t>(step);
      ASSERT_EQ(u32_vec_push(&v, value), GRD_SUCCESS);
      ref.push_back(value);
    } else if (!ref.empty()) {
      ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
      ref.pop_back();
    }
    ASSERT_EQ(u32_vec_size(&v), ref.size());
  }
  EXPECT_GT(ref.size(), 20000u);
  ASSERT_NO_FATAL_FAILURE(ExpectMatches(v, ref));
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorReference, SingleElementBucketsMatchStdVector) {
  // BUCKET_CAPACITY == 1: shift 0, mask 0, every push opens a new bucket
  std::mt19937 rng(99991u);
  std::uniform_int_distribution<int> pick(0, 99);

  one_vec v;
  ASSERT_EQ(one_vec_init(&v, nullptr), GRD_SUCCESS);
  std::vector<uint32_t> ref;

  for (int step = 0; step < 5000; ++step) {
    if (pick(rng) < 65) {
      const uint32_t value = static_cast<uint32_t>(rng());
      ASSERT_EQ(one_vec_push(&v, value), GRD_SUCCESS);
      ref.push_back(value);
    } else if (!ref.empty()) {
      ASSERT_EQ(one_vec_pop(&v), GRD_SUCCESS);
      ref.pop_back();
    }
    ASSERT_EQ(one_vec_size(&v), ref.size());
    ASSERT_EQ(one_vec_bucket_count(&v), ref.size()); // one bucket per element
  }
  for (size_t i = 0; i < ref.size(); ++i) ASSERT_EQ(*one_vec_at(&v, i), ref[i]);
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, one_vec_BUCKET_CAPACITY));
  one_vec_free(&v);
}

TEST(BucketVectorReference, ArenaBackedSequence) {
  grd_memory arena;
  ASSERT_EQ(grd_memory_init_arena(&arena, 4 * 1024 * 1024), GRD_SUCCESS);

  // the arena has no realloc, so index growth takes the copy path here
  std::mt19937 rng(1312u);
  std::uniform_int_distribution<int> pick(0, 99);

  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, &arena), GRD_SUCCESS);
  std::vector<uint32_t> ref;

  for (int step = 0; step < 20000; ++step) {
    const int roll = pick(rng);
    if (roll < 70) {
      const uint32_t value = static_cast<uint32_t>(rng());
      ASSERT_EQ(u32_vec_push(&v, value), GRD_SUCCESS);
      ref.push_back(value);
    } else if (roll < 95) {
      if (!ref.empty()) {
        ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
        ref.pop_back();
      }
    } else {
      u32_vec_clear(&v);
      ref.clear();
    }
    ASSERT_EQ(u32_vec_size(&v), ref.size());
  }
  ASSERT_NO_FATAL_FAILURE(ExpectMatches(v, ref));
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));

  u32_vec_free(&v);
  grd_memory_free(&arena);
}

// --- extreme values ------------------------------------------------------------------------

TEST(BucketVectorLimits, ReserveZeroAllocatesNothing) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  EXPECT_EQ(u32_vec_reserve(&v, 0), GRD_SUCCESS);
  EXPECT_EQ(v.bucket_count, 0u);
  EXPECT_EQ(v.bucket_capacity, 0u);
  EXPECT_EQ(v.buckets, nullptr);
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));

  EXPECT_EQ(u32_vec_reserve(&v, 1), GRD_SUCCESS);
  EXPECT_EQ(v.bucket_count, 1u);
  EXPECT_EQ(u32_vec_size(&v), 0u); // reserve never creates elements
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorLimits, ReserveRejectsCountOverflow) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  ASSERT_EQ(u32_vec_push(&v, 42u), GRD_SUCCESS);

  // rounding up to whole buckets would overflow before the shift
  EXPECT_EQ(u32_vec_reserve(&v, SIZE_MAX), GRD_ERROR_ARITHMETIC_OVERFLOW);
  EXPECT_EQ(u32_vec_reserve(&v, SIZE_MAX - 1), GRD_ERROR_ARITHMETIC_OVERFLOW);
  EXPECT_EQ(u32_vec_reserve(&v, SIZE_MAX - u32_vec_BUCKET_MASK + 1), GRD_ERROR_ARITHMETIC_OVERFLOW);
  // and the payload of that many elements could never be addressed either
  EXPECT_EQ(u32_vec_reserve(&v, SIZE_MAX / 2), GRD_ERROR_ARITHMETIC_OVERFLOW);
  EXPECT_EQ(u32_vec_reserve(&v, SIZE_MAX / sizeof(uint32_t) + 1), GRD_ERROR_ARITHMETIC_OVERFLOW);

  // the bound follows the payload size: the larger the element, the earlier it bites
  pay_vec big;
  ASSERT_EQ(pay_vec_init(&big, nullptr), GRD_SUCCESS);
  EXPECT_EQ(pay_vec_reserve(&big, SIZE_MAX / sizeof(payload) + 1), GRD_ERROR_ARITHMETIC_OVERFLOW);
  EXPECT_EQ(pay_vec_reserve(&big, 64), GRD_SUCCESS); // a sane count still goes through
  pay_vec_free(&big);

  // the rejected calls left the vector untouched and usable
  EXPECT_EQ(u32_vec_size(&v), 1u);
  EXPECT_EQ(*u32_vec_at(&v, 0), 42u);
  ASSERT_EQ(u32_vec_push(&v, 43u), GRD_SUCCESS);
  EXPECT_EQ(*u32_vec_back(&v), 43u);
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorLimits, ReserveHugeFailsWithoutDamage) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (uint32_t i = 0; i < 100; ++i) ASSERT_EQ(u32_vec_push(&v, i), GRD_SUCCESS);
  const size_t buckets_before = v.bucket_count;

  // rejected by the guard, so the allocator is never asked for the impossible
  EXPECT_NE(u32_vec_reserve(&v, SIZE_MAX - u32_vec_BUCKET_MASK), GRD_SUCCESS);
  EXPECT_NE(u32_vec_reserve(&v, SIZE_MAX / 3), GRD_SUCCESS);

  EXPECT_EQ(v.bucket_count, buckets_before);
  EXPECT_EQ(u32_vec_size(&v), 100u);
  for (uint32_t i = 0; i < 100; ++i) ASSERT_EQ(*u32_vec_at(&v, i), i);
  ASSERT_EQ(u32_vec_push(&v, 100u), GRD_SUCCESS);
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorLimits, RepeatedClearAndReserve) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  size_t buckets_high_water = 0;

  for (int round = 0; round < 200; ++round) {
    const size_t count = static_cast<size_t>(round % 50) * 7 + 1;
    ASSERT_EQ(u32_vec_reserve(&v, count), GRD_SUCCESS);
    ASSERT_EQ(u32_vec_reserve(&v, count), GRD_SUCCESS); // idempotent
    ASSERT_EQ(u32_vec_reserve(&v, 0), GRD_SUCCESS);     // never shrinks

    for (size_t i = 0; i < count; ++i) {
      ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
    }
    ASSERT_EQ(u32_vec_size(&v), count);
    ASSERT_EQ(*u32_vec_back(&v), static_cast<uint32_t>(count - 1));

    // buckets only ever accumulate, they are never given back by clear
    ASSERT_GE(v.bucket_count, buckets_high_water);
    buckets_high_water = v.bucket_count;

    u32_vec_clear(&v);
    u32_vec_clear(&v); // idempotent
    ASSERT_EQ(u32_vec_size(&v), 0u);
    ASSERT_EQ(v.bucket_count, buckets_high_water);
    ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY)) << "round " << round;
  }

  // the high water mark covers the largest round, nothing beyond it
  const size_t largest = 49u * 7u + 1u;
  EXPECT_EQ(buckets_high_water, (largest + u32_vec_BUCKET_MASK) / u32_vec_BUCKET_CAPACITY);
  u32_vec_free(&v);
}

TEST(BucketVectorLimits, ReserveKeepsExistingContent) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (uint32_t i = 0; i < 37; ++i) ASSERT_EQ(u32_vec_push(&v, i * 5), GRD_SUCCESS);
  const uint32_t *stable = u32_vec_at(&v, 12);

  ASSERT_EQ(u32_vec_reserve(&v, 10000), GRD_SUCCESS);
  EXPECT_EQ(u32_vec_size(&v), 37u);
  EXPECT_EQ(u32_vec_at(&v, 12), stable); // reserve moves no payload
  for (uint32_t i = 0; i < 37; ++i) ASSERT_EQ(*u32_vec_at(&v, i), i * 5);
  EXPECT_EQ(u32_vec_at(&v, 37), nullptr); // reserved capacity is not content
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));
  u32_vec_free(&v);
}

TEST(BucketVectorLimits, FreeAfterHeavyUseResetsEverything) {
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  for (uint32_t i = 0; i < 5000; ++i) ASSERT_EQ(u32_vec_push(&v, i), GRD_SUCCESS);
  u32_vec_free(&v);

  EXPECT_EQ(v.buckets, nullptr);
  EXPECT_EQ(v.bucket_count, 0u);
  EXPECT_EQ(v.bucket_capacity, 0u);
  EXPECT_EQ(v.size, 0u);
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY));

  // and the descriptor is immediately usable again, allocator setting intact
  ASSERT_EQ(u32_vec_push(&v, 1u), GRD_SUCCESS);
  EXPECT_EQ(u32_vec_size(&v), 1u);
  u32_vec_free(&v);
}

// --- internal invariants over long operation sequences --------------------------------------

TEST(BucketVectorInvariants, HoldThroughLongMixedSequence) {
  std::mt19937 rng(20250101u);
  std::uniform_int_distribution<int> pick(0, 99);

  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  size_t expected_size = 0;

  for (int step = 0; step < 30000; ++step) {
    const int roll = pick(rng);
    if (roll < 50) {
      ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(step)), GRD_SUCCESS);
      ++expected_size;
    } else if (roll < 90) {
      if (expected_size) {
        ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
        --expected_size;
      } else {
        ASSERT_EQ(u32_vec_pop(&v), GRD_ERROR_ARRAY_INDEX_OUT_OF_BOUNDS);
      }
    } else if (roll < 97) {
      ASSERT_EQ(u32_vec_reserve(&v, expected_size + 40), GRD_SUCCESS);
    } else {
      u32_vec_clear(&v);
      expected_size = 0;
    }

    ASSERT_EQ(u32_vec_size(&v), expected_size) << "step " << step;
    // checked on every single step: the counters must never drift, not even briefly
    ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, u32_vec_BUCKET_CAPACITY)) << "step " << step;

    // used buckets follow from the size alone
    const size_t used_buckets = (expected_size + u32_vec_BUCKET_MASK) / u32_vec_BUCKET_CAPACITY;
    ASSERT_EQ(u32_vec_bucket_count(&v), used_buckets) << "step " << step;
    ASSERT_LE(used_buckets, v.bucket_count);
  }
  u32_vec_free(&v);
}

TEST(BucketVectorInvariants, BucketBoundaryWalk) {
  // step back and forth across the same boundary many times
  u32_vec v;
  ASSERT_EQ(u32_vec_init(&v, nullptr), GRD_SUCCESS);
  const size_t capacity = u32_vec_BUCKET_CAPACITY;
  // three full buckets plus a single element in the fourth: sitting right on the boundary
  for (size_t i = 0; i <= capacity * 3; ++i) {
    ASSERT_EQ(u32_vec_push(&v, static_cast<uint32_t>(i)), GRD_SUCCESS);
  }
  ASSERT_EQ(v.tail_index, 3u);
  ASSERT_EQ(v.tail_used, 1u);

  for (int round = 0; round < 500; ++round) {
    ASSERT_EQ(u32_vec_pop(&v), GRD_SUCCESS);
    EXPECT_EQ(v.tail_used, capacity); // stepped back into the now-last full bucket
    EXPECT_EQ(v.tail_index, 2u);
    EXPECT_EQ(u32_vec_size(&v), capacity * 3);
    ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, capacity)) << "round " << round;

    ASSERT_EQ(u32_vec_push(&v, 7u), GRD_SUCCESS);
    EXPECT_EQ(v.tail_used, 1u); // and forward into the reused bucket
    EXPECT_EQ(v.tail_index, 3u);
    EXPECT_EQ(*u32_vec_back(&v), 7u);
    ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, capacity)) << "round " << round;
  }
  EXPECT_EQ(v.bucket_count, 4u); // no bucket was allocated twice for the same slot
  u32_vec_free(&v);
}

TEST(BucketVectorInvariants, PayloadTypeHoldsTheSameInvariants) {
  std::mt19937 rng(864231u);
  std::uniform_int_distribution<int> pick(0, 99);

  pay_vec v;
  ASSERT_EQ(pay_vec_init(&v, nullptr), GRD_SUCCESS);
  std::vector<uint64_t> ref;

  for (int step = 0; step < 10000; ++step) {
    if (pick(rng) < 60) {
      payload *slot = nullptr;
      ASSERT_EQ(pay_vec_emplace(&v, &slot), GRD_SUCCESS);
      slot->id = static_cast<uint64_t>(step);
      std::memset(slot->blob, static_cast<int>(step & 0xff), sizeof(slot->blob));
      ref.push_back(static_cast<uint64_t>(step));
    } else if (!ref.empty()) {
      ASSERT_EQ(pay_vec_pop(&v), GRD_SUCCESS);
      ref.pop_back();
    }
    ASSERT_EQ(pay_vec_size(&v), ref.size());
    if (step % 100 == 0) {
      ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, pay_vec_BUCKET_CAPACITY)) << "step " << step;
    }
  }
  for (size_t i = 0; i < ref.size(); ++i) {
    ASSERT_EQ(pay_vec_at(&v, i)->id, ref[i]) << "at index " << i;
    ASSERT_EQ(pay_vec_at(&v, i)->blob[0], static_cast<uint8_t>(ref[i] & 0xff));
  }
  ASSERT_NO_FATAL_FAILURE(CheckInvariants(v, pay_vec_BUCKET_CAPACITY));
  pay_vec_free(&v);
}
