# GD32E230C8T6_UartTest

基于 **GD32E230C8T6**（cortex-m23，64K Flash / 8K RAM）的**空项目框架**。
由 **GD32 DevKit** 扩展创建，使用 arm-none-eabi-gcc + CMake + Ninja，调试走 pyOCD。

> 框架中**不含任何外设代码**：`main()` 仅初始化 1ms 系统滴答，随后进入空循环。
> 不同系列的外设库 API 并不统一，固定使用某一种会导致在其他系列上编译失败。

## 快速开始

```
cmake --preset debug          # 配置
cmake --build --preset debug  # 编译
```

（上述命令在 Windows / macOS / Linux 上通用；`#` 后面是注释。）

在 VS Code 中：点**底部状态栏**的四个按钮 —— `项目名`（点击可切换项目）、`编译`、`烧录`（只烧不调试）、`调试`。
按钮由 **GD32 DevKit** 扩展提供，它按项目绝对路径执行，**不依赖项目里的配置文件**，
所以无论你单独打开本项目、还是打开上层目录（多项目容器），这四个按钮都能用。

> 项目里**不再自带** `.vscode/tasks.json` 与 `.vscode/launch.json`：
> 一旦项目被当作子目录打开（上层目录才是工作区），这两个文件根本不会生效，
> 留着只会让人误以为可用。编译 / 烧录 / 调试统一走上边的按钮；
> 不装扩展时，用本节开头的 `cmake` 命令 + `pyocd flash --target gd32e230c8t6 build/debug/GD32E230C8T6_UartTest.elf` 也能完成同样的事。
> 状态栏只放常用的三项；`编译（Release）` 与 `清理构建目录` 在命令面板里搜 `GD32` 即可。

## 目录结构

```
GD32E230C8T6_UartTest/
├─ CMakeLists.txt                 项目定义
├─ .gd32-project.json             标记文件（状态栏按钮靠它认出本项目，删掉则不显示按钮）
├─ CMakePresets.json              debug / release 预设
├─ cmake/gcc-arm-none-eabi.cmake  工具链（cortex-m23）
├─ include/
│  ├─ main.h                      板级配置：引脚宏定义
│  ├─ gd32e23x_libopt.h
│  └─ systick.h
├─ src/
│  ├─ main.c                      ← 在此编写你的代码
│  ├─ systick.c                   1ms 滴答延时
│  └─ gd32e23x_it.c               中断服务函数
├─ .vscode/                       settings / c_cpp_properties / extensions（不含任务与调试配置）
└─ build/                         构建产物（自动生成）
```
## 用 VS Code 打开本项目

- **单独打开本项目文件夹**（推荐）：项目自带的 .vscode/c_cpp_properties.json 直接生效，
  IntelliSense 不会飘红，CMake Tools 也都正常。
- 若打开的是**上层目录**（多项目容器）：编辑器只读根目录的 .vscode/c_cpp_properties.json，
  项目里这份会被忽略；创建时若检测到这种情况，扩展会把本项目的 include 与固件库目录写进根目录
  那份配置里名为 GD32 的配置（原有条目保留）。若根目录那份不是本扩展写的（或不是合法 JSON），
  则一个字都不改，只在创建完成的页面上给出提示。
- 无论哪种情况，编译 / 烧录 / 调试都用状态栏那四个按钮（见上）。




本项目还额外生成了：
- `src/system_gd32e23x.c`
- `sdk/GD32E23x.svd`

## 编写外设代码前请注意

**不同系列的外设库 API 可能不同**，请勿直接套用其他系列的代码。已知两套 GPIO 写法：

```c
/* 旧系列（GD32C10x / GD32F30x 等） */
gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

/* 新系列（GD32F4xx 等） */
gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_2);
gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
```

**推荐做法**：参考本固件库 `Examples/` 目录中现成的初始化代码：

```
<固件库>/Examples/
```

## 系统时钟

当前方案：**__SYSTEM_CLOCK_72M_PLL_IRC8M_DIV2**

- 方案列表来自该系列自身的 `system_gd32e23x.c`，并非硬编码
- 使用 `HXTAL` 的方案要求目标硬件提供对应频率的外部晶振
- 如需修改：编辑 `src/system_gd32e23x.c`，将需要使用的
  `#define __SYSTEM_CLOCK_xxx` 取消注释，其余全部注释（**只能保留一个**）

## 关键编译参数

- 芯片宏：`GD32E230`（该系列头文件可能强制要求，不传会直接 `#error`）
- 启动文件：`startup_gd32e23x.S`
- 链接脚本：`gd32e230x8_flash.ld`

## 调试

1. 使用 SWD 连接：`SWDIO / SWCLK / GND`（建议同时连接 `NRST`）；
2. 点状态栏的 `调试`（或命令面板搜 `GD32: 调试当前项目`）；
3. 调试会话将自动执行：编译 → 烧录 → 停在 `main()`。

> 支持的调试器：任何 **CMSIS-DAP** 类（PWLINK、GD-Link、DAPLink…）。
> 注意：新版 ST-Link 固件仅允许访问 STM32，对 GD32 会拒绝连接，请勿使用。
