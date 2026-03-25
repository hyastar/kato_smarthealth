你为项目规划的这套代码架构（Core 驱动层 -> BSP 应用层 -> Algorithm 算法业务层）非常清晰，完全符合标准的工业级嵌入式开发规范。这种分层设计能极大提高代码的可读性和可移植性。

为了让 Cursor 准确无误地理解你的意图，并完美融合我们之前配置的 FreeRTOS、互斥锁、消息队列和空闲中断 DMA，你需要给它一个**“上帝视角”的架构级指令**。

请直接将以下这段包含完整背景、架构规范和文件级任务分配的超级指令复制并发送给 Cursor（建议开启 Cursor 的 Composer 功能或者在一个新的 Chat 中发送，并利用 @ 功能把提到的文件引入上下文）：

📋 复制以下完整指令给 Cursor：
Markdown
# 角色与任务
你是一个资深的 STM32 嵌入式系统架构师和 C 语言开发专家。请帮我补全当前基于 STM32F411CEU6 和 FreeRTOS (CMSIS_V2) 的健康体征监测系统代码。

# 架构与分层规范
本项目严格遵循以下三层架构，请你在编写代码时绝对遵守：
1. **Core 文件夹 (驱动层)**：由 STM32CubeMX 自动生成。你只能在 `/* USER CODE BEGIN */` 和 `/* USER CODE END */` 之间添加代码，绝对不能修改已有生成的初始化代码，否则重新生成时会丢失。
2. **BSP 文件夹 (应用支持层)**：包含 `bsp_sht30.c/.h`、`bsp_max30102.c/.h`、`bsp_usart.c/.h`、`bsp_ble.c/.h`。这里只负责封装具体外设的读写时序和硬件控制逻辑，**不要**在这里面直接调用 FreeRTOS 的 API（如延时或互斥锁），以保证底层驱动的纯粹性。
3. **algorithm 文件夹 (算法与业务层)**：包含 `sensor_task.c/.h`。负责具体 RTOS 任务的逻辑调度、传感器数据平滑滤波算法，以及互斥锁和队列的管理。

# 核心数据结构 (全局统一)
请在相应的头文件中（如 `main.h` 的 USER CODE 区，或新建一个 `global_def.h` 并被全局引用）统一定义以下数据结构：
```c
typedef struct {
    uint8_t  heart_rate;  // 心率 (bpm)
    uint8_t  spo2;        // 血氧饱和度 (%)
    float    temperature; // 环境温度 (℃)
    float    humidity;    // 环境湿度 (%)
} SensorData_t; // 该结构体数据将被存入 Item Size 为 16 字节的 FreeRTOS 队列中
各模块详细编写指南与要求
1. BSP 层开发 (位于 BSP/Src/ 和 BSP/Inc/)
bsp_sht30.c：

基于 HAL_I2C 编写 SHT30 的单次高精度测量驱动。

实现函数 uint8_t SHT30_Read_TempHum(float *temp, float *hum)。

bsp_max30102.c：

基于 HAL_I2C 编写 MAX30102 的初始化和 FIFO 读取逻辑。

实现初始化函数配置为 SpO2 模式，实现函数读取红光(Red)和红外(IR)的原始 AC/DC 数据。

bsp_ble.c：

提供蓝牙模块的控制接口，如通过控制 PA4 (WUP) 和 PA5 (RELOAD) 的高低电平来实现复位或唤醒。

封装一帧健康数据包的打包逻辑（如转换为 JSON 或特定帧头帧尾的十六进制协议）。

bsp_usart.c：

可留空或用于存放特定指令的解析函数，串口基础收发依赖 Core 层的 HAL 库回调。

2. Algorithm 层开发 (位于 algorithm/Src/ 和 algorithm/Inc/)
sensor_task.c：

算法实现：针对 MAX30102 读取到的红光/红外原始数据，实现一个简单的滑动平均平滑滤波算法（Moving Average Filter），并计算出心率 heart_rate 和血氧 spo2。

任务逻辑封装：实现一个供 FreeRTOS 调用的函数（如 void SensorTask_Entry(void)）。

RTOS 资源管理：在该任务循环中，必须严格使用 osMutexAcquire(I2C1MutexHandle, osWaitForever) 和 osMutexRelease(I2C1MutexHandle) 来包裹对 bsp_sht30.c 和 bsp_max30102.c 读取函数的调用，防止 I2C1 冲突。

队列发送：读取并滤波得到 4 个有效数据后，打包进 SensorData_t 结构体，通过 osMessageQueuePut(SensorDataQueueHandle, &data, 0, 0) 发送。

3. Core 层适配 (位于 Core/Src/)
main.c / usart.c：

在 main.c 的 USER CODE 区实现 类似usart_send函数实现 使用空闲中断+dma+环形缓冲区。

在 main.c 初始化 RTOS 前，调用 HAL_UARTEx_ReceiveToIdle_DMA(&huart2, ble_rx_buffer, BUFFER_SIZE) 开启 USART2 的空闲中断+DMA 接收。

stm32f4xx_it.c (或 main.c 底部的回调区)：

实现 HAL_UARTEx_RxEventCallback，当 USART2 触发空闲中断时，通过 FreeRTOS 消息队列将收到的蓝牙指令发送给应用层处理，并再次重启 DMA 接收。

freertos.c：

在 CubeMX 生成的 StartTask03 (SensorTask) 壳子中，直接调用你刚才在 sensor_task.c 中写的 SensorTask_Entry()。

在 StartTask04 (BLETask) 中，实现阻塞等待 SensorDataQueueHandle 队列的数据，收到数据后调用 bsp_ble.c 的打包函数，通过 USART2 将数据发给手机。

在 StartTask02 (MonitorTask) 中，判断队列数据，若心率血氧异常，拉高 PB0 触发蜂鸣器报警。

请你先消化以上架构规范。然后，请按顺序一步步为我输出 bsp_sht30.c/.h、bsp_max30102.c/.h、sensor_task.c/.h 以及在 main.c 和 freertos.c 中需要添加的核心代码片段。确保代码健壮，包含必要的注释。