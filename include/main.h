/*!
    \file    main.h
    \brief   板级配置（引脚宏定义）

    注意：不同系列的外设库 API 可能不同：
        旧系列（C10x / F30x）：gpio_init(port, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, pin)
        新系列（F4xx）：      gpio_mode_set + gpio_output_options_set（复用引脚还需 gpio_af_set）
    编写外设代码前，请先参考该系列官方固件库的 Examples/ 目录，
    **请勿直接套用其他系列的代码**。
*/

#ifndef MAIN_H
#define MAIN_H

#include "gd32e23x.h"
#include "gd32e23x_libopt.h"

/* 示例（请按实际硬件修改）：
    #define LED_GPIO_PORT   GPIOC
    #define LED_PIN         GPIO_PIN_2
*/

#endif /* MAIN_H */
