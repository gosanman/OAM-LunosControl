#pragma once

// Board selection. The device header sets the OKNXHW_* macros for controller and front; the
// OpenKNX hardware definition then expands them into the actual pins, so those are not
// duplicated here. Only board-specific fan pins live in the device headers.
#if defined(DEVICE_REG1_FAN_ADDON_X2)
    #include "Reg1_FanAddon_X2.h"
#else
    // Default: original MrSpieb FanControl board
    #include "MrSpiebFanControlHardware.h"
#endif

#include <HardwareConfig.h>
