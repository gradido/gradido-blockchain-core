#include "gradido_blockchain_core/memory.h"
#include "gradido_blockchain_core/result.h"
#include <gtest/gtest.h>

TEST(MemoryTest, DynamicAreaAllocation)
{
  // init
  grd_memory mem;
  EXPECT_EQ(grd_memory_init_arena(&mem, 100), GRD_SUCCESS);

  // test valid alloc
  grd_memory_block block{};
  EXPECT_EQ(grd_memory_block_alloc(&block, &mem, 99), GRD_SUCCESS);
  EXPECT_EQ(block.size, 99);
  EXPECT_TRUE(block.data);

  // test alloc over the allocated area
  EXPECT_EQ(grd_memory_block_alloc(&block, &mem, 2), GRD_ERROR_OUT_OF_MEMORY);
  EXPECT_EQ(grd_memory_overflow_total(&mem), 2);

  grd_memory_free(&mem);
}
