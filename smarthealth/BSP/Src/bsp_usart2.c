/**
  ******************************************************************************
  * @file    bsp_usart2.c
  * @brief   BSP USART2 BLE 驱动实现（空闲中断 + DMA + FreeRTOS Queue）
  ******************************************************************************
  */
#include "bsp_usart2.h"
#include "usart.h"
#include "main.h"
#include "cmsis_os2.h"  // 解决 osDelay 找不到的问题

#include "FreeRTOS.h"
#include <string.h>

extern UART_HandleTypeDef huart2;
extern DMA_HandleTypeDef  hdma_usart2_rx;
extern DMA_HandleTypeDef  hdma_usart2_tx;

static uint8_t dma_raw_buf[USART2_DMA_BUF_SIZE];
static QueueHandle_t rx_queue;
static volatile uint16_t dma_last_pos = 0;
static volatile bool dma_tx_done = true;
volatile bool usart1_tx_done = true;  /* USART1 发送完成标志（bsp_usart.c 共用）*/

static void usart2_process_idle(BaseType_t *pxHigherPriorityTaskWoken);

void BSP_USART2_Init(void)
{
    rx_queue = xQueueCreate(USART2_QUEUE_DEPTH, sizeof(USART2_Frame_t));
    if (rx_queue == NULL) while (1);

    dma_last_pos = 0;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);

    if (HAL_UART_Receive_DMA(&huart2, dma_raw_buf, USART2_DMA_BUF_SIZE) != HAL_OK)
        while (1);
}

void BSP_USART2_IRQ_Handler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint8_t idle_detected = (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_IDLE) &&
                             __HAL_UART_GET_IT_SOURCE(&huart2, UART_IT_IDLE));
    if (idle_detected)
        usart2_process_idle(&xHigherPriorityTaskWoken);

    HAL_UART_IRQHandler(&huart2);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static void usart2_process_idle(BaseType_t *pxHigherPriorityTaskWoken)
{
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);

    uint16_t cur_pos = USART2_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);

    if (cur_pos == dma_last_pos) return;

    USART2_Frame_t frame;
    memset(&frame, 0, sizeof(frame));

    if (cur_pos > dma_last_pos) {
        frame.len = cur_pos - dma_last_pos;
        if (frame.len > USART2_MAX_FRAME_SIZE) frame.len = USART2_MAX_FRAME_SIZE;
        memcpy(frame.data, &dma_raw_buf[dma_last_pos], frame.len);
    } else {
        uint16_t seg1_len = USART2_DMA_BUF_SIZE - dma_last_pos;
        uint16_t seg2_len = cur_pos;
        frame.len = seg1_len + seg2_len;
        if (frame.len > USART2_MAX_FRAME_SIZE) frame.len = USART2_MAX_FRAME_SIZE;
        memcpy(frame.data, &dma_raw_buf[dma_last_pos], seg1_len);
        memcpy(&frame.data[seg1_len], dma_raw_buf, seg2_len);
    }

    dma_last_pos = cur_pos;

    if (frame.len > 0)
        xQueueSendToBackFromISR(rx_queue, &frame, pxHigherPriorityTaskWoken);
}

void BSP_USART2_Send(uint8_t *buf, uint16_t len)
{
    dma_tx_done = false;
    HAL_UART_Transmit_DMA(&huart2, buf, len);
    while (!dma_tx_done) osDelay(1);
}

void BSP_USART2_SendString(const char *str)
{
    if (str == NULL) return;
    BSP_USART2_Send((uint8_t *)str, (uint16_t)strlen(str));
}

BaseType_t BSP_USART2_Receive(USART2_Frame_t *frame, TickType_t timeout)
{
    return xQueueReceive(rx_queue, frame, timeout);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        dma_tx_done = true;
    }
    else if (huart->Instance == USART1)
    {
        usart1_tx_done = true;
    }
}
