/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/os_interface/linux/drm_memory_operations_handler.h"
#include "test.h"
#include "unit_tests/mocks/mock_graphics_allocation.h"

#include <memory>

using namespace NEO;

struct DrmMemoryOperationsHandlerTest : public ::testing::Test {
    void SetUp() override {
        drmMemoryOperationsHandler = std::make_unique<DrmMemoryOperationsHandler>();

        allocationPtr = &graphicsAllocation;
    }

    MockGraphicsAllocation graphicsAllocation;
    GraphicsAllocation *allocationPtr;
    std::unique_ptr<DrmMemoryOperationsHandler> drmMemoryOperationsHandler;
};

TEST_F(DrmMemoryOperationsHandlerTest, whenMakingResidentAllocaionExpectMakeResidentFail) {
    EXPECT_EQ(drmMemoryOperationsHandler->makeResident(ArrayRef<GraphicsAllocation *>(&allocationPtr, 1)), MemoryOperationsStatus::UNSUPPORTED);
    EXPECT_EQ(drmMemoryOperationsHandler->isResident(graphicsAllocation), MemoryOperationsStatus::UNSUPPORTED);
}

TEST_F(DrmMemoryOperationsHandlerTest, whenEvictingResidentAllocationExpectEvictFalse) {
    EXPECT_EQ(drmMemoryOperationsHandler->makeResident(ArrayRef<GraphicsAllocation *>(&allocationPtr, 1)), MemoryOperationsStatus::UNSUPPORTED);
    EXPECT_EQ(drmMemoryOperationsHandler->evict(graphicsAllocation), MemoryOperationsStatus::UNSUPPORTED);
    EXPECT_EQ(drmMemoryOperationsHandler->isResident(graphicsAllocation), MemoryOperationsStatus::UNSUPPORTED);
}
