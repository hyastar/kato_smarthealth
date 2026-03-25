/**
  ******************************************************************************
  * @file    bsp_usart.c
  * @brief   BSP USART1 驱动实现
  *
  * @note    采用方案：DMA 循环接收 + 空闲中断 + FreeRTOS Queue（整帧入队）
  *
  *          数据流：
  *            DMA(Circular) → dma_raw_buf[256]
  *                 ↓ 空闲中断触发
  *            整帧 memcpy  → rx_queue（每帧一次入队）
  *                 ↓
  *            BSP_USART1_Receive() → 业务任务
  *
  * @attention
  *   - 不重复初始化 CubeMX 已生成的 GPIO/DMA/UART 句柄
  *   - 直接使用 CubeMX 生成的 huart1, hdma_usart1_rx, hdma_usart1_tx
  *   - ISR 中整帧入队，避免逐字节调用 xQueueSendToBackFromISR
  *
  * @author  Kato
  * @date    2026-03-20
  ******************************************************************************
  */
#include "bsp_usart.h"
#include "usart.h"
#include "main.h"

#include "FreeRTOS.h"
#include "queue.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * CubeMX 生成的句柄（外部引用，不重复初始化）
 *============================================================================*/
extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef  hdma_usart1_rx;
extern DMA_HandleTypeDef  hdma_usart1_tx;
extern volatile bool usart1_tx_done;   /* 引用自 bsp_usart2.c */

/*============================================================================
 * 私有静态变量
 *============================================================================*/
/** DMA 循环接收缓冲区（裸缓冲区，DMA 直接写入） */
static uint8_t dma_raw_buf[USART1_DMA_BUF_SIZE];

/** FreeRTOS 帧队列（ISR → 任务） */
static QueueHandle_t rx_queue;

/** DMA 上次处理位置（用于检测新数据） */
static volatile uint16_t dma_last_pos = 0;

/*============================================================================
 * 内部函数声明
 *============================================================================*/
static void usart1_process_idle(BaseType_t *pxHigherPriorityTaskWoken);

/*============================================================================
 * BSP_USART1_Init()
 *============================================================================*/
/**
  * @brief  初始化 BSP USART1
  * @note   确保在此之前已调用 MX_USART1_UART_Init()
  *         此函数完成：
  *           1. 创建 FreeRTOS 队列
  *           2. 使能 UART IDLE 中断
  *           3. 启动 DMA 循环接收
  */
void BSP_USART1_Init(void)
{
    /* 1. 创建 FreeRTOS 帧队列 */
    rx_queue = xQueueCreate(USART1_QUEUE_DEPTH, sizeof(USART1_Frame_t));
    if (rx_queue == NULL)
    {
        /* 队列创建失败，死循环（实际应用中应调用 Error_Handler） */
        while (1);
    }

    /* 2. 重置 DMA 处理位置 */
    dma_last_pos = 0;

    /* 3. 使能空闲中断（注意：CubeMX 的 MSPInit 已配置 NVIC） */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);

    /* 4. 启动 DMA 循环接收（DMA 会持续写入 dma_raw_buf） */
    if (HAL_UART_Receive_DMA(&huart1, dma_raw_buf, USART1_DMA_BUF_SIZE) != HAL_OK)
    {
        while (1);
    }
}

/*============================================================================
 * BSP_USART1_IRQ_Handler()
 *============================================================================*/
/**
  * @brief  BSP USART1 中断处理函数入口
  * @note   此函数需要在 stm32f4xx_it.c 的 USART1_IRQHandler 中调用
  * @retval None
  */
void BSP_USART1_IRQ_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 先检测 IDLE 标志（某些 HAL 版本会在 IRQHandler 中清除） */
    uint8_t idle_detected = (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_IDLE) &&
                             __HAL_UART_GET_IT_SOURCE(&huart1, UART_IT_IDLE));

    if (idle_detected)
    {
        usart1_process_idle(&xHigherPriorityTaskWoken);
    }

    /* 处理其他 UART 中断（RXNE、TC、错误等） */
    HAL_UART_IRQHandler(&huart1);

    /* 如果有更高优先级任务被唤醒，请求调度 */
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*============================================================================
 * usart1_process_idle() - 内部函数
 *============================================================================*/
/**
  * @brief  处理空闲中断（内部调用）
  * @note   从 DMA 缓冲区提取新数据并整帧入队
  */
static void usart1_process_idle(BaseType_t *pxHigherPriorityTaskWoken)
{
    /* 清除 IDLE 标志 */
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);

    /* 计算当前 DMA 写入位置 */
    /* DMA CNDTR 倒计数：cur_pos = BufSize - CNDTR */
    uint16_t cur_pos = USART1_DMA_BUF_SIZE
                     - __HAL_DMA_GET_COUNTER(huart1.hdmarx);

    /* 无新数据，直接返回 */
    if (cur_pos == dma_last_pos)
    {
        return;
    }

    /* 构建帧结构体 */
    USART1_Frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* 计算数据长度，处理 DMA 回环 */
    if (cur_pos > dma_last_pos)
    {
        /* 情况1：正常（未回环）[dma_last_pos, cur_pos) */
        frame.len = cur_pos - dma_last_pos;
        if (frame.len > USART1_MAX_FRAME_SIZE)
        {
            frame.len = USART1_MAX_FRAME_SIZE;  /* 截断超长帧 */
        }
        memcpy(frame.data, &dma_raw_buf[dma_last_pos], frame.len);
    }
    else
    {
        /* 情况2：DMA 回环 [dma_last_pos, BUF_SIZE) + [0, cur_pos) */
        uint16_t seg1_len = USART1_DMA_BUF_SIZE - dma_last_pos;
        uint16_t seg2_len = cur_pos;
        frame.len = seg1_len + seg2_len;

        if (frame.len > USART1_MAX_FRAME_SIZE)
        {
            /* 超过最大帧长，丢弃旧数据，保留最新部分 */
            uint16_t overflow = frame.len - USART1_MAX_FRAME_SIZE;
            if (overflow < seg2_len)
            {
                /* 从 seg2 头部丢弃 */
                memcpy(frame.data, &dma_raw_buf[overflow], seg2_len - overflow);
                memcpy(&frame.data[seg2_len - overflow], dma_raw_buf, seg1_len);
                frame.len = USART1_MAX_FRAME_SIZE;
            }
            else
            {
                /* 全部丢弃，只保留最新部分 */
                overflow -= seg2_len;
                memcpy(frame.data, &dma_raw_buf[dma_last_pos + overflow], seg1_len - overflow);
                frame.len = seg1_len - overflow;
            }
        }
        else
        {
            /* 正常拷贝，回环场景 */
            memcpy(frame.data, &dma_raw_buf[dma_last_pos], seg1_len);
            memcpy(&frame.data[seg1_len], dma_raw_buf, seg2_len);
        }
    }

    /* 更新 DMA 处理位置 */
    dma_last_pos = cur_pos;

    /* 整帧一次入队（只调用一次 ISR 入队函数） */
    if (frame.len > 0)
    {
        xQueueSendToBackFromISR(rx_queue, &frame, pxHigherPriorityTaskWoken);
    }
}

/*============================================================================
 * BSP_USART1_Send()
 *============================================================================*/
/**
  * @brief  DMA 发送数据（阻塞等待完成）
  * @param  buf  待发送数据指针
  * @param  len  数据长度
  * @note   使用 DMA Normal 模式，发送完成前阻塞任务
  */
void BSP_USART1_Send(uint8_t *buf, uint16_t len)
{
    usart1_tx_done = false;

    HAL_UART_Transmit_DMA(&huart1, buf, len);

    /* 等待 DMA 发送完成 */
    while (!usart1_tx_done)
    {
        osDelay(1);
    }
}

/*============================================================================
 * BSP_USART1_Receive()
 *============================================================================*/
/**
  * @brief  从队列读取一帧数据
  * @param  frame   帧结构体指针，用于接收数据
  * @param  timeout 等待超时时间
  * @retval pdPASS  成功读取
  * @retval pdFAIL  超时
  */
BaseType_t BSP_USART1_Receive(USART1_Frame_t *frame, TickType_t timeout)
{
    return xQueueReceive(rx_queue, frame, timeout);
}

/*============================================================================
 * 轻量打印接口实现
 *============================================================================*/

/**
  * @brief  发送原始字节（轮询，调度器启动前后均可用）
  */
void BSP_USART1_Write(const uint8_t *buf, uint16_t len)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, len, HAL_MAX_DELAY);
}

/**
  * @brief  发送字符串
  */
void BSP_USART1_Print(const char *str)
{
    if (str == NULL) return;
    BSP_USART1_Write((const uint8_t *)str, (uint16_t)strlen(str));
}

/**
  * @brief  格式化打印（栈上固定 128 字节缓冲区，不支持浮点）
  */
void BSP_USART1_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;

    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        BSP_USART1_Write((const uint8_t *)buf, (uint16_t)len);
    }
}

/*============================================================================
 * DMA 发送完成回调（HAL 库会自动调用）
 *============================================================================*/
/**
 * @brief  DMA TX 完成回调
 * @note   已合并至 bsp_usart2.c 统一处理，本文件不再定义
 */
/*
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        dma_tx_done = true;
    }
}
*/
