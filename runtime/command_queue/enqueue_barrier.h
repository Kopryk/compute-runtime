/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/device/device.h"
#include "core/memory_manager/surface.h"
#include "runtime/command_queue/command_queue_hw.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/event/event.h"

#include <new>

namespace NEO {

template <typename GfxFamily>
cl_int CommandQueueHw<GfxFamily>::enqueueBarrierWithWaitList(
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {
    NullSurface s;
    Surface *surfaces[] = {&s};
    enqueueHandler<CL_COMMAND_BARRIER>(surfaces,
                                       false,
                                       MultiDispatchInfo(),
                                       numEventsInWaitList,
                                       eventWaitList,
                                       event);
    return CL_SUCCESS;
}
} // namespace NEO
