/**
  ******************************************************************************
  * @file    sensor_task.h
  * @brief   传感器任务 - 算法与业务层
  *
  * @note    职责说明：
  *            1. 使用互斥锁保护 I2C1 读取 SHT30 + MAX30102
  *            2. 对原始数据进行滤波和心率/血氧计算
  *            3. 打包 SensorData_t 结构体并发送到 FreeRTOS 队列
  *
  *          FreeRTOS 资源：
  *            - 使用 I2C1_MutexHandle 互斥锁
  *            - 使用 SensorDataQueueHandle 消息队列（Item Size=16）
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#ifndef __SENSOR_TASK_H__
#define __SENSOR_TASK_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "SensorData.h"
#include "cmsis_os.h"

/*============================================================================
 * 任务配置
 *============================================================================*/
#define SENSOR_TASK_PERIOD_MS    100   /* 主循环周期 100ms = 10Hz */
#define SENSOR_TASK_STACK        512   /* 任务栈大小（字） */

/*============================================================================
 * 滤波算法参数
 *============================================================================*/
#define MAX30102_FILTER_SIZE     32    /* 滑动平均窗口大小（32点@10Hz≈3.2秒） */
#define SPO2_UPDATE_INTERVAL    10    /* 每10个样本更新一次血氧（1秒） */
#define HR_UPDATE_INTERVAL       5    /* 每5个样本更新一次心率（0.5秒） */

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  传感器任务入口（供 FreeRTOS 调用）
 * @param  argument  未使用
 */
void SensorTask_Entry(void *argument);

/**
 * @brief  获取最近一次有效传感器数据
 * @note   供 MonitorTask 等其他任务查询当前状态
 */
bool SensorTask_GetLatestData(SensorData_t *out_data);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_TASK_H__ */
