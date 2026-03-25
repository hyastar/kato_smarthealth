#ifndef BSP_SHT30_H
#define BSP_SHT30_H

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

/* SHT30 I2C地址：ADDR引脚接GND=0x44 */
#define SHT30_I2C               (&hi2c1)
#define SHT30_ADDR              (0x44 << 1)

/* 单次测量命令：高重复精度 */
#define SHT30_CMD_MEAS_H        0x2C06

HAL_StatusTypeDef BSP_SHT30_Init(void);
HAL_StatusTypeDef BSP_SHT30_Read(float *temperature, float *humidity);

#endif /* BSP_SHT30_H */
