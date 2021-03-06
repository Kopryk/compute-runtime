/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/unit_tests/direct_submission/dispatchers/dispatcher_fixture.h"

#include "core/helpers/aligned_memory.h"
#include "core/memory_manager/memory_constants.h"

void DispatcherFixture::SetUp() {
    DeviceFixture::SetUp();

    bufferAllocation = alignedMalloc(MemoryConstants::pageSize, MemoryConstants::pageSize);
    cmdBuffer.replaceBuffer(bufferAllocation, MemoryConstants::pageSize);
}

void DispatcherFixture::TearDown() {
    alignedFree(bufferAllocation);

    DeviceFixture::TearDown();
}
