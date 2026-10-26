# ============================================================
#  arm-none-eabi-gcc 交叉编译工具链
#  目标芯片: GD32E230C8T6  (cortex-m23)
# ============================================================

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX "arm-none-eabi-" CACHE STRING "工具链前缀")

# 如果 arm-none-eabi-gcc 没有加入 PATH，取消注释并改成实际路径
# （下面三行只是示例，留下你所在系统那一行即可）
# Windows: set(TOOLCHAIN_BIN_DIR "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/14.2 rel1/bin")
# macOS:   set(TOOLCHAIN_BIN_DIR "/opt/homebrew/bin")        # brew --cask gcc-arm-embedded
# Linux:   set(TOOLCHAIN_BIN_DIR "/usr/bin")

if(DEFINED TOOLCHAIN_BIN_DIR)
  set(_TC "${TOOLCHAIN_BIN_DIR}/")
else()
  set(_TC "")
endif()

if(WIN32)
  set(_SUFFIX ".exe")
else()
  set(_SUFFIX "")
endif()

set(CMAKE_C_COMPILER   "${_TC}${TOOLCHAIN_PREFIX}gcc${_SUFFIX}")
set(CMAKE_CXX_COMPILER "${_TC}${TOOLCHAIN_PREFIX}g++${_SUFFIX}")
set(CMAKE_ASM_COMPILER "${_TC}${TOOLCHAIN_PREFIX}gcc${_SUFFIX}")
set(CMAKE_OBJCOPY      "${_TC}${TOOLCHAIN_PREFIX}objcopy${_SUFFIX}" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_TC}${TOOLCHAIN_PREFIX}size${_SUFFIX}"    CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${_TC}${TOOLCHAIN_PREFIX}objdump${_SUFFIX}" CACHE FILEPATH "")

# 工具链只用来链接，不做宿主机可执行文件探测
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 注意: 不要在这里设 CMAKE_EXECUTABLE_SUFFIX，会被平台模块覆盖。
# 产物后缀在 CMakeLists.txt 里用 set_target_properties(... SUFFIX ".elf") 设置。

# ---- cortex-m23（无 FPU） ----
set(MCU_FLAGS "-mcpu=cortex-m23 -mthumb -mfloat-abi=soft")

set(CMAKE_C_FLAGS_INIT          "${MCU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_ASM_FLAGS_INIT        "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS}")
