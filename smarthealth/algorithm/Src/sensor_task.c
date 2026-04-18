/**
  ******************************************************************************
  * @file    sensor_task.c
  * @brief   传感器任务 - 算法与业务层
  *
  * @note    架构层级：Algorithm（算法与业务层）
  *
  *          功能说明：
  *            1. 每 100ms 循环一次，读取 SHT30 和 MAX30102
  *            2. 使用 I2C1_MutexHandle 互斥锁包裹所有 I2C 读写操作
  *            3. 对 MAX30102 原始数据进行滑动平均滤波
  *            4. 计算心率（HR）和血氧（SpO2）
  *            5. 温湿度每 2 秒更新一次（功耗优化）
  *            6. 打包 SensorData_t 通过 SensorDataQueueHandle 发送
  *
  *          数据流：
  *            MAX30102_FIFO[32] → 原始IR/Red
  *                   ↓ 滑动平均滤波 (Moving Average Filter)
  *            滤波后IR/Red → 心率/血氧计算
  *                   ↓
  *            SensorData_t → osMessageQueuePut(SensorDataQueueHandle)
  *                   ↓
  *            BLETask（发送蓝牙）
  *            MonitorTask（异常报警）
  *
  * @attention
  *   - 本文件属于 Algorithm 层，可调用 FreeRTOS API
  *   - I2C 读写必须在持有 I2C1_MutexHandle 的前提下进行
  *   - SENSOR_TASK_PERIOD_MS 必须与 FreeRTOS 任务调度周期匹配
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#include "sensor_task.h"
#include "bsp_sht30.h"
#include "bsp_max30102.h"
#include "cmsis_os.h"
#include <string.h>
#include <math.h>

/*============================================================================
 * FreeRTOS 句柄（由 freertos.c 创建）
 *============================================================================*/
extern osMutexId_t I2C1_MutexHandle;
extern osMessageQueueId_t SensorDataQueueHandle;

/*============================================================================
 * 滤波算法 - 滑动平均滤波器
 *============================================================================*/
typedef struct {
    uint32_t ir_buf[MAX30102_FILTER_SIZE];   /* IR 环形缓冲区 */
    uint32_t red_buf[MAX30102_FILTER_SIZE];   /* Red 环形缓冲区 */
    uint16_t head;       /* 写入位置 */
    uint16_t count;      /* 已填充样本数 */
    float    ir_ac;      /* IR 交流成分（最大值-最小值）/2 */
    float    red_ac;     /* Red 交流成分 */
    float    ir_dc;      /* IR 直流成分（均值） */
    float    red_dc;     /* Red 直流成分 */
} MovingAverageFilter_t;

static void Filter_Init(MovingAverageFilter_t *f)
{
    memset(f, 0, sizeof(*f));
}

static void Filter_AddSample(MovingAverageFilter_t *f, uint32_t ir, uint32_t red)
{
    f->ir_buf[f->head]  = ir;
    f->red_buf[f->head] = red;
    f->head = (f->head + 1) % MAX30102_FILTER_SIZE;
    if (f->count < MAX30102_FILTER_SIZE)
        f->count++;

    if (f->count >= MAX30102_FILTER_SIZE) {
        uint32_t ir_min = 0xFFFFFFFF, ir_max = 0, red_min = 0xFFFFFFFF, red_max = 0;
        uint32_t ir_sum = 0, red_sum = 0;
        for (int i = 0; i < MAX30102_FILTER_SIZE; i++) {
            uint32_t iv = f->ir_buf[i];
            uint32_t rv = f->red_buf[i];
            if (iv < ir_min) ir_min = iv;
            if (iv > ir_max) ir_max = iv;
            if (rv < red_min) red_min = rv;
            if (rv > red_max) red_max = rv;
            ir_sum  += iv;
            red_sum += rv;
        }
        f->ir_dc  = (float)ir_sum  / MAX30102_FILTER_SIZE;
        f->red_dc = (float)red_sum / MAX30102_FILTER_SIZE;
        f->ir_ac  = ((float)(ir_max - ir_min)) / 2.0f;
        f->red_ac = ((float)(red_max - red_min)) / 2.0f;
    }
}

/*============================================================================
 * SpO2 计算（Maxim 公式）
 *
 * R = (AC_IR / DC_IR) / (AC_Red / DC_Red)
 * SpO2 = -45.060 * R^2 + 30.354 * R + 94.845
 *============================================================================*/
static uint8_t calculate_SpO2(float ir_ac, float ir_dc, float red_ac, float red_dc)
{
    if (ir_dc < 1.0f || red_dc < 1.0f || red_ac < 1.0f)
        return 0;

    float R = (ir_ac / ir_dc) / (red_ac / red_dc);
    float spo2 = -45.060f * R * R + 30.354f * R + 94.845f;

    if (spo2 > 100.0f) spo2 = 100.0f;
    if (spo2 < 0.0f)   spo2 = 0.0f;

    return (uint8_t)(spo2 + 0.5f);
}

/*============================================================================
 * 心率计算（峰值检测）
 *
 * 每 HR_UPDATE_INTERVAL 个样本进行一次心率估算：
 *   - 找 IR 交流信号的最大值点
 *   - 计算相邻峰值间的时间间隔 → BPM
 *============================================================================*/
#define HR_PEAK_BUF_SIZE  16   /* 存储最近16个峰值位置 */

typedef struct {
    uint32_t ir_buf[HR_UPDATE_INTERVAL];  /* 原始 IR 数据 */
    uint16_t pos_buf[HR_PEAK_BUF_SIZE];    /* 峰值位置 */
    uint8_t  head;
    uint8_t  count;
    uint8_t  last_hr;
} HRDetector_t;

static void HR_Init(HRDetector_t *d)
{
    memset(d, 0, sizeof(*d));
    d->last_hr = 0;
}

static uint8_t HR_Update(HRDetector_t *d, uint32_t ir_value, uint8_t sample_idx)
{
    d->ir_buf[sample_idx] = ir_value;

    if (sample_idx == 0) {
        /* 检测 HR_UPDATE_INTERVAL 区间内的峰值位置 */
        uint8_t peaks = 0;
        // uint16_t prev_pos = 0;
        for (int i = 1; i < HR_UPDATE_INTERVAL - 1; i++) {
            if (d->ir_buf[i] > d->ir_buf[i-1] && d->ir_buf[i] > d->ir_buf[i+1]) {
                d->pos_buf[d->head % HR_PEAK_BUF_SIZE] = i;
                d->head++;
                d->count++;
                peaks++;
            }
        }
        if (peaks >= 2) {
            /* 计算平均峰间距 → 心率 */
            uint8_t n = (d->head >= 2) ? 2 : d->head;
            uint8_t start = (d->head >= n) ? (d->head - n) : 0;
            uint32_t interval_sum = 0;
            for (uint8_t i = start + 1; i < d->head; i++) {
                interval_sum += d->pos_buf[i % HR_PEAK_BUF_SIZE] - d->pos_buf[(i-1) % HR_PEAK_BUF_SIZE];
            }
            float avg_interval = (float)interval_sum / (n - 1);
            if (avg_interval > 0) {
                float bpm = 6000.0f / (avg_interval * SENSOR_TASK_PERIOD_MS);
                if (bpm >= 40.0f && bpm <= 200.0f) {
                    d->last_hr = (uint8_t)(bpm + 0.5f);
                }
            }
        }
    }
    return d->last_hr;
}

/*============================================================================
 * 静态变量
 *============================================================================*/
static MovingAverageFilter_t g_filter;
static HRDetector_t          g_hr;
static SensorData_t          g_latest_data;
static bool                  g_data_valid = false;

/*============================================================================
 * 温湿度读取（带互斥锁）
 *============================================================================*/
static HAL_StatusTypeDef read_sht30(float *temp, float *hum)
{
    HAL_StatusTypeDef ret = HAL_ERROR;

    if (osMutexAcquire(I2C1_MutexHandle, osWaitForever) == osOK) {
        ret = BSP_SHT30_Read(temp, hum);
        osMutexRelease(I2C1_MutexHandle);
    }
    return ret;
}

/*============================================================================
 * MAX30102 读取（带互斥锁）
 *============================================================================*/
static HAL_StatusTypeDef read_max30102(MAX30102_Data_t *data)
{
    HAL_StatusTypeDef ret = HAL_ERROR;

    if (osMutexAcquire(I2C1_MutexHandle, osWaitForever) == osOK) {
        ret = BSP_MAX30102_ReadFIFO(data);
        osMutexRelease(I2C1_MutexHandle);
    }
    return ret;
}

/*============================================================================
 * 传感器初始化（带互斥锁）
 * @note    [已简化] 仅初始化 SHT30，MAX30102 已禁用
 *============================================================================*/
static HAL_StatusTypeDef init_sensors(void)
{
    HAL_StatusTypeDef ret = HAL_ERROR;

    if (osMutexAcquire(I2C1_MutexHandle, osWaitForever) == osOK) {
        ret = HAL_OK;
        if (BSP_SHT30_Init() != HAL_OK) {
            ret = HAL_ERROR;
        }
        /* [已禁用] MAX30102 初始化
         * if (BSP_MAX30102_Init() != HAL_OK) {
         *     ret = HAL_ERROR;
         * }
         */
        osMutexRelease(I2C1_MutexHandle);
    }
    return ret;
}

/*============================================================================
 * SensorTask_Entry - 主任务入口
 * @note    [已简化] 仅读取 SHT30 心率/血氧已禁用（固定为0）
 *============================================================================*/
void SensorTask_Entry(void *argument)
{
    (void)argument;

    TickType_t last_wake = xTaskGetTickCount();
    float      temperature = 25.0f, humidity = 60.0f;
    float      last_temp = 25.0f, last_hum = 60.0f;
    uint8_t    th_update_cntdown = 20;  /* 前2秒先读一次温湿度 */

    /* [已禁用] MAX30102 滤波器和心率检测器初始化
     * Filter_Init(&g_filter);
     * HR_Init(&g_hr);
     */

    /* 初始化传感器 */
    if (init_sensors() != HAL_OK) {
        /* 传感器初始化失败，任务挂起等待 */
        vTaskSuspend(NULL);
    }

    /* 先读取一次温湿度作为初始值 */
    if (read_sht30(&last_temp, &last_hum) == HAL_OK) {
        temperature = last_temp;
        humidity    = last_hum;
    }

    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));

        /* ── 每2秒更新一次温湿度 ── */
        if (th_update_cntdown == 0) {
            if (read_sht30(&temperature, &humidity) != HAL_OK) {
                temperature = last_temp;  /* 失败时保持上次数值 */
                humidity    = last_hum;
            } else {
                last_temp = temperature;
                last_hum  = humidity;
            }
            th_update_cntdown = 200 / SENSOR_TASK_PERIOD_MS;  /* 20 × 100ms = 2000ms */
        }
        th_update_cntdown--;

        /* ── 打包并发送（心率/血氧固定为0）── */
        SensorData_t data;
        data.heart_rate  = 0;   /* [已禁用] MAX30102 心率 */
        data.spo2        = 0;   /* [已禁用] MAX30102 血氧 */
        data.temperature = temperature;
        data.humidity    = humidity;

        /* 非阻塞发送，队列满时丢弃旧数据 */
        osMessageQueuePut(SensorDataQueueHandle, &data, 0, 0);

        /* 更新全局最新数据 */
        g_latest_data = data;
        g_data_valid  = true;
    }
}

/* [已禁用] 以下函数暂时保留但不被调用，如需恢复 MAX30102 请取消注释
 *
 * ── MAX30102 读取 ──
static HAL_StatusTypeDef read_max30102(MAX30102_Data_t *data)
{
    HAL_StatusTypeDef ret = HAL_ERROR;
    if (osMutexAcquire(I2C1_MutexHandle, osWaitForever) == osOK) {
        ret = BSP_MAX30102_ReadFIFO(data);
        osMutexRelease(I2C1_MutexHandle);
    }
    return ret;
}
 *
 * ── 滑动平均滤波 ──
static void Filter_AddSample(MovingAverageFilter_t *f, uint32_t ir, uint32_t red)
{
    ...
}
 *
 * ── 血氧计算 ──
static uint8_t calculate_SpO2(float ir_ac, float ir_dc, float red_ac, float red_dc)
{
    ...
}
 *
 * ── 心率更新 ──
static uint8_t HR_Update(HRDetector_t *d, uint32_t ir_value, uint8_t sample_idx)
{
    ...
}
 */

/*============================================================================
 * 获取最新传感器数据
 *============================================================================*/
bool SensorTask_GetLatestData(SensorData_t *out_data)
{
    if (out_data == NULL) return false;
    if (!g_data_valid)    return false;
    *out_data = g_latest_data;
    return true;
}
