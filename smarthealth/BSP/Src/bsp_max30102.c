/**
  ******************************************************************************
  * @file    bsp_max30102.c
  * @brief   MAX30102 SpO2/HR 传感器驱动实现
  *
  * @note    功能说明：
  *            - I2C 读写寄存器时序
  *            - SpO2 模式配置（红光+红外）
  *            - FIFO 数据读取
  *
  *          注意事项：
  *            - 本驱动仅封装外设操作，不持有 FreeRTOS 互斥锁
  *            - 调用者须在持有 I2C1_MutexHandle 的前提下调用本驱动函数
  *            - FIFO 为 32 字节深度(16 samples × 3bytes)，读取不及时会溢出
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#include "bsp_max30102.h"
#include "main.h"
#include <string.h>

/*============================================================================
 * 私有宏
 *============================================================================*/
#define MAX30102_TIMEOUT        100   /* I2C 超时 ms */

/*============================================================================
 * 内部函数
 *============================================================================*/

/**
 * @brief  写单个寄存器
 */
static HAL_StatusTypeDef reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return HAL_I2C_Master_Transmit(
        MAX30102_I2C, MAX30102_ADDR, buf, 2, MAX30102_TIMEOUT);
}

/**
 * @brief  读单个寄存器
 */
static HAL_StatusTypeDef reg_read(uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Master_Transmit(
        MAX30102_I2C, MAX30102_ADDR, &reg, 1, MAX30102_TIMEOUT)
        ? HAL_ERROR
        : HAL_I2C_Master_Receive(
              MAX30102_I2C, MAX30102_ADDR, val, 1, MAX30102_TIMEOUT);
}

/**
 * @brief  读取连续寄存器（burst read）
 */
static HAL_StatusTypeDef reg_burst_read(uint8_t reg, uint8_t *buf, uint8_t len)
{
    HAL_StatusTypeDef ret;
    ret = HAL_I2C_Master_Transmit(
        MAX30102_I2C, MAX30102_ADDR, &reg, 1, MAX30102_TIMEOUT);
    if (ret != HAL_OK) return ret;

    return HAL_I2C_Master_Receive(
        MAX30102_I2C, MAX30102_ADDR, buf, len, MAX30102_TIMEOUT);
}

/*============================================================================
 * 公共函数实现
 *============================================================================*/

HAL_StatusTypeDef BSP_MAX30102_Init(void)
{
    uint8_t part_id;
    if (reg_read(MAX30102_REG_PART_ID, &part_id) != HAL_OK)
        return HAL_ERROR;

    /* MAX30102 芯片 ID 应为 0x15 */
    if (part_id != 0x15)
        return HAL_ERROR;

    return BSP_MAX30102_ConfigSpO2();
}

HAL_StatusTypeDef BSP_MAX30102_ConfigSpO2(void)
{
    /* 1. 软件复位 */
    reg_write(MAX30102_REG_MODE_CONFIG, 0x40);
    HAL_Delay(10);

    /* 2. FIFO 配置
     *    b7:     SMP_AVE[2:0]  → 0: 不平均, 1: 2次平均, ..., 7: 128次平均
     *    b6:     FIFO_ROLLOVER_EN → 1: 溢出时回环
     *    b5-b0:  FIFO_A_FULL[4:0] → 触发 Almost Full 中断的水位 */
    reg_write(MAX30102_REG_FIFO_CONFIG,
              (0 << 5) |            /* 不使能回环（由应用控制读取速度） */
              (MAX30102_FIFO_THRESH & 0x1F));

    /* 3. Particle Sensing 配置（采样率 + ADC分辨率）
     *    b7-b6:  LED_PW[1:0]  → 00: 13-bit@200us, 01: 16-bit@400us, 10: 17-bit@800us, 11: 18-bit@1600us
     *    b5-b0:  SAMPLE[5:0]  → 采样率分频系数，有效范围 50~2048 */
    reg_write(MAX30102_REG_PARTICLE_SENSOR,
              (0x03 << 6) |                                    /* 18-bit, 1600us（高精度） */
              ((100000 / MAX30102_SAMPLE_RATE / 400 - 1) & 0x3F)); /* 100Hz */

    /* 4. LED1 (IR红外) 脉幅 */
    reg_write(MAX30102_REG_LED_PULSE_AMP,
              (0 << 7) |           /* LED2/IR disable? 不，0=正常 */
              (MAX30102_LED_PULSE_AMP & 0x7F));

    /* 5. LED2 (红光) 脉幅（在同一寄存器的高字节） */
    reg_write(0x0D,  /* LED2_pulse_amplitude, Aiken 顺序 */
              (0 << 7) |
              (MAX30102_LED_PULSE_AMP & 0x7F));

    /* 6. 多路 LED 控制：LED1=IR, LED2=Red（每 slot 1 LED） */
    reg_write(MAX30102_REG_MULTI_LED_CTRL1,
              (1 << 4) |   /* LED2 Slot2: 1=Red  */
              (0 << 0));   /* LED1 Slot1: 0=IR   */

    reg_write(MAX30102_REG_MULTI_LED_CTRL2,
              (2 << 4) |   /* LED2 Slot4: Red (第2个slot继续) */
              (1 << 0));   /* LED1 Slot3: IR (第2个slot继续) */

    /* 7. 清除 FIFO */
    BSP_MAX30102_ClearFIFO();

    /* 8. 启动 SpO2 模式 */
    reg_write(MAX30102_REG_MODE_CONFIG,
              (0 << 7) |   /* RESET=0 */
              (1 << 6) |   /* FIFO_ROLLOVER_EN */
              (0 << 5) |   /* FIFO_OVF_IRQ_EN */
              (0 << 4) |   /* PROX_INT_EN */
              (0x03));     /* MODE[2:0]=011 → SpO2 模式 */

    return HAL_OK;
}

uint8_t BSP_MAX30102_ReadPartID(void)
{
    uint8_t id = 0;
    reg_read(MAX30102_REG_PART_ID, &id);
    return id;
}

void BSP_MAX30102_ClearFIFO(void)
{
    uint8_t dummy;
    reg_write(MAX30102_REG_FIFO_WR_PTR, 0x00);
    reg_write(MAX30102_REG_FIFO_OVF_COUNTER, 0x00);
    reg_write(MAX30102_REG_FIFO_RD_PTR, 0x00);
    /* 读空 FIFO 中的旧数据 */
    uint8_t buf[6];
    for (int i = 0; i < 32; i++) {
        reg_burst_read(MAX30102_REG_FIFO_DATA, buf, 6);
    }
    (void)dummy;
}

uint8_t BSP_MAX30102_GetFIFOWords(void)
{
    uint8_t write_ptr, ovf, read_ptr;
    reg_read(MAX30102_REG_FIFO_WR_PTR, &write_ptr);
    reg_read(MAX30102_REG_FIFO_OVF_COUNTER, &ovf);
    reg_read(MAX30102_REG_FIFO_RD_PTR, &read_ptr);

    uint8_t available = write_ptr - read_ptr;
    if (ovf > 0 || available > 16)
        return 16;   /* 溢出，假设 FIFO 已满 */
    return available & 0x1F;
}

HAL_StatusTypeDef BSP_MAX30102_ReadFIFO(MAX30102_Data_t *data)
{
    uint8_t buf[6];
    HAL_StatusTypeDef ret;

    /* 每次读取3字节×2通道=6字节 */
    ret = reg_burst_read(MAX30102_REG_FIFO_DATA, buf, 6);
    if (ret != HAL_OK) return ret;

    /* MAX30102 数据格式：高字节[18:16]在bit[17:16]，低字节在bit[15:8]，中字节在bit[7:0] */
    data->ir   = ((uint32_t)buf[0] << 16) |
                 ((uint32_t)buf[1] << 8)  |
                 ((uint32_t)buf[2]);
    data->red  = ((uint32_t)buf[3] << 16) |
                 ((uint32_t)buf[4] << 8)  |
                 ((uint32_t)buf[5]);

    return HAL_OK;
}

void BSP_MAX30102_EnableFIFOInterrupt(bool enable)
{
    uint8_t val;
    reg_read(MAX30102_REG_MODE_CONFIG, &val);
    if (enable) {
        val |= (1 << 5);   /* FIFO_OVF_IRQ_EN */
    } else {
        val &= ~(1 << 5);
    }
    reg_write(MAX30102_REG_MODE_CONFIG, val);
}
