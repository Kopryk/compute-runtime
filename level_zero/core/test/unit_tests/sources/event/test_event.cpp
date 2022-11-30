/*
 * Copyright (C) 2020-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/debug_manager_state_restore.h"
#include "shared/test/common/helpers/variable_backup.h"
#include "shared/test/common/mocks/mock_compilers.h"
#include "shared/test/common/mocks/mock_csr.h"
#include "shared/test/common/mocks/mock_memory_manager.h"
#include "shared/test/common/mocks/mock_memory_operations_handler.h"
#include "shared/test/common/mocks/mock_timestamp_packet.h"
#include "shared/test/common/test_macros/hw_test.h"

#include "level_zero/core/source/context/context_imp.h"
#include "level_zero/core/source/driver/driver_handle_imp.h"
#include "level_zero/core/source/event/event.h"
#include "level_zero/core/source/hw_helpers/l0_hw_helper.h"
#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"
#include "level_zero/core/test/unit_tests/fixtures/event_fixture.h"
#include "level_zero/core/test/unit_tests/mocks/mock_built_ins.h"
#include "level_zero/core/test/unit_tests/mocks/mock_device.h"
#include "level_zero/core/test/unit_tests/mocks/mock_event.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>

using namespace std::chrono_literals;

namespace CpuIntrinsicsTests {
extern std::atomic<uint32_t> pauseCounter;
extern volatile TagAddressType *pauseAddress;
extern TaskCountType pauseValue;
extern uint32_t pauseOffset;
extern std::function<void()> setupPauseAddress;
} // namespace CpuIntrinsicsTests

namespace L0 {
namespace ult {
using EventPoolCreate = Test<DeviceFixture>;
using EventCreate = Test<DeviceFixture>;

class MemoryManagerEventPoolFailMock : public NEO::MemoryManager {
  public:
    MemoryManagerEventPoolFailMock(NEO::ExecutionEnvironment &executionEnvironment) : NEO::MemoryManager(executionEnvironment) {}
    void *createMultiGraphicsAllocationInSystemMemoryPool(RootDeviceIndicesContainer &rootDeviceIndices, AllocationProperties &properties, NEO::MultiGraphicsAllocation &multiGraphicsAllocation) override {
        return nullptr;
    };
    GraphicsAllocation *createGraphicsAllocationFromMultipleSharedHandles(const std::vector<osHandle> &handles, AllocationProperties &properties, bool requireSpecificBitness, bool isHostIpcAllocation) override { return nullptr; }
    NEO::GraphicsAllocation *createGraphicsAllocationFromSharedHandle(osHandle handle, const AllocationProperties &properties, bool requireSpecificBitness, bool isHostIpcAllocation, bool reuseSharedAllocation) override { return nullptr; }
    void addAllocationToHostPtrManager(NEO::GraphicsAllocation *memory) override{};
    void removeAllocationFromHostPtrManager(NEO::GraphicsAllocation *memory) override{};
    NEO::GraphicsAllocation *createGraphicsAllocationFromNTHandle(void *handle, uint32_t rootDeviceIndex, AllocationType allocType) override { return nullptr; };
    AllocationStatus populateOsHandles(NEO::OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) override { return AllocationStatus::Success; };
    void cleanOsHandles(NEO::OsHandleStorage &handleStorage, uint32_t rootDeviceIndex) override{};
    void freeGraphicsMemoryImpl(NEO::GraphicsAllocation *gfxAllocation) override{};
    void freeGraphicsMemoryImpl(GraphicsAllocation *gfxAllocation, bool isImportedAllocation) override{};
    uint64_t getSystemSharedMemory(uint32_t rootDeviceIndex) override { return 0; };
    uint64_t getLocalMemorySize(uint32_t rootDeviceIndex, uint32_t deviceBitfield) override { return 0; };
    double getPercentOfGlobalMemoryAvailable(uint32_t rootDeviceIndex) override { return 0; }
    AddressRange reserveGpuAddress(const void *requiredStartAddress, size_t size, RootDeviceIndicesContainer rootDeviceIndices, uint32_t *reservedOnRootDeviceIndex) override {
        return {};
    }
    void freeGpuAddress(AddressRange addressRange, uint32_t rootDeviceIndex) override{};
    NEO::GraphicsAllocation *createGraphicsAllocation(OsHandleStorage &handleStorage, const NEO::AllocationData &allocationData) override { return nullptr; };
    NEO::GraphicsAllocation *allocateGraphicsMemoryForNonSvmHostPtr(const NEO::AllocationData &allocationData) override { return nullptr; };
    NEO::GraphicsAllocation *allocateGraphicsMemoryWithAlignment(const NEO::AllocationData &allocationData) override { return nullptr; };
    NEO::GraphicsAllocation *allocateUSMHostGraphicsMemory(const NEO::AllocationData &allocationData) override { return nullptr; };
    NEO::GraphicsAllocation *allocateGraphicsMemory64kb(const NEO::AllocationData &allocationData) override { return nullptr; };
    NEO::GraphicsAllocation *allocate32BitGraphicsMemoryImpl(const NEO::AllocationData &allocationData, bool useLocalMemory) override { return nullptr; };
    NEO::GraphicsAllocation *allocateGraphicsMemoryInDevicePool(const NEO::AllocationData &allocationData, AllocationStatus &status) override { return nullptr; };
    NEO::GraphicsAllocation *allocateGraphicsMemoryWithGpuVa(const NEO::AllocationData &allocationData) override { return nullptr; };

    NEO::GraphicsAllocation *allocateGraphicsMemoryForImageImpl(const NEO::AllocationData &allocationData, std::unique_ptr<Gmm> gmm) override { return nullptr; };
    NEO::GraphicsAllocation *allocateMemoryByKMD(const NEO::AllocationData &allocationData) override { return nullptr; };
    void *lockResourceImpl(NEO::GraphicsAllocation &graphicsAllocation) override { return nullptr; };
    void unlockResourceImpl(NEO::GraphicsAllocation &graphicsAllocation) override{};
};

struct EventPoolFailTests : public ::testing::Test {
    void SetUp() override {

        neoDevice =
            NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(NEO::defaultHwInfo.get());
        auto mockBuiltIns = new MockBuiltins();
        neoDevice->executionEnvironment->rootDeviceEnvironments[0]->builtins.reset(mockBuiltIns);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<DriverHandleImp>();
        driverHandle->initialize(std::move(devices));
        prevMemoryManager = driverHandle->getMemoryManager();
        currMemoryManager = new MemoryManagerEventPoolFailMock(*neoDevice->executionEnvironment);
        driverHandle->setMemoryManager(currMemoryManager);
        device = driverHandle->devices[0];

        context = std::make_unique<ContextImp>(driverHandle.get());
        EXPECT_NE(context, nullptr);
        context->getDevices().insert(std::make_pair(device->getRootDeviceIndex(), device->toHandle()));
        auto neoDevice = device->getNEODevice();
        context->rootDeviceIndices.push_back(neoDevice->getRootDeviceIndex());
        context->deviceBitfields.insert({neoDevice->getRootDeviceIndex(), neoDevice->getDeviceBitfield()});
    }

    void TearDown() override {
        driverHandle->setMemoryManager(prevMemoryManager);
        delete currMemoryManager;
    }
    NEO::MemoryManager *prevMemoryManager = nullptr;
    NEO::MemoryManager *currMemoryManager = nullptr;
    std::unique_ptr<DriverHandleImp> driverHandle;
    NEO::MockDevice *neoDevice = nullptr;
    L0::Device *device = nullptr;
    std::unique_ptr<ContextImp> context;
};

TEST_F(EventPoolFailTests, whenCreatingEventPoolAndAllocationFailsThenOutOfDeviceMemoryIsReturned) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};

    ze_event_pool_handle_t eventPool = {};
    ze_result_t res = context->createEventPool(&eventPoolDesc, 0, nullptr, &eventPool);
    EXPECT_EQ(res, ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY);
}

TEST_F(EventPoolCreate, GivenEventPoolThenAllocationContainsAtLeast16Bytes) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    auto allocation = &eventPool->getAllocation();
    ASSERT_NE(nullptr, allocation);

    uint32_t minAllocationSize = eventPool->getEventSize();
    EXPECT_GE(allocation->getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex())->getUnderlyingBufferSize(),
              minAllocationSize);
}

HWTEST_F(EventPoolCreate, givenTimestampEventsThenEventSizeSufficientForAllKernelTimestamps) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    auto &hwInfo = device->getHwInfo();
    auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
    auto &hwHelper = NEO::HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint32_t maxPacketCount = EventPacketsCount::maxKernelSplit * NEO::TimestampPacketSizeControl::preferredPacketCount;
    if (l0HwHelper.useDynamicEventPacketsCount(hwInfo)) {
        maxPacketCount = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);
    }
    uint32_t packetsSize = maxPacketCount *
                           static_cast<uint32_t>(NEO::TimestampPackets<typename FamilyType::TimestampPacketType>::getSinglePacketSize());
    uint32_t kernelTimestampsSize = static_cast<uint32_t>(alignUp(packetsSize, hwHelper.getTimestampPacketAllocatorAlignment()));
    EXPECT_EQ(kernelTimestampsSize, eventPool->getEventSize());
}

TEST_F(EventPoolCreate, givenEventPoolCreatedWithTimestampFlagThenHasTimestampEventsReturnsTrue) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    EventPoolImp *eventPoolImp = static_cast<EventPoolImp *>(eventPool.get());
    EXPECT_TRUE(eventPoolImp->isEventPoolTimestampFlagSet());
}

TEST_F(EventPoolCreate, givenEventPoolCreatedWithNoTimestampFlagThenHasTimestampEventsReturnsFalse) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    EventPoolImp *eventPoolImp = static_cast<EventPoolImp *>(eventPool.get());
    EXPECT_FALSE(eventPoolImp->isEventPoolTimestampFlagSet());
}

TEST_F(EventPoolCreate, givenEventPoolCreatedWithTimestampFlagAndOverrideTimestampEventsFlagThenHasTimestampEventsReturnsFalse) {
    DebugManagerStateRestore restore;
    NEO::DebugManager.flags.OverrideTimestampEvents.set(0);

    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    EventPoolImp *eventPoolImp = static_cast<EventPoolImp *>(eventPool.get());
    EXPECT_FALSE(eventPoolImp->isEventPoolTimestampFlagSet());
}

TEST_F(EventPoolCreate, givenEventPoolCreatedWithoutTimestampFlagAndOverrideTimestampEventsFlagThenHasTimestampEventsReturnsTrue) {
    DebugManagerStateRestore restore;
    NEO::DebugManager.flags.OverrideTimestampEvents.set(1);

    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = 0;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    EventPoolImp *eventPoolImp = static_cast<EventPoolImp *>(eventPool.get());
    EXPECT_TRUE(eventPoolImp->isEventPoolTimestampFlagSet());
}

TEST_F(EventPoolCreate, givenAnEventIsCreatedFromThisEventPoolThenEventContainsDeviceCommandStreamReceiver) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;

    ze_event_desc_t eventDesc = {};
    eventDesc.index = 0;
    eventDesc.signal = ZE_EVENT_SCOPE_FLAG_HOST;
    eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;

    ze_event_handle_t event = nullptr;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    eventPool->createEvent(&eventDesc, &event);

    std::unique_ptr<L0::Event> eventObject(L0::Event::fromHandle(event));
    ASSERT_NE(nullptr, eventObject->csr);
    ASSERT_EQ(device->getNEODevice()->getDefaultEngine().commandStreamReceiver, eventObject->csr);
}

TEST_F(EventPoolCreate, GivenNoDeviceThenEventPoolIsCreated) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        4};

    ze_result_t result = ZE_RESULT_SUCCESS;
    auto eventPool = EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    eventPool->destroy();
}

TEST_F(EventPoolCreate, GivenDeviceThenEventPoolIsCreated) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        4};

    auto deviceHandle = device->toHandle();
    ze_result_t result = ZE_RESULT_SUCCESS;
    auto eventPool = EventPool::create(driverHandle.get(), context, 1, &deviceHandle, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    auto &l0CoreHelper = device->getNEODevice()->getRootDeviceEnvironment().getHelper<L0CoreHelper>();

    if (l0CoreHelper.alwaysAllocateEventInLocalMem()) {
        EXPECT_EQ(NEO::AllocationType::GPU_TIMESTAMP_DEVICE_BUFFER, eventPool->getAllocation().getAllocationType());
    } else {
        EXPECT_EQ(NEO::AllocationType::BUFFER_HOST_MEMORY, eventPool->getAllocation().getAllocationType());
    }
    eventPool->destroy();
}

class MemoryManagerEventPoolIPCMock : public NEO::MockMemoryManager {
  public:
    MemoryManagerEventPoolIPCMock(NEO::ExecutionEnvironment &executionEnvironment) : NEO::MockMemoryManager(executionEnvironment) {}
    void *createMultiGraphicsAllocationInSystemMemoryPool(RootDeviceIndicesContainer &rootDeviceIndices, AllocationProperties &properties, NEO::MultiGraphicsAllocation &multiGraphicsAllocation) override {
        alloc = new NEO::MockGraphicsAllocation(&buffer, sizeof(buffer));
        alloc->isShareableHostMemory = true;
        multiGraphicsAllocation.addAllocation(alloc);
        return reinterpret_cast<void *>(alloc->getUnderlyingBuffer());
    };
    char buffer[64];
    NEO::MockGraphicsAllocation *alloc;
};

using EventPoolIPCHandleTests = Test<DeviceFixture>;

TEST_F(EventPoolIPCHandleTests, whenGettingIpcHandleForEventPoolThenHandleAndNumberOfEventsAreReturnedInHandle) {
    uint32_t numEvents = 4;
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_IPC,
        numEvents};

    auto deviceHandle = device->toHandle();
    ze_result_t result = ZE_RESULT_SUCCESS;
    auto curMemoryManager = driverHandle->getMemoryManager();
    MemoryManagerEventPoolIPCMock *mockMemoryManager = new MemoryManagerEventPoolIPCMock(*neoDevice->executionEnvironment);
    driverHandle->setMemoryManager(mockMemoryManager);
    auto eventPool = EventPool::create(driverHandle.get(), context, 1, &deviceHandle, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, eventPool);

    ze_ipc_event_pool_handle_t ipcHandle = {};
    ze_result_t res = eventPool->getIpcHandle(&ipcHandle);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    int handle = -1;
    memcpy_s(&handle, sizeof(int), ipcHandle.data, sizeof(int));
    EXPECT_NE(handle, -1);

    uint32_t expectedNumEvents = 0;
    memcpy_s(&expectedNumEvents, sizeof(expectedNumEvents), ipcHandle.data + sizeof(int), sizeof(expectedNumEvents));
    EXPECT_EQ(numEvents, expectedNumEvents);

    res = eventPool->destroy();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    delete mockMemoryManager;
    driverHandle->setMemoryManager(curMemoryManager);
}

using EventPoolCreateMultiDevice = Test<MultiDeviceFixture>;

TEST_F(EventPoolCreateMultiDevice, whenGettingIpcHandleForEventPoolWhenHostShareableMemoryIsFalseThenUnsuportedIsReturned) {
    uint32_t numEvents = 4;
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_IPC,
        numEvents};

    uint32_t deviceCount = 0;
    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount, numRootDevices);

    ze_device_handle_t *devices = new ze_device_handle_t[deviceCount];
    result = zeDeviceGet(driverHandle.get(), &deviceCount, devices);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    auto eventPool = EventPool::create(driverHandle.get(), context, deviceCount, devices, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, eventPool);

    ze_ipc_event_pool_handle_t ipcHandle = {};
    ze_result_t res = eventPool->getIpcHandle(&ipcHandle);
    EXPECT_EQ(res, ZE_RESULT_ERROR_UNSUPPORTED_FEATURE);

    res = eventPool->destroy();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    delete[] devices;
}

TEST_F(EventPoolIPCHandleTests, whenOpeningIpcHandleForEventPoolThenEventPoolIsCreatedAndEventSizesAreTheSame) {
    uint32_t numEvents = 4;
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_IPC,
        numEvents};

    auto deviceHandle = device->toHandle();
    ze_result_t result = ZE_RESULT_SUCCESS;
    auto curMemoryManager = driverHandle->getMemoryManager();
    MemoryManagerEventPoolIPCMock *mockMemoryManager = new MemoryManagerEventPoolIPCMock(*neoDevice->executionEnvironment);
    driverHandle->setMemoryManager(mockMemoryManager);
    auto eventPool = EventPool::create(driverHandle.get(), context, 1, &deviceHandle, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, eventPool);

    ze_ipc_event_pool_handle_t ipcHandle = {};
    ze_result_t res = eventPool->getIpcHandle(&ipcHandle);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    ze_event_pool_handle_t ipcEventPoolHandle = {};
    res = context->openEventPoolIpcHandle(ipcHandle, &ipcEventPoolHandle);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    auto ipcEventPool = static_cast<L0::EventPoolImp *>(L0::EventPool::fromHandle(ipcEventPoolHandle));

    EXPECT_EQ(ipcEventPool->getEventSize(), eventPool->getEventSize());
    EXPECT_EQ(numEvents, static_cast<uint32_t>(ipcEventPool->getNumEvents()));

    res = ipcEventPool->closeIpcHandle();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    res = eventPool->destroy();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    delete mockMemoryManager;
    driverHandle->setMemoryManager(curMemoryManager);
}

TEST_F(EventPoolIPCHandleTests, GivenEventPoolWithIPCEventFlagAndDeviceMemoryThenShareableEventMemoryIsTrue) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_IPC,
        1};

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    L0::EventPoolImp *eventPoolImp = reinterpret_cast<L0::EventPoolImp *>(eventPool.get());

    EXPECT_TRUE(eventPoolImp->isShareableEventMemory);
}

TEST_F(EventPoolIPCHandleTests, GivenEventPoolWithIPCEventFlagAndHostMemoryThenShareableEventMemoryIsFalse) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_IPC | ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    auto allocation = &eventPool->getAllocation();
    ASSERT_NE(nullptr, allocation);

    uint32_t minAllocationSize = eventPool->getEventSize();
    EXPECT_GE(allocation->getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex())->getUnderlyingBufferSize(),
              minAllocationSize);
    EXPECT_FALSE(allocation->getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex())->isShareableHostMemory);
}

using EventPoolOpenIPCHandleFailTests = Test<DeviceFixture>;

TEST_F(EventPoolOpenIPCHandleFailTests, givenFailureToAllocateMemoryWhenOpeningIpcHandleForEventPoolThenInvalidArgumentIsReturned) {
    uint32_t numEvents = 4;
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_IPC,
        numEvents};

    auto deviceHandle = device->toHandle();
    ze_result_t result = ZE_RESULT_SUCCESS;
    auto originalMemoryManager = driverHandle->getMemoryManager();
    MemoryManagerEventPoolIPCMock *mockMemoryManager = new MemoryManagerEventPoolIPCMock(*neoDevice->executionEnvironment);
    driverHandle->setMemoryManager(mockMemoryManager);
    auto eventPool = EventPool::create(driverHandle.get(), context, 1, &deviceHandle, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, eventPool);

    ze_ipc_event_pool_handle_t ipcHandle = {};
    ze_result_t res = eventPool->getIpcHandle(&ipcHandle);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    {
        NEO::MemoryManager *prevMemoryManager = nullptr;
        NEO::MemoryManager *currMemoryManager = nullptr;

        prevMemoryManager = driverHandle->getMemoryManager();
        currMemoryManager = new FailMemoryManager(*neoDevice->executionEnvironment);
        driverHandle->setMemoryManager(currMemoryManager);

        ze_event_pool_handle_t ipcEventPoolHandle = {};
        res = context->openEventPoolIpcHandle(ipcHandle, &ipcEventPoolHandle);
        EXPECT_EQ(res, ZE_RESULT_ERROR_INVALID_ARGUMENT);

        driverHandle->setMemoryManager(prevMemoryManager);
        delete currMemoryManager;
    }

    res = eventPool->destroy();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    delete mockMemoryManager;
    driverHandle->setMemoryManager(originalMemoryManager);
}

class MultiDeviceEventPoolOpenIPCHandleFailTestsMemoryManager : public FailMemoryManager {
  public:
    MultiDeviceEventPoolOpenIPCHandleFailTestsMemoryManager(NEO::ExecutionEnvironment &executionEnvironment) : FailMemoryManager(executionEnvironment) {}

    GraphicsAllocation *createGraphicsAllocationFromSharedHandle(osHandle handle, const AllocationProperties &properties, bool requireSpecificBitness, bool isHostIpcAllocation, bool reuseSharedAllocation) override {
        return &mockAllocation0;
    }

    GraphicsAllocation *createGraphicsAllocationFromExistingStorage(AllocationProperties &properties, void *ptr, MultiGraphicsAllocation &multiGraphicsAllocation) override {
        if (calls == 0) {
            calls++;
            return &mockAllocation1;
        }
        return nullptr;
    }

    void freeGraphicsMemory(GraphicsAllocation *gfxAllocation) override {
    }

    NEO::MockGraphicsAllocation mockAllocation0;
    NEO::MockGraphicsAllocation mockAllocation1;
    uint32_t calls = 0;
};

using MultiDeviceEventPoolOpenIPCHandleFailTests = Test<MultiDeviceFixture>;

TEST_F(MultiDeviceEventPoolOpenIPCHandleFailTests,
       givenFailureToAllocateMemoryWhenOpeningIpcHandleForEventPoolWithMultipleDevicesThenOutOfHostMemoryIsReturned) {
    uint32_t numEvents = 4;
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_IPC,
        numEvents};

    auto deviceHandle = driverHandle->devices[0]->toHandle();
    ze_result_t result = ZE_RESULT_SUCCESS;
    NEO::MockDevice *neoDevice = static_cast<NEO::MockDevice *>(driverHandle->devices[0]->getNEODevice());
    auto originalMemoryManager = driverHandle->getMemoryManager();
    MemoryManagerEventPoolIPCMock *mockMemoryManager = new MemoryManagerEventPoolIPCMock(*neoDevice->executionEnvironment);
    driverHandle->setMemoryManager(mockMemoryManager);
    auto eventPool = EventPool::create(driverHandle.get(), context, 1, &deviceHandle, &eventPoolDesc, result);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(nullptr, eventPool);

    ze_ipc_event_pool_handle_t ipcHandle = {};
    ze_result_t res = eventPool->getIpcHandle(&ipcHandle);
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);

    {
        NEO::MemoryManager *prevMemoryManager = nullptr;
        NEO::MemoryManager *currMemoryManager = nullptr;

        prevMemoryManager = driverHandle->getMemoryManager();
        currMemoryManager = new MultiDeviceEventPoolOpenIPCHandleFailTestsMemoryManager(*neoDevice->executionEnvironment);
        driverHandle->setMemoryManager(currMemoryManager);

        ze_event_pool_handle_t ipcEventPoolHandle = {};
        res = context->openEventPoolIpcHandle(ipcHandle, &ipcEventPoolHandle);
        EXPECT_EQ(res, ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY);

        driverHandle->setMemoryManager(prevMemoryManager);
        delete currMemoryManager;
    }

    res = eventPool->destroy();
    EXPECT_EQ(res, ZE_RESULT_SUCCESS);
    delete mockMemoryManager;
    driverHandle->setMemoryManager(originalMemoryManager);
}

TEST_F(EventPoolCreate, GivenNullptrDeviceAndNumberOfDevicesWhenCreatingEventPoolThenReturnError) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};

    ze_device_handle_t devices[] = {nullptr, device->toHandle()};
    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 2, devices, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_ERROR_INVALID_ARGUMENT, result);
    ASSERT_EQ(nullptr, eventPool);
}

TEST_F(EventPoolCreate, GivenNullptrDeviceWithoutNumberOfDevicesWhenCreatingEventPoolThenEventPoolCreated) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};

    ze_device_handle_t devices[] = {nullptr, device->toHandle()};
    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, devices, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
}

TEST_F(EventPoolCreate, whenHostVisibleFlagNotSetThenEventAllocationIsOnDevice) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        0u,
        4};

    ze_device_handle_t devices[] = {nullptr, device->toHandle()};

    auto memoryManager = static_cast<MockMemoryManager *>(neoDevice->getMemoryManager());
    memoryManager->recentlyPassedDeviceBitfield = systemMemoryBitfield;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, devices, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    EXPECT_EQ(NEO::AllocationType::GPU_TIMESTAMP_DEVICE_BUFFER, eventPool->getAllocation().getAllocationType());
    EXPECT_NE(systemMemoryBitfield, memoryManager->recentlyPassedDeviceBitfield);
    EXPECT_EQ(neoDevice->getDeviceBitfield(), memoryManager->recentlyPassedDeviceBitfield);
}

TEST_F(EventPoolCreate, whenAllocationMemoryFailsThenEventAllocationIsNotCreated) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        0u,
        4};

    ze_device_handle_t devices[] = {nullptr, device->toHandle()};

    auto memoryManager = static_cast<MockMemoryManager *>(neoDevice->getMemoryManager());
    memoryManager->isMockHostMemoryManager = true;
    memoryManager->forceFailureInPrimaryAllocation = true;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, devices, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY, result);
    EXPECT_EQ(nullptr, eventPool);
}

TEST_F(EventCreate, givenAnEventCreatedThenTheEventHasTheDeviceCommandStreamReceiverSet) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};
    const ze_event_desc_t eventDesc = {
        ZE_STRUCTURE_TYPE_EVENT_DESC,
        nullptr,
        0,
        ZE_EVENT_SCOPE_FLAG_DEVICE,
        ZE_EVENT_SCOPE_FLAG_DEVICE};

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    std::unique_ptr<L0::Event> event(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(nullptr, event);
    ASSERT_NE(nullptr, event->csr);
    ASSERT_EQ(device->getNEODevice()->getDefaultEngine().commandStreamReceiver, event->csr);
}

TEST_F(EventCreate, givenEventWhenSignaledAndResetFromTheHostThenCorrectDataAndOffsetAreSet) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;

    ze_event_desc_t eventDesc = {};
    eventDesc.index = 0;
    eventDesc.signal = ZE_EVENT_SCOPE_FLAG_HOST;

    ze_result_t result = ZE_RESULT_SUCCESS;
    auto eventPool = std::unique_ptr<L0::EventPool>(L0::EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    auto &hwInfo = device->getHwInfo();
    auto &l0CoreHelper = device->getNEODevice()->getRootDeviceEnvironment().getHelper<L0CoreHelper>();
    auto event = std::unique_ptr<L0::Event>(l0CoreHelper.createEvent(eventPool.get(), &eventDesc, device));
    ASSERT_NE(nullptr, event);

    if (l0CoreHelper.multiTileCapablePlatform()) {
        EXPECT_TRUE(event->isUsingContextEndOffset());
    } else {
        EXPECT_FALSE(event->isUsingContextEndOffset());
    }

    uint32_t *eventCompletionMemory = reinterpret_cast<uint32_t *>(event->getHostAddress());
    if (event->isUsingContextEndOffset()) {
        eventCompletionMemory = ptrOffset(eventCompletionMemory, event->getContextEndOffset());
    }
    uint32_t maxPacketsCount = EventPacketsCount::maxKernelSplit * NEO::TimestampPacketSizeControl::preferredPacketCount;
    if (l0CoreHelper.useDynamicEventPacketsCount(hwInfo)) {
        maxPacketsCount = l0CoreHelper.getEventBaseMaxPacketCount(hwInfo);
    }

    for (uint32_t i = 0; i < maxPacketsCount; i++) {
        EXPECT_EQ(Event::STATE_INITIAL, *eventCompletionMemory);
        eventCompletionMemory = ptrOffset(eventCompletionMemory, event->getSinglePacketSize());
    }

    result = event->queryStatus();
    EXPECT_EQ(ZE_RESULT_NOT_READY, result);

    uint64_t gpuAddr = event->getGpuAddress(device);
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));

    event->hostSignal();
    result = event->queryStatus();
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));

    event->reset();
    result = event->queryStatus();
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));
    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
}

TEST_F(EventCreate, givenAnEventCreateWithInvalidIndexUsingThisEventPoolThenErrorIsReturned) {
    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        1};
    const ze_event_desc_t eventDesc = {
        ZE_STRUCTURE_TYPE_EVENT_DESC,
        nullptr,
        2,
        ZE_EVENT_SCOPE_FLAG_DEVICE,
        ZE_EVENT_SCOPE_FLAG_DEVICE};

    ze_event_handle_t event = nullptr;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    ze_result_t value = eventPool->createEvent(&eventDesc, &event);

    ASSERT_EQ(nullptr, event);
    ASSERT_EQ(ZE_RESULT_ERROR_INVALID_ARGUMENT, value);
}

HWTEST2_F(EventCreate, givenPlatformSupportMultTileWhenDebugKeyIsSetToNotUseContextEndThenDoNotUseContextEndOffset, IsXeHpOrXeHpcCore) {
    DebugManagerStateRestore restorer;
    NEO::DebugManager.flags.UseContextEndOffsetForEventCompletion.set(0);
    auto &l0CoreHelper = getHelper<L0CoreHelper>();

    bool useContextEndOffset = l0CoreHelper.multiTileCapablePlatform();
    EXPECT_TRUE(useContextEndOffset);

    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        0,
        1};
    const ze_event_desc_t eventDesc = {
        ZE_STRUCTURE_TYPE_EVENT_DESC,
        nullptr,
        0,
        0,
        0};

    ze_event_handle_t eventHandle = nullptr;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    ze_result_t value = eventPool->createEvent(&eventDesc, &eventHandle);
    ASSERT_NE(nullptr, eventHandle);
    EXPECT_EQ(ZE_RESULT_SUCCESS, value);

    auto event = Event::fromHandle(eventHandle);
    EXPECT_FALSE(event->isEventTimestampFlagSet());
    EXPECT_FALSE(event->isUsingContextEndOffset());

    event->destroy();
}

HWTEST2_F(EventCreate, givenPlatformNotSupportsMultTileWhenDebugKeyIsSetToUseContextEndThenUseContextEndOffset, IsNotXeHpOrXeHpcCore) {
    DebugManagerStateRestore restorer;
    NEO::DebugManager.flags.UseContextEndOffsetForEventCompletion.set(1);
    auto &l0CoreHelper = getHelper<L0CoreHelper>();
    bool useContextEndOffset = l0CoreHelper.multiTileCapablePlatform();
    EXPECT_FALSE(useContextEndOffset);

    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        0,
        1};
    const ze_event_desc_t eventDesc = {
        ZE_STRUCTURE_TYPE_EVENT_DESC,
        nullptr,
        0,
        0,
        0};

    ze_event_handle_t eventHandle = nullptr;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);

    ze_result_t value = eventPool->createEvent(&eventDesc, &eventHandle);
    ASSERT_NE(nullptr, eventHandle);
    EXPECT_EQ(ZE_RESULT_SUCCESS, value);

    auto event = Event::fromHandle(eventHandle);
    EXPECT_FALSE(event->isEventTimestampFlagSet());
    EXPECT_TRUE(event->isUsingContextEndOffset());

    event->destroy();
}

using EventSynchronizeTest = Test<EventFixture<1, 0>>;
using EventUsedPacketSignalSynchronizeTest = Test<EventUsedPacketSignalFixture<1, 0>>;

TEST_F(EventSynchronizeTest, GivenGpuHangWhenHostSynchronizeIsCalledThenDeviceLostIsReturned) {
    const auto csr = std::make_unique<MockCommandStreamReceiver>(*neoDevice->getExecutionEnvironment(), 0, neoDevice->getDeviceBitfield());
    csr->isGpuHangDetectedReturnValue = true;

    event->csr = csr.get();
    event->gpuHangCheckPeriod = 0ms;

    constexpr uint64_t timeout = std::numeric_limits<std::uint64_t>::max();
    auto result = event->hostSynchronize(timeout);

    EXPECT_EQ(ZE_RESULT_ERROR_DEVICE_LOST, result);
}

TEST_F(EventSynchronizeTest, GivenNoGpuHangAndOneNanosecondTimeoutWhenHostSynchronizeIsCalledThenResultNotReadyIsReturnedDueToTimeout) {
    const auto csr = std::make_unique<MockCommandStreamReceiver>(*neoDevice->getExecutionEnvironment(), 0, neoDevice->getDeviceBitfield());
    csr->isGpuHangDetectedReturnValue = false;

    event->csr = csr.get();
    event->gpuHangCheckPeriod = 0ms;

    constexpr uint64_t timeoutNanoseconds = 1;
    auto result = event->hostSynchronize(timeoutNanoseconds);

    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
}

TEST_F(EventSynchronizeTest, GivenLongPeriodOfGpuCheckAndOneNanosecondTimeoutWhenHostSynchronizeIsCalledThenResultNotReadyIsReturnedDueToTimeout) {
    const auto csr = std::make_unique<MockCommandStreamReceiver>(*neoDevice->getExecutionEnvironment(), 0, neoDevice->getDeviceBitfield());
    event->csr = csr.get();
    event->gpuHangCheckPeriod = 50000000ms;

    constexpr uint64_t timeoutNanoseconds = 1;
    auto result = event->hostSynchronize(timeoutNanoseconds);

    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
}

TEST_F(EventSynchronizeTest, givenCallToEventHostSynchronizeWithTimeoutZeroAndStateInitialHostSynchronizeReturnsNotReady) {
    ze_result_t result = event->hostSynchronize(0);
    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
}

TEST_F(EventSynchronizeTest, givenCallToEventHostSynchronizeWithNonZeroTimeoutAndStateInitialHostSynchronizeReturnsNotReady) {
    ze_result_t result = event->hostSynchronize(10);
    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
}

TEST_F(EventSynchronizeTest, givenCallToEventHostSynchronizeWithTimeoutZeroWhenStateSignaledThenHostSynchronizeReturnsSuccess) {
    uint32_t *hostAddr = static_cast<uint32_t *>(event->getHostAddress());
    *hostAddr = Event::STATE_SIGNALED;

    event->setUsingContextEndOffset(false);
    ze_result_t result = event->hostSynchronize(0);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventSynchronizeTest, givenCallToEventHostSynchronizeWithTimeoutNonZeroWhenStateSignaledThenHostSynchronizeReturnsSuccess) {
    uint32_t *hostAddr = static_cast<uint32_t *>(event->getHostAddress());
    *hostAddr = Event::STATE_SIGNALED;

    event->setUsingContextEndOffset(false);
    ze_result_t result = event->hostSynchronize(10);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventSynchronizeTest, givenCallToEventHostSynchronizeWithTimeoutZeroWhenOffsetEventStateSignaledThenHostSynchronizeReturnsSuccess) {
    uint32_t *hostAddr = static_cast<uint32_t *>(event->getHostAddress());
    hostAddr = ptrOffset(hostAddr, event->getContextEndOffset());
    *hostAddr = Event::STATE_SIGNALED;

    event->setUsingContextEndOffset(true);
    ze_result_t result = event->hostSynchronize(0);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventUsedPacketSignalSynchronizeTest, givenInfiniteTimeoutWhenWaitingForNonTimestampEventCompletionThenReturnOnlyAfterAllEventPacketsAreCompleted) {
    constexpr uint32_t packetsInUse = 2;
    event->setPacketsInUse(packetsInUse);
    event->setUsingContextEndOffset(false);

    const size_t eventPacketSize = event->getSinglePacketSize();
    const size_t eventCompletionOffset = event->getContextStartOffset();

    VariableBackup<volatile TagAddressType *> backupPauseAddress(&CpuIntrinsicsTests::pauseAddress);
    VariableBackup<TaskCountType> backupPauseValue(&CpuIntrinsicsTests::pauseValue, Event::STATE_CLEARED);
    VariableBackup<uint32_t> backupPauseOffset(&CpuIntrinsicsTests::pauseOffset);
    VariableBackup<std::function<void()>> backupSetupPauseAddress(&CpuIntrinsicsTests::setupPauseAddress);
    CpuIntrinsicsTests::pauseCounter = 0u;
    CpuIntrinsicsTests::pauseAddress = static_cast<TagAddressType *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));

    uint32_t *hostAddr = static_cast<uint32_t *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));
    for (uint32_t i = 0; i < packetsInUse; i++) {
        *hostAddr = Event::STATE_CLEARED;
        hostAddr = ptrOffset(hostAddr, eventPacketSize);
    }

    CpuIntrinsicsTests::setupPauseAddress = [&]() {
        if (CpuIntrinsicsTests::pauseCounter > 10) {
            volatile TagAddressType *nextPacket = CpuIntrinsicsTests::pauseAddress;
            for (uint32_t i = 0; i < packetsInUse; i++) {
                *nextPacket = Event::STATE_SIGNALED;
                nextPacket = ptrOffset(nextPacket, eventPacketSize);
            }
        }
    };

    constexpr uint64_t infiniteTimeout = std::numeric_limits<std::uint64_t>::max();
    ze_result_t result = event->hostSynchronize(infiniteTimeout);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventUsedPacketSignalSynchronizeTest, givenInfiniteTimeoutWhenWaitingForOffsetedNonTimestampEventCompletionThenReturnOnlyAfterAllEventPacketsAreCompleted) {
    constexpr uint32_t packetsInUse = 2;
    event->setPacketsInUse(packetsInUse);
    event->setUsingContextEndOffset(true);

    const size_t eventPacketSize = event->getSinglePacketSize();
    const size_t eventCompletionOffset = event->getContextEndOffset();

    VariableBackup<volatile TagAddressType *> backupPauseAddress(&CpuIntrinsicsTests::pauseAddress);
    VariableBackup<TaskCountType> backupPauseValue(&CpuIntrinsicsTests::pauseValue, Event::STATE_CLEARED);
    VariableBackup<uint32_t> backupPauseOffset(&CpuIntrinsicsTests::pauseOffset);
    VariableBackup<std::function<void()>> backupSetupPauseAddress(&CpuIntrinsicsTests::setupPauseAddress);
    CpuIntrinsicsTests::pauseCounter = 0u;
    CpuIntrinsicsTests::pauseAddress = static_cast<TagAddressType *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));

    uint32_t *hostAddr = static_cast<uint32_t *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));
    for (uint32_t i = 0; i < packetsInUse; i++) {
        *hostAddr = Event::STATE_CLEARED;
        hostAddr = ptrOffset(hostAddr, eventPacketSize);
    }

    CpuIntrinsicsTests::setupPauseAddress = [&]() {
        if (CpuIntrinsicsTests::pauseCounter > 10) {
            volatile TagAddressType *nextPacket = CpuIntrinsicsTests::pauseAddress;
            for (uint32_t i = 0; i < packetsInUse; i++) {
                *nextPacket = Event::STATE_SIGNALED;
                nextPacket = ptrOffset(nextPacket, eventPacketSize);
            }
        }
    };

    constexpr uint64_t infiniteTimeout = std::numeric_limits<std::uint64_t>::max();
    ze_result_t result = event->hostSynchronize(infiniteTimeout);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventUsedPacketSignalSynchronizeTest, givenInfiniteTimeoutWhenWaitingForTimestampEventCompletionThenReturnOnlyAfterAllEventPacketsAreCompleted) {
    constexpr uint32_t packetsInUse = 2;
    event->setPacketsInUse(packetsInUse);
    event->setEventTimestampFlag(true);

    const size_t eventPacketSize = event->getSinglePacketSize();
    const size_t eventCompletionOffset = event->getContextEndOffset();

    VariableBackup<volatile TagAddressType *> backupPauseAddress(&CpuIntrinsicsTests::pauseAddress);
    VariableBackup<TaskCountType> backupPauseValue(&CpuIntrinsicsTests::pauseValue, Event::STATE_CLEARED);
    VariableBackup<uint32_t> backupPauseOffset(&CpuIntrinsicsTests::pauseOffset);
    VariableBackup<std::function<void()>> backupSetupPauseAddress(&CpuIntrinsicsTests::setupPauseAddress);
    CpuIntrinsicsTests::pauseCounter = 0u;
    CpuIntrinsicsTests::pauseAddress = static_cast<TagAddressType *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));

    uint32_t *hostAddr = static_cast<uint32_t *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));
    for (uint32_t i = 0; i < packetsInUse; i++) {
        *hostAddr = Event::STATE_CLEARED;
        hostAddr = ptrOffset(hostAddr, eventPacketSize);
    }

    CpuIntrinsicsTests::setupPauseAddress = [&]() {
        if (CpuIntrinsicsTests::pauseCounter > 10) {
            volatile TagAddressType *nextPacket = CpuIntrinsicsTests::pauseAddress;
            for (uint32_t i = 0; i < packetsInUse; i++) {
                *nextPacket = Event::STATE_SIGNALED;
                nextPacket = ptrOffset(nextPacket, eventPacketSize);
            }
        }
    };

    constexpr uint64_t infiniteTimeout = std::numeric_limits<std::uint64_t>::max();
    ze_result_t result = event->hostSynchronize(infiniteTimeout);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

using EventPoolIPCEventResetTests = Test<DeviceFixture>;

TEST_F(EventPoolIPCEventResetTests, whenOpeningIpcHandleForEventPoolCreateWithIpcFlagThenEventsInNewPoolAreNotReset) {
    std::unique_ptr<L0::EventPoolImp> eventPool = nullptr;
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    ze_result_t result = ZE_RESULT_SUCCESS;
    eventPool = std::unique_ptr<L0::EventPoolImp>(static_cast<L0::EventPoolImp *>(L0::EventPool::create(driverHandle.get(),
                                                                                                        context,
                                                                                                        0,
                                                                                                        nullptr,
                                                                                                        &eventPoolDesc,
                                                                                                        result)));
    EXPECT_NE(nullptr, eventPool);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    std::unique_ptr<L0::EventImp<uint32_t>> event0;
    ze_event_desc_t eventDesc = {};
    eventDesc.index = 0;
    eventDesc.signal = 0;
    eventDesc.wait = 0;
    event0 = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(),
                                                                                                                       &eventDesc,
                                                                                                                       device)));
    EXPECT_NE(nullptr, event0);

    uint32_t *hostAddr = static_cast<uint32_t *>(event0->getHostAddress());
    if (event0->isUsingContextEndOffset()) {
        hostAddr = ptrOffset(hostAddr, event0->getContextEndOffset());
    }
    EXPECT_EQ(*hostAddr, Event::STATE_INITIAL);

    // change state
    event0->hostSignal();
    EXPECT_EQ(*hostAddr, Event::STATE_SIGNALED);

    // create an event from the pool with the same index as event0, but this time, since isImportedIpcPool is true, no reset should happen
    eventPool->isImportedIpcPool = true;
    std::unique_ptr<L0::EventImp<uint32_t>> event1;
    event1 = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(),
                                                                                                                       &eventDesc,
                                                                                                                       device)));
    EXPECT_NE(nullptr, event1);

    uint32_t *hostAddr1 = static_cast<uint32_t *>(event1->getHostAddress());
    if (event1->isUsingContextEndOffset()) {
        hostAddr1 = ptrOffset(hostAddr1, event1->getContextEndOffset());
    }
    EXPECT_EQ(*hostAddr1, Event::STATE_SIGNALED);

    // create another event from the pool with the same index, but this time, since isImportedIpcPool is false, reset should happen
    eventPool->isImportedIpcPool = false;
    std::unique_ptr<L0::EventImp<uint32_t>> event2;
    event2 = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(),
                                                                                                                       &eventDesc,
                                                                                                                       device)));
    EXPECT_NE(nullptr, event2);

    uint32_t *hostAddr2 = static_cast<uint32_t *>(event2->getHostAddress());
    if (event2->isUsingContextEndOffset()) {
        hostAddr2 = ptrOffset(hostAddr2, event2->getContextEndOffset());
    }
    EXPECT_EQ(*hostAddr2, Event::STATE_INITIAL);
}

using EventAubCsrTest = Test<DeviceFixture>;

HWTEST_F(EventAubCsrTest, givenCallToEventHostSynchronizeWithAubModeCsrReturnsSuccess) {
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::MockDevice *neoDevice = nullptr;
    L0::Device *device = nullptr;

    neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(NEO::defaultHwInfo.get());
    auto mockBuiltIns = new MockBuiltins();
    neoDevice->executionEnvironment->rootDeviceEnvironments[0]->builtins.reset(mockBuiltIns);
    NEO::DeviceVector devices;
    devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
    driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
    driverHandle->initialize(std::move(devices));
    device = driverHandle->devices[0];
    int32_t tag;
    auto aubCsr = new MockCsrAub<FamilyType>(tag, *neoDevice->executionEnvironment, neoDevice->getRootDeviceIndex(), neoDevice->getDeviceBitfield());
    neoDevice->resetCommandStreamReceiver(aubCsr);

    std::unique_ptr<L0::EventPool> eventPool = nullptr;
    std::unique_ptr<L0::Event> event;

    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;

    ze_event_desc_t eventDesc = {};
    eventDesc.index = 0;
    eventDesc.signal = 0;
    eventDesc.wait = 0;

    ze_result_t result = ZE_RESULT_SUCCESS;
    eventPool = std::unique_ptr<L0::EventPool>(L0::EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    event = std::unique_ptr<L0::Event>(L0::Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(nullptr, event);

    result = event->hostSynchronize(10);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

struct EventCreateAllocationResidencyTest : public ::testing::Test {
    void SetUp() override {
        neoDevice = NEO::MockDevice::createWithNewExecutionEnvironment<NEO::MockDevice>(NEO::defaultHwInfo.get());
        mockMemoryOperationsHandler = new NEO::MockMemoryOperationsHandlerTests;
        neoDevice->getExecutionEnvironment()->rootDeviceEnvironments[0]->memoryOperationsInterface.reset(
            mockMemoryOperationsHandler);
        NEO::DeviceVector devices;
        devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        device = driverHandle->devices[0];
    }

    void TearDown() override {
    }

    NEO::MockMemoryOperationsHandlerTests *mockMemoryOperationsHandler;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::MockDevice *neoDevice = nullptr;
    L0::Device *device = nullptr;
};

struct TimestampEventCreateMultiKernelFixture : public EventFixture<1, 1> {
    void setUp() {
        DebugManager.flags.UsePipeControlMultiKernelEventSync.set(0);
        DebugManager.flags.SignalAllEventPackets.set(0);
        EventFixture<1, 1>::setUp();
    }

    DebugManagerStateRestore restorer;
};

using TimestampEventCreate = Test<EventFixture<1, 1>>;
using TimestampEventCreateMultiKernel = Test<TimestampEventCreateMultiKernelFixture>;
using TimestampEventUsedPacketSignalCreate = Test<EventUsedPacketSignalFixture<1, 1>>;

TEST_F(TimestampEventCreate, givenEventCreatedWithTimestampThenIsTimestampEventFlagSet) {
    EXPECT_TRUE(event->isEventTimestampFlagSet());
}

TEST_F(TimestampEventCreate, givenEventTimestampsCreatedWhenResetIsInvokeThenCorrectDataAreSet) {
    auto &hwInfo = device->getHwInfo();
    auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint32_t maxKernelCount = EventPacketsCount::maxKernelSplit;
    if (l0HwHelper.useDynamicEventPacketsCount(hwInfo)) {
        maxKernelCount = l0HwHelper.getEventMaxKernelCount(hwInfo);
    }

    EXPECT_NE(nullptr, event->kernelEventCompletionData);
    for (auto j = 0u; j < maxKernelCount; j++) {
        for (auto i = 0u; i < NEO::TimestampPacketSizeControl::preferredPacketCount; i++) {
            EXPECT_EQ(static_cast<uint64_t>(Event::State::STATE_INITIAL), event->kernelEventCompletionData[j].getContextStartValue(i));
            EXPECT_EQ(static_cast<uint64_t>(Event::State::STATE_INITIAL), event->kernelEventCompletionData[j].getGlobalStartValue(i));
            EXPECT_EQ(static_cast<uint64_t>(Event::State::STATE_INITIAL), event->kernelEventCompletionData[j].getContextEndValue(i));
            EXPECT_EQ(static_cast<uint64_t>(Event::State::STATE_INITIAL), event->kernelEventCompletionData[j].getGlobalEndValue(i));
        }
        EXPECT_EQ(1u, event->kernelEventCompletionData[j].getPacketsUsed());
    }

    EXPECT_EQ(1u, event->getKernelCount());
}

TEST_F(TimestampEventCreate, givenSingleTimestampEventThenAllocationSizeCreatedForAllTimestamps) {
    auto allocation = &eventPool->getAllocation();
    ASSERT_NE(nullptr, allocation);

    uint32_t minTimestampEventAllocation = eventPool->getEventSize();
    EXPECT_GE(allocation->getGraphicsAllocation(device->getNEODevice()->getRootDeviceIndex())->getUnderlyingBufferSize(),
              minTimestampEventAllocation);
}

TEST_F(TimestampEventCreate, givenTimestampEventThenAllocationsIsDependentIfAllocationOnLocalMemAllowed) {
    auto allocation = &eventPool->getAllocation();
    ASSERT_NE(nullptr, allocation);

    auto &l0CoreHelper = device->getNEODevice()->getRootDeviceEnvironment().getHelper<L0CoreHelper>();

    if (l0CoreHelper.alwaysAllocateEventInLocalMem()) {
        EXPECT_EQ(NEO::AllocationType::GPU_TIMESTAMP_DEVICE_BUFFER, allocation->getAllocationType());
    } else {
        EXPECT_EQ(NEO::AllocationType::TIMESTAMP_PACKET_TAG_BUFFER, allocation->getAllocationType());
    }
}

HWTEST2_F(TimestampEventCreateMultiKernel, givenEventTimestampWhenPacketCountIsSetThenCorrectOffsetIsReturned, IsAtLeastXeHpCore) {
    EXPECT_EQ(1u, event->getPacketsInUse());
    auto gpuAddr = event->getGpuAddress(device);
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));

    event->setPacketsInUse(4u);
    EXPECT_EQ(4u, event->getPacketsInUse());

    gpuAddr += (4u * event->getSinglePacketSize());

    event->increaseKernelCount();
    event->setPacketsInUse(2u);
    EXPECT_EQ(6u, event->getPacketsInUse());
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));

    gpuAddr += (2u * event->getSinglePacketSize());
    event->increaseKernelCount();
    EXPECT_EQ(gpuAddr, event->getPacketAddress(device));
    EXPECT_EQ(7u, event->getPacketsInUse());
}

TEST_F(TimestampEventCreate, givenEventWhenSignaledAndResetFromTheHostThenCorrectDataAreSet) {
    EXPECT_NE(nullptr, event->kernelEventCompletionData);
    event->hostSignal();
    ze_result_t result = event->queryStatus();
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    event->reset();
    result = event->queryStatus();
    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
    for (auto j = 0u; j < event->getKernelCount(); j++) {
        for (auto i = 0u; i < NEO::TimestampPacketSizeControl::preferredPacketCount; i++) {
            EXPECT_EQ(Event::State::STATE_INITIAL, event->kernelEventCompletionData[j].getContextStartValue(i));
            EXPECT_EQ(Event::State::STATE_INITIAL, event->kernelEventCompletionData[j].getGlobalStartValue(i));
            EXPECT_EQ(Event::State::STATE_INITIAL, event->kernelEventCompletionData[j].getContextEndValue(i));
            EXPECT_EQ(Event::State::STATE_INITIAL, event->kernelEventCompletionData[j].getGlobalEndValue(i));
        }
        EXPECT_EQ(1u, event->kernelEventCompletionData[j].getPacketsUsed());
    }
    EXPECT_EQ(1u, event->getKernelCount());
}

TEST_F(TimestampEventCreate, givenpCountZeroCallingQueryTimestampExpThenpCountSetProperly) {
    uint32_t pCount = 0;
    auto result = event->queryTimestampsExp(device, &pCount, nullptr);

    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_NE(0u, pCount);
}

TEST_F(TimestampEventUsedPacketSignalCreate, givenpCountLargerThanSupportedWhenCallingQueryTimestampExpThenpCountSetProperly) {
    uint32_t pCount = 10;
    event->setPacketsInUse(2u);
    auto result = event->queryTimestampsExp(device, &pCount, nullptr);

    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(2u, pCount);
}

TEST_F(TimestampEventCreate, givenEventWithStaticPartitionOffThenQueryTimestampExpReturnsUnsupported) {
    DebugManagerStateRestore restore;
    NEO::DebugManager.flags.EnableStaticPartitioning.set(0);

    uint32_t pCount = 0;
    auto result = event->queryTimestampsExp(device, &pCount, nullptr);

    EXPECT_EQ(ZE_RESULT_ERROR_UNSUPPORTED_FEATURE, result);
}

using TimestampDeviceEventCreate = Test<EventFixture<0, 1>>;

TEST_F(TimestampDeviceEventCreate, givenTimestampDeviceEventThenAllocationsIsOfGpuDeviceTimestampType) {
    auto allocation = &eventPool->getAllocation();
    ASSERT_NE(nullptr, allocation);

    EXPECT_EQ(NEO::AllocationType::GPU_TIMESTAMP_DEVICE_BUFFER, allocation->getAllocationType());
}

using EventQueryTimestampExpWithSubDevice = Test<MultiDeviceFixture>;

TEST_F(EventQueryTimestampExpWithSubDevice, givenEventWhenQuerytimestampExpWithSubDeviceThenReturnsCorrectValueReturned) {
    DebugManagerStateRestore restorer;
    NEO::DebugManager.flags.SignalAllEventPackets.set(0);

    std::unique_ptr<L0::EventPool> eventPool;
    std::unique_ptr<L0::EventImp<uint32_t>> event;
    uint32_t deviceCount = 1;
    ze_device_handle_t rootDeviceHandle;

    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, &rootDeviceHandle);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    deviceCount = 0;
    result = zeDeviceGetSubDevices(rootDeviceHandle, &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_TRUE(deviceCount >= 2);

    auto subDeviceHandle = std::make_unique<ze_device_handle_t[]>(deviceCount);
    result = zeDeviceGetSubDevices(rootDeviceHandle, &deviceCount, subDeviceHandle.get());
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE | ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;

    ze_event_desc_t eventDesc = {};
    eventDesc.index = 0;
    eventDesc.signal = 0;
    eventDesc.wait = 0;

    auto subDeviceId = 0u;
    auto subdevice = L0::Device::fromHandle(subDeviceHandle[subDeviceId]);
    eventPool = std::unique_ptr<L0::EventPool>(L0::EventPool::create(driverHandle.get(), context, 1, &subDeviceHandle[0], &eventPoolDesc, result));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    ASSERT_NE(nullptr, eventPool);
    event = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(), &eventDesc, subdevice)));
    ASSERT_NE(nullptr, event);

    typename MockTimestampPackets32::Packet packetData[2];
    event->setPacketsInUse(2u);

    packetData[0].contextStart = 1u;
    packetData[0].contextEnd = 2u;
    packetData[0].globalStart = 3u;
    packetData[0].globalEnd = 4u;

    packetData[1].contextStart = 5u;
    packetData[1].contextEnd = 6u;
    packetData[1].globalStart = 7u;
    packetData[1].globalEnd = 8u;

    event->hostAddress = packetData;

    ze_kernel_timestamp_result_t results[2];
    uint32_t numPackets = 2;

    for (uint32_t packetId = 0; packetId < numPackets; packetId++) {
        event->kernelEventCompletionData[0].assignDataToAllTimestamps(packetId, event->hostAddress);
        event->hostAddress = ptrOffset(event->hostAddress, NEO::TimestampPackets<uint32_t>::getSinglePacketSize());
    }
    uint32_t pCount = 0;
    result = event->queryTimestampsExp(subdevice, &pCount, nullptr);

    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(1u, pCount);
    result = event->queryTimestampsExp(subdevice, &pCount, results);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    for (uint32_t i = 0; i < pCount; i++) {
        EXPECT_EQ(packetData[subDeviceId].contextStart, results[i].context.kernelStart);
        EXPECT_EQ(packetData[subDeviceId].contextEnd, results[i].context.kernelEnd);
        EXPECT_EQ(packetData[subDeviceId].globalStart, results[i].global.kernelStart);
        EXPECT_EQ(packetData[subDeviceId].globalEnd, results[i].global.kernelEnd);
    }
}

HWCMDTEST_F(IGFX_GEN9_CORE, TimestampEventCreate, givenEventTimestampsWhenQueryKernelTimestampThenCorrectDataAreSet) {
    typename MockTimestampPackets32::Packet data = {};
    data.contextStart = 1u;
    data.contextEnd = 2u;
    data.globalStart = 3u;
    data.globalEnd = 4u;

    event->hostAddress = &data;
    ze_kernel_timestamp_result_t result = {};

    event->queryKernelTimestamp(&result);
    EXPECT_EQ(data.contextStart, result.context.kernelStart);
    EXPECT_EQ(data.contextEnd, result.context.kernelEnd);
    EXPECT_EQ(data.globalStart, result.global.kernelStart);
    EXPECT_EQ(data.globalEnd, result.global.kernelEnd);
}

TEST_F(TimestampEventUsedPacketSignalCreate, givenEventWhenQueryingTimestampExpThenCorrectDataSet) {
    typename MockTimestampPackets32::Packet packetData[2];
    event->setPacketsInUse(2u);

    packetData[0].contextStart = 1u;
    packetData[0].contextEnd = 2u;
    packetData[0].globalStart = 3u;
    packetData[0].globalEnd = 4u;

    packetData[1].contextStart = 5u;
    packetData[1].contextEnd = 6u;
    packetData[1].globalStart = 7u;
    packetData[1].globalEnd = 8u;

    event->hostAddress = packetData;

    ze_kernel_timestamp_result_t results[2];
    uint32_t pCount = 2;

    for (uint32_t packetId = 0; packetId < pCount; packetId++) {
        event->kernelEventCompletionData[0].assignDataToAllTimestamps(packetId, event->hostAddress);
        event->hostAddress = ptrOffset(event->hostAddress, NEO::TimestampPackets<uint32_t>::getSinglePacketSize());
    }

    auto result = event->queryTimestampsExp(device, &pCount, results);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    for (uint32_t i = 0; i < pCount; i++) {
        EXPECT_EQ(packetData[i].contextStart, results[i].context.kernelStart);
        EXPECT_EQ(packetData[i].contextEnd, results[i].context.kernelEnd);
        EXPECT_EQ(packetData[i].globalStart, results[i].global.kernelStart);
        EXPECT_EQ(packetData[i].globalEnd, results[i].global.kernelEnd);
    }
}

HWTEST2_F(TimestampEventCreateMultiKernel, givenTimeStampEventUsedOnTwoKernelsWhenL3FlushSetOnFirstKernelThenDoNotUseSecondPacketOfFirstKernel, IsAtLeastXeHpCore) {
    typename MockTimestampPackets32::Packet packetData[4];

    event->hostAddress = packetData;

    constexpr uint32_t kernelStartValue = 5u;
    constexpr uint32_t kernelEndValue = 10u;

    constexpr uint32_t waStartValue = 2u;
    constexpr uint32_t waEndValue = 15u;

    // 1st kernel 1st packet
    packetData[0].contextStart = kernelStartValue;
    packetData[0].contextEnd = kernelEndValue;
    packetData[0].globalStart = kernelStartValue;
    packetData[0].globalEnd = kernelEndValue;

    // 1st kernel 2nd packet for L3 Flush
    packetData[1].contextStart = waStartValue;
    packetData[1].contextEnd = waEndValue;
    packetData[1].globalStart = waStartValue;
    packetData[1].globalEnd = waEndValue;

    // 2nd kernel 1st packet
    packetData[2].contextStart = kernelStartValue;
    packetData[2].contextEnd = kernelEndValue;
    packetData[2].globalStart = kernelStartValue;
    packetData[2].globalEnd = kernelEndValue;

    event->setPacketsInUse(2u);
    event->setL3FlushForCurrentKernel();

    event->increaseKernelCount();
    EXPECT_EQ(1u, event->getPacketsUsedInLastKernel());

    ze_kernel_timestamp_result_t results;
    event->queryKernelTimestamp(&results);

    EXPECT_EQ(static_cast<uint64_t>(kernelStartValue), results.context.kernelStart);
    EXPECT_EQ(static_cast<uint64_t>(kernelStartValue), results.global.kernelStart);
    EXPECT_EQ(static_cast<uint64_t>(kernelEndValue), results.context.kernelEnd);
    EXPECT_EQ(static_cast<uint64_t>(kernelEndValue), results.global.kernelEnd);
}

HWTEST2_F(TimestampEventCreateMultiKernel, givenTimeStampEventUsedOnTwoKernelsWhenL3FlushSetOnSecondKernelThenDoNotUseSecondPacketOfSecondKernel, IsAtLeastXeHpCore) {
    typename MockTimestampPackets32::Packet packetData[4];

    event->hostAddress = packetData;

    constexpr uint32_t kernelStartValue = 5u;
    constexpr uint32_t kernelEndValue = 10u;

    constexpr uint32_t waStartValue = 2u;
    constexpr uint32_t waEndValue = 15u;

    // 1st kernel 1st packet
    packetData[0].contextStart = kernelStartValue;
    packetData[0].contextEnd = kernelEndValue;
    packetData[0].globalStart = kernelStartValue;
    packetData[0].globalEnd = kernelEndValue;

    // 2nd kernel 1st packet
    packetData[1].contextStart = kernelStartValue;
    packetData[1].contextEnd = kernelEndValue;
    packetData[1].globalStart = kernelStartValue;
    packetData[1].globalEnd = kernelEndValue;

    // 2nd kernel 2nd packet for L3 Flush
    packetData[2].contextStart = waStartValue;
    packetData[2].contextEnd = waEndValue;
    packetData[2].globalStart = waStartValue;
    packetData[2].globalEnd = waEndValue;

    EXPECT_EQ(1u, event->getPacketsUsedInLastKernel());

    event->increaseKernelCount();
    event->setPacketsInUse(2u);
    event->setL3FlushForCurrentKernel();

    ze_kernel_timestamp_result_t results;
    event->queryKernelTimestamp(&results);

    EXPECT_EQ(static_cast<uint64_t>(kernelStartValue), results.context.kernelStart);
    EXPECT_EQ(static_cast<uint64_t>(kernelStartValue), results.global.kernelStart);
    EXPECT_EQ(static_cast<uint64_t>(kernelEndValue), results.context.kernelEnd);
    EXPECT_EQ(static_cast<uint64_t>(kernelEndValue), results.global.kernelEnd);
}

HWTEST2_F(TimestampEventCreateMultiKernel, givenOverflowingTimeStampDataOnTwoKernelsWhenQueryKernelTimestampIsCalledOverflowIsObserved, IsAtLeastXeHpCore) {
    typename MockTimestampPackets32::Packet packetData[4] = {};
    event->hostAddress = packetData;

    uint32_t maxTimeStampValue = std::numeric_limits<uint32_t>::max();

    // 1st kernel 1st packet (overflowing context timestamp)
    packetData[0].contextStart = maxTimeStampValue - 1;
    packetData[0].contextEnd = maxTimeStampValue + 1;
    packetData[0].globalStart = maxTimeStampValue - 2;
    packetData[0].globalEnd = maxTimeStampValue - 1;

    // 2nd kernel 1st packet (overflowing global timestamp)
    packetData[1].contextStart = maxTimeStampValue - 2;
    packetData[1].contextEnd = maxTimeStampValue - 1;
    packetData[1].globalStart = maxTimeStampValue - 1;
    packetData[1].globalEnd = maxTimeStampValue + 1;

    // 2nd kernel 2nd packet (overflowing context timestamp)
    memcpy(&packetData[2], &packetData[0], sizeof(MockTimestampPackets32::Packet));
    packetData[2].contextStart = maxTimeStampValue;
    packetData[2].contextEnd = maxTimeStampValue + 2;

    EXPECT_EQ(1u, event->getPacketsUsedInLastKernel());

    event->increaseKernelCount();
    event->setPacketsInUse(2u);

    ze_kernel_timestamp_result_t results;
    event->queryKernelTimestamp(&results);

    auto &hwHelper = HwHelper::get(device->getHwInfo().platform.eRenderCoreFamily);
    if (hwHelper.useOnlyGlobalTimestamps() == false) {
        EXPECT_EQ(static_cast<uint64_t>(maxTimeStampValue - 2), results.context.kernelStart);
        EXPECT_EQ(static_cast<uint64_t>(maxTimeStampValue + 2), results.context.kernelEnd);
    }

    EXPECT_EQ(static_cast<uint64_t>(maxTimeStampValue - 2), results.global.kernelStart);
    EXPECT_EQ(static_cast<uint64_t>(maxTimeStampValue + 1), results.global.kernelEnd);
}

HWTEST_EXCLUDE_PRODUCT(TimestampEventCreate, givenEventTimestampsWhenQueryKernelTimestampThenCorrectDataAreSet, IGFX_GEN12LP_CORE);

TEST_F(TimestampEventCreate, givenEventWhenQueryKernelTimestampThenNotReadyReturned) {
    struct MockEventQuery : public EventImp<uint32_t> {
        MockEventQuery(L0::EventPool *eventPool, int index, L0::Device *device) : EventImp(eventPool, index, device, false) {}

        ze_result_t queryStatus() override {
            return ZE_RESULT_NOT_READY;
        }
    };

    auto mockEvent = std::make_unique<MockEventQuery>(eventPool.get(), 1u, device);

    ze_kernel_timestamp_result_t resultTimestamp = {};

    auto result = mockEvent->queryKernelTimestamp(&resultTimestamp);

    EXPECT_EQ(ZE_RESULT_NOT_READY, result);
    EXPECT_EQ(0u, resultTimestamp.context.kernelStart);
    EXPECT_EQ(0u, resultTimestamp.context.kernelEnd);
    EXPECT_EQ(0u, resultTimestamp.global.kernelStart);
    EXPECT_EQ(0u, resultTimestamp.global.kernelEnd);
}

TEST_F(EventPoolCreateMultiDevice, givenReturnSubDevicesAsApiDevicesWhenCallZeGetDevicesThenSubDevicesAreReturnedAsSeparateDevices) {
    DebugManagerStateRestore restorer;
    NEO::DebugManager.flags.ReturnSubDevicesAsApiDevices.set(1);

    uint32_t deviceCount = 0;
    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount, numRootDevices * numSubDevices);

    ze_device_handle_t *devices = new ze_device_handle_t[deviceCount];
    result = zeDeviceGet(driverHandle.get(), &deviceCount, devices);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    uint32_t i = 0u;
    for (const auto device : driverHandle->devices) {
        auto deviceImpl = static_cast<DeviceImp *>(device);
        for (const auto subdevice : deviceImpl->subDevices) {
            EXPECT_EQ(devices[i], subdevice);
            i++;
        }
    }

    static_cast<DeviceImp *>(driverHandle->devices[1])->numSubDevices = 0;
    uint32_t deviceCount2 = 0;
    result = zeDeviceGet(driverHandle.get(), &deviceCount2, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount2, (numRootDevices - 1) * numSubDevices + 1);
    ze_device_handle_t *devices2 = new ze_device_handle_t[deviceCount2];

    result = zeDeviceGet(driverHandle.get(), &deviceCount2, devices2);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    i = 0u;
    for (const auto device : driverHandle->devices) {
        auto deviceImpl = static_cast<DeviceImp *>(device);
        if (deviceImpl->numSubDevices > 0) {
            for (const auto subdevice : deviceImpl->subDevices) {
                EXPECT_EQ(devices2[i], subdevice);
                i++;
            }
        } else {
            EXPECT_EQ(devices2[i], device);
            i++;
        }
    }

    static_cast<DeviceImp *>(driverHandle->devices[1])->numSubDevices = numSubDevices;
    delete[] devices2;
    delete[] devices;
}

TEST_F(EventPoolCreateMultiDevice, whenCreatingEventPoolWithMultipleDevicesThenEventPoolCreateSucceeds) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    uint32_t deviceCount = 0;
    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount, numRootDevices);

    ze_device_handle_t *devices = new ze_device_handle_t[deviceCount];
    result = zeDeviceGet(driverHandle.get(), &deviceCount, devices);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(),
                                                               context,
                                                               deviceCount,
                                                               devices,
                                                               &eventPoolDesc,
                                                               result));
    EXPECT_NE(nullptr, eventPool);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    auto allocation = &eventPool->getAllocation();
    EXPECT_NE(nullptr, allocation);

    EXPECT_EQ(allocation->getGraphicsAllocations().size(), numRootDevices);

    delete[] devices;
}

TEST_F(EventPoolCreateMultiDevice, whenCreatingEventPoolWithMultipleDevicesThenDontDuplicateRootDeviceIndices) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    uint32_t deviceCount = 1;
    ze_device_handle_t rootDeviceHandle;

    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, &rootDeviceHandle);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    deviceCount = 0;
    result = zeDeviceGetSubDevices(rootDeviceHandle, &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_TRUE(deviceCount >= 2);

    auto subDeviceHandle = std::make_unique<ze_device_handle_t[]>(deviceCount);
    result = zeDeviceGetSubDevices(rootDeviceHandle, &deviceCount, subDeviceHandle.get());
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(),
                                                               context,
                                                               deviceCount,
                                                               subDeviceHandle.get(),
                                                               &eventPoolDesc,
                                                               result));
    EXPECT_NE(nullptr, eventPool);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    auto allocation = &eventPool->getAllocation();
    EXPECT_NE(nullptr, allocation);

    EXPECT_EQ(allocation->getGraphicsAllocations().size(), 1u);
}

TEST_F(EventPoolCreateMultiDevice, whenCreatingEventPoolWithNoDevicesThenEventPoolCreateSucceedsAndAllDeviceAreUsed) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(),
                                                               context,
                                                               0,
                                                               nullptr,
                                                               &eventPoolDesc,
                                                               result));
    EXPECT_NE(nullptr, eventPool);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    auto allocation = &eventPool->getAllocation();
    EXPECT_NE(nullptr, allocation);

    EXPECT_EQ(allocation->getGraphicsAllocations().size(), numRootDevices);
}

using EventPoolCreateSingleDevice = Test<DeviceFixture>;

TEST_F(EventPoolCreateSingleDevice, whenCreatingEventPoolWithNoDevicesThenEventPoolCreateSucceedsAndSingleDeviceIsUsed) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    ze_result_t result = ZE_RESULT_SUCCESS;
    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(),
                                                               context,
                                                               0,
                                                               nullptr,
                                                               &eventPoolDesc,
                                                               result));
    EXPECT_NE(nullptr, eventPool);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    auto allocation = &eventPool->getAllocation();
    EXPECT_NE(nullptr, allocation);

    EXPECT_EQ(allocation->getGraphicsAllocations().size(), 1u);
}

struct EventPoolCreateNegativeTest : public ::testing::Test {
    void SetUp() override {

        executionEnvironment = new NEO::ExecutionEnvironment();
        executionEnvironment->prepareRootDeviceEnvironments(numRootDevices);
        for (uint32_t i = 0; i < numRootDevices; i++) {
            executionEnvironment->rootDeviceEnvironments[i]->setHwInfo(NEO::defaultHwInfo.get());
            executionEnvironment->rootDeviceEnvironments[i]->initGmm();
        }

        std::vector<std::unique_ptr<NEO::Device>> devices;
        for (uint32_t i = 0; i < numRootDevices; i++) {
            neoDevice = NEO::MockDevice::create<NEO::MockDevice>(executionEnvironment, i);
            devices.push_back(std::unique_ptr<NEO::Device>(neoDevice));
        }

        driverHandle = std::make_unique<Mock<L0::DriverHandleImp>>();
        driverHandle->initialize(std::move(devices));
        static_cast<MockMemoryManager *>(driverHandle->getMemoryManager())->isMockEventPoolCreateMemoryManager = true;

        device = driverHandle->devices[0];

        ze_context_handle_t hContext;
        ze_context_desc_t desc = {ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0};
        ze_result_t res = driverHandle->createContext(&desc, 0u, nullptr, &hContext);
        EXPECT_EQ(ZE_RESULT_SUCCESS, res);
        context = static_cast<ContextImp *>(Context::fromHandle(hContext));
    }
    void TearDown() override {
        context->destroy();
    }

    NEO::ExecutionEnvironment *executionEnvironment = nullptr;
    std::unique_ptr<Mock<L0::DriverHandleImp>> driverHandle;
    NEO::MockDevice *neoDevice = nullptr;
    L0::Device *device = nullptr;
    const uint32_t numRootDevices = 2u;
    L0::ContextImp *context = nullptr;
};

TEST_F(EventPoolCreateNegativeTest, whenCreatingEventPoolButMemoryManagerFailsThenErrorIsReturned) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    uint32_t deviceCount = 0;
    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount, numRootDevices);

    ze_device_handle_t *devices = new ze_device_handle_t[deviceCount];
    result = zeDeviceGet(driverHandle.get(), &deviceCount, devices);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(),
                                                               context,
                                                               deviceCount,
                                                               devices,
                                                               &eventPoolDesc,
                                                               result));
    EXPECT_EQ(ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY, result);
    EXPECT_EQ(nullptr, eventPool);
    delete[] devices;
}

TEST_F(EventPoolCreateNegativeTest, whenInitializingEventPoolButMemoryManagerFailsThenErrorIsReturned) {
    ze_event_pool_desc_t eventPoolDesc = {};
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE;
    eventPoolDesc.count = 32;

    uint32_t deviceCount = 0;
    ze_result_t result = zeDeviceGet(driverHandle.get(), &deviceCount, nullptr);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    EXPECT_EQ(deviceCount, numRootDevices);

    ze_device_handle_t *devices = new ze_device_handle_t[deviceCount];
    result = zeDeviceGet(driverHandle.get(), &deviceCount, devices);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    auto eventPool = new L0::EventPoolImp(&eventPoolDesc);
    EXPECT_NE(nullptr, eventPool);

    result = eventPool->initialize(driverHandle.get(), context, numRootDevices, devices);
    EXPECT_EQ(ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY, result);

    delete eventPool;
    delete[] devices;
}

using EventTests = Test<EventFixture<1, 0>>;
using EventUsedPacketSignalTests = Test<EventUsedPacketSignalFixture<1, 0>>;

TEST_F(EventTests, WhenQueryingStatusThenSuccessIsReturned) {
    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(event, nullptr);

    auto result = event->hostSignal();
    ASSERT_EQ(ZE_RESULT_SUCCESS, result);

    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);

    event->destroy();
}

TEST_F(EventTests, GivenResetWhenQueryingStatusThenNotReadyIsReturned) {
    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(event, nullptr);

    auto result = event->hostSignal();
    ASSERT_EQ(ZE_RESULT_SUCCESS, result);

    event->setUsingContextEndOffset(true);

    result = event->reset();
    ASSERT_EQ(ZE_RESULT_SUCCESS, result);

    EXPECT_EQ(event->queryStatus(), ZE_RESULT_NOT_READY);

    event->destroy();
}

TEST_F(EventTests, WhenDestroyingAnEventThenSuccessIsReturned) {
    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(event, nullptr);

    auto result = event->destroy();
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
}

TEST_F(EventTests, givenTwoEventsCreatedThenTheyHaveDifferentAddresses) {
    ze_event_desc_t eventDesc0 = {};
    eventDesc0.index = 0;
    eventDesc.index = 0;

    ze_event_desc_t eventDesc1 = {};
    eventDesc1.index = 1;
    eventDesc.index = 1;

    auto event0 = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc0, device));
    ASSERT_NE(event0, nullptr);

    auto event1 = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc1, device));
    ASSERT_NE(event1, nullptr);

    EXPECT_NE(event0->hostAddress, event1->hostAddress);
    EXPECT_NE(event0->getGpuAddress(device), event1->getGpuAddress(device));

    event0->destroy();
    event1->destroy();
}

TEST_F(EventTests, givenRegularEventUseMultiplePacketsWhenHostSignalThenExpectAllPacketsAreSignaled) {
    eventDesc.index = 0;
    eventDesc.signal = 0;
    eventDesc.wait = 0;
    auto event = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(),
                                                                                                                           &eventDesc,
                                                                                                                           device)));
    ASSERT_NE(event, nullptr);

    uint32_t *hostAddr = static_cast<uint32_t *>(event->getHostAddress());
    if (event->isUsingContextEndOffset()) {
        hostAddr = ptrOffset(hostAddr, event->getContextEndOffset());
    }
    EXPECT_EQ(*hostAddr, Event::STATE_INITIAL);
    EXPECT_EQ(1u, event->getPacketsInUse());

    constexpr uint32_t packetsUsed = 4u;
    event->setPacketsInUse(packetsUsed);
    event->setEventTimestampFlag(false);
    event->hostSignal();
    for (uint32_t i = 0; i < packetsUsed; i++) {
        EXPECT_EQ(Event::STATE_SIGNALED, *hostAddr);
        hostAddr = ptrOffset(hostAddr, event->getSinglePacketSize());
    }
}

TEST_F(EventUsedPacketSignalTests, givenEventUseMultiplePacketsWhenHostSignalThenExpectAllPacketsAreSignaled) {
    eventDesc.index = 0;
    eventDesc.signal = 0;
    eventDesc.wait = 0;
    auto event = std::unique_ptr<L0::EventImp<uint32_t>>(static_cast<L0::EventImp<uint32_t> *>(L0::Event::create<uint32_t>(eventPool.get(),
                                                                                                                           &eventDesc,
                                                                                                                           device)));
    ASSERT_NE(event, nullptr);

    size_t eventOffset = 0;
    if (event->isUsingContextEndOffset()) {
        eventOffset = event->getContextEndOffset();
    }

    uint32_t *hostAddr = static_cast<uint32_t *>(ptrOffset(event->getHostAddress(), eventOffset));

    EXPECT_EQ(Event::STATE_INITIAL, *hostAddr);
    EXPECT_EQ(1u, event->getPacketsInUse());

    constexpr uint32_t packetsUsed = 4u;
    event->setPacketsInUse(packetsUsed);
    event->setEventTimestampFlag(false);

    event->hostSignal();
    for (uint32_t i = 0; i < packetsUsed; i++) {
        EXPECT_EQ(Event::STATE_SIGNALED, *hostAddr);
        hostAddr = ptrOffset(hostAddr, event->getSinglePacketSize());
    }
}

HWTEST2_F(EventUsedPacketSignalTests, WhenSettingL3FlushOnEventThenSetOnParticularKernel, IsAtLeastXeHpCore) {
    DebugManagerStateRestore restorer;
    DebugManager.flags.UsePipeControlMultiKernelEventSync.set(0);

    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    ASSERT_NE(event, nullptr);
    EXPECT_FALSE(event->getL3FlushForCurrenKernel());

    event->setL3FlushForCurrentKernel();
    EXPECT_TRUE(event->getL3FlushForCurrenKernel());

    event->increaseKernelCount();
    EXPECT_EQ(2u, event->getKernelCount());

    EXPECT_FALSE(event->getL3FlushForCurrenKernel());

    event->setL3FlushForCurrentKernel();
    EXPECT_TRUE(event->getL3FlushForCurrenKernel());

    event->reset();
    EXPECT_FALSE(event->getL3FlushForCurrenKernel());

    constexpr size_t expectedL3FlushOnKernelCount = 0;
    EXPECT_EQ(expectedL3FlushOnKernelCount, event->l3FlushAppliedOnKernel.count());

    event->destroy();
}

struct EventSizeFixture : public DeviceFixture {
    void setUp() {
        DeviceFixture::setUp();
        hDevice = device->toHandle();
    }

    void tearDown() {
        eventObj0.reset(nullptr);
        eventObj1.reset(nullptr);
        eventPool.reset(nullptr);
        DeviceFixture::tearDown();
    }

    void createEvents() {
        ze_event_handle_t hEvent0 = 0;
        ze_event_handle_t hEvent1 = 0;
        ze_event_desc_t eventDesc0 = {};
        ze_event_desc_t eventDesc1 = {};

        eventDesc0.index = 0;
        eventDesc1.index = 1;

        auto result = eventPool->createEvent(&eventDesc0, &hEvent0);
        ASSERT_EQ(ZE_RESULT_SUCCESS, result);

        result = eventPool->createEvent(&eventDesc1, &hEvent1);
        ASSERT_EQ(ZE_RESULT_SUCCESS, result);

        eventObj0.reset(L0::Event::fromHandle(hEvent0));
        eventObj1.reset(L0::Event::fromHandle(hEvent1));
    }

    ze_event_pool_desc_t eventPoolDesc = {
        ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        nullptr,
        ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        4};

    DebugManagerStateRestore restore;
    ze_device_handle_t hDevice = 0;
    std::unique_ptr<EventPoolImp> eventPool;
    std::unique_ptr<L0::Event> eventObj0;
    std::unique_ptr<L0::Event> eventObj1;
};

using EventSizeTests = Test<EventSizeFixture>;

HWTEST_F(EventSizeTests, whenCreatingEventPoolThenUseCorrectSizeAndAlignment) {
    ze_result_t result = ZE_RESULT_SUCCESS;
    eventPool.reset(static_cast<EventPoolImp *>(EventPool::create(device->getDriverHandle(), context, 1, &hDevice, &eventPoolDesc, result)));
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);
    auto &hwHelper = device->getHwHelper();
    auto &hwInfo = device->getHwInfo();

    auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint32_t packetCount = EventPacketsCount::eventPackets;
    if (l0HwHelper.useDynamicEventPacketsCount(hwInfo)) {
        packetCount = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);
    }

    auto expectedAlignment = static_cast<uint32_t>(hwHelper.getTimestampPacketAllocatorAlignment());
    auto singlePacketSize = TimestampPackets<typename FamilyType::TimestampPacketType>::getSinglePacketSize();
    auto expectedSize = static_cast<uint32_t>(alignUp(packetCount * singlePacketSize, expectedAlignment));

    EXPECT_EQ(expectedSize, eventPool->getEventSize());

    createEvents();

    constexpr size_t timestampPacketTypeSize = sizeof(typename FamilyType::TimestampPacketType);

    EXPECT_EQ(timestampPacketTypeSize / 4, eventObj0->getTimestampSizeInDw());
    EXPECT_EQ(timestampPacketTypeSize / 4, eventObj1->getTimestampSizeInDw());

    EXPECT_EQ(0u, eventObj0->getContextStartOffset());
    EXPECT_EQ(timestampPacketTypeSize, eventObj0->getGlobalStartOffset());
    EXPECT_EQ(timestampPacketTypeSize * 2, eventObj0->getContextEndOffset());
    EXPECT_EQ(timestampPacketTypeSize * 3, eventObj0->getGlobalEndOffset());

    EXPECT_EQ(timestampPacketTypeSize * 4, eventObj0->getSinglePacketSize());

    auto hostPtrDiff = ptrDiff(eventObj1->getHostAddress(), eventObj0->getHostAddress());
    EXPECT_EQ(expectedSize, hostPtrDiff);
}

HWTEST_F(EventSizeTests, givenDebugFlagwhenCreatingEventPoolThenUseCorrectSizeAndAlignment) {
    auto &hwHelper = device->getHwHelper();
    auto &hwInfo = device->getHwInfo();
    auto expectedAlignment = static_cast<uint32_t>(hwHelper.getTimestampPacketAllocatorAlignment());

    auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

    uint32_t packetCount = EventPacketsCount::eventPackets;
    if (l0HwHelper.useDynamicEventPacketsCount(hwInfo)) {
        packetCount = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);
    }

    {
        DebugManager.flags.OverrideTimestampPacketSize.set(4);

        ze_result_t result = ZE_RESULT_SUCCESS;
        eventPool.reset(static_cast<EventPoolImp *>(EventPool::create(device->getDriverHandle(), context, 1, &hDevice, &eventPoolDesc, result)));
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        auto singlePacketSize = TimestampPackets<uint32_t>::getSinglePacketSize();

        auto expectedSize = static_cast<uint32_t>(alignUp(packetCount * singlePacketSize, expectedAlignment));

        EXPECT_EQ(expectedSize, eventPool->getEventSize());

        createEvents();

        EXPECT_EQ(1u, eventObj0->getTimestampSizeInDw());
        EXPECT_EQ(1u, eventObj1->getTimestampSizeInDw());

        auto hostPtrDiff = ptrDiff(eventObj1->getHostAddress(), eventObj0->getHostAddress());
        EXPECT_EQ(expectedSize, hostPtrDiff);
    }

    {
        DebugManager.flags.OverrideTimestampPacketSize.set(8);

        ze_result_t result = ZE_RESULT_SUCCESS;
        eventPool.reset(static_cast<EventPoolImp *>(EventPool::create(device->getDriverHandle(), context, 1, &hDevice, &eventPoolDesc, result)));
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        auto singlePacketSize = TimestampPackets<uint64_t>::getSinglePacketSize();

        auto expectedSize = static_cast<uint32_t>(alignUp(packetCount * singlePacketSize, expectedAlignment));

        EXPECT_EQ(expectedSize, eventPool->getEventSize());

        createEvents();

        EXPECT_EQ(2u, eventObj0->getTimestampSizeInDw());
        EXPECT_EQ(2u, eventObj1->getTimestampSizeInDw());

        auto hostPtrDiff = ptrDiff(eventObj1->getHostAddress(), eventObj0->getHostAddress());
        EXPECT_EQ(expectedSize, hostPtrDiff);
    }

    {
        DebugManager.flags.OverrideTimestampPacketSize.set(12);
        ze_result_t result = ZE_RESULT_SUCCESS;
        EXPECT_ANY_THROW(EventPool::create(device->getDriverHandle(), context, 1, &hDevice, &eventPoolDesc, result));
        EXPECT_ANY_THROW(createEvents());
    }
}

HWTEST_F(EventTests,
         WhenHostEventSyncThenExpectDownloadEventAllocationWithEachQuery) {
    std::map<GraphicsAllocation *, uint32_t> downloadAllocationTrack;

    constexpr uint32_t iterations = 5;

    VariableBackup<volatile TagAddressType *> backupPauseAddress(&CpuIntrinsicsTests::pauseAddress);
    VariableBackup<TaskCountType> backupPauseValue(&CpuIntrinsicsTests::pauseValue, Event::STATE_CLEARED);
    VariableBackup<uint32_t> backupPauseOffset(&CpuIntrinsicsTests::pauseOffset);
    VariableBackup<std::function<void()>> backupSetupPauseAddress(&CpuIntrinsicsTests::setupPauseAddress);
    neoDevice->getUltCommandStreamReceiver<FamilyType>().commandStreamReceiverType = CommandStreamReceiverType::CSR_TBX;
    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));

    ASSERT_NE(event, nullptr);
    ASSERT_NE(nullptr, event->csr);
    ASSERT_EQ(device->getNEODevice()->getDefaultEngine().commandStreamReceiver, event->csr);
    event->setUsingContextEndOffset(false);

    size_t eventCompletionOffset = event->getContextStartOffset();
    if (event->isUsingContextEndOffset()) {
        eventCompletionOffset = event->getContextEndOffset();
    }
    TagAddressType *eventAddress = static_cast<TagAddressType *>(ptrOffset(event->getHostAddress(), eventCompletionOffset));
    *eventAddress = Event::STATE_INITIAL;

    CpuIntrinsicsTests::pauseCounter = 0u;
    CpuIntrinsicsTests::pauseAddress = eventAddress;

    CpuIntrinsicsTests::setupPauseAddress = [&]() {
        if (CpuIntrinsicsTests::pauseCounter >= iterations) {
            volatile TagAddressType *packet = CpuIntrinsicsTests::pauseAddress;
            *packet = Event::STATE_SIGNALED;
        }
    };

    auto ultCsr = static_cast<UltCommandStreamReceiver<FamilyType> *>(event->csr);
    VariableBackup<std::function<void(GraphicsAllocation & gfxAllocation)>> backupCsrDownloadImpl(&ultCsr->downloadAllocationImpl);
    ultCsr->downloadAllocationImpl = [&downloadAllocationTrack](GraphicsAllocation &gfxAllocation) {
        downloadAllocationTrack[&gfxAllocation]++;
    };

    constexpr uint64_t timeout = std::numeric_limits<std::uint64_t>::max();
    auto result = event->hostSynchronize(timeout);
    EXPECT_EQ(ZE_RESULT_SUCCESS, result);

    auto eventAllocation = &event->getAllocation(device);
    uint32_t downloadedAllocations = downloadAllocationTrack[eventAllocation];
    EXPECT_EQ(iterations + 1, downloadedAllocations);

    event->destroy();
}

HWTEST_F(EventTests, WhenDownloadAllocationNotRequiredThenDontDownloadAllocation) {
    neoDevice->getUltCommandStreamReceiver<FamilyType>().commandStreamReceiverType = CommandStreamReceiverType::CSR_HW;
    auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
    event->queryStatus();
    EXPECT_FALSE(static_cast<UltCommandStreamReceiver<FamilyType> *>(event->csr)->downloadAllocationsCalled);
    event->destroy();
}

HWTEST_F(EventTests, WhenDownloadAllocationRequiredThenDownloadAllocation) {
    CommandStreamReceiverType csrTypes[] = {CommandStreamReceiverType::CSR_TBX, CommandStreamReceiverType::CSR_TBX_WITH_AUB};
    for (auto csrType : csrTypes) {
        neoDevice->getUltCommandStreamReceiver<FamilyType>().commandStreamReceiverType = csrType;
        auto event = whiteboxCast(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
        event->queryStatus();
        EXPECT_TRUE(static_cast<UltCommandStreamReceiver<FamilyType> *>(event->csr)->downloadAllocationsCalled);
        event->destroy();
    }
}

struct MockEventCompletion : public EventImp<uint32_t> {
    using EventImp<uint32_t>::gpuStartTimestamp;
    using EventImp<uint32_t>::gpuEndTimestamp;

    MockEventCompletion(L0::EventPool *eventPool, int index, L0::Device *device) : EventImp(eventPool, index, device, false) {
        auto neoDevice = device->getNEODevice();
        auto &hwInfo = neoDevice->getHardwareInfo();
        auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

        signalAllEventPackets = L0HwHelper::useSignalAllEventPackets(hwInfo);

        uint32_t maxKernels = EventPacketsCount::maxKernelSplit;
        if (l0HwHelper.useDynamicEventPacketsCount(hwInfo)) {
            maxKernels = l0HwHelper.getEventMaxKernelCount(hwInfo);
        }
        kernelEventCompletionData = std::make_unique<KernelEventCompletionData<uint32_t>[]>(maxKernels);

        auto alloc = eventPool->getAllocation().getGraphicsAllocation(neoDevice->getRootDeviceIndex());

        uint64_t baseHostAddr = reinterpret_cast<uint64_t>(alloc->getUnderlyingBuffer());
        totalEventSize = eventPool->getEventSize();
        eventPoolOffset = index * totalEventSize;
        hostAddress = reinterpret_cast<void *>(baseHostAddr + eventPoolOffset);
        csr = neoDevice->getDefaultEngine().commandStreamReceiver;

        maxKernelCount = maxKernels;
        maxPacketCount = eventPool->getEventMaxPackets();
    }

    void assignKernelEventCompletionData(void *address) override {
        assignKernelEventCompletionDataCounter++;
    }

    ze_result_t hostEventSetValue(uint32_t eventValue) override {
        if (shouldHostEventSetValueFail) {
            return ZE_RESULT_ERROR_UNKNOWN;
        }
        return EventImp<uint32_t>::hostEventSetValue(eventValue);
    }

    bool shouldHostEventSetValueFail = false;
    uint32_t assignKernelEventCompletionDataCounter = 0u;
};

TEST_F(EventTests, WhenQueryingStatusAfterHostSignalThenDontAccessMemoryAndReturnSuccess) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    auto result = event->hostSignal();
    EXPECT_EQ(result, ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->assignKernelEventCompletionDataCounter, 0u);
}

TEST_F(EventTests, WhenQueryingStatusAfterHostSignalThatFailedThenAccessMemoryAndReturnSuccess) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    event->shouldHostEventSetValueFail = true;
    event->hostSignal();
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->assignKernelEventCompletionDataCounter, 1u);
}

TEST_F(EventTests, WhenQueryingStatusThenAccessMemoryOnce) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->assignKernelEventCompletionDataCounter, 1u);
}

TEST_F(EventTests, WhenQueryingStatusAfterResetThenAccessMemory) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->reset(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->queryStatus(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->assignKernelEventCompletionDataCounter, 2u);
}

TEST_F(EventTests, WhenResetEventThenZeroCpuTimestamps) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    event->gpuStartTimestamp = 10u;
    event->gpuEndTimestamp = 20u;
    EXPECT_EQ(event->reset(), ZE_RESULT_SUCCESS);
    EXPECT_EQ(event->gpuStartTimestamp, 0u);
    EXPECT_EQ(event->gpuEndTimestamp, 0u);
}

TEST_F(EventTests, WhenEventResetIsCalledThenKernelCountAndPacketsUsedHaveNotBeenReset) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    event->gpuStartTimestamp = 10u;
    event->gpuEndTimestamp = 20u;
    event->zeroKernelCount();

    EXPECT_EQ(ZE_RESULT_SUCCESS, event->reset());

    EXPECT_EQ(0u, event->getKernelCount());
    EXPECT_EQ(0u, event->getPacketsInUse());
    EXPECT_EQ(event->gpuStartTimestamp, 0u);
    EXPECT_EQ(event->gpuEndTimestamp, 0u);
}

TEST_F(EventTests, GivenResetAllPacketsWhenResetPacketsThenOneKernelCountAndOnePacketUsed) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    event->gpuStartTimestamp = 10u;
    event->gpuEndTimestamp = 20u;
    event->zeroKernelCount();
    auto resetAllPackets = true;
    event->resetPackets(resetAllPackets);

    EXPECT_EQ(1u, event->getKernelCount());
    EXPECT_EQ(1u, event->getPacketsInUse());
    EXPECT_EQ(event->gpuStartTimestamp, 0u);
    EXPECT_EQ(event->gpuEndTimestamp, 0u);
}

TEST_F(EventTests, GivenResetAllPacketsFalseWhenResetPacketsThenKernelCountAndPacketsUsedHaveNotBeenReset) {
    auto event = std::make_unique<MockEventCompletion>(eventPool.get(), 1u, device);
    event->gpuStartTimestamp = 10u;
    event->gpuEndTimestamp = 20u;
    event->zeroKernelCount();
    auto resetAllPackets = false;
    event->resetPackets(resetAllPackets);

    EXPECT_EQ(0u, event->getKernelCount());
    EXPECT_EQ(0u, event->getPacketsInUse());
    EXPECT_EQ(event->gpuStartTimestamp, 0u);
    EXPECT_EQ(event->gpuEndTimestamp, 0u);
}

TEST_F(EventSynchronizeTest, whenEventSetCsrThenCorrectCsrSet) {
    auto defaultCsr = neoDevice->getDefaultEngine().commandStreamReceiver;
    const auto mockCsr = std::make_unique<MockCommandStreamReceiver>(*neoDevice->getExecutionEnvironment(), 0, neoDevice->getDeviceBitfield());

    EXPECT_EQ(event->csr, defaultCsr);
    event->setCsr(mockCsr.get());
    EXPECT_EQ(event->csr, mockCsr.get());

    event->reset();
    EXPECT_EQ(event->csr, defaultCsr);
}

template <int32_t multiTile, int32_t signalRemainingPackets>
struct EventDynamicPacketUseFixture : public DeviceFixture {
    void setUp() {
        NEO::DebugManager.flags.UseDynamicEventPacketsCount.set(1);
        if constexpr (multiTile == 1) {
            DebugManager.flags.CreateMultipleSubDevices.set(2);
            DebugManager.flags.EnableImplicitScaling.set(1);
        }
        NEO::DebugManager.flags.SignalAllEventPackets.set(signalRemainingPackets);
        if constexpr (signalRemainingPackets == 1) {
            NEO::DebugManager.flags.UsePipeControlMultiKernelEventSync.set(0);
            NEO::DebugManager.flags.CompactL3FlushEventPacket.set(0);
        }
        DeviceFixture::setUp();
    }

    void testAllDevices() {
        auto &hwInfo = device->getHwInfo();
        auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
        auto &hwHelper = NEO::HwHelper::get(hwInfo.platform.eRenderCoreFamily);

        ze_event_pool_desc_t eventPoolDesc = {
            ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
            nullptr,
            0,
            1};

        ze_result_t result = ZE_RESULT_SUCCESS;
        std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        ASSERT_NE(nullptr, eventPool);

        auto eventPoolMaxPackets = eventPool->getEventMaxPackets();
        auto expectedPoolMaxPackets = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);
        if constexpr (multiTile == 1) {
            expectedPoolMaxPackets *= 2;
        }
        EXPECT_EQ(expectedPoolMaxPackets, eventPoolMaxPackets);

        auto eventSize = eventPool->getEventSize();
        auto expectedEventSize = static_cast<uint32_t>(alignUp(expectedPoolMaxPackets * hwHelper.getSingleTimestampPacketSize(), hwHelper.getTimestampPacketAllocatorAlignment()));
        EXPECT_EQ(expectedEventSize, eventSize);

        ze_event_desc_t eventDesc = {
            ZE_STRUCTURE_TYPE_EVENT_DESC,
            nullptr,
            0,
            ZE_EVENT_SCOPE_FLAG_DEVICE,
            ZE_EVENT_SCOPE_FLAG_DEVICE};

        std::unique_ptr<L0::Event> event(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));

        EXPECT_EQ(expectedPoolMaxPackets, event->getMaxPacketsCount());

        uint32_t maxKernels = l0HwHelper.getEventMaxKernelCount(hwInfo);
        EXPECT_EQ(maxKernels, event->getMaxKernelCount());
    }

    void testSingleDevice() {
        ze_result_t result = ZE_RESULT_SUCCESS;

        auto &hwInfo = device->getHwInfo();
        auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);
        auto &hwHelper = NEO::HwHelper::get(hwInfo.platform.eRenderCoreFamily);

        ze_event_pool_desc_t eventPoolDesc = {
            ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
            nullptr,
            0,
            1};

        std::vector<ze_device_handle_t> deviceHandles;

        L0::Device *eventDevice = device;
        if constexpr (multiTile == 1) {
            uint32_t count = 2;
            ze_device_handle_t subDevices[2];
            result = device->getSubDevices(&count, subDevices);
            ASSERT_EQ(ZE_RESULT_SUCCESS, result);
            deviceHandles.push_back(subDevices[0]);
            eventDevice = Device::fromHandle(subDevices[0]);
        } else {
            deviceHandles.push_back(device->toHandle());
        }

        std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 1, deviceHandles.data(), &eventPoolDesc, result));
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        ASSERT_NE(nullptr, eventPool);

        auto eventPoolMaxPackets = eventPool->getEventMaxPackets();
        auto expectedPoolMaxPackets = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);

        EXPECT_EQ(expectedPoolMaxPackets, eventPoolMaxPackets);

        auto eventSize = eventPool->getEventSize();
        auto expectedEventSize = static_cast<uint32_t>(alignUp(expectedPoolMaxPackets * hwHelper.getSingleTimestampPacketSize(), hwHelper.getTimestampPacketAllocatorAlignment()));
        EXPECT_EQ(expectedEventSize, eventSize);

        ze_event_desc_t eventDesc = {
            ZE_STRUCTURE_TYPE_EVENT_DESC,
            nullptr,
            0,
            ZE_EVENT_SCOPE_FLAG_DEVICE,
            ZE_EVENT_SCOPE_FLAG_DEVICE};

        std::unique_ptr<L0::Event> event(Event::create<uint32_t>(eventPool.get(), &eventDesc, eventDevice));

        EXPECT_EQ(expectedPoolMaxPackets, event->getMaxPacketsCount());

        uint32_t maxKernels = l0HwHelper.getEventMaxKernelCount(hwInfo);
        EXPECT_EQ(maxKernels, event->getMaxKernelCount());
    }

    void testSignalAllPackets(uint32_t eventValueAfterSignal, uint32_t queryRetAfterPartialReset, ze_event_pool_flags_t flags, bool signalAll) {
        ze_result_t result = ZE_RESULT_SUCCESS;

        auto &hwInfo = device->getHwInfo();
        auto &l0HwHelper = L0HwHelper::get(hwInfo.platform.eRenderCoreFamily);

        ze_event_pool_desc_t eventPoolDesc = {
            ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
            nullptr,
            flags,
            1};

        std::unique_ptr<L0::EventPool> eventPool(EventPool::create(driverHandle.get(), context, 0, nullptr, &eventPoolDesc, result));
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        ASSERT_NE(nullptr, eventPool);

        auto eventPoolMaxPackets = eventPool->getEventMaxPackets();
        auto expectedPoolMaxPackets = l0HwHelper.getEventBaseMaxPacketCount(hwInfo);

        EXPECT_EQ(expectedPoolMaxPackets, eventPoolMaxPackets);

        ze_event_desc_t eventDesc = {
            ZE_STRUCTURE_TYPE_EVENT_DESC,
            nullptr,
            0,
            ZE_EVENT_SCOPE_FLAG_DEVICE,
            ZE_EVENT_SCOPE_FLAG_DEVICE};

        std::unique_ptr<L0::Event> event(Event::create<uint32_t>(eventPool.get(), &eventDesc, device));
        EXPECT_EQ(expectedPoolMaxPackets, event->getMaxPacketsCount());

        // natively one packet is in use
        uint32_t usedPackets = event->getPacketsInUse();

        uint32_t maxPackets = event->getMaxPacketsCount();
        size_t packetSize = event->getSinglePacketSize();
        void *eventHostAddress = event->getHostAddress();
        uint32_t remainingPackets = maxPackets - usedPackets;
        void *remainingPacketsAddress = ptrOffset(eventHostAddress, (usedPackets * packetSize));
        if (event->isUsingContextEndOffset()) {
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, event->getContextEndOffset());
        }

        for (uint32_t i = 0; i < remainingPackets; i++) {
            uint32_t *completionField = reinterpret_cast<uint32_t *>(remainingPacketsAddress);
            EXPECT_EQ(Event::STATE_INITIAL, *completionField);
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, packetSize);
        }

        result = event->queryStatus();
        EXPECT_EQ(ZE_RESULT_NOT_READY, result);
        event->resetCompletionStatus(false);

        event->hostSignal();

        remainingPacketsAddress = ptrOffset(eventHostAddress, (usedPackets * packetSize));
        if (event->isUsingContextEndOffset()) {
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, event->getContextEndOffset());
        }

        for (uint32_t i = 0; i < remainingPackets; i++) {
            uint32_t *completionField = reinterpret_cast<uint32_t *>(remainingPacketsAddress);
            EXPECT_EQ(eventValueAfterSignal, *completionField);
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, packetSize);
        }

        result = event->queryStatus();
        EXPECT_EQ(ZE_RESULT_SUCCESS, result);
        event->resetCompletionStatus(false);

        remainingPacketsAddress = ptrOffset(eventHostAddress, (usedPackets * packetSize));
        if (event->isUsingContextEndOffset()) {
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, event->getContextEndOffset());
        }

        {
            uint32_t *completionField = reinterpret_cast<uint32_t *>(remainingPacketsAddress);
            *completionField = Event::STATE_CLEARED;

            result = event->queryStatus();
            EXPECT_EQ(queryRetAfterPartialReset, result);
            event->resetCompletionStatus(false);

            *completionField = Event::STATE_SIGNALED;

            result = event->queryStatus();
            EXPECT_EQ(ZE_RESULT_SUCCESS, result);
            event->resetCompletionStatus(false);
        }

        event->reset();

        uint32_t eventValueAfterReset = Event::STATE_CLEARED;
        for (uint32_t i = 0; i < remainingPackets; i++) {
            uint32_t *completionField = reinterpret_cast<uint32_t *>(remainingPacketsAddress);

            // manually signaled free packet will not be cleared when signal all is not active
            if (!signalAll && i == 0) {
                eventValueAfterReset = Event::STATE_SIGNALED;
            } else {
                eventValueAfterReset = Event::STATE_CLEARED;
            }
            EXPECT_EQ(eventValueAfterReset, *completionField);
            remainingPacketsAddress = ptrOffset(remainingPacketsAddress, packetSize);
        }

        result = event->queryStatus();
        EXPECT_EQ(ZE_RESULT_NOT_READY, result);
    }

    DebugManagerStateRestore restorer;
};

using EventDynamicPacketUseTest = Test<EventDynamicPacketUseFixture<0, 0>>;
HWTEST2_F(EventDynamicPacketUseTest, givenDynamicPacketEstimationWhenGettingMaxPacketFromAllDevicesThenMaxPossibleSelected, IsAtLeastSkl) {
    testAllDevices();
}

HWTEST2_F(EventDynamicPacketUseTest, givenDynamicPacketEstimationWhenGettingMaxPacketFromSingleDeviceThenMaxFromThisDeviceSelected, IsAtLeastSkl) {
    testSingleDevice();
}

using EventMultiTileDynamicPacketUseTest = Test<EventDynamicPacketUseFixture<1, 0>>;
HWTEST2_F(EventMultiTileDynamicPacketUseTest, givenDynamicPacketEstimationWhenGettingMaxPacketFromAllDevicesThenMaxPossibleSelected, IsAtLeastXeHpCore) {
    testAllDevices();
}

HWTEST2_F(EventMultiTileDynamicPacketUseTest, givenDynamicPacketEstimationWhenGettingMaxPacketFromSingleOneTileDeviceThenMaxFromThisDeviceSelected, IsAtLeastXeHpCore) {
    testSingleDevice();
}

using EventSignalAllPacketsTest = Test<EventDynamicPacketUseFixture<0, 1>>;
HWTEST2_F(EventSignalAllPacketsTest, givenDynamicPacketEstimationWhenImmediateEventSignalMaxPacketThenAllPacketCompletionSignaled, IsAtLeastXeHpCore) {
    testSignalAllPackets(Event::STATE_SIGNALED, ZE_RESULT_NOT_READY, 0, true);
}

HWTEST2_F(EventSignalAllPacketsTest, givenDynamicPacketEstimationWhenTimestampEventSignalMaxPacketThenAllPacketCompletionSignaled, IsAtLeastXeHpCore) {
    testSignalAllPackets(Event::STATE_SIGNALED, ZE_RESULT_NOT_READY, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, true);
}

using EventSignalUsedPacketsTest = Test<EventDynamicPacketUseFixture<0, 0>>;
HWTEST2_F(EventSignalUsedPacketsTest, givenDynamicPacketEstimationWhenImmediateSignalUsedPacketThenUsedPacketCompletionSignaled, IsAtLeastXeHpCore) {
    testSignalAllPackets(Event::STATE_CLEARED, ZE_RESULT_SUCCESS, 0, false);
}

HWTEST2_F(EventSignalUsedPacketsTest, givenDynamicPacketEstimationWhenTimestampSignalUsedPacketThenUsedPacketCompletionSignaled, IsAtLeastXeHpCore) {
    testSignalAllPackets(Event::STATE_CLEARED, ZE_RESULT_SUCCESS, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, false);
}

} // namespace ult
} // namespace L0
