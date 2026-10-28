/*
 * Copyright (c) 2026 DAECGE
 * SPDX-License-Identifier: Apache-2.0
 */

#include "modbus_decode.h"

int modbus_decode_float32(uint16_t reg0, uint16_t reg1, modbus_word_order_t order, float *out_val)
{
    if (out_val == NULL) {
        return -1;
    }

    uint32_t u32 = 0U;

    switch (order) {
    case MODBUS_ENDIANNESS_BE:
        /* reg0 = MSW, reg1 = LSW (AB CD) */
        u32 = ((uint32_t)reg0 << 16) | (uint32_t)reg1;
        break;
    case MODBUS_ENDIANNESS_LE:
        /* reg0 = LSW, reg1 = MSW (CD AB) */
        u32 = ((uint32_t)reg1 << 16) | (uint32_t)reg0;
        break;
    case MODBUS_ENDIANNESS_BYTE_SWAP_BE: {
        uint16_t s0 = ((reg0 & 0xFFU) << 8) | ((reg0 >> 8) & 0xFFU);
        uint16_t s1 = ((reg1 & 0xFFU) << 8) | ((reg1 >> 8) & 0xFFU);
        u32 = ((uint32_t)s0 << 16) | (uint32_t)s1;
        break;
    }
    case MODBUS_ENDIANNESS_BYTE_SWAP_LE: {
        uint16_t s0 = ((reg0 & 0xFFU) << 8) | ((reg0 >> 8) & 0xFFU);
        uint16_t s1 = ((reg1 & 0xFFU) << 8) | ((reg1 >> 8) & 0xFFU);
        u32 = ((uint32_t)s1 << 16) | (uint32_t)s0;
        break;
    }
    default:
        return -1;
    }

    memcpy(out_val, &u32, sizeof(float));
    return 0;
}

int modbus_decode_int16_scaled(uint16_t reg, float scale_multiplier, float *out_val)
{
    if (out_val == NULL) {
        return -1;
    }
    int16_t s16 = (int16_t)reg;
    *out_val = (float)s16 * scale_multiplier;
    return 0;
}

int modbus_decode_uint16_scaled(uint16_t reg, float scale_multiplier, float *out_val)
{
    if (out_val == NULL) {
        return -1;
    }
    *out_val = (float)reg * scale_multiplier;
    return 0;
}

int modbus_decode_int32(uint16_t reg0, uint16_t reg1, bool word_swap, int32_t *out_val)
{
    if (out_val == NULL) {
        return -1;
    }
    uint32_t u32 = word_swap ? (((uint32_t)reg1 << 16) | reg0) : (((uint32_t)reg0 << 16) | reg1);
    *out_val = (int32_t)u32;
    return 0;
}

int modbus_decode_uint32(uint16_t reg0, uint16_t reg1, bool word_swap, uint32_t *out_val)
{
    if (out_val == NULL) {
        return -1;
    }
    *out_val = word_swap ? (((uint32_t)reg1 << 16) | reg0) : (((uint32_t)reg0 << 16) | reg1);
    return 0;
}
