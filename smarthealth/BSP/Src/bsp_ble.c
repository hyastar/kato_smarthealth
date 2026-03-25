/**
  ******************************************************************************
  * @file    bsp_ble.c
  * @brief   BLE 蓝牙模块驱动实现
  *
  * @note    功能说明：
  *            - GPIO 控制（PA4 WUP / PA5 RELOAD / PA1 STA）
  *            - 健康数据 JSON/十六进制帧打包
  *
  *          数据发送说明：
  *            - 实际串口发送由 Core 层的 usart2 DMA + 空闲中断实现
  *            - 本文件只负责数据打包，发送由 freertos.c 中的 BLETask 调用
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#include "bsp_ble.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * GPIO 初始化（静态，在 BSP_BLE_Init 中执行一次）
 *============================================================================*/

/**
 * @brief  初始化 BLE GPIO
 */
void BSP_BLE_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA4 WUP - 推挽输出，默认高电平（正常工作） */
    GPIO_InitStruct.Pin   = BLE_WUP_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BLE_WUP_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(BLE_WUP_PORT, BLE_WUP_PIN, GPIO_PIN_SET);   /* 默认唤醒 */

    /* PA5 RELOAD - 推挽输出，默认高电平 */
    GPIO_InitStruct.Pin   = BLE_RELOAD_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BLE_RELOAD_PORT, &GPIO_InitStruct);
    HAL_GPIO_WritePin(BLE_RELOAD_PORT, BLE_RELOAD_PIN, GPIO_PIN_SET);

    /* PA1 STA - 浮空输入，检测连接状态 */
    GPIO_InitStruct.Pin  = BLE_STA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BLE_STA_PORT, &GPIO_InitStruct);
}

void BSP_BLE_Wakeup(void)
{
    HAL_GPIO_WritePin(BLE_WUP_PORT, BLE_WUP_PIN, GPIO_PIN_SET);
}

void BSP_BLE_Sleep(void)
{
    HAL_GPIO_WritePin(BLE_WUP_PORT, BLE_WUP_PIN, GPIO_PIN_RESET);
}

void BSP_BLE_ResetToFactory(void)
{
    HAL_GPIO_WritePin(BLE_RELOAD_PORT, BLE_RELOAD_PIN, GPIO_PIN_RESET);
    HAL_Delay(700);   /* 保持低电平 > 500ms */
    HAL_GPIO_WritePin(BLE_RELOAD_PORT, BLE_RELOAD_PIN, GPIO_PIN_SET);
}

bool BSP_BLE_IsConnected(void)
{
    return HAL_GPIO_ReadPin(BLE_STA_PORT, BLE_STA_PIN) == GPIO_PIN_SET;
}

/*============================================================================
 * CRC8 校验（Maxim/Dallas 多项式 0x31）
 *============================================================================*/
static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

/*============================================================================
 * 数据打包
 *============================================================================*/

/**
 * @brief  打包健康数据为 JSON 字符串
 */
int BSP_BLE_PackSensorDataJSON(const SensorData_t *data, char *json_buf, size_t buf_len)
{
    if (data == NULL || json_buf == NULL || buf_len < 32)
        return -1;

    /* 安全截断浮点数：temp/hum 取小数点后1位，避免 JSON 过长 */
    int t_int = (int)(data->temperature * 10);
    int h_int = (int)(data->humidity * 10);

    return snprintf(json_buf, buf_len,
        "{\"hr\":%u,\"spo2\":%u,\"temp\":%d.%d,\"hum\":%d.%d}",
        data->heart_rate,
        data->spo2,
        t_int / 10, (t_int < 0 ? (-t_int) % 10 : t_int % 10),
        h_int / 10, (h_int < 0 ? (-h_int) % 10 : h_int % 10));
}

/**
 * @brief  打包健康数据为十六进制协议帧
 * @retval 帧长度（固定 14 字节）
 *
 * 帧格式：
 *   0xAA  帧头1
 *   0x06  协议版本
 *   0x0C  数据长度（本协议固定12字节payload）
 *   hr    心率 (1B)
 *   spo2  血氧 (1B)
 *   temp  温度×100转int16 (2B, big-endian)
 *   hum   湿度×100转int16 (2B, big-endian)
 *   seq   包序号 (1B)
 *   crc8  0xAA~seq 的CRC (1B)
 *   0x55  帧尾1
 */
static uint8_t s_seq = 0;

int BSP_BLE_PackSensorDataFrame(const SensorData_t *data, uint8_t *frame)
{
    if (data == NULL || frame == NULL)
        return -1;

    int16_t temp_x100 = (int16_t)(data->temperature * 100.0f);
    int16_t hum_x100  = (int16_t)(data->humidity * 100.0f);

    frame[0] = 0xAA;
    frame[1] = 0x06;
    frame[2] = 0x0C;
    frame[3] = data->heart_rate;
    frame[4] = data->spo2;
    frame[5] = (uint8_t)((temp_x100 >> 8) & 0xFF);  /* temp BE high */
    frame[6] = (uint8_t)(temp_x100 & 0xFF);          /* temp BE low  */
    frame[7] = (uint8_t)((hum_x100 >> 8) & 0xFF);   /* hum  BE high */
    frame[8] = (uint8_t)(hum_x100 & 0xFF);           /* hum  BE low  */
    frame[9] = s_seq++;
    frame[10] = crc8(&frame[0], 10);
    frame[11] = 0x55;

    return 12;
}
