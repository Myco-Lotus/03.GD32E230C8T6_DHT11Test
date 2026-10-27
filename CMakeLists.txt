cmake_minimum_required(VERSION 3.20)

# ============================================================
#  GD32E230C8T6_DHT11Test
#  目标芯片: GD32E230C8T6  (cortex-m23, 64K Flash / 8K RAM)
#  由 "GD32 DevKit" 扩展生成
# ============================================================
project(GD32E230C8T6_DHT11Test LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ------------------------------------------------------------
# 官方固件库
# ------------------------------------------------------------
set(GD32_SDK "${CMAKE_CURRENT_SOURCE_DIR}/sdk/GD32E23x_Firmware_Library"
    CACHE PATH "GD32 固件库根目录")

if(NOT EXISTS "${GD32_SDK}/Firmware")
  message(FATAL_ERROR
    "找不到 GD32 固件库: ${GD32_SDK}\n"
    "请修改 GD32_SDK，或在 VS Code 的 GD32 面板中重新检测环境。")
endif()

set(GD32_CMSIS  "${GD32_SDK}/Firmware/CMSIS")
set(GD32_DEVICE "${GD32_CMSIS}/GD/GD32E23x")
set(GD32_PERIPH "${GD32_SDK}/Firmware/GD32E23x_standard_peripheral")

# 链接脚本：项目内有就用项目内的（可自定义内存布局），否则用 SDK 的
set(GD32_LDSCRIPT "${GD32_DEVICE}/Source/GCC/Ld/gd32e230x8_flash.ld" CACHE FILEPATH "链接脚本")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/linker/gd32e230x8_flash.ld")
  set(GD32_LDSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/gd32e230x8_flash.ld")
  message(STATUS "使用项目内的链接脚本: linker/gd32e230x8_flash.ld")
endif()

# 系统时钟：项目内有自己的 system 文件就用它（可自定义时钟方案）
set(GD32_SYSTEM_SRC "${GD32_DEVICE}/Source/system_gd32e23x.c")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/system_gd32e23x.c")
  set(GD32_SYSTEM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/system_gd32e23x.c")
  message(STATUS "使用项目内的 ${systemName}（时钟: __SYSTEM_CLOCK_72M_PLL_IRC8M_DIV2）")
endif()

# ------------------------------------------------------------
# 应用源文件
# ------------------------------------------------------------
add_executable(${PROJECT_NAME}
  src/main.c
  src/systick.c
  src/gd32e23x_it.c
  src/dht11.c
  src/oled.c
  # CMSIS: 系统初始化 + GCC 启动文件 + newlib 系统调用桩
  ${GD32_SYSTEM_SRC}
  ${GD32_DEVICE}/Source/GCC/startup_gd32e23x.S
  ${GD32_DEVICE}/Source/GCC/newlib/syscalls.c
)

# 标准外设库全部源文件（用不到的会被 --gc-sections 裁掉）
file(GLOB GD32_PERIPH_SRC CONFIGURE_DEPENDS ${GD32_PERIPH}/Source/*.c)
target_sources(${PROJECT_NAME} PRIVATE ${GD32_PERIPH_SRC})

# Windows + CMAKE_SYSTEM_NAME=Generic 时 CMake 默认不给产物加后缀，
# 强制产出 .elf：调试器需要它读取符号表
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

# ------------------------------------------------------------
# 头文件路径
# ------------------------------------------------------------
target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${GD32_CMSIS}
  ${GD32_DEVICE}/Include
  ${GD32_PERIPH}/Include
)

# ------------------------------------------------------------
# 宏定义
# ------------------------------------------------------------
# 来自 partinfo 的解析结果，各系列要求不一样：
#   家族宏 GD32F30X / 器件码宏 GD32F450 / 密度宏 GD32F30X_HD
# 缺哪个都可能让头文件直接 #error，所以一并传上（多余的宏没影响）。
target_compile_definitions(${PROJECT_NAME} PRIVATE
  GD32E230
  $<$<CONFIG:Debug>:DEBUG>
)

target_compile_options(${PROJECT_NAME} PRIVATE
  $<$<CONFIG:Debug>:-Og>
  $<$<CONFIG:Debug>:-g3>
  $<$<CONFIG:Release>:-Os>
)

# ------------------------------------------------------------
# 链接
# ------------------------------------------------------------
target_link_options(${PROJECT_NAME} PRIVATE
  -T${GD32_LDSCRIPT}
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
  --specs=nano.specs
  # 关掉 binutils 2.39+ 对链接脚本 RWX 段的噪音警告
  -Wl,--no-warn-rwx-segments
)

# ------------------------------------------------------------
# 生成 .hex / .bin 并打印固件大小
# ------------------------------------------------------------
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
  COMMENT "生成 ${PROJECT_NAME}.hex / ${PROJECT_NAME}.bin"
)
