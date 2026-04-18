/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    freertos.c
  * @brief   FreeRTOS 应用程序 - 任务实现
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "sensor_task.h"
#include "bsp_ble.h"
#include "bsp_usart2.h"
#include "SensorData.h"
#include "cmsis_os2.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define BLE_ALERT_BUZZER_TIMEOUT_MS  5000   /* 报警持续5秒后自动停止 */
#define BLE_SEND_INTERVAL_MS          2000   /* 每2秒发送一次蓝牙数据 */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for MonitorTask */
osThreadId_t MonitorTaskHandle;
const osThreadAttr_t MonitorTask_attributes = {
  .name = "MonitorTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for SensorTask */
osThreadId_t SensorTaskHandle;
const osThreadAttr_t SensorTask_attributes = {
  .name = "SensorTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for BLETask */
osThreadId_t BLETaskHandle;
const osThreadAttr_t BLETask_attributes = {
  .name = "BLETask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for SensorDataQueue */
osMessageQueueId_t SensorDataQueueHandle;
const osMessageQueueAttr_t SensorDataQueue_attributes = {
  .name = "SensorDataQueue"
};
/* Definitions for I2C1_Mutex */
osMutexId_t I2C1_MutexHandle;
const osMutexAttr_t I2C1_Mutex_attributes = {
  .name = "I2C1_Mutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void StartTask02_Monitor(void *argument);
void StartTask03_Sensor(void *argument);
void StartTask04_BLE(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Create the mutex(es) */
  /* creation of I2C1_Mutex */
  I2C1_MutexHandle = osMutexNew(&I2C1_Mutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of SensorDataQueue */
  /* Item Size = 16 字节（sizeof(SensorData_t)=10 字节，留6字节余量） */
  SensorDataQueueHandle = osMessageQueueNew (8, 16, &SensorDataQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of MonitorTask */
  /* [已禁用] MonitorTask - 暂时注释掉，避免因未连接心率传感器导致误报警
   * MonitorTaskHandle = osThreadNew(StartTask02_Monitor, NULL, &MonitorTask_attributes);
   */

  /* creation of SensorTask */
  SensorTaskHandle = osThreadNew(StartTask03_Sensor, NULL, &SensorTask_attributes);

  /* creation of BLETask */
  BLETaskHandle = osThreadNew(StartTask04_BLE, NULL, &BLETask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
 * @brief  MonitorTask - 监控异常并触发蜂鸣器报警
 * @param  argument: Not used
 * @retval None
 *
 * 逻辑说明：
 *   - [已禁用] 此任务已被休眠，不再运行
 *   - 原逻辑：阻塞等待 SensorDataQueue 数据，若心率或血氧异常，拉高 PB0 触发蜂鸣器
 *   - 如需恢复，请删除 vTaskSuspend(NULL);
 */
/* USER CODE END Header_StartTask02 */
void StartTask02_Monitor(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  (void)argument;

  /* 任务创建后立即休眠，不再运行 */
  vTaskSuspend(NULL);

  /* 以下为原逻辑（已禁用）：
  SensorData_t data;
  TickType_t buzz_end_tick = 0;

  for(;;)
  {
    if (osMessageQueueGet(SensorDataQueueHandle, &data, NULL,
                           pdMS_TO_TICKS(2000)) == osOK)
    {
      if (SensorData_IsAlert(&data))
      {
        HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
        buzz_end_tick = xTaskGetTickCount()
                      + pdMS_TO_TICKS(BLE_ALERT_BUZZER_TIMEOUT_MS);
      }
    }

    if (buzz_end_tick > 0 && xTaskGetTickCount() >= buzz_end_tick)
    {
      HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
      buzz_end_tick = 0;
    }
  }
  */
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
  * @brief  SensorTask - 直接调用 Algorithm 层的 SensorTask_Entry
  * @param  argument: Not used
  * @retval None
  *
  * 逻辑说明：
  *   - 此任务是 FreeRTOS 线程入口，调用 algorithm/sensor_task.c 中的
  *     SensorTask_Entry() 实现
  *   - 传感器读取 → 滤波 → 计算心率/血氧 → 入队
  */
/* USER CODE END Header_StartTask03 */
void StartTask03_Sensor(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  SensorTask_Entry(argument);
  /* 不应返回 */
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
  * @brief  BLETask - 接收传感器数据并通过蓝牙发送
  * @param  argument: Not used
  * @retval None
  *
  * 逻辑说明：
  *   - 阻塞等待 SensorDataQueue 数据
  *   - 调用 bsp_ble.c 的打包函数，将数据转为 JSON 或十六进制帧
  *   - 通过 BSP_USART2_Send 发送到手机
  *   - 同时处理手机下发的 BLE 指令（通过 USART2 接收队列）
  */
/* USER CODE END Header_StartTask04 */
void StartTask04_BLE(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
  (void)argument;
  SensorData_t sensor_data;
  USART2_Frame_t cmd_frame;
  // uint8_t ble_frame[20];
  char json_buf[128];
  TickType_t last_send_tick = 0;
  const TickType_t send_interval = pdMS_TO_TICKS(BLE_SEND_INTERVAL_MS);

  /* 初始化 BLE GPIO */
  BSP_BLE_Init();
  BSP_BLE_Wakeup();

  /* 初始化 USART2 DMA + 空闲中断接收 */
  BSP_USART2_Init();

  /* 等待 BLE 连接 */
  while (!BSP_BLE_IsConnected())
  {
    osDelay(500);
  }

  for(;;)
  {
    /* ── 接收手机指令（超时50ms）── */
    if (BSP_USART2_Receive(&cmd_frame, pdMS_TO_TICKS(50)) == pdPASS)
    {
      /* 简单指令解析（示例） */
      if (cmd_frame.len > 0) {
        /* TODO: 实现具体蓝牙指令解析逻辑 */
      }
    }

    /* ── 发送传感器数据（固定间隔）── */
    if (xTaskGetTickCount() - last_send_tick >= send_interval)
    {
      if (SensorTask_GetLatestData(&sensor_data))
      {
        /* 优先使用 JSON 格式（可读性好） */
        int json_len = BSP_BLE_PackSensorDataJSON(&sensor_data, json_buf, sizeof(json_buf));
        if (json_len > 0) {
          BSP_USART2_SendString(json_buf);
          BSP_USART2_SendString("\r\n");
        }
      }
      last_send_tick = xTaskGetTickCount();
    }

    osDelay(10);
  }
  /* USER CODE END StartTask04 */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
