/*!
    \file    systick.c
    \brief   1ms 系统滴答延时（在 SysTick 中断里递减计数）

    用的是 CMSIS 的 SysTick_Config()，不依赖任何外设库 API，各系列通用。
*/

#include "systick.h"

static volatile uint32_t delay;

/*!
    \brief      初始化 SysTick，产生 1000Hz (1ms) 中断
*/
void systick_config(void)
{
    if(0U != SysTick_Config(SystemCoreClock / 1000U)) {
        /* 配置失败：卡死等待调试 */
        while(1) {
        }
    }
    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

/*!
    \brief      阻塞延时
    \param[in]  count: 延时毫秒数
*/
void delay_1ms(uint32_t count)
{
    delay = count;
    while(0U != delay) {
    }
}

/*!
    \brief      SysTick 中断里调用，递减延时计数
*/
void delay_decrement(void)
{
    if(0U != delay) {
        delay--;
    }
}
