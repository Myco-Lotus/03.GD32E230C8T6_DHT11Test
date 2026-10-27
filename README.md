# GD32E230C8T6_DHT11Test

基于 **GD32E230C8T6**（Cortex-M23，64K Flash / 8K RAM）的环境监测小工程：
**UART 计算器 + DHT11 温湿度采集 + SSD1306 OLED 中文显示**。

- arm-none-eabi-gcc + CMake + Ninja 构建，调试走 pyOCD（GD32 DevKit 扩展驱动）；
- 串口发 `3+5` 回 `3 + 5 = 8`，支持 `+ - * /`（整数除法）；
- DHT11 每 2s 采样一次，串口回传中文（UTF-8）：`温度:24℃ 湿度:39%RH`；
- OLED 同步显示：黄区 `温度:xx℃`，蓝区 `湿度:xx %RH`（16x16 汉字点阵）。

> 不同系列的外设库 API 并不统一，移植时请勿直接套用（见下文「编写外设代码前请注意」）。

## 功能一览

| 模块 | 说明 |
|---|---|
| UART 计算器 | 115200-8N1；收到一行算式立即回传结果；解析失败静默丢弃（过载安全） |
| DHT11 温湿度 | PB10 单总线；2s 独立节拍；读取全程开中断，串口收发零丢失 |
| OLED 显示 | SSD1306 0.96" I2C（PB6/PB7 模拟 I2C）；黄蓝双色分区；中文点阵 |
| LED2 | PC13；每收到一条完整命令非阻塞闪烁 50ms |

## 硬件接线

| 外设 | 引脚 | 接开发板 | 备注 |
|---|---|---|---|
| DHT11 | VCC / DATA / GND | 3V3 / **PB10** / GND | DATA 需 4.7k~10kΩ 上拉到 VCC（三针模块已内置可省） |
| OLED | GND / VCC / SCL / SDA | GND / 3V3 / **PB6** / **PB7** | 模块自带上拉；背面 ADRESS SELECT 默认地址 0x3C，若被焊成 0x3D 需同步改 `oled.c` 的 `OLED_ADDR_W` |
| USB-TTL | RX / TX / GND | **PA9(MCU-TX)** / **PA10(MCU-RX)** / GND | 与 MCU 交叉连接 |
| LED2 | — | PC13 | 板载 |

## 使用说明

1. 串口助手设 **115200，8N1**，编码选 **UTF-8**（固件回传为 UTF-8 中文）。
   为什么不是 9600：115200 是按「PC 以 10ms 周期连发算式」的带宽预算选定的
   （72MHz ÷ 115200 = 625 整分频，波特率零误差）；
2. 发送算式（回车结尾，可带空格）：`3+5`、`100/7`、`-4*6`；
   - 除法向零取整；`1/0` 回 `ERR: div by zero`；格式错误的行静默忽略；
   - 温湿度每 2s 一行 `温度:24℃ 湿度:39%RH`；计算流量再大也不打乱该周期，
     冲突时先答计算、温湿度紧随其后；
3. OLED：上电先显示 `温度:--℃`、`湿度:-- %RH` 占位，2s 后填入真值；
   对着 DHT11 哈气，2s 内数值跟变。

## 快速开始

```bash
cmake --preset debug       # 配置
cmake --build --preset debug   # 编译
```

（上述命令在 Windows / macOS / Linux 上通用。）

在 VS Code 中：点**底部状态栏**的四个按钮 —— `项目名`、`编译`、`烧录`（只烧不调试）、`调试`。
按钮由 **GD32 DevKit** 扩展提供，按项目绝对路径执行，不依赖项目里的配置文件；
无论单独打开本项目、还是打开上层目录（多项目容器），四个按钮都能用。

不装扩展时，等价命令：

```bash
pyocd flash --target gd32e230c8t6 build/debug/GD32E230C8T6_DHT11Test.elf
```

> 状态栏只放常用三项；`编译（Release）` 与 `清理构建目录` 在命令面板搜 `GD32`。

## 目录结构

```
GD32E230C8T6_DHT11Test/
├─ CMakeLists.txt        项目定义（含新增源文件登记）
├─ .gd32-project.json    标记文件（状态栏按钮靠它认出本项目）
├─ CMakePresets.json     debug / release 预设
├─ cmake/gcc-arm-none-eabi.cmake   工具链（cortex-m23）
├─ include/
│  ├─ main.h             板级配置：引脚宏定义
│  ├─ gd32e23x_libopt.h
│  ├─ systick.h
│  ├─ dht11.h            DHT11 驱动接口
│  ├─ oled.h             SSD1306 驱动接口
│  └─ oledfont.h         字库：8x16 ASCII 子集 + 16x16 汉字（温/度/湿/℃）
├─ src/
│  ├─ main.c             应用主体：计算器 + 2s 温湿度节拍 + OLED 刷新
│  ├─ dht11.c            DHT11 单总线驱动
│  ├─ oled.c             SSD1306 驱动（PB6/PB7 模拟 I2C）
│  ├─ systick.c          1ms 滴答延时
│  ├─ gd32e23x_it.c      中断服务函数
│  └─ system_gd32e23x.c  系统时钟配置
├─ sdk/GD32E23x.svd
└─ build/                构建产物（自动生成，不入库）
```

## 软件设计要点

- **中断只入队**：USART ISR（约 2~3us）仅把字节塞进 256B 的 SPSC 无锁环形缓冲；
  拼行、解析、发送全部在主循环单线程完成，任意两行应答永不交织；
- **整行重同步**：接收缓冲满时从最旧端丢弃到行尾，宁可整行丢弃也不产生
  「缝合行」—— 过载下不出错误运算、不出碎片 ERR；
- **节拍解耦**：温湿度 2s 节拍独立计时，计算流量无权复位或推迟它；
- **DHT11 读取期间开中断**：单总线位宽容差 ≥23us，远大于 ISR 耗时，
  读取窗口内串口收发零丢失；单次读取失败静默跳过，下一拍自动重试；
- **字库按需取模**：64K Flash 放不下全字库，只存 温/度/湿/℃ 四个 16x16 汉字
  与常用 ASCII；「逐行式」汉字字模经转置算法映射到 SSD1306 页寻址的
  「列字节」格式后上屏；
- **时序预算**：DHT11 单次读取 ≈24ms（起始 19ms + 40bit ≈ 4ms），
  OLED 单次刷新 ≈10ms，合计占 2s 节拍不足 2%。

## 编写外设代码前请注意

**不同系列的外设库 API 可能不同**，请勿直接套用其他系列的代码。已知两套 GPIO 写法：

```c
/* 旧系列（GD32C10x / GD32F30x 等） */
gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

/* 新系列（GD32E230 属于这类） */
gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_2);
gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
```

**推荐做法**：参考本固件库 `Examples/` 目录中现成的初始化代码。

## 系统时钟

当前方案：**__SYSTEM_CLOCK_72M_PLL_IRC8M_DIV2**

- 方案列表来自该系列自身的 `system_gd32e23x.c`，并非硬编码；
- 使用 `HXTAL` 的方案要求目标硬件提供对应频率的外部晶振；
- 如需修改：编辑 `src/system_gd32e23x.c`，将需要的 `#define __SYSTEM_CLOCK_xxx`
  取消注释，其余全部注释（**只能保留一个**）。

## 关键编译参数

- 芯片宏：`GD32E230`（该系列头文件强制要求，不传会直接 `#error`）；
- 启动文件：`startup_gd32e23x.S`；
- 链接脚本：`gd32e230x8_flash.ld`。

## 调试

1. SWD 连接：`SWDIO / SWCLK / GND`（建议同时接 `NRST`）；
2. 点状态栏 `调试`（或命令面板搜 `GD32: 调试当前项目`）；
3. 调试会话自动执行：编译 → 烧录 → 停在 `main()`。

> 支持任何 **CMSIS-DAP** 类调试器（PWLINK、GD-Link、DAPLink…）。
> 注意：新版 ST-Link 固件仅允许访问 STM32，对 GD32 会拒绝连接，请勿使用。
