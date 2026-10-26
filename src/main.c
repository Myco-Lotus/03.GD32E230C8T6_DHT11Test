/*!
    \file    main.c
    \brief   GD32E230C8T6 UART 计算器 v4 —— 静默待机
             - 收到一条完整消息（回车结尾）→ LED2 闪一下 + 给出计算应答
             - 平时无任何输出，LED2 熄灭
*/

#include "gd32e23x.h"
#include "systick.h"
#include <stdio.h>
#include <stdarg.h>

/* ---------- 接收环形缓冲 ---------- */
#define RX_BUF_SIZE  64U
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0U;
static volatile uint16_t rx_tail = 0U;

/* ---------- 行缓冲 ---------- */
#define LINE_BUF_SIZE  32U
static char    line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0U;

static void uart_printf(const char *fmt, ...)
{
    char    buf[128];
    va_list args;
    int     len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }
    for (int i = 0; i < len && i < (int)sizeof(buf) - 1; i++) {
        usart_data_transmit(USART0, (uint8_t)buf[i]);
        while (RESET == usart_flag_get(USART0, USART_FLAG_TBE)) {
        }
    }
}

/* USART0 接收中断 */
void USART0_IRQHandler(void)
{
    if (RESET != usart_interrupt_flag_get(USART0, USART_INT_FLAG_RBNE)) {
        uint8_t  ch   = (uint8_t)usart_data_receive(USART0);
        uint16_t next = (uint16_t)((rx_head + 1U) % RX_BUF_SIZE);
        if (next != rx_tail) {
            rx_buf[rx_head] = ch;
            rx_head = next;
        }
    }
    if (RESET != usart_flag_get(USART0, USART_FLAG_ORERR)) {
        usart_data_receive(USART0);
    }
}

/* LED2：PC13，高电平点亮。平时熄灭 */
static void led_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_13);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    gpio_bit_reset(GPIOC, GPIO_PIN_13);          /* 上电 = 灭 */
}

/* 闪一下：亮 ms 毫秒后自动熄灭 */
static void led_flash(uint32_t ms)
{
    gpio_bit_set(GPIOC, GPIO_PIN_13);
    delay_1ms(ms);
    gpio_bit_reset(GPIOC, GPIO_PIN_13);
}

/* 计算型应答（同 v3）*/
static void process_line(const char *s)
{
    int  a, b;
    char op;

    if (sscanf(s, " %d %c %d", &a, &op, &b) == 3) {
        int r;
        switch (op) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/':
            if (0 == b) { uart_printf("ERR: div by zero\r\n"); return; }
            r = a / b;
            break;
        default:
            uart_printf("ERR: only + - * / (got '%c')\r\n", op);
            return;
        }
        uart_printf("%d %c %d = %d\r\n", a, op, b, r);
    } else {
        uart_printf("ERR: send like 3+5 (got \"%s\")\r\n", s);
    }
}

static void usart0_init(uint32_t baud)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);

    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_9);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_9);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_10);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_10);

    usart_deinit(USART0);
    usart_baudrate_set(USART0, baud);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);

    nvic_irq_enable(USART0_IRQn, 1U);            /* 2 参数版 */
    usart_interrupt_enable(USART0, USART_INT_RBNE);

    usart_enable(USART0);
}

int main(void)
{
    systick_config();
    led_init();
    usart0_init(9600U);

    uart_printf("\r\n==========================================\r\n");
    uart_printf("  GD32E230 UART Calculator v4\r\n");
    uart_printf("  send: 3+5  ->  reply: 3 + 5 = 8\r\n");
    uart_printf("  LED blinks once per message\r\n");
    uart_printf("==========================================\r\n");

    while (1) {
        /* 处理中断攒下的字节，拼行 */
        while (rx_tail != rx_head) {
            char ch = (char)rx_buf[rx_tail];
            rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_SIZE);

            if (('\r' == ch) || ('\n' == ch)) {
                if (line_len > 0U) {
                    line_buf[line_len] = '\0';
                    led_flash(100U);          /* ★ 收到一条消息 → 闪一下 */
                    process_line(line_buf);   /* ★ 给出计算应答 */
                    line_len = 0U;
                }
            } else if (line_len < (LINE_BUF_SIZE - 1U)) {
                line_buf[line_len++] = ch;
            } else {
                line_len = 0U;
                uart_printf("ERR: line too long\r\n");
            }
        }

        delay_1ms(10U);   /* 轻量轮询，应答延迟 < 10ms */
    }
}
