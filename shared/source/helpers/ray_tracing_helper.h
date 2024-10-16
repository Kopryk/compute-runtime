/*
 * Copyright (C) 2018-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/device/device.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/basic_math.h"
#include "shared/source/helpers/gfx_core_helper.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "ocl_igc_shared/raytracing/ocl_raytracing_structures.h"

#include <cstdint>
namespace NEO {
class RayTracingHelper : public NonCopyableOrMovableClass {
  public:
    static constexpr uint32_t hitInfoSize = 64;
    static constexpr uint32_t bvhStackSize = 96;
    static constexpr uint32_t memoryBackedFifoSizePerDss = 8 * MemoryConstants::kiloByte;
    static constexpr uint32_t maxBvhLevels = 8;

    static size_t getDispatchGlobalSize() {
        return static_cast<size_t>(alignUp(sizeof(RTDispatchGlobals), MemoryConstants::cacheLineSize));
    }

    static size_t getRTStackSizePerTile(const Device &device, uint32_t tiles, uint32_t maxBvhLevel, uint32_t extraBytesLocal, uint32_t extraBytesGlobal) {
        return static_cast<size_t>(alignUp(getStackSizePerRay(maxBvhLevel, extraBytesLocal) * (getNumRtStacks(device.getHardwareInfo())) + extraBytesGlobal, MemoryConstants::cacheLineSize));
    }

    static size_t getTotalMemoryBackedFifoSize(const Device &device) {
        return static_cast<size_t>(NEO::GfxCoreHelper::getHighestEnabledDualSubSlice(device.getHardwareInfo())) * memoryBackedFifoSizePerDss;
    }

    static size_t getMemoryBackedFifoSizeToPatch() {
        return static_cast<size_t>(Math::log2(memoryBackedFifoSizePerDss / MemoryConstants::kiloByte) - 1);
    }

    static uint32_t getNumRtStacks(const HardwareInfo &hwInfo) {
        return NEO::GfxCoreHelper::getHighestEnabledDualSubSlice(hwInfo) * getNumRtStacksPerDss(hwInfo);
    }

    static uint32_t getNumRtStacksPerDss(const HardwareInfo &hwInfo) {
        return hwInfo.capabilityTable.syncNumRTStacksPerDSS;
    }

    static uint32_t getStackSizePerRay(uint32_t maxBvhLevel, uint32_t extraBytesLocal) {
        return hitInfoSize + bvhStackSize * maxBvhLevel + extraBytesLocal;
    }
};
} // namespace NEO
