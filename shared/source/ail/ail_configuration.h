/*
 * Copyright (C) 2021-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "igfxfmid.h"

#include <cstdint>
#include <string>

/*
 * AIL (Application Intelligence Layer) is a set of per-application controls that influence driver behavior.
 * The primary goal is to improve user experience and/or performance.
 *
 * AIL provides application detection mechanism based on running processes in the system.
 * Mechanism works on Windows and Linux, is flexible and easily extendable to new applications.
 *
 * E.g. AIL can detect running Blender application and enable fp64 emulation on hardware
 * that does not support native fp64.
 *
 * Disclaimer: we should never use this for benchmarking or conformance purposes - this would be cheating.
 *
 */

namespace NEO {

struct RuntimeCapabilityTable;

enum class AILEnumeration : uint32_t {
    DISABLE_BLITTER,
    DISABLE_COMPRESSION,
    ENABLE_FP64,
    DISABLE_HOST_PTR_TRACKING,
    AIL_MAX_OPTIONS_COUNT
};

class AILConfiguration {
  public:
    AILConfiguration() = default;

    MOCKABLE_VIRTUAL bool initProcessExecutableName();

    static AILConfiguration *get(PRODUCT_FAMILY productFamily);

    virtual void apply(RuntimeCapabilityTable &runtimeCapabilityTable);

    virtual void modifyKernelIfRequired(std::string &kernel) = 0;

    virtual bool isFallbackToPatchtokensRequired(const std::string &kernelSources) = 0;

  protected:
    virtual void applyExt(RuntimeCapabilityTable &runtimeCapabilityTable) = 0;
    std::string processName;

    bool sourcesContain(const std::string &sources, std::string_view contentToFind) const;
    MOCKABLE_VIRTUAL bool isKernelHashCorrect(const std::string &kernelSources, uint64_t expectedHash) const;
};

extern AILConfiguration *ailConfigurationTable[IGFX_MAX_PRODUCT];

template <PRODUCT_FAMILY Product>
class AILConfigurationHw : public AILConfiguration {
  public:
    static AILConfigurationHw<Product> &get() {
        static AILConfigurationHw<Product> ailConfiguration;
        return ailConfiguration;
    }

    void applyExt(RuntimeCapabilityTable &runtimeCapabilityTable) override;

    void modifyKernelIfRequired(std::string &kernel) override;
    bool isFallbackToPatchtokensRequired(const std::string &kernelSources) override;
};

template <PRODUCT_FAMILY product>
struct EnableAIL {
    EnableAIL() {
        ailConfigurationTable[product] = &AILConfigurationHw<product>::get();
    }
};

} // namespace NEO
