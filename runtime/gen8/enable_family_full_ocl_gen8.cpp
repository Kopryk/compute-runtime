/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/command_queue/command_queue_hw.h"
#include "runtime/command_stream/aub_command_stream_receiver_hw.h"
#include "runtime/command_stream/command_stream_receiver_hw.h"
#include "runtime/command_stream/tbx_command_stream_receiver_hw.h"
#include "runtime/device_queue/device_queue_hw.h"
#include "runtime/event/perf_counter.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/mem_obj/image.h"
#include "runtime/sampler/sampler.h"

#include <type_traits>

namespace NEO {

typedef BDWFamily Family;

struct EnableOCLGen8 {
    EnableOCLGen8() {
        populateFactoryTable<AUBCommandStreamReceiverHw<Family>>();
        populateFactoryTable<TbxCommandStreamReceiverHw<Family>>();
        populateFactoryTable<CommandQueueHw<Family>>();
        populateFactoryTable<DeviceQueueHw<Family>>();
        populateFactoryTable<CommandStreamReceiverHw<Family>>();
        populateFactoryTable<BufferHw<Family>>();
        populateFactoryTable<ImageHw<Family>>();
        populateFactoryTable<SamplerHw<Family>>();
    }
};

static EnableOCLGen8 enable;
} // namespace NEO
