/*! \file oled.c \brief SSD1306 0.96" OLED 驱动（PB6=SCL / PB7=SDA 模拟 I2C）
 *
 *  【SSD1306 显存组织 —— 页寻址模式】
 *  内部 GDDRAM 共 1024 字节，对应 128x64 像素。纵向按 8 行一切分成 8 "页"：
 *      页 p 的第 c 列 = 1 个字节，bit0 = 该列最上面一行像素，bit7 = 最下面一行
 *  写显存三步：设页地址(0xB0|p) → 设列地址(0x00~0x0F 低 4 位 / 0x10~0x1F 高 4 位)
 *  → 连续写数据字节（地址指针自动后移）。
 *  页寻址的好处：不用维护 1KB 本地显存镜像（E230 只有 8KB RAM），直接局部刷新。
 *
 *  【黄蓝双色物理分区】页 0-1（顶部 16 行）= 黄色像素；页 2-7 = 蓝色像素。
 *  软件不区分颜色 —— 写到哪页就发哪种光。排版：温度放黄区(页0)，湿度放蓝区(页3)。
 *
 *  【I2C 每笔传输的帧结构】
 *      START → 从机写地址 0x78 → 控制字节 → 数据... → STOP
 *      控制字节 0x00 = 后续全是命令；0x40 = 后续全是显存数据 */
#include "gd32e23x.h"
#include "systick.h"
#include "oled.h"
#include "oledfont.h"
#include <stdio.h>
#include <string.h>

#define OLED_ADDR_W   0x78U          /* 7 位地址 0x3C 左移 1 位 = 写地址；模块背面焊盘默认档 */

#define OLED_PORT     GPIOB
#define OLED_RCC      RCU_GPIOB
#define OLED_SCL      GPIO_PIN_6
#define OLED_SDA      GPIO_PIN_7

#define SCL_H()   gpio_bit_set(OLED_PORT, OLED_SCL)
#define SCL_L()   gpio_bit_reset(OLED_PORT, OLED_SCL)
#define SDA_H()   gpio_bit_set(OLED_PORT, OLED_SDA)
#define SDA_L()   gpio_bit_reset(OLED_PORT, OLED_SDA)
#define SDA_READ()   gpio_input_bit_get(OLED_PORT, OLED_SDA)

/* 半位延时：72MHz 下空转约 2us → SCL 约 250kHz，处于 SSD1306 舒适区（上限 400kHz）*/
static void i2c_dly(void)
{
    volatile uint32_t i = 20U;
    while (i--) { __NOP(); }
}

/* ---------- I2C 总线原语（开漏输出：写 1 = 释放总线靠上拉变高，写 0 = 主动拉低）---------- */
static void i2c_start(void)
{   /* 起始条件：SCL 高电平期间 SDA 由高变低 */
    SDA_H(); SCL_H(); i2c_dly();
    SDA_L(); i2c_dly();
    SCL_L(); i2c_dly();
}
static void i2c_stop(void)
{   /* 停止条件：SCL 高电平期间 SDA 由低变高 */
    SDA_L(); i2c_dly();
    SCL_H(); i2c_dly();
    SDA_H(); i2c_dly();
}
static void i2c_send_byte(uint8_t b)
{   /* 发 1 字节：MSB 先行（I2C 规定位序），SCL 低电平期间改数据、高电平期间被从机采样 */
    for (uint8_t i = 0U; i < 8U; i++) {
        if (b & 0x80U) { SDA_H(); } else { SDA_L(); }
        b <<= 1U;
        i2c_dly();  SCL_H(); i2c_dly();     /* 高电平期 = 从机采样窗口 */
        SCL_L();
    }
    /* 第 9 个时钟：应答位。SSD1306 会拉低 SDA 表示 ACK；
       只写场景可以忽略校验，但仍须把时钟脉冲补齐让从机复位位计数器 */
    SDA_H();                                /* 释放 SDA，把总线交还给从机 */
    i2c_dly();  SCL_H(); i2c_dly();
    (void)SDA_READ();                       /* 此处读到 0 = ACK（保留读动作便于调试）*/
    SCL_L(); i2c_dly();
}

/* ---------- SSD1306 访问层 ---------- */
static void oled_write(uint8_t ctrl, const uint8_t *p, uint16_t n)
{
    i2c_start();
    i2c_send_byte(OLED_ADDR_W);
    i2c_send_byte(ctrl);                    /* 0x00=命令流 / 0x40=显存数据流 */
    while (n--) { i2c_send_byte(*p++); }
    i2c_stop();
}
static void oled_cmd(uint8_t c)             { oled_write(0x00U, &c, 1U); }

static void oled_set_pos(uint8_t x, uint8_t page)
{   /* 页寻址坐标：页号一条命令，列地址拆成低/高 4 位两条命令 */
    oled_cmd((uint8_t)(0xB0U | page));
    oled_cmd((uint8_t)(0x00U | (x & 0x0FU)));
    oled_cmd((uint8_t)(0x10U | (x >> 4)));
}
static void oled_burst_data(const uint8_t *p, uint8_t n) { oled_write(0x40U, p, n); }

void oled_init(void)
{
    rcu_periph_clock_enable(OLED_RCC);
    /* 两根线都配成开漏输出 + 上拉：这是 I2C 总线的标准电气形态，
       开漏写 1 = 只释放不驱动，任何设备都能拉低，天然避免电平打架 */
    gpio_mode_set(OLED_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, OLED_SCL | OLED_SDA);
    gpio_output_options_set(OLED_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_50MHZ, OLED_SCL | OLED_SDA);
    SCL_H(); SDA_H();                       /* 总线空闲态 = 双高 */

    delay_1ms(100U);                        /* 模块上电稳定（数据手册要求 >10ms，取宽裕值）*/

    /* ---- 初始化命令序列（顺序不可乱，两条命令是生死线）----
     * 0x8D 0x14 电荷泵：OLED 需要十几伏驱动电压，模块靠内部升压电路产生，
     *                 这条不发 → 屏幕永远全黑（最常见的"不亮"原因）；
     * 0xA1 / 0xC8 方向重映射：决定图像不镜像、不上下颠倒 */
    oled_cmd(0xAE);                         /* 关显示（配置期间先灭屏防花屏）*/
    oled_cmd(0xD5); oled_cmd(0x80);         /* 内部时钟分频 = 默认 */
    oled_cmd(0xA8); oled_cmd(0x3F);         /* 占空比 1/64 = 64 行全用 */
    oled_cmd(0xD3); oled_cmd(0x00);         /* 显示偏移 = 0 */
    oled_cmd(0x40);                         /* 显示起始行 = 0 */
    oled_cmd(0x8D); oled_cmd(0x14);         /* ★ 电荷泵使能（生死线）*/
    oled_cmd(0x20); oled_cmd(0x02);         /* ★ 寻址模式 = 页寻址（本驱动的前提）*/
    oled_cmd(0xA1);                         /* 段重映射：列 127→SEG0（左右方向）*/
    oled_cmd(0xC8);                         /* COM 扫描反向（上下方向）*/
    oled_cmd(0xDA); oled_cmd(0x12);         /* COM 引脚 = 交替模式（适配 128x64 模块）*/
    oled_cmd(0x81); oled_cmd(0xCF);         /* 对比度 0xCF（0x00~0xFF，可调亮度）*/
    oled_cmd(0xD9); oled_cmd(0xF1);         /* 预充电周期 */
    oled_cmd(0xDB); oled_cmd(0x40);         /* VCOMH 电压档位 */
    oled_cmd(0xA4);                         /* 显示跟随显存内容（而非全亮测试态）*/
    oled_cmd(0xA6);                         /* 正常显示（非反色）*/
    oled_cmd(0xAF);                         /* 开显示 —— 此刻屏幕亮起 */
    oled_clear();
}

void oled_clear(void)
{   /* 清屏 = 8 页 x 128 列全写 0。一次 burst 连写 128 字节（地址指针自动递增）*/
    static uint8_t zeros[128];
    for (uint8_t p = 0U; p < 8U; p++) {
        oled_set_pos(0U, p);
        oled_burst_data(zeros, 128U);
    }
}

void oled_show_str(uint8_t x, uint8_t page, const char *s)
{   /* ASCII 8x16：字库本身就是"页格式"（前 8 字节上半页、后 8 字节下半页），
       直写即可，无需转置。表外字符显示空格（安全降级，不崩不乱码）*/
    while (*s != '\0') {
        const char *hit = strchr(ASCII_TAB, *s);
        uint8_t idx = (hit != 0) ? (uint8_t)(hit - ASCII_TAB) : (uint8_t)(sizeof(ASCII_TAB) - 2U);
        oled_set_pos(x, page);      oled_burst_data(&F8X16_TAB[idx][0], 8U);
        oled_set_pos(x, page + 1U); oled_burst_data(&F8X16_TAB[idx][8], 8U);
        x += 8U;
        s++;
    }
}

void oled_show_hanzi(uint8_t idx, uint8_t x, uint8_t page)
{   /* ★ 本文件最核心的算法：字模"转置"
     *
     *  问题：汉字字模是"逐行式"（行优先），SSD1306 页寻址吃的是"列字节"（列优先），
     *        两者数据组织相差 90 度，必须转置后再上屏。
     *  字模侧：f[2r+half] 的位 (7-(c&7)) = 像素(行 r, 列 c)
     *          其中 half = (c>=8) ? 1 : 0（每行两字节，左半/右半）
     *  显存侧：上半页字节 top[c] 的位 r = 像素(行 r, 列 c)（bit0 = 最上行）
     *          下半页字节 bot[c] 的位 r = 像素(行 8+r, 列 c)
     *  转置 = 对每一列 c，把 16 个行像素从"行打散的位"重新聚成"列聚拢的字节"：
     *          top[c] |= ((f[2r+half] >> bit) & 1) << r      （r=0..7）
     *  复杂度 16x16 = 256 次位操作，72MHz 下 <5us，可忽略 */
    uint8_t top[16], bot[16];
    const uint8_t *f = HZK16[idx];

    for (uint8_t c = 0U; c < 16U; c++) {
        uint8_t half   = (c >= 8U) ? 1U : 0U;   /* 列落在字模的左/右半字节 */
        uint8_t bitpos = (uint8_t)(7U - (c & 7U)); /* 该列在字节内的位号，MSB=最左列 */
        uint8_t u = 0U, d = 0U;
        for (uint8_t r = 0U; r < 8U; r++) {
            u |= (uint8_t)(((f[2U * r + half]        >> bitpos) & 1U) << r);
            d |= (uint8_t)(((f[2U * (r + 8U) + half] >> bitpos) & 1U) << r);
        }
        top[c] = u; bot[c] = d;
    }
    oled_set_pos(x, page);        oled_burst_data(top, 16U);   /* 上半 8 行 → 页 page */
    oled_set_pos(x, page + 1U);   oled_burst_data(bot, 16U);   /* 下半 8 行 → 页 page+1 */
}
