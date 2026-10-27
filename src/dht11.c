/*! \file dht11.c \brief DHT11 单总线驱动（GD32E230, GPIO 模拟时序）
    时序依据官方手册：起始拉低>=18ms -> 释放20-40us -> 传感器80us低+80us高
    -> 40bit（每帧50us低 + 26-28us高=0 / 70us高=1）-> 前4字节和校验 */
#include "gd32e23x.h"
#include "dht11.h"
#include "systick.h"

/* ---------- us 级延时：读 SysTick 当前值递减计数，不碰 delay_1ms 的中断机制 ---------- */
static void delay_us(uint32_t us)
{
    uint32_t need = us * (SystemCoreClock / 1000000U);  /* 72MHz -> 1us = 72 tick */
    uint32_t last = SysTick->VAL;
    uint32_t gone = 0U;
    while (gone < need) {
        uint32_t now = SysTick->VAL;
        if (now <= last) { gone += last - now; }                              /* 正常递减 */
        else { gone += last + (SysTick->LOAD + 1U) - now; }                   /* 跨周期回绕 */
        last = now;
    }
}

/* ---------- 数据线方向切换（单总线：主机与传感器分时驱动同一根线）---------- */
static void dht11_out(void)
{
    gpio_mode_set(DHT11_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, DHT11_PIN);
    gpio_output_options_set(DHT11_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, DHT11_PIN);
}
static void dht11_in(void)
{
    gpio_mode_set(DHT11_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, DHT11_PIN);
}
#define DHT_H()   gpio_bit_set(DHT11_PORT, DHT11_PIN)
#define DHT_L()   gpio_bit_reset(DHT11_PORT, DHT11_PIN)
#define DHT_R()   gpio_input_bit_get(DHT11_PORT, DHT11_PIN)

void dht11_init(void)
{
    rcu_periph_clock_enable(DHT11_RCC);
    dht11_out();
    DHT_H();                              /* 空闲态：上拉为高 */
}

/* ---------- 读 1 位：等 50us 低结束 -> 延 40us 采样（高=1 / 低=0）-> 等高结束 ---------- */
static uint8_t dht_read_bit(uint8_t *err)
{
    uint32_t tmo;
    tmo = 200000U;  while ((DHT_R() == RESET) && (--tmo)) { }   /* 等低电平结束 */
    if (0U == tmo) { *err = 1U; return 0U; }
    delay_us(40U);                                              /* 26-28us 与 70us 的分界点 */
    uint8_t bit = (DHT_R() != RESET) ? 1U : 0U;
    tmo = 200000U;  while ((DHT_R() != RESET) && (--tmo)) { }   /* 等本位高电平结束 */
    if (0U == tmo) { *err = 1U; return 0U; }
    return bit;
}

static uint8_t dht_read_byte(uint8_t *err)
{
    uint8_t i, b = 0U;
    for (i = 0U; i < 8U; i++) {                     /* 高位先出 */
        b = (uint8_t)((b << 1) | dht_read_bit(err));
        if (*err) { return 0U; }
    }
    return b;
}

/* ======================= DHT11 单次读取时序预算 =======================
 *  [阶段1] 主机起始：拉低 19ms（手册要求 >=18ms）→ 释放总线转输入上拉
 *  [阶段2] 传感器响应：20~40us 内拉低 80us → 拉高 80us（"我在"握手）
 *  [阶段3] 40bit 数据：每位 = 50us 低电平间隔 + 高电平段
 *          高电平 26~28us = "0"；70us = "1"（以 40us 处采样区分，两边余量 >=23us）
 *          合计 40 × (76~120us) ≈ 3.0~4.8ms
 *  单次读取总耗时 ≈ 24ms；手册要求两次读取间隔 >=1s（本工程 2s，充裕）
 *  中断策略：全程开中断 —— USART ISR 仅 2~3us，远小于位宽容差，
 *  PC 同时发车字节照常入环形缓冲，零丢失（v7 已验证）。              */
uint8_t dht11_read(uint8_t *humi_int, uint8_t *humi_dec,
                   uint8_t *temp_int, uint8_t *temp_dec)
{
    uint8_t   e = 0U, r = 0U;
    uint8_t   hi = 0U, hd = 0U, ti = 0U, td = 0U, chk = 0U;
    uint32_t  tmo;

    /* 起始信号（约 19ms）*/
    dht11_out();
    DHT_L();
    delay_1ms(19U);
    DHT_H();
    dht11_in();

    /* 响应 + 40bit：中断保持开启，串口接收零丢失 */
    do {
        tmo = 200000U; while ((DHT_R() != RESET) && (--tmo)) { }
        if (0U == tmo) { r = 1U; break; }
        tmo = 200000U; while ((DHT_R() == RESET) && (--tmo)) { }
        if (0U == tmo) { r = 1U; break; }
        tmo = 200000U; while ((DHT_R() != RESET) && (--tmo)) { }
        if (0U == tmo) { r = 1U; break; }

        hi = dht_read_byte(&e); if (e) { r = 2U; break; }
        hd = dht_read_byte(&e); if (e) { r = 2U; break; }
        ti = dht_read_byte(&e); if (e) { r = 2U; break; }
        td = dht_read_byte(&e); if (e) { r = 2U; break; }
        chk = dht_read_byte(&e); if (e) { r = 2U; break; }

        if ((uint8_t)(hi + hd + ti + td) != chk) { r = 3U; }
    } while (0);

    if (0U == r) {
        *humi_int = hi; *humi_dec = hd; *temp_int = ti; *temp_dec = td;
    }
    return r;
}

