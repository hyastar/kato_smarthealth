#include "bsp_sht30.h"
#include "FreeRTOS.h"
#include "task.h"

/* CRC8校验（SHT30标准多项式0x31） */
static uint8_t sht30_crc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ?
                  (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

HAL_StatusTypeDef BSP_SHT30_Init(void)
{
    return HAL_I2C_IsDeviceReady(SHT30_I2C,
                                  SHT30_ADDR, 3, 100);
}

/**
 * @brief 读取温度和湿度
 * @param temperature 输出温度（°C）
 * @param humidity    输出湿度（%RH）
 */
HAL_StatusTypeDef BSP_SHT30_Read(float *temperature,
                                  float *humidity)
{
    HAL_StatusTypeDef ret;

    /* 发送单次测量命令 */
    uint8_t cmd[2];
    cmd[0] = (SHT30_CMD_MEAS_H >> 8) & 0xFF;
    cmd[1] =  SHT30_CMD_MEAS_H       & 0xFF;

    ret = HAL_I2C_Master_Transmit(
        SHT30_I2C, SHT30_ADDR, cmd, 2, 100);
    if (ret != HAL_OK) return ret;

    /* 等待测量完成（高精度约15ms） */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 读6字节：温度(2)+CRC(1)+湿度(2)+CRC(1) */
    uint8_t buf[6];
    ret = HAL_I2C_Master_Receive(
        SHT30_I2C, SHT30_ADDR, buf, 6, 100);
    if (ret != HAL_OK) return ret;

    /* CRC校验 */
    if (sht30_crc(&buf[0], 2) != buf[2]) return HAL_ERROR;
    if (sht30_crc(&buf[3], 2) != buf[5]) return HAL_ERROR;

    /* 转换公式（SHT30数据手册） */
    uint16_t raw_t = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t raw_h = ((uint16_t)buf[3] << 8) | buf[4];

    *temperature = -45.0f + 175.0f * (float)raw_t / 65535.0f;
    *humidity    = 100.0f * (float)raw_h / 65535.0f;

    return HAL_OK;
}
