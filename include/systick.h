/*!
    \file    systick.h
    \brief   1ms 系统滴答延时
*/

#ifndef SYSTICK_H
#define SYSTICK_H

#include "gd32e23x.h"

void systick_config(void);
void delay_1ms(uint32_t count);
void delay_decrement(void);

#endif /* SYSTICK_H */
