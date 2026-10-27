/*! \file dht11.h \brief DHT11 单总线驱动（GD32E230）—— PB10，可在改宏换脚 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

/* ---- 引脚配置：换脚只改这里三行（如平移到 PA8：RCU_GPIOA/GPIOA/GPIO_PIN_8）---- */
#define DHT11_RCC     RCU_GPIOB
#define DHT11_PORT    GPIOB
#define DHT11_PIN     GPIO_PIN_10

void    dht11_init(void);
/* 读一次 DHT11。返回 0=成功；1=无响应；2=读位超时；3=校验和错误。
   成功时通过指针带回：湿度整数/小数、温度整数/小数（DHT11 小数恒为 0）*/
uint8_t dht11_read(uint8_t *humi_int, uint8_t *humi_dec,
                   uint8_t *temp_int, uint8_t *temp_dec);

#endif /* DHT11_H */
