/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "level_zero/sysman/source/api/temperature/sysman_os_temperature.h"
#include "level_zero/sysman/source/windows/zes_os_sysman_imp.h"

namespace L0 {
namespace Sysman {
class KmdSysManager;
class WddmTemperatureImp : public OsTemperature, NEO::NonCopyableOrMovableClass {
  public:
    ze_result_t getProperties(zes_temp_properties_t *pProperties) override;
    ze_result_t getSensorTemperature(double *pTemperature) override;
    bool isTempModuleSupported() override;
    uint32_t getNumTempDomainsSupported();
    void setSensorType(zes_temp_sensors_t sensorType);
    WddmTemperatureImp(OsSysman *pOsSysman);
    WddmTemperatureImp() = default;
    ~WddmTemperatureImp() override = default;

  protected:
    KmdSysManager *pKmdSysManager = nullptr;
    zes_temp_sensors_t type = ZES_TEMP_SENSORS_GLOBAL;
};

} // namespace Sysman
} // namespace L0