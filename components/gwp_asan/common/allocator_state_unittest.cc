// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/gwp_asan/common/allocator_state.h"

#include <limits>

#include "base/process/process_metrics.h"
#include "base/test/gtest_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gwp_asan {
namespace internal {

static constexpr size_t kMaxMetadata = AllocatorState::kMaxMetadata;
static constexpr size_t kMaxSlots = AllocatorState::kMaxSlots;

class AllocatorStateTest : public testing::Test {
 protected:
  void InitializeState(size_t page_size,
                       size_t num_metadata,
                       size_t total_pages,
                       int base_addr_offset = 0,
                       int first_page_offset = 0,
                       int end_addr_offset = 0) {
    state_.page_size = page_size;
    state_.num_metadata = num_metadata;
    state_.total_pages = total_pages;

    // Some arbitrary page-aligned address
    const uintptr_t base = page_size * 10;
    state_.pages_base_addr = base_addr_offset + base;
    state_.first_page_addr = first_page_offset + base + page_size;
    state_.pages_end_addr =
        end_addr_offset + base + page_size * (total_pages * 2 + 1);

    // An invalid address, but it's never dereferenced in AllocatorState.
    state_.metadata_addr = 0x1234;
  }

  AllocatorState state_;
};

TEST_F(AllocatorStateTest, Valid) {
  InitializeState(base::GetPageSize(), 1, 1);
  EXPECT_TRUE(state_.IsValid());
  InitializeState(base::GetPageSize(), kMaxMetadata, kMaxSlots);
  EXPECT_TRUE(state_.IsValid());
}

TEST_F(AllocatorStateTest, InvalidNumPages) {
  InitializeState(base::GetPageSize(), 1, 0);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize(), 1, kMaxSlots + 1);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize(), 1, std::numeric_limits<size_t>::max());
  EXPECT_FALSE(state_.IsValid());
}

TEST_F(AllocatorStateTest, InvalidPageSize) {
  InitializeState(0, 1, 1);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize() + 1, 1, 1);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize() * 2, 1, 1);
  EXPECT_FALSE(state_.IsValid());
}

TEST_F(AllocatorStateTest, InvalidAddresses) {
  // Invalid [pages_base_addr, first_page_addr, pages_end_addr]
  InitializeState(base::GetPageSize(), 1, 1, 1, 1, 1);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize(), 1, 1, 1, 0, 0);
  EXPECT_FALSE(state_.IsValid());
  InitializeState(base::GetPageSize(), 1, 1, 0, 1, 0);
  EXPECT_FALSE(state_.IsValid());
  InitializeState(base::GetPageSize(), 1, 1, 0, 0, 1);
  EXPECT_FALSE(state_.IsValid());

  InitializeState(base::GetPageSize(), 1, 1, base::GetPageSize(), 0, 0);
  EXPECT_FALSE(state_.IsValid());
  InitializeState(base::GetPageSize(), 1, 1, 0, base::GetPageSize(), 0);
  EXPECT_FALSE(state_.IsValid());
  InitializeState(base::GetPageSize(), 1, 1, 0, 0, base::GetPageSize());
  EXPECT_FALSE(state_.IsValid());
  InitializeState(base::GetPageSize(), 1, 0, 0, base::GetPageSize());
  EXPECT_FALSE(state_.IsValid());
}

TEST_F(AllocatorStateTest, GetNearestValidPageEdgeCases) {
  InitializeState(base::GetPageSize(), kMaxMetadata, kMaxSlots);
  EXPECT_TRUE(state_.IsValid());

  EXPECT_EQ(
      state_.GetPageAddr(state_.GetNearestValidPage(state_.pages_base_addr)),
      state_.first_page_addr);
  EXPECT_EQ(
      state_.GetPageAddr(state_.GetNearestValidPage(state_.pages_end_addr - 1)),
      state_.pages_end_addr - (2 * state_.page_size));
}

TEST_F(AllocatorStateTest, GetErrorTypeEdgeCases) {
  InitializeState(base::GetPageSize(), kMaxMetadata, kMaxSlots);
  EXPECT_TRUE(state_.IsValid());

  EXPECT_EQ(state_.GetErrorType(state_.pages_base_addr, true, false),
            AllocatorState::ErrorType::kBufferUnderflow);
  EXPECT_EQ(state_.GetErrorType(state_.pages_end_addr - 1, true, false),
            AllocatorState::ErrorType::kBufferOverflow);
}

// Correctly handle the edge case when a free() occurs on a page that has never
// been allocated.
TEST_F(AllocatorStateTest, GetErrorTypeFreeInvalidAddressEdgeCase) {
  InitializeState(base::GetPageSize(), kMaxMetadata, kMaxSlots);
  EXPECT_TRUE(state_.IsValid());

  state_.free_invalid_address = state_.first_page_addr;
  EXPECT_EQ(state_.GetErrorType(state_.first_page_addr, false, false),
            AllocatorState::ErrorType::kFreeInvalidAddress);
}

}  // namespace internal
}  // namespace gwp_asan
