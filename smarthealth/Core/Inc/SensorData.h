/**
  ******************************************************************************
  * @file    SensorData.h
  * @brief   全局统一数据结构定义
  *
  * @note    本文件定义了传感器数据结构的统一定义，
  *            贯穿 Core → BSP → Algorithm 三层架构。
  *            所有引用本项目代码的文件都应包含此头文件。
  *
  *          数据流：
  *            BSP 层读取原始数据
  *              ↓ (互斥锁保护 I2C)
  *            Algorithm 层计算心率/血氧/温湿度
  *              ↓
  *            SensorData_t 结构体通过 FreeRTOS 队列发送
  *              ↓
  *            BLETask/MonitorTask 消费数据
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#ifndef __SENSOR_DATA_H__
#define __SENSOR_DATA_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 统一传感器数据结构
 *============================================================================*/
/**
 * @brief  传感器数据结构
 * @note   Item Size = 1 + 1 + 4 + 4 = 10 字节
 *         实际队列 Item Size 配置为 16 字节（留扩展余量）
 */
typedef struct {
    uint8_t  heart_rate;    /* 心率 (bpm)，0=无效 */
    uint8_t  spo2;           /* 血氧饱和度 (%)，0=无效 */
    float    temperature;   /* 环境温度 (℃) */
    float    humidity;       /* 环境湿度 (%RH) */
} SensorData_t;

/*============================================================================
 * 异常阈值定义
 *============================================================================*/
#define SPO2_MIN_NORMAL    95    /* 血氧下限，正常不应低于此值 */
#define SPO2_MILD_LOW      90    /* 轻度低血氧 */
#define HR_MIN_NORMAL      50    /* 心率下限（静息） */
#define HR_MAX_NORMAL      120   /* 心率上限（运动后正常） */
#define HR_BRADYCARDIA     40    /* 心动过缓阈值 */
#define HR_TACHYCARDIA     150   /* 心动过速阈值 */

#define TEMP_HYPOTHERMIA   35.0f  /* 低体温阈值 */
#define TEMP_NORMAL_LOW    36.0f  /* 正常体温下限 */
#define TEMP_NORMAL_HIGH   37.2f  /* 正常体温上限（口温） */
#define TEMP_FEVER         38.0f  /* 发热阈值 */
#define TEMP_HYPERTHERMIA  39.5f  /* 高热阈值 */

/*============================================================================
 * 辅助函数
 *============================================================================*/

/**
 * @brief  判断血氧是否正常
 */
static inline bool SensorData_SpO2Normal(const SensorData_t *d)
{
    return d->spo2 >= SPO2_MIN_NORMAL;
}

/**
 * @brief  判断心率是否正常（静息状态）
 */
static inline bool SensorData_HRNormal(const SensorData_t *d)
{
    return d->heart_rate >= HR_MIN_NORMAL && d->heart_rate <= HR_MAX_NORMAL;
}

/**
 * @brief  判断心率是否异常（心动过速/过缓）
 */
static inline bool SensorData_HRAlert(const SensorData_t *d)
{
    return d->heart_rate < HR_BRADYCARDIA || d->heart_rate > HR_TACHYCARDIA;
}

/**
 * @brief  判断体温是否正常
 */
static inline bool SensorData_TempNormal(const SensorData_t *d)
{
    return d->temperature >= TEMP_NORMAL_LOW && d->temperature <= TEMP_NORMAL_HIGH;
}

/**
 * @brief  判断是否需要报警（任一指标异常）
 */
static inline bool SensorData_IsAlert(const SensorData_t *d)
{
    return !SensorData_SpO2Normal(d)
        || !SensorData_HRNormal(d)
        || !SensorData_TempNormal(d);
}

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_DATA_H__ */
