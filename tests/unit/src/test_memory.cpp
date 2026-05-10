#include <gtest/gtest.h>
#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"

TEST(MemoryTest, DynamicAreaAllocation)
{
  // init
  grdu_memory mem;
  EXPECT_EQ(grdu_memory_init_arena(&mem, 100), GRD_SUCCESS);

  // test valid alloc
  grdu_memory_block block{};
  EXPECT_EQ(grdu_memory_buffer_alloc(&block, &mem, 99), GRD_SUCCESS);
  EXPECT_EQ(block.size, 99);
  EXPECT_TRUE(block.data);

  // test alloc over the allocated area
  EXPECT_EQ(grdu_memory_buffer_alloc(&block, &mem, 2), GRD_ERROR_OUT_OF_MEMORY);
  EXPECT_EQ(grdu_memory_overflow_total(&mem), 2);

  grdu_memory_free(&mem);
}
