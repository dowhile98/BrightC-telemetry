/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file modbus_device_table.h
 * @brief Declarative compile-time Modbus slave topology for BrightC V3.
 *
 * The topology is fixed at build time: the slaves, their buses, their unit
 * addresses and their device types come from `app/Kconfig.modbus` and are
 * instantiated in one immutable table. Nothing is loaded from EEPROM and
 * nothing is edited at runtime, so a given binary always polls the same
 * devices.
 *
 * The electrical configuration of each bus is *not* here: the devicetree is
 * the single source of truth for baudrate, parity and stop bits
 * (app/boards/brightc-controllerv3.overlay).
 *
 * Pure ISO C99/C11: zero Zephyr headers, zero dynamic memory allocations.
 */

#ifndef APP_COMMON_MODBUS_DEVICE_TABLE_H_
#define APP_COMMON_MODBUS_DEVICE_TABLE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/modbus_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Standard Field Device Slave Addresses & Identifiers                       */
/* ========================================================================= */

/*
 * In a production build every symbol below comes from app/Kconfig.modbus.
 * The fallbacks exist only for host unit tests that do not source that
 * fragment (any test that includes app_defaults.h/hardware_pin_map.h pulls
 * this header in transitively). They MUST stay identical to the Kconfig
 * defaults; the modbus unit-test build sources app/Kconfig.modbus instead of
 * relying on them.
 */
#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339
#define CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339 2U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1
#define CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1 1U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2
#define CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2 5U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI
#define CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI 3U
#endif

#ifndef CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY
#define CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY 4U
#endif

#define MODBUS_ADDR_JSY_MK339         CONFIG_BRIGHTC_MODBUS_ADDR_JSY_MK339
#define MODBUS_ADDR_CWT_TM16PT_1      CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_1
#define MODBUS_ADDR_CWT_TM16PT_2      CONFIG_BRIGHTC_MODBUS_ADDR_CWT_TM16PT_2
#define MODBUS_ADDR_MBSL32DI          CONFIG_BRIGHTC_MODBUS_ADDR_MBSL32DI
#define MODBUS_ADDR_WAVESHARE_RELAY   CONFIG_BRIGHTC_MODBUS_ADDR_WAVESHARE_RELAY

/*
 * Enable flags: a device exists if and only if Kconfig enabled it. There is no
 * implicit "enabled because Kconfig is missing" branch, so the table compiled
 * into the binary and the one compiled into the unit tests cannot disagree.
 */
#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_JSY_MK339)
#define MODBUS_ENABLE_JSY_MK339 true
#else
#define MODBUS_ENABLE_JSY_MK339 false
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_1)
#define MODBUS_ENABLE_CWT_TM16PT_1 true
#else
#define MODBUS_ENABLE_CWT_TM16PT_1 false
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_CWT_TM16PT_2)
#define MODBUS_ENABLE_CWT_TM16PT_2 true
#else
#define MODBUS_ENABLE_CWT_TM16PT_2 false
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_MBSL32DI)
#define MODBUS_ENABLE_MBSL32DI true
#else
#define MODBUS_ENABLE_MBSL32DI false
#endif

#if defined(CONFIG_BRIGHTC_MODBUS_ENABLE_WAVESHARE_RELAY)
#define MODBUS_ENABLE_WAVESHARE_RELAY true
#else
#define MODBUS_ENABLE_WAVESHARE_RELAY false
#endif

/* Backward compatibility aliases */
#define DEFAULT_MODBUS_DEV_ID_WAVESHARE_RELAY MODBUS_ADDR_WAVESHARE_RELAY
#define DEFAULT_MODBUS_WAVESHARE_TIMEOUT_MS   300U

/**
 * @brief Total number of standard field slaves in the BrightC topology.
 *
 * Every standard device keeps a row even when Kconfig disables it; the row is
 * then compiled with `is_enabled = false` and the registry never schedules it.
 */
#define MODBUS_STANDARD_SLAVES_COUNT 5U

/**
 * @brief Immutable standard Modbus slave descriptors table.
 *
 * Defined once in modbus_device_table.c so every translation unit shares the
 * same copy.
 */
extern const modbus_slave_descriptor_t k_brightc_modbus_slave_table[
    MODBUS_STANDARD_SLAVES_COUNT];

/* ========================================================================= */
/* Compile-time topology validation                                          */
/* ========================================================================= */

/*
 * A duplicated unit address on the same bus is a wiring/configuration error
 * that must fail the build, not the boot: `modbus_registry_register()` would
 * return -EADDRINUSE at run time, after the board is already deployed.
 */
_Static_assert(MODBUS_ADDR_JSY_MK339 >= MODBUS_SLAVE_ADDR_MIN &&
                   MODBUS_ADDR_JSY_MK339 <= MODBUS_SLAVE_ADDR_MAX,
               "JSY-MK-339 address out of the Modbus unit-identifier range");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_1 >= MODBUS_SLAVE_ADDR_MIN &&
                   MODBUS_ADDR_CWT_TM16PT_1 <= MODBUS_SLAVE_ADDR_MAX,
               "CWT-TM-16PT #1 address out of the Modbus unit-identifier range");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_2 >= MODBUS_SLAVE_ADDR_MIN &&
                   MODBUS_ADDR_CWT_TM16PT_2 <= MODBUS_SLAVE_ADDR_MAX,
               "CWT-TM-16PT #2 address out of the Modbus unit-identifier range");
_Static_assert(MODBUS_ADDR_MBSL32DI >= MODBUS_SLAVE_ADDR_MIN &&
                   MODBUS_ADDR_MBSL32DI <= MODBUS_SLAVE_ADDR_MAX,
               "MBSL32DI address out of the Modbus unit-identifier range");
_Static_assert(MODBUS_ADDR_WAVESHARE_RELAY >= MODBUS_SLAVE_ADDR_MIN &&
                   MODBUS_ADDR_WAVESHARE_RELAY <= MODBUS_SLAVE_ADDR_MAX,
               "Waveshare relay address out of the Modbus unit-identifier range");

/* RS485-1: JSY-MK-339, CWT-TM-16PT #1, CWT-TM-16PT #2 */
_Static_assert(MODBUS_ADDR_JSY_MK339 != MODBUS_ADDR_CWT_TM16PT_1,
               "RS485-1: JSY-MK-339 and CWT-TM-16PT #1 share the same unit address");
_Static_assert(MODBUS_ADDR_JSY_MK339 != MODBUS_ADDR_CWT_TM16PT_2,
               "RS485-1: JSY-MK-339 and CWT-TM-16PT #2 share the same unit address");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_1 != MODBUS_ADDR_CWT_TM16PT_2,
               "RS485-1: the two CWT-TM-16PT concentrators share the same unit address");

/* RS485-2: MBSL32DI, Waveshare relay */
_Static_assert(MODBUS_ADDR_MBSL32DI != MODBUS_ADDR_WAVESHARE_RELAY,
               "RS485-2: MBSL32DI and the Waveshare relay share the same unit address");

/*
 * The address is also the registry key (device_id == slave_addr), so it must be
 * unique across the whole plant and not only within one bus. A collision here
 * would make two devices alias each other in the registry, and the failure would
 * only surface at run time on a deployed unit.
 */
_Static_assert(MODBUS_ADDR_JSY_MK339 != MODBUS_ADDR_MBSL32DI,
               "JSY-MK-339 and MBSL32DI share the same unit address");
_Static_assert(MODBUS_ADDR_JSY_MK339 != MODBUS_ADDR_WAVESHARE_RELAY,
               "JSY-MK-339 and the Waveshare relay share the same unit address");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_1 != MODBUS_ADDR_MBSL32DI,
               "CWT-TM-16PT #1 and MBSL32DI share the same unit address");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_1 != MODBUS_ADDR_WAVESHARE_RELAY,
               "CWT-TM-16PT #1 and the Waveshare relay share the same unit address");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_2 != MODBUS_ADDR_MBSL32DI,
               "CWT-TM-16PT #2 and MBSL32DI share the same unit address");
_Static_assert(MODBUS_ADDR_CWT_TM16PT_2 != MODBUS_ADDR_WAVESHARE_RELAY,
               "CWT-TM-16PT #2 and the Waveshare relay share the same unit address");

_Static_assert(MODBUS_STANDARD_SLAVES_COUNT <= MODBUS_MAX_TOTAL_SLAVES,
               "The compiled topology exceeds MODBUS_MAX_TOTAL_SLAVES");

/* ========================================================================= */
/* Helper Functions                                                          */
/* ========================================================================= */

/**
 * @brief Retrieve the compiled Modbus slave descriptors table.
 *
 * @param[out] out_count Optional pointer receiving the number of descriptors.
 * @return Pointer to the immutable descriptor array.
 */
static inline const modbus_slave_descriptor_t *modbus_device_table_get_slaves(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = MODBUS_STANDARD_SLAVES_COUNT;
    }
    return k_brightc_modbus_slave_table;
}

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_MODBUS_DEVICE_TABLE_H_ */
