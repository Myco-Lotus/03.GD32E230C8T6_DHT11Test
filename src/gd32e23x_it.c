/*!
    \file    gd32e23x_it.c
    \brief   中断服务函数（向量表里的弱定义在这里被覆盖）

    这里只处理 CMSIS 标准的内核异常和 SysTick。
    外设中断请自己加：函数名必须和启动文件里的向量表名字完全一致。
*/

#include "gd32e23x.h"
#include "systick.h"

/* Cortex-M 内核异常 */

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
    /* 出错后停在这里，方便用调试器看调用栈 */
    while(1) {
    }
}

void MemManage_Handler(void)
{
    while(1) {
    }
}

void BusFault_Handler(void)
{
    while(1) {
    }
}

void UsageFault_Handler(void)
{
    while(1) {
    }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/* 系统滴答：供 delay_1ms() 使用 */
void SysTick_Handler(void)
{
    delay_decrement();
}
