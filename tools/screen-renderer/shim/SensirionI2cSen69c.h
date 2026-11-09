// SPDX-License-Identifier: GPL-3.0-or-later

/// @file
/// @brief Stand-in for Sensirion's SEN69C driver: the device status register
/// the Device page decodes, and an empty class for the SensorService member.
#pragma once

#include <cstdint>

/// The SEN69C's device status register, bit by bit (layout copied from the
/// driver).
typedef union {
  struct {
    uint32_t reserved1 : 4;        ///< Bits 0 to 3: reserved.
    uint32_t fanError : 1;         ///< Bit 4: the fan doesn't turn.
    uint32_t reserved2 : 1;        ///< Bit 5: reserved.
    uint32_t rhtError : 1;         ///< Bit 6: humidity and temperature sensor error.
    uint32_t gasError : 1;         ///< Bit 7: VOC and NOx sensor error.
    uint32_t reserved3 : 1;        ///< Bit 8: reserved.
    uint32_t co22Error : 1;        ///< Bit 9: CO2 sensor error.
    uint32_t hchoError : 1;        ///< Bit 10: formaldehyde sensor error.
    uint32_t pmError : 1;          ///< Bit 11: particle sensor error.
    uint32_t co21Error : 1;        ///< Bit 12: CO2 sensor error, too.
    uint32_t reserved4 : 8;        ///< Bits 13 to 20: reserved.
    uint32_t fanSpeedWarning : 1;  ///< Bit 21: the fan runs too fast or too slow.
    uint32_t reserved5 : 10;       ///< Bits 22 to 31: reserved.
  };
  uint32_t value;  ///< The whole register; 0 means no errors.
} SEN69CDeviceStatus;

/// Stand-in for Sensirion's driver class: empty, as only SensorService's
/// declaration needs it.
class SensirionI2cSen69c {};
