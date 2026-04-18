/**
  ******************************************************************************
  * @file    bsp_ble.h
  * @brief   BLE 蓝牙模块驱动
  *
 * @note    硬件连接：
 *            BLE WUP    → PA5 (GPIO输出，正常工作时拉高)
 *            BLE STA    → PA4 (GPIO输入，检测连接状态)
 *            BLE TX/RX  → USART2 (PA2/PA3)
 *            BLE RELOAD → 已禁用（注释）
  *
  *          驱动说明：
  *            - PA4/PA5 控制蓝牙模块的唤醒和恢复出厂设置
  *            - 数据收发通过 USART2 (空闲中断+DMA) 实现，参见 bsp_ble2.h/c
  *            - 封装健康数据包的 JSON 打包逻辑
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#ifndef __BSP_BLE_H__
#define __BSP_BLE_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "SensorData.h"    /* 统一数据结构：心率/血氧/温湿度 */

/*============================================================================
 * BLE GPIO 控制宏（引脚已根据 Pin.md 适配）
 *============================================================================*/
#define BLE_WUP_PORT      GPIOA
#define BLE_WUP_PIN       GPIO_PIN_5   /* PA5 - 唤醒引脚 */
#define BLE_STA_PORT      GPIOA
#define BLE_STA_PIN       GPIO_PIN_4   /* PA4 - 连接状态检测 */

/* [已禁用] BLE RELOAD 引脚 - 暂时注释掉相关定义
 * #define BLE_RELOAD_PORT   GPIOA
 * #define BLE_RELOAD_PIN    GPIO_PIN_1
 */

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  初始化 BLE 控制 GPIO（PA5 WUP / PA4 STA）
 * @note   在 MX_GPIO_Init() 之后调用
 */
void BSP_BLE_Init(void);

/**
 * @brief  唤醒 BLE 模块（WUP 拉高）
 */
void BSP_BLE_Wakeup(void);

/**
 * @brief  进入 BLE 低功耗（WUP 拉低）
 */
void BSP_BLE_Sleep(void);

/**
 * @brief  恢复出厂设置（RELOAD 低脉冲 600ms）
 * @note   调用后会阻塞 700ms
 * @note   [已禁用] 此功能暂时注释，如需恢复请取消注释
 */
/* void BSP_BLE_ResetToFactory(void); */

/**
 * @brief  获取 BLE 连接状态
 * @retval true   已连接
 * @retval false 未连接
 */
bool BSP_BLE_IsConnected(void);

/**
 * @brief  打包健康数据为 JSON 字符串
 * @param  data     传感器数据
 * @param  json_buf 输出缓冲区
 * @param  buf_len 缓冲区大小（建议 >= 128 字节）
 * @return 生成的 JSON 字符串长度，<=0 表示失败
 * @note   输出格式：{"hr":72,"spo2":98,"temp":25.6,"hum":60.2}
 */
int BSP_BLE_PackSensorDataJSON(const SensorData_t *data, char *json_buf, size_t buf_len);

/**
 * @brief  打包健康数据为十六进制协议帧
 * @param  data    传感器数据
 * @param  frame   输出帧缓冲区（至少 20 字节）
 * @retval 帧长度
 * @note   帧格式：0xAA + 0x06 + len + hr + spo2 + temp(2B) + hum(2B) + crc8 + 0x55
 */
int BSP_BLE_PackSensorDataFrame(const SensorData_t *data, uint8_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BLE_H__ */
