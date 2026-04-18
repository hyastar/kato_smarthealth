### SHT30 → STM32

|SHT30|STM32|丝印|
|---|---|---|
|VCC|3.3V|`3.3`|
|GND|GND|`G`|
|SCL|PB6|`B6`|
|SDA|PB7|`B7`|

### 正点原子BLE 5.4 → STM32（USART2）

|BLE引脚|STM32|丝印|说明|
|---|---|---|---|
|VCC|3.3V|`3.3`|—|
|GND|GND|`G`|—|
|RXD|PA2|`A2`|USART2_TX，**MCU发→模块收**|
|TXD|PA3|`A3`|USART2_RX，**模块发→MCU收**|
|STA|PA4|`A4`|GPIO输入，检测连接状态|
|WUP|PA5|`A5`|GPIO输出，正常工作保持高电平|