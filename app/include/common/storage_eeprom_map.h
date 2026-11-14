/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file storage_eeprom_map.h
 * @brief Centralized memory layout and compile-time layout verification for AT24C256C EEPROM (32 KB).
 *
 * Pure C11 definition adhering to Clean Architecture principles without OS coupling.
 */

#ifndef APP_COMMON_STORAGE_EEPROM_MAP_H_
#define APP_COMMON_STORAGE_EEPROM_MAP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EEPROM_AT24C256C_CAPACITY_BYTES   32768U
#define EEPROM_AT24C256C_PAGE_SIZE_BYTES  64U

/* Base offsets and sizes for configuration domains (ADR-6 & REVF Baseline) */
#define EEPROM_ADDR_DEVICE                0x0000U
#define EEPROM_SIZE_DEVICE                256U
#define EEPROM_SLOT_DEVICE                128U

#define EEPROM_ADDR_NET_ETH               0x0100U
#define EEPROM_SIZE_NET_ETH               128U
#define EEPROM_SLOT_NET_ETH               64U

#define EEPROM_ADDR_NET_WIFI              0x0180U
#define EEPROM_SIZE_NET_WIFI              640U
#define EEPROM_SLOT_NET_WIFI              320U

#define EEPROM_ADDR_HVAC                  0x0400U
#define EEPROM_SIZE_HVAC                  1152U
#define EEPROM_SLOT_HVAC                  576U

#define EEPROM_ADDR_CALIBRATION           0x0880U
#define EEPROM_SIZE_CALIBRATION           384U
#define EEPROM_SLOT_CALIBRATION           192U

#define EEPROM_ADDR_THERMAL_SITE          0x0A00U
#define EEPROM_SIZE_THERMAL_SITE          1536U
#define EEPROM_SLOT_THERMAL_SITE          768U

#define EEPROM_ADDR_THERMAL_BASELINE      0x1000U
#define EEPROM_SIZE_THERMAL_BASELINE      1024U
#define EEPROM_SLOT_THERMAL_BASELINE      512U

#define EEPROM_ADDR_RESERVED_MODBUS       0x1400U
#define EEPROM_SIZE_RESERVED_MODBUS       1024U

#define EEPROM_ADDR_EVENT_LOG             0x1800U
#define EEPROM_SIZE_EVENT_LOG             (EEPROM_AT24C256C_CAPACITY_BYTES - EEPROM_ADDR_EVENT_LOG)

/* Non-overlapping domain layout and physical capacity verification (ADR-6) */
_Static_assert(EEPROM_ADDR_DEVICE + EEPROM_SIZE_DEVICE <= EEPROM_ADDR_NET_ETH,
               "DEVICE domain overlaps NET_ETH");
_Static_assert(EEPROM_ADDR_NET_ETH + EEPROM_SIZE_NET_ETH <= EEPROM_ADDR_NET_WIFI,
               "NET_ETH domain overlaps NET_WIFI");
_Static_assert(EEPROM_ADDR_NET_WIFI + EEPROM_SIZE_NET_WIFI <= EEPROM_ADDR_HVAC,
               "NET_WIFI domain overlaps HVAC");
_Static_assert(EEPROM_ADDR_HVAC + EEPROM_SIZE_HVAC <= EEPROM_ADDR_CALIBRATION,
               "HVAC domain overlaps CALIBRATION");
_Static_assert(EEPROM_ADDR_CALIBRATION + EEPROM_SIZE_CALIBRATION <= EEPROM_ADDR_THERMAL_SITE,
               "CALIBRATION domain overlaps THERMAL_SITE");
_Static_assert(EEPROM_ADDR_THERMAL_SITE + EEPROM_SIZE_THERMAL_SITE <= EEPROM_ADDR_THERMAL_BASELINE,
               "THERMAL_SITE domain overlaps THERMAL_BASELINE");
_Static_assert(EEPROM_ADDR_THERMAL_BASELINE + EEPROM_SIZE_THERMAL_BASELINE <= EEPROM_ADDR_RESERVED_MODBUS,
               "THERMAL_BASELINE domain overlaps RESERVED_MODBUS");
_Static_assert(EEPROM_ADDR_RESERVED_MODBUS + EEPROM_SIZE_RESERVED_MODBUS <= EEPROM_ADDR_EVENT_LOG,
               "RESERVED_MODBUS domain overlaps EVENT_LOG");
_Static_assert(EEPROM_ADDR_EVENT_LOG + EEPROM_SIZE_EVENT_LOG <= EEPROM_AT24C256C_CAPACITY_BYTES,
               "EVENT_LOG exceeds physical EEPROM capacity");

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_STORAGE_EEPROM_MAP_H_ */
