#ifndef BSP_ADS1115_H
#define BSP_ADS1115_H

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

/* I2C句柄 */
#define ADS1115_I2C             (&hi2c1)

/* I2C地址：ADDR接GND=0x48 */
#define ADS1115_ADDR            (0x48 << 1)

/* 寄存器 */
#define ADS1115_REG_CONV        0x00
#define ADS1115_REG_CFG         0x01

/* PGA ±6.144V，覆盖0~5V MQ输出 */
#define ADS1115_PGA_6144        (0x00 << 9)

/* 单次转换模式 */
#define ADS1115_MODE_SINGLE     (0x01 << 8)

/* 128SPS，每通道转换约10ms */
#define ADS1115_DR_128SPS       (0x04 << 5)

/* 启动单次转换 */
#define ADS1115_OS_START        (0x01 << 15)

/* MUX：单端4通道 */
#define ADS1115_MUX_AIN0        (0x04 << 12)
#define ADS1115_MUX_AIN1        (0x05 << 12)
#define ADS1115_MUX_AIN2        (0x06 << 12)
#define ADS1115_MUX_AIN3        (0x07 << 12)

/* V/LSB：±6.144V量程 */
#define ADS1115_LSB             (0.0001875f)

/* 关闭比较器（低两位=11） */
#define ADS1115_COMP_DISABLE    (0x03)

/* 供电电压（万用表实测后修改） */
#define ADS1115_VC              (5.0f)

/* 通道数 */
#define ADS1115_CH_COUNT        4

HAL_StatusTypeDef BSP_ADS1115_Init(void);
HAL_StatusTypeDef BSP_ADS1115_ReadAllChannels(float voltage[ADS1115_CH_COUNT]);

#endif /* BSP_ADS1115_H */
