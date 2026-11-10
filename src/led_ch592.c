#include "CH59x_common.h"
#include "led.h"

/*
 * PA8 状态指示灯：推挽输出，固件运行期间保持常亮。
 * 默认按低电平点亮（LED 阳极接 VCC、阴极接 PA8）。若板上为高电平点亮，
 * 将 LED_ACTIVE_LOW 改为 0 后重新构建。
 */
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 1
#endif

#define LED_PIN GPIO_Pin_8

void led_init(void)
{
#if LED_ACTIVE_LOW
    GPIOA_ResetBits(LED_PIN);
#else
    GPIOA_SetBits(LED_PIN);
#endif
    GPIOA_ModeCfg(LED_PIN,GPIO_ModeOut_PP_5mA);
}
