/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file alarm_registry.c
 * @brief Implementation of the Pure Domain Alarm Descriptor Catalog.
 */

#include "domain/alarm/alarm_registry.h"

static const alarm_descriptor_t g_default_catalog[] = {
    /* AC1 Contacts */
    { ALARM_ID_AC1_HIGH_PRESSURE,  "AC1_HP",   "AC Unit 1 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC1_LOW_PRESSURE,   "AC1_LP",   "AC Unit 1 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC1_AIRFLOW_FAULT,  "AC1_FLOW", "AC Unit 1 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC1_VOLTAGE_FAULT,  "AC1_VOLT", "AC Unit 1 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC2 Contacts */
    { ALARM_ID_AC2_HIGH_PRESSURE,  "AC2_HP",   "AC Unit 2 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC2_LOW_PRESSURE,   "AC2_LP",   "AC Unit 2 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC2_AIRFLOW_FAULT,  "AC2_FLOW", "AC Unit 2 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC2_VOLTAGE_FAULT,  "AC2_VOLT", "AC Unit 2 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC3 Contacts */
    { ALARM_ID_AC3_HIGH_PRESSURE,  "AC3_HP",   "AC Unit 3 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC3_LOW_PRESSURE,   "AC3_LP",   "AC Unit 3 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC3_AIRFLOW_FAULT,  "AC3_FLOW", "AC Unit 3 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC3_VOLTAGE_FAULT,  "AC3_VOLT", "AC Unit 3 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC4 Contacts */
    { ALARM_ID_AC4_HIGH_PRESSURE,  "AC4_HP",   "AC Unit 4 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC4_LOW_PRESSURE,   "AC4_LP",   "AC Unit 4 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC4_AIRFLOW_FAULT,  "AC4_FLOW", "AC Unit 4 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC4_VOLTAGE_FAULT,  "AC4_VOLT", "AC Unit 4 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC5 Contacts */
    { ALARM_ID_AC5_HIGH_PRESSURE,  "AC5_HP",   "AC Unit 5 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC5_LOW_PRESSURE,   "AC5_LP",   "AC Unit 5 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC5_AIRFLOW_FAULT,  "AC5_FLOW", "AC Unit 5 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC5_VOLTAGE_FAULT,  "AC5_VOLT", "AC Unit 5 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC6 Contacts */
    { ALARM_ID_AC6_HIGH_PRESSURE,  "AC6_HP",   "AC Unit 6 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC6_LOW_PRESSURE,   "AC6_LP",   "AC Unit 6 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC6_AIRFLOW_FAULT,  "AC6_FLOW", "AC Unit 6 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC6_VOLTAGE_FAULT,  "AC6_VOLT", "AC Unit 6 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC7 Contacts */
    { ALARM_ID_AC7_HIGH_PRESSURE,  "AC7_HP",   "AC Unit 7 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC7_LOW_PRESSURE,   "AC7_LP",   "AC Unit 7 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC7_AIRFLOW_FAULT,  "AC7_FLOW", "AC Unit 7 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC7_VOLTAGE_FAULT,  "AC7_VOLT", "AC Unit 7 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* AC8 Contacts */
    { ALARM_ID_AC8_HIGH_PRESSURE,  "AC8_HP",   "AC Unit 8 High Pressure Cutout",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC8_LOW_PRESSURE,   "AC8_LP",   "AC Unit 8 Low Pressure Cutout",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_AC8_AIRFLOW_FAULT,  "AC8_FLOW", "AC Unit 8 Airflow/Fan Failure",    ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_AC8_VOLTAGE_FAULT,  "AC8_VOLT", "AC Unit 8 Voltage/Phase Fault",   ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* Facility Contacts */
    { ALARM_ID_FACILITY_FIRE,      "FIRE",     "Facility Smoke/Fire Alarm",        ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_FATAL,    ALARM_PRIORITY_EMERGENCY, false, true,  false },
    { ALARM_ID_FACILITY_DOOR_OPEN, "DOOR",     "Containment Door Open",            ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_INFO,     ALARM_PRIORITY_LOW,       false, false, false },
    { ALARM_ID_FACILITY_WATER_LEAK,"LEAK",     "Raised Floor Water Leak",          ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_FACILITY_SURGE_PROT,"SPD",      "Surge Protective Device Trip",     ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_FACILITY_PHASE_MON, "PHASE_MON","Main 3-Phase Monitor Relay",       ALARM_SOURCE_DIGITAL_INPUT, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* Environmental / Temperatures (Aggregated for Groups 1..4 / Salas 1..4) */
    { ALARM_ID_GROUP1_TEMP_LOW,    "G1_T_LOW", "Group 1 Low Temperature Alarm",    ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, true  },
    { ALARM_ID_GROUP1_TEMP_HIGH,   "G1_T_HIGH","Group 1 High Temperature Alarm",   ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, true  },
    { ALARM_ID_GROUP1_TEMP_BOOST,  "G1_BOOST", "Group 1 Cooling Boost State",      ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_GROUP2_TEMP_LOW,    "G2_T_LOW", "Group 2 Low Temperature Alarm",    ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, true  },
    { ALARM_ID_GROUP2_TEMP_HIGH,   "G2_T_HIGH","Group 2 High Temperature Alarm",   ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, true  },
    { ALARM_ID_GROUP2_TEMP_BOOST,  "G2_BOOST", "Group 2 Cooling Boost State",      ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_GROUP3_TEMP_LOW,    "G3_T_LOW", "Group 3 Low Temperature Alarm",    ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, true  },
    { ALARM_ID_GROUP3_TEMP_HIGH,   "G3_T_HIGH","Group 3 High Temperature Alarm",   ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, true  },
    { ALARM_ID_GROUP3_TEMP_BOOST,  "G3_BOOST", "Group 3 Cooling Boost State",      ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_GROUP4_TEMP_LOW,    "G4_T_LOW", "Group 4 Low Temperature Alarm",    ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, true  },
    { ALARM_ID_GROUP4_TEMP_HIGH,   "G4_T_HIGH","Group 4 High Temperature Alarm",   ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, true  },
    { ALARM_ID_GROUP4_TEMP_BOOST,  "G4_BOOST", "Group 4 Cooling Boost State",      ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* Electrical Power Quality */
    { ALARM_ID_LINE_UNDER_VOLTAGE, "V_UNDER",  "AC Line Under-Voltage (< -8%)",    ALARM_SOURCE_ELECTRICAL,    ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_LINE_OVER_VOLTAGE,  "V_OVER",   "AC Line Over-Voltage (> +8%)",     ALARM_SOURCE_ELECTRICAL,    ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_ENERGY_METER_COMM_FAULT, "MTR_COMM", "JSY-MK-339 Meter Comm Fault",ALARM_SOURCE_ELECTRICAL,    ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },

    /* HVAC Control */
    { ALARM_ID_HVAC_G1_BYPASS_ACTIVE, "G1_BYPASS", "HVAC Group 1 Bypass Operation", ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_G2_BYPASS_ACTIVE, "G2_BYPASS", "HVAC Group 2 Bypass Operation", ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_G3_BYPASS_ACTIVE, "G3_BYPASS", "HVAC Group 3 Bypass Operation", ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_G4_BYPASS_ACTIVE, "G4_BYPASS", "HVAC Group 4 Bypass Operation", ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT1_LOCKOUT, "AC1_LOCK", "AC Unit 1 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT2_LOCKOUT, "AC2_LOCK", "AC Unit 2 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT3_LOCKOUT, "AC3_LOCK", "AC Unit 3 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT4_LOCKOUT, "AC4_LOCK", "AC Unit 4 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT5_LOCKOUT, "AC5_LOCK", "AC Unit 5 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT6_LOCKOUT, "AC6_LOCK", "AC Unit 6 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT7_LOCKOUT, "AC7_LOCK", "AC Unit 7 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },
    { ALARM_ID_HVAC_UNIT8_LOCKOUT, "AC8_LOCK", "AC Unit 8 Safety Lockout",         ALARM_SOURCE_HVAC_CONTROL,  ALARM_SEVERITY_CRITICAL, ALARM_PRIORITY_HIGH,      false, false, false },

    /* Sensor Faults (Groups 1..4) */
    { ALARM_ID_SENSOR_GROUP1_FAULT,"SENS_G1",  "Group 1 Control Sensor Fault",     ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_SENSOR_GROUP2_FAULT,"SENS_G2",  "Group 2 Control Sensor Fault",     ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_SENSOR_GROUP3_FAULT,"SENS_G3",  "Group 3 Control Sensor Fault",     ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_SENSOR_GROUP4_FAULT,"SENS_G4",  "Group 4 Control Sensor Fault",     ALARM_SOURCE_SENSOR_ANALOG, ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },

    /* Infrastructure */
    { ALARM_ID_INFRA_ETHERNET_DOWN,"ETH_DOWN", "RMII Ethernet Link Down",          ALARM_SOURCE_SYSTEM_INFRA,  ALARM_SEVERITY_INFO,     ALARM_PRIORITY_LOW,       false, false, false },
    { ALARM_ID_INFRA_SD_CARD_FAULT,"SD_FAULT", "MicroSD Card Disconnected/Fault",  ALARM_SOURCE_SYSTEM_INFRA,  ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false },
    { ALARM_ID_INFRA_MODBUS_FAULT, "MB_FAULT", "RS-485 Modbus Telemetry Bus Fault",ALARM_SOURCE_SYSTEM_INFRA,  ALARM_SEVERITY_WARNING,  ALARM_PRIORITY_MEDIUM,    false, false, false }
};

void alarm_registry_init(alarm_registry_t *self)
{
    if (self == NULL) {
        return;
    }

    self->count = 0;
    size_t default_count = sizeof(g_default_catalog) / sizeof(g_default_catalog[0]);
    if (default_count > CONFIG_ALARM_MANAGER_MAX_ALARMS) {
        default_count = CONFIG_ALARM_MANAGER_MAX_ALARMS;
    }

    for (size_t i = 0; i < default_count; i++) {
        self->descriptors[i] = g_default_catalog[i];
    }
    self->count = default_count;
}

int alarm_registry_register(alarm_registry_t *self, const alarm_descriptor_t *desc)
{
    if (self == NULL || desc == NULL || desc->alarm_id == ALARM_ID_NONE || desc->name == NULL) {
        return -EINVAL;
    }

    /* Check if already registered */
    if (alarm_registry_find_index(self, desc->alarm_id) >= 0) {
        return -EEXIST;
    }

    if (self->count >= CONFIG_ALARM_MANAGER_MAX_ALARMS) {
        return -ENOSPC;
    }

    self->descriptors[self->count] = *desc;
    self->count++;

    return 0;
}

int alarm_registry_find_index(const alarm_registry_t *self, uint16_t alarm_id)
{
    if (self == NULL || alarm_id == ALARM_ID_NONE) {
        return -1;
    }

    for (size_t i = 0; i < self->count; i++) {
        if (self->descriptors[i].alarm_id == alarm_id) {
            return (int)i;
        }
    }

    return -1;
}

int alarm_registry_get(const alarm_registry_t *self, uint16_t alarm_id, alarm_descriptor_t *out_desc)
{
    if (self == NULL || out_desc == NULL) {
        return -EINVAL;
    }

    int idx = alarm_registry_find_index(self, alarm_id);
    if (idx < 0) {
        return -ENOENT;
    }

    *out_desc = self->descriptors[idx];
    return 0;
}

size_t alarm_registry_get_count(const alarm_registry_t *self)
{
    if (self == NULL) {
        return 0;
    }
    return self->count;
}
