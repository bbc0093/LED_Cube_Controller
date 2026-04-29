/**
 * @file bq25731_registers.h
 * @brief BQ25731 register map and field definitions (private).
 *
 * All registers are 16-bit.  The BQ25731 uses little-endian byte order on
 * the I2C bus: the LSByte (lower address) is transmitted first.  When a
 * 16-bit register value is assembled from two read bytes the result is:
 *   reg16 = buf[0] | ((uint16_t)buf[1] << 8)
 *
 * Register addresses are the LSByte address (written on I2C); the MSByte
 * follows automatically at address + 1.  This matches the "MSB/LSBh"
 * notation in the BQ25731 datasheet register map table (Table 9-7).
 *
 * Verified against BQ25731 datasheet SLUSE66A, sections 9.6.1–9.6.22.
 *
 * @author William Crow
 * @date 4/28/2026
 * @last_modified 2026-04-28
 */

/*
 * @copyright
 * MIT License
 * Copyright (c) 2026 William Crow
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 */

#ifndef BQ25731_REGISTERS_H
#define BQ25731_REGISTERS_H

// ── Register addresses (LSByte address as sent on I2C) ────────────────────────
#define BQ25731_REG_CHARGE_OPTION0     0x00u  // 01/00h  reset = E70Eh
#define BQ25731_REG_CHARGE_CURRENT     0x02u  // 03/02h  reset = 0080h (256 mA at 5mΩ default)
#define BQ25731_REG_CHARGE_VOLTAGE     0x04u  // 05/04h  reset depends on CELL_BATPRESZ pin
#define BQ25731_REG_OTG_VOLTAGE        0x06u  // 07/06h
#define BQ25731_REG_OTG_CURRENT        0x08u  // 09/08h
#define BQ25731_REG_INPUT_VOLTAGE      0x0Au  // 0B/0Ah
#define BQ25731_REG_IIN_HOST           0x0Eu  // 0F/0Eh  reset = 4100h (3.2 A at 5mΩ default)
#define BQ25731_REG_CHARGER_STATUS     0x20u  // 21/20h
#define BQ25731_REG_PROCHOT_STATUS     0x22u  // 23/22h
#define BQ25731_REG_IIN_DPM            0x24u  // 25/24h  read-only
#define BQ25731_REG_ADC_VBUS_PSYS      0x26u  // 27/26h  high byte = VBUS, low byte = PSYS
#define BQ25731_REG_ADC_IBAT           0x28u  // 29/28h  high byte = ICHG[6:0], low byte = IDCHG[6:0]
#define BQ25731_REG_ADC_IIN_CMPIN      0x2Au  // 2B/2Ah  high byte = IIN,  low byte = CMPIN
#define BQ25731_REG_ADC_VSYS_VBAT      0x2Cu  // 2D/2Ch  high byte = VSYS, low byte = VBAT
#define BQ25731_REG_MANUFACTURER_ID    0x2Eu  // 2Eh     single byte (0x40), followed by Device ID
#define BQ25731_REG_DEVICE_ID          0x2Fu  // 2Fh     single byte (0xD6 for BQ25731)
#define BQ25731_REG_CHARGE_OPTION1     0x30u  // 31/30h  reset = 3F00h
#define BQ25731_REG_CHARGE_OPTION2     0x32u  // 33/32h
#define BQ25731_REG_CHARGE_OPTION3     0x34u  // 35/34h
#define BQ25731_REG_PROCHOT_OPTION0    0x36u  // 37/36h
#define BQ25731_REG_PROCHOT_OPTION1    0x38u  // 39/38h
#define BQ25731_REG_ADC_OPTION         0x3Au  // 3B/3Ah  reset = 2000h
#define BQ25731_REG_CHARGE_OPTION4     0x3Cu  // 3D/3Ch
#define BQ25731_REG_VMIN_ACTIVE_PROT   0x3Eu  // 3F/3Eh

// ── ChargeOption0 (0x00) bit positions in reg16 ───────────────────────────────
// High byte (I2C addr 0x01) occupies reg16[15:8]:
#define BQ25731_OPT0_EN_LWPWR          (1u << 15)  // Low-power mode (default 1)
#define BQ25731_OPT0_WDTMR_MASK        (3u << 13)  // Watchdog timer field [14:13]
#define BQ25731_OPT0_WDTMR_DISABLE     (0u << 13)  //   00 = disabled
#define BQ25731_OPT0_WDTMR_5S          (1u << 13)  //   01 = 5 s
#define BQ25731_OPT0_WDTMR_88S         (2u << 13)  //   10 = 88 s
#define BQ25731_OPT0_WDTMR_175S        (3u << 13)  //   11 = 175 s (default)
#define BQ25731_OPT0_EN_OOA            (1u << 10)  // Out-of-audio mode (default 1)
#define BQ25731_OPT0_PWM_FREQ          (1u <<  9)  // Switching freq: 0=800kHz, 1=400kHz (default 1)
// Low byte (I2C addr 0x00) occupies reg16[7:0]:
#define BQ25731_OPT0_IADPT_GAIN        (1u <<  4)  // IADPT ratio: 0=20×, 1=40×
#define BQ25731_OPT0_IBAT_GAIN         (1u <<  3)  // IBAT ratio:  0=8×,  1=16× (default 1)
#define BQ25731_OPT0_EN_IIN_DPM        (1u <<  1)  // IIN_DPM enable (default 1)
#define BQ25731_OPT0_CHRG_INHIBIT      (1u <<  0)  // 0=charging enabled (default), 1=inhibit

// ── ChargeOption1 (0x30) bit positions in reg16 ───────────────────────────────
// High byte (I2C addr 0x31) occupies reg16[15:8]:
#define BQ25731_OPT1_PSYS_CONFIG_MASK  (3u << 12)  // PSYS enable: 11=off (default)
#define BQ25731_OPT1_PSYS_OFF          (3u << 12)
#define BQ25731_OPT1_PSYS_PBUS_PBAT   (0u << 12)  // PSYS = PBUS + PBAT
#define BQ25731_OPT1_RSNS_RAC          (1u << 11)  // Input sense R: 0=10mΩ, 1=5mΩ (default)
#define BQ25731_OPT1_RSNS_RSR          (1u << 10)  // Charge sense R: 0=10mΩ, 1=5mΩ (default)

// ── ChargeCurrent (0x02) ──────────────────────────────────────────────────────
// 7-bit current code in reg16[12:6].  bit 6 = 128 mA (LSB at RSR_REF).
// Encode: reg16 = (current_ma / lsb_ma) << 6 & BQ25731_CHARGE_CURRENT_MASK
// Decode: current_ma = ((reg16 & mask) >> 6) * lsb_ma
// Scale:  lsb_ma = CHARGE_CURRENT_LSB_MA × RSR_REF / rsr_mohm
#define BQ25731_CHARGE_CURRENT_MASK    0x1FC0u
#define BQ25731_CHARGE_CURRENT_LSB_MA  128u
#define BQ25731_CHARGE_CURRENT_RSR_REF 5u         // mΩ reference for LSB spec

// ── ChargeVoltage (0x04) ──────────────────────────────────────────────────────
// 12-bit voltage in reg16[14:3].  bit 3 = 8 mV (LSB).
// Encode: reg16 = voltage_mv & BQ25731_CHARGE_VOLTAGE_MASK  (voltage must be multiple of 8)
#define BQ25731_CHARGE_VOLTAGE_MASK    0x7FF8u
#define BQ25731_CHARGE_VOLTAGE_LSB_MV  8u

// ── IIN_HOST (0x0E) ───────────────────────────────────────────────────────────
// 7-bit current code in reg16[14:8] (high byte bits[6:0], bit7 reserved).
// bit 8 = 100 mA (LSB at RAC_REF).  Offset: code 0 → 100 mA minimum.
// Encode: reg16 = (current_ma / lsb_ma) << 8 & BQ25731_IIN_HOST_MASK
// Scale:  lsb_ma = IIN_HOST_LSB_MA × RAC_REF / rac_mohm
#define BQ25731_IIN_HOST_MASK          0x7F00u
#define BQ25731_IIN_HOST_LSB_MA        100u
#define BQ25731_IIN_HOST_RAC_REF       5u         // mΩ reference for LSB spec

// ── ADCOption (0x3A) ──────────────────────────────────────────────────────────
// High byte (I2C addr 0x3B) occupies reg16[15:8]:
#define BQ25731_ADC_OPT_CONV           (1u << 15)  // 0=one-shot, 1=continuous
#define BQ25731_ADC_OPT_START          (1u << 14)  // Start conversion (self-clears on one-shot)
#define BQ25731_ADC_OPT_FULLSCALE      (1u << 13)  // PSYS/CMPIN range: 0=2.04V, 1=3.06V (default)
// Low byte (I2C addr 0x3A) — individual channel enables:
#define BQ25731_ADC_EN_CMPIN           (1u << 7)
#define BQ25731_ADC_EN_VBUS            (1u << 6)
#define BQ25731_ADC_EN_PSYS            (1u << 5)
#define BQ25731_ADC_EN_IIN             (1u << 4)
#define BQ25731_ADC_EN_IDCHG           (1u << 3)
#define BQ25731_ADC_EN_ICHG            (1u << 2)
#define BQ25731_ADC_EN_VSYS            (1u << 1)
#define BQ25731_ADC_EN_VBAT            (1u << 0)
#define BQ25731_ADC_EN_ALL             0x00FFu     // Enable all channels

// One-shot conversion of all channels, preserving 3.06 V full-scale for PSYS/CMPIN.
#define BQ25731_ADC_ONESHOT_ALL  (BQ25731_ADC_OPT_START | BQ25731_ADC_OPT_FULLSCALE | BQ25731_ADC_EN_ALL)

// ── ADC result scaling ────────────────────────────────────────────────────────

// ADCVBUS_PSYS (0x26): high byte (0x27) = VBUS, low byte (0x26) = PSYS
// VBUS: 96 mV/LSB, range 0–24.48 V.
// PSYS: 12 mV/LSB at FULLSCALE=1 (3.06 V range).  Requires PSYS_CONFIG ≠ 11b in ChargeOption1.
#define BQ25731_ADC_VBUS_LSB_MV        96u
#define BQ25731_ADC_PSYS_LSB_MV        12u

// ADCIBAT (0x28): high byte (0x29) = ICHG bits[6:0], low byte (0x28) = IDCHG bits[6:0]
// LSB scales with sense resistor: lsb_ma = BASE_LSB × RSR_REF / rsr_mohm
#define BQ25731_ADC_ICHG_LSB_MA        128u       // At RSR = 5 mΩ
#define BQ25731_ADC_IDCHG_LSB_MA       512u       // At RSR = 5 mΩ
#define BQ25731_ADC_IBAT_RSR_REF       5u

// ADCIINCMPIN (0x2A): high byte (0x2B) = IIN, low byte (0x2A) = CMPIN
// IIN:   100 mA/LSB at RAC = 5 mΩ.  lsb_ma = BASE_LSB × RAC_REF / rac_mohm
// CMPIN: 12 mV/LSB at FULLSCALE=1 (3.06 V range).
#define BQ25731_ADC_IIN_LSB_MA         100u       // At RAC = 5 mΩ
#define BQ25731_ADC_CMPIN_LSB_MV       12u
#define BQ25731_ADC_IIN_RAC_REF        5u

// ADCVSYSVBAT (0x2C): high byte (0x2D) = VSYS, low byte (0x2C) = VBAT
// Both: 64 mV/LSB, base offset 2880 mV.  Range 2.88–19.2 V (1S–4S).
#define BQ25731_ADC_VBAT_LSB_MV        64u
#define BQ25731_ADC_VBAT_BASE_MV       2880u
#define BQ25731_ADC_VSYS_LSB_MV        64u
#define BQ25731_ADC_VSYS_BASE_MV       2880u

// ── ChargerStatus (0x20) bit positions in reg16 ───────────────────────────────
// High byte (I2C addr 0x21) → reg16[15:8]:
#define BQ25731_STATUS_AC_STAT         (1u << 15)  // Input present (3.5–26 V range)
#define BQ25731_STATUS_ICO_DONE        (1u << 14)  // ICO algorithm complete
#define BQ25731_STATUS_IN_VAP          (1u << 13)  // In VAP mode
#define BQ25731_STATUS_IN_VINDPM       (1u << 12)  // Input voltage regulation active
#define BQ25731_STATUS_IN_IDPM         (1u << 11)  // Input current regulation active
#define BQ25731_STATUS_IN_FCHRG        (1u << 10)  // Fast-charge active
#define BQ25731_STATUS_IN_OTG          (1u <<  8)  // OTG mode active
// Low byte (I2C addr 0x20) → reg16[7:0]:
#define BQ25731_STATUS_FAULT_ACOV      (1u <<  7)  // AC overvoltage (latched until read)
#define BQ25731_STATUS_FAULT_BATOC     (1u <<  6)  // Battery overcurrent (latched until read)
#define BQ25731_STATUS_FAULT_ACOC      (1u <<  5)  // AC overcurrent (latched until read)
#define BQ25731_STATUS_FAULT_SYSOVP    (1u <<  4)  // System OVP (R/W: write 0 to clear)
#define BQ25731_STATUS_FAULT_VSYS_UVP  (1u <<  3)  // System UVP (R/W: write 0 to clear)
#define BQ25731_STATUS_FAULT_CONV_OFF  (1u <<  2)  // Force converter off triggered
#define BQ25731_STATUS_FAULT_OTG_OVP   (1u <<  1)  // OTG overvoltage
#define BQ25731_STATUS_FAULT_OTG_UVP   (1u <<  0)  // OTG undervoltage

#define BQ25731_STATUS_FAULT_MASK  (BQ25731_STATUS_FAULT_ACOV    | \
                                    BQ25731_STATUS_FAULT_BATOC   | \
                                    BQ25731_STATUS_FAULT_ACOC    | \
                                    BQ25731_STATUS_FAULT_SYSOVP  | \
                                    BQ25731_STATUS_FAULT_VSYS_UVP | \
                                    BQ25731_STATUS_FAULT_CONV_OFF | \
                                    BQ25731_STATUS_FAULT_OTG_OVP | \
                                    BQ25731_STATUS_FAULT_OTG_UVP)

// ── I2C address and ID values ─────────────────────────────────────────────────
#define BQ25731_I2C_ADDR               0x6Bu
#define BQ25731_MANUFACTURER_ID        0x40u  // Texas Instruments
#define BQ25731_DEVICE_ID              0xD6u  // BQ25731

#endif // BQ25731_REGISTERS_H