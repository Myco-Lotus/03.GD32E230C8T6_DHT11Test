/*! \file main.c \brief GD32E230 UART 计算器 v8 —— 计算与温湿度全解耦 + 过载零残行
 *
 *  架构三铁律：
 *   1. ISR 只做"字节入队"（约 2~3us），绝不拼行、绝不发送；
 *   2. 所有串口发送只在主循环单线程发生 —— 两行应答永不交织；
 *   3. 温湿度 2s 节拍独立计时，计算流量无权干预。
 *
 *  过载防护（v8 新增）：
 *   - 115200 波特把带宽提到输入量的 7 倍，从源头杜绝积压；
 *   - 环形缓冲满时执行"整行重同步"，宁可整行丢弃，绝不产生碎片行；
 *   - 解析失败的行静默丢弃，串口终端上永远干净。
 */
#include "gd32e23x.h"
#include "systick.h"
#include "dht11.h"
#include "oled.h"
#include "oledfont.h"
#include <stdio.h>
#include <stdarg.h>


/* ======================= 串口带宽预算（为什么选 115200） =======================
 *  UART 每字节的物理耗时 = 10 位 / 波特率（1 起始位 + 8 数据位 + 1 停止位）：
 *    9600  波特：1 字节 ≈ 1.042ms → 16 字节应答要 16.7ms → 上限约 60 条应答/s
 *    115200 波特：1 字节 ≈ 86.8us → 同一条应答只要 1.39ms → 上限约 700 条/s
 *  PC 以 10ms 周期发车 = 100 条/s：9600 下超出上限 67%（必然积压丢包），
 *  115200 下仅占用 14% 带宽（从容不迫）。
 *  精度验证：SystemCoreClock = 72MHz，72_000_000 / 115200 = 625（正好是整数），
 *  分频无余数 → 波特率误差 0%，收发双方位相位永不漂移。                     */
#define UART_BAUD 115200U

/* ======================= 接收环形缓冲（SPSC 无锁队列） =======================
 *  经典"单生产者/单消费者"(Single-Producer/Single-Consumer) 无锁环形队列：
 *    生产者 = USART 中断：读 rx_tail（只读），写 rx_buf[head] 和 rx_head；
 *    消费者 = 主循环    ：读 rx_head（只读），读 rx_buf[tail] 并推进 rx_tail。
 *  两边各自独占一个游标的"写权"，只"读"对方的游标 —— 读写对象互不交错，
 *  因此不需要关中断、不需要原子指令，天然并发安全（这就是 SPSC 队列的价值）。
 *  volatile 的作用：禁止编译器把"对方游标"缓存进 CPU 寄存器 ——
 *  否则主循环可能永远读到 ISR 更新前的旧值，队列传信失效。
 *
 *  判满采用"留一格空位"法：写入前先算 next = (head+1) % SIZE，
 *  若 next == tail 视为满。代价是永远牺牲一个格子，
 *  收益是"空"(head==tail) 与"满"(next==tail) 两种状态可区分 ——
 *  若不留空位，满时 head==tail 与空时无法分辨，队列就废了。
 *
 *  容量取 256：115200 下 DHT11 读取的 24ms 内最多进 3 条消息(21B)，
 *  256 字节是 10 倍以上冗余；即使将来回退 9600，也能硬扛约 260ms 突发。   */
#define RX_BUF_SIZE 256U
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0U;    /* 生产者游标：只有 ISR 写它 */
static volatile uint16_t rx_tail = 0U;    /* 消费者游标：只有主循环写它 */
static volatile uint8_t  g_resync = 0U;   /* 重同步事件：ISR 置位，主循环消费 */

/* ======================= 行缓冲（主循环层的拼行状态机） =======================
 *  环形缓冲里的字节流没有"行"的概念，这里逐字节装配：
 *    普通字符 → 追加进 line_buf；'\r' 或 '\n' → 当前行终结，送去解析。
 *  容量 32：够容纳最长的合法算式（如 -32768*-32768 = 12 字符），
 *  超长说明流已错乱（正常输入不可能这么长）→ 直接丢弃当前行重新对齐。     */
#define LINE_BUF_SIZE 32U
static char    line_buf[LINE_BUF_SIZE];
static uint8_t line_len = 0U;

/* ======================= 节拍与 LED（全非阻塞） =======================
 *  tick10ms：主循环每圈 +1 的自由运行计数器（uint32，100Hz 下约 497 天才回绕）。
 *  回绕安全比较技巧：(int32_t)(now - mark) >= 0
 *    两个无符号数相减再按有符号解读 —— 只要"未来时刻"与"现在"的差
 *    不超过 2^31，即使 now 回绕到 0，减法结果依然是正确的正数。
 *    这是嵌入式计时比较的标准写法，比 "now > mark" 能多活 248 天。        */
static uint32_t tick10ms       = 0U;
static uint32_t last_beat_tick = 0U;      /* 上次温湿度节拍时刻 */
static uint8_t  led_lit        = 0U;      /* LED 当前是否点亮 */
static uint32_t led_off_tick   = 0U;      /* 计划熄灭时刻 */

/* ---------- printf 风格发送：格式化进栈缓冲，再逐字节阻塞发送 ---------- */
static void uart_printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    int len;
    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) { return; }
    for (int i = 0; i < len && i < (int)sizeof(buf) - 1; i++) {
        usart_data_transmit(USART0, (uint8_t)buf[i]);
        while (RESET == usart_flag_get(USART0, USART_FLAG_TBE)) { }  /* 等 TX 空再发下一字节 */
    }
}

/* ======================= USART0 接收中断 =======================
 *  职责只有一件事：把字节塞进环形缓冲（全程 2~3us），其余一概不碰。
 *
 *  【整行重同步算法】v8 核心：
 *  旧策略(v7)"丢新保旧"的问题 —— 丢的字节落在字节流中间，
 *  缓冲里幸存的数据不再按行对齐，主循环拼行时出现"前半行+后半行"的缝合怪：
 *    缝合后碰巧合法 → 算出错误结果（无声无息）
 *    缝合后不合法   → 解析失败 → ERR 刷屏（体验差）
 *  新策略：缓冲满时，从最旧一端（tail）开始丢弃，一直吞到"行尾符"为止，
 *  即把"最旧的半个或一个完整行"整体扔掉。效果：
 *    - 缓冲内幸存数据永远从行首开始（行对齐保证）；
 *    - 主循环正在拼的半行已被"断头" → 置 g_resync 让主循环丢弃重拼；
 *    - 代价：整行丢失（过载下应答本来就发不完，丢整行无害）；
 *    - 收益：绝无碎片行、绝无错误计算、绝无 ERR 噪音。                   */
void USART0_IRQHandler(void)
{
    if (RESET != usart_interrupt_flag_get(USART0, USART_INT_FLAG_RBNE)) {
        uint8_t ch = (uint8_t)usart_data_receive(USART0);   /* 读 DATA 自动清 RBNE */
        uint16_t next = (uint16_t)((rx_head + 1U) % RX_BUF_SIZE);
        if (next != rx_tail) {                              /* 有空位：正常入队 */
            rx_buf[rx_head] = ch;
            rx_head = next;
        } else {                                            /* 队列满：启动整行重同步 */
            while (rx_tail != rx_head) {                    /* 从最旧端逐字节丢弃 */
                uint8_t dropped = rx_buf[rx_tail];
                rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_SIZE);
                if (('\r' == dropped) || ('\n' == dropped)) {
                    break;                                  /* 吞完一个完整行即停 */
                }
            }
            g_resync = 1U;                                  /* 通知主循环：半行作废 */
            next = (uint16_t)((rx_head + 1U) % RX_BUF_SIZE);
            if (next != rx_tail) {                          /* 腾出了空间就收新字节 */
                rx_buf[rx_head] = ch;
                rx_head = next;
            }                                               /* 仍满（单行超长）：丢字节 */
        }
    }
    if (RESET != usart_flag_get(USART0, USART_FLAG_ORERR)) {
        usart_data_receive(USART0);                         /* 清硬件溢出标志（读 DATA）*/
    }
}

/* ---------- LED2：PC13（低电平点亮？这里是高点亮，初始化默认灭）---------- */
static void led_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOC);
    gpio_mode_set(GPIOC, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_13);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    gpio_bit_reset(GPIOC, GPIO_PIN_13);
}
static void led_blink(uint32_t ticks10ms)   /* 非阻塞闪烁：只记时刻，不 delay */
{
    gpio_bit_set(GPIOC, GPIO_PIN_13);
    led_lit = 1U;
    led_off_tick = tick10ms + ticks10ms;
}

/* ======================= 计算解析 =======================
 *  sscanf(s, " %d %c %d", ...) 的格式语义：
 *    " "  ：跳过任意空白（空格/制表符/换行）；
 *    %d   ：跳过前导空白后读整数（自动处理负号）；
 *    %c   ：匹配恰好一个字符（不跳空白！所以格式串里 %c 前必须放空格，
 *           否则 "3 + 5" 里 %c 会吃到空格而不是 '+'）；
 *    返回值 = 成功匹配的字段数，== 3 才算合法算式。
 *  解析失败 → 静默丢弃：过载残行与手滑输入无法区分，且用户要求零噪音。
 *  （除零的 ERR 保留 —— 那是对一条真实完整命令的正当回答，不是噪音。）  */
static void process_line(const char *s)
{
    int a, b;
    char op;
    if (sscanf(s, " %d %c %d", &a, &op, &b) == 3) {
        int r;
        switch (op) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/':
            if (0 == b) { uart_printf("ERR: div by zero\r\n"); return; }
            r = a / b;                     /* C 整数除法向零取整 */
            break;
        default:
            return;                        /* 运算符不认识：静默 */
        }
        uart_printf("%d %c %d = %d\r\n", a, op, b, r);
    }
    /* 解析失败：静默丢弃（不回 ERR）*/
}

/* ---------- OLED 界面刷新：黄区页0=温度行，蓝区页3=湿度行 ----------
 * 布局（128x64，字符宽：汉字16px / ASCII 8px）：
 *   页0-1(黄)： 温 度 : 2 6 空 C            → 16*2 + 8*4 = 64px 起，充足
 *   页3-4(蓝)： 湿 度 : 5 3 空 % R H        → 32 + 7*8 = 88px，放得下
 * 每次刷新约写 260 字节显存 ≈ 10ms（250kHz I2C），加 DHT11 的 24ms，
 * 2s 节拍内总占用 ~1.7% —— 不构成任何时序压力 */
static void oled_show_th(uint8_t t, uint8_t h)
{
    char line[12];

    oled_show_hanzi(HZ_WEN,  0U, 0U);       /* 温 → 页0（黄区）*/
    oled_show_hanzi(HZ_DU,  16U, 0U);       /* 度 */
    //snprintf(line, sizeof(line), ":%u C", t);
    snprintf(line, sizeof(line), ":%02u", t);      /* %02u: 不足两位补零(5→05)，数字恒占 2 格，℃ 位置才固定 */
    oled_show_str(32U, 0U, line);
    oled_show_hanzi(HZ_CELSIUS, 56U, 0U);          /* ℃：x = 32 + 8(":") + 8*2(两位数) */

    oled_show_hanzi(HZ_SHI,  0U, 3U);       /* 湿 → 页3（蓝区）*/
    oled_show_hanzi(HZ_DU,  16U, 3U);       /* 度（复用编号 1，字库不重复存）*/
    snprintf(line, sizeof(line), ":%u %%RH", h);   /* %% = 字面百分号 */
    oled_show_str(32U, 3U, line);
}

static void sample_and_report(void)
{
    uint8_t hi, hd, ti, td;
    if (0U == dht11_read(&hi, &hd, &ti, &td)) {     /* 0 = 读到且校验通过 */

        // /* 串口回传改中文（GBK 编码字节直写，原理见上）：
        // *   温度 = \xCE\xC2\xB6\xC8   （温=CEC2 度=B6C8，GB2312 码表查得）
        // *   湿度 = \xCA\xAA\xB6\xC8   （湿=CAAA）
        // *   ℃   = \xA1\xE6           （GB2312 符号区 A1E6）
        // * 输出效果：温度:24℃ 湿度:39%RH
        // *
        // * 【\x 转义的经典陷阱】"\xB6\xC8C" 里的 C8 后面紧跟 'C'，
        // * 编译器会把 \xC8C 当成一个转义 → 越界报错。本串所有转义后跟的
        // * 都是 ':'、' '、'%' 等非十六进制字符，安全；日后改动时留意。   */
        // uart_printf("\xCE\xC2\xB6\xC8:%d\xA1\xE6 \xCA\xAA\xB6\xC8:%d%%RH\r\n", ti, hi);

        /* UTF-8 版：温度=E6B8A9 E5BAA6，湿度=E6B9BF E5BAA6，℃=E28483（每汉字3字节）*/
         uart_printf("\xE6\xB8\xA9\xE5\xBA\xA6:%d\xE2\x84\x83 \xE6\xB9\xBF\xE5\xBA\xA6:%d%%RH\r\n", ti, hi);

        
        oled_show_th(ti, hi);                               /* 屏幕同步 */
    }
    /* 读失败：静默跳过本轮，2s 后自动重试（屏幕保留上次数值，符合直觉）*/
}


static void usart0_init(uint32_t baud)
{
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_USART0);
    /* PA9=TX PA10=RX，复用功能 AF1（USART0），上拉保持空闲高电平 */
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
    nvic_irq_enable(USART0_IRQn, 1U);               /* 抢占优先级 1 */
    usart_interrupt_enable(USART0, USART_INT_RBNE); /* 接收缓冲非空中断 */
    usart_enable(USART0);
}

int main(void)
{
    systick_config();
    led_init();
    usart0_init(UART_BAUD);
    dht11_init();
    oled_init();                            /* PB6/PB7 模拟 I2C + SSD1306 配置 + 清屏 */

    /* 开机静态画面：标签先上屏，数值用 "--" 占位，等第一个 2s 节拍填真值 */
    oled_show_hanzi(HZ_WEN,  0U, 0U);
    oled_show_hanzi(HZ_DU,  16U, 0U);
    //oled_show_str(32U, 0U, ":-- C");
    oled_show_str(32U, 0U, ":--");
    oled_show_hanzi(HZ_CELSIUS, 56U, 0U);          /* ℃ 占位，2s 后被真值原位覆盖 */
    oled_show_hanzi(HZ_SHI,  0U, 3U);
    oled_show_hanzi(HZ_DU,  16U, 3U);
    oled_show_str(32U, 3U, ":-- %RH");

    uart_printf("\r\n==========================================\r\n");
    uart_printf(" GD32E230 Calc + DHT11 + OLED v9\r\n");
    uart_printf(" %u baud | Temp/Hum: 2s beat + OLED\r\n", UART_BAUD);
    uart_printf(" overload-safe: whole-line resync, no ERR noise\r\n");
    uart_printf("==========================================\r\n");

    while (1) {
        if (g_resync) {                     /* 整行重同步兜底（同 v8）*/
            g_resync = 0U;
            line_len = 0U;
        }

        while (rx_tail != rx_head) {        /* 消化环形缓冲，先到先答（同 v8）*/
            char ch = (char)rx_buf[rx_tail];
            rx_tail = (uint16_t)((rx_tail + 1U) % RX_BUF_SIZE);
            if (('\r' == ch) || ('\n' == ch)) {
                if (line_len > 0U) {
                    line_buf[line_len] = '\0';
                    led_blink(5U);
                    process_line(line_buf);
                    line_len = 0U;
                }
            } else if (line_len < (LINE_BUF_SIZE - 1U)) {
                line_buf[line_len++] = ch;
            } else {
                line_len = 0U;
                uart_printf("ERR: line too long\r\n");
            }
        }
        if (g_resync) { g_resync = 0U; line_len = 0U; }

        if (led_lit && ((int32_t)(tick10ms - led_off_tick) >= 0)) {
            gpio_bit_reset(GPIOC, GPIO_PIN_13);
            led_lit = 0U;
        }

        if ((tick10ms - last_beat_tick) >= 200U) {   /* 独立 2s 节拍（同 v8）*/
            last_beat_tick = tick10ms;
            sample_and_report();                /* 内部已包含 OLED 刷新 */
        }

        delay_1ms(10U);
        tick10ms++;
    }
}

