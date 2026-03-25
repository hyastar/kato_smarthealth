/**
  ******************************************************************************
  * @file    bsp_usart2.h
  * @brief   BSP USART2 BLE 驱动 - 空闲中断 + DMA + FreeRTOS Queue
  *
  * @note    架构设计：
  *            DMA(Circular) → usart2_raw_buf[BUF_SIZE]
  *                 ↓ 空闲中断触发
  *            整帧 memcpy  → rx_queue（每帧一次入队）
  *                 ↓ xQueueReceive
  *            BLETask 处理手机指令
  *
  *          USART2 连接 BLE 模块（TX=PA2, RX=PA3）
  *
  * @author  Kato
  * @date    2026-03-25
  ******************************************************************************
  */
#ifndef __BSP_USART2_H__
#define __BSP_USART2_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"

#define USART2_DMA_BUF_SIZE   256
#define USART2_MAX_FRAME_SIZE 128
#define USART2_QUEUE_DEPTH    8

typedef struct {
    uint8_t  data[USART2_MAX_FRAME_SIZE];
    uint16_t len;
} USART2_Frame_t;

void BSP_USART2_Init(void);
void BSP_USART2_Send(uint8_t *buf, uint16_t len);
void BSP_USART2_SendString(const char *str);
BaseType_t BSP_USART2_Receive(USART2_Frame_t *frame, TickType_t timeout);
void BSP_USART2_IRQ_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_USART2_H__ */
