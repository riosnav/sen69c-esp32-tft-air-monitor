// SPDX-License-Identifier: GPL-3.0-or-later
//
// Stand-in for Sensirion's SEN69C driver: the device status register the
// Device page decodes (layout copied from the driver), and an empty class
// for the SensorService member.
#pragma once

#include <cstdint>

typedef union {
  struct {
    uint32_t reserved1 : 4;
    uint32_t fanError : 1;
    uint32_t reserved2 : 1;
    uint32_t rhtError : 1;
    uint32_t gasError : 1;
    uint32_t reserved3 : 1;
    uint32_t co22Error : 1;
    uint32_t hchoError : 1;
    uint32_t pmError : 1;
    uint32_t co21Error : 1;
    uint32_t reserved4 : 8;
    uint32_t fanSpeedWarning : 1;
    uint32_t reserved5 : 10;
  };
  uint32_t value;
} SEN69CDeviceStatus;

class SensirionI2cSen69c {};
