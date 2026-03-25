/**
  ******************************************************************************
  * @file    bsp_max30102.h
  * @brief   MAX30102 SpO2/HR 传感器驱动
  *
  * @note    硬件连接：
  *            MAX30102 SCL → PB6 (I2C1_SCL)
  *            MAX30102 SDA → PB7 (I2C1_SDA)
  *            MAX30102 INT → PA0 (EXTI0)
  *
  *          驱动说明：
  *            - 基于 HAL_I2C 封装读写时序
  *            - 仅负责外设控制，不调用 FreeRTOS API（互斥锁由上层任务管理）
  *            - 配置为 SpO2 模式，读取红光(Red)和红外(IR)原始数据
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#ifndef __BSP_MAX30102_H__
#define __BSP_MAX30102_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

extern I2C_HandleTypeDef hi2c1;

/*============================================================================
 * MAX30102 I2C 配置
 *============================================================================*/
#define MAX30102_I2C             (&hi2c1)
#define MAX30102_ADDR            (0x57 << 1)   /* ADDR接GND=0x57 */

/*============================================================================
 * MAX30102 寄存器地址
 *============================================================================*/
#define MAX30102_REG_FIFO_WR_PTR        0x04
#define MAX30102_REG_FIFO_OVF_COUNTER   0x05
#define MAX30102_REG_FIFO_RD_PTR        0x06
#define MAX30102_REG_FIFO_DATA          0x07
#define MAX30102_REG_FIFO_CONFIG         0x08
#define MAX30102_REG_MODE_CONFIG         0x09
#define MAX30102_REG_PARTICLE_SENSOR     0x0A
#define MAX30102_REG_LED_PULSE_AMP       0x0C
#define MAX30102_REG_MULTI_LED_CTRL1     0x11
#define MAX30102_REG_MULTI_LED_CTRL2     0x12
#define MAX30102_REG_DIE_TEMP_EN         0x21
#define MAX30102_REG_DIE_TEMP_RD         0x22
#define MAX30102_REG_DIE_TEMP_CONFIG     0x21
#define MAX30102_REG_REV_ID              0xFE
#define MAX30102_REG_PART_ID             0xFF

/*============================================================================
 * MAX30102 配置参数
 *============================================================================*/
#define MAX30102_SAMPLE_RATE     100   /* 采样率：100Hz */
#define MAX30102_LED_PULSE_AMP  0x7F  /* LED电流：0~255, 越大亮度越高 */
#define MAX30102_FIFO_THRESH     0x03 /* 水位阈值 */

/*============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief  MAX30102 FIFO 数据结构
 */
typedef struct {
    uint32_t ir;   /* 红外光原始值 */
    uint32_t red;  /* 红光原始值 */
} MAX30102_Data_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  初始化 MAX30102
 * @retval HAL_OK      成功
 * @retval HAL_ERROR   失败（设备未响应）
 */
HAL_StatusTypeDef BSP_MAX30102_Init(void);

/**
 * @brief  配置 MAX30102 为 SpO2 模式
 * @retval HAL_OK/HAL_ERROR
 */
HAL_StatusTypeDef BSP_MAX30102_ConfigSpO2(void);

/**
 * @brief  读取 FIFO 数据
 * @note   读取后自动清除 FIFO 指针，实际读取的是 FIFO 中最旧的数据
 * @param  data  指向数据结构的指针
 * @retval HAL_OK      成功
 * @retval HAL_ERROR   无新数据或I2C错误
 */
HAL_StatusTypeDef BSP_MAX30102_ReadFIFO(MAX30102_Data_t *data);

/**
 * @brief  检查 FIFO 中有多少个样本待读
 * @retval 样本数量
 */
uint8_t BSP_MAX30102_GetFIFOWords(void);

/**
 * @brief  清除 FIFO
 */
void BSP_MAX30102_ClearFIFO(void);

/**
 * @brief  读取芯片 ID（用于自检）
 * @retval 芯片ID，MAX30102应为0x15
 */
uint8_t BSP_MAX30102_ReadPartID(void);

/**
 * @brief  使能/禁止 FIFO Almost Full 中断
 * @param  enable  true=使能, false=禁止
 */
void BSP_MAX30102_EnableFIFOInterrupt(bool enable);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_MAX30102_H__ */
