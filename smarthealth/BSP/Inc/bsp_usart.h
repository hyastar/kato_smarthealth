/**
  ******************************************************************************
  * @file    bsp_usart.h
  * @brief   BSP USART1 驱动 - DMA 循环接收 + 空闲中断 + FreeRTOS Queue
  *
  * @note    架构设计：
  *            DMA(Circular) → dma_raw_buf[256]
  *                 ↓ 空闲中断触发
  *            整帧 memcpy  → FreeRTOS Queue（每帧一条消息）
  *                 ↓ xQueueReceive 阻塞
  *            业务任务处理
  *
  *          特点：
  *            - 不重复初始化 CubeMX 已生成的 GPIO/DMA/UART
  *            - ISR 中整帧入队（非逐字节），保证中断响应效率
  *            - 帧结构体传递，解耦 DMA 缓冲区和业务层
  *
  * @author  Kato
  * @date    2026-03-20
  ******************************************************************************
  */
#ifndef __BSP_USART_H__
#define __BSP_USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Includes
 *============================================================================*/
#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "cmsis_os2.h"

/*============================================================================
 * 宏定义
 *============================================================================*/
/** DMA 原始缓冲区大小（必须为 2 的幂次以便优化） */
#define USART1_DMA_BUF_SIZE     256

/** 单帧最大字节数（Modbus RTU 典型帧最大 256 字节） */
#define USART1_MAX_FRAME_SIZE  128

/** 帧队列深度（可缓冲的未处理帧数量） */
#define USART1_QUEUE_DEPTH      8

/** 串口配置 */
#define USART1_BAUDRATE         115200

/*============================================================================
 * 类型定义
 *============================================================================*/
/**
 * @brief  USART1 接收帧结构体
 * @note   Queue 中存放的是帧结构体，而非单个字节
 */
typedef struct {
    uint8_t  data[USART1_MAX_FRAME_SIZE];  /*!< 帧数据缓冲区 */
    uint16_t len;                           /*!< 实际帧长度 */
} USART1_Frame_t;

/*============================================================================
 * 函数声明
 *============================================================================*/

/**
 * @brief  BSP USART1 初始化
 * @note   调用此函数前，确保 CubeMX 已调用 MX_USART1_UART_Init()
 *         此函数只做：创建队列、使能 IDLE 中断、启动 DMA 接收
 */
void BSP_USART1_Init(void);

/**
 * @brief  USART1 DMA 发送（阻塞等待完成）
 * @param  buf  待发送数据指针
 * @param  len  数据长度
 * @note   使用 DMA Normal 模式，发送完成前会阻塞任务
 */
void BSP_USART1_Send(uint8_t *buf, uint16_t len);

/**
 * @brief  从接收队列读取一帧数据
 * @param  frame   帧结构体指针，用于接收数据
 * @param  timeout 等待超时时间（portMAX_DELAY 表示无限等待）
 * @retval pdPASS   成功读取一帧
 * @retval pdFAIL   超时无数据
 */
BaseType_t BSP_USART1_Receive(USART1_Frame_t *frame, TickType_t timeout);

/**
 * @brief  BSP USART1 中断处理函数入口
 * @note   此函数需要在 stm32f4xx_it.c 的 USART1_IRQHandler 中调用
 */
void BSP_USART1_IRQ_Handler(void);

/*============================================================================
 * 轻量打印接口（可选使用）
 *============================================================================*/

/**
 * @brief  发送原始字节数据（轮询方式）
 * @param  buf  数据指针
 * @param  len  长度
 */
void BSP_USART1_Write(const uint8_t *buf, uint16_t len);

/**
 * @brief  发送字符串
 * @param  str  字符串指针
 */
void BSP_USART1_Print(const char *str);

/**
 * @brief  格式化打印（不支持浮点，最大 128 字节）
 * @param  fmt  格式化字符串
 * @param  ...  可变参数
 */
void BSP_USART1_Printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USART_H__ */
