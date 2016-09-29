/*
 * led.h
 *
 *  Created on: 23.7.2014
 *      Author: horinek
 */

#ifndef LED_H_
#define LED_H_

#include "../psychosonda.h"

void led_init();
void led_stop();
void led_set(uint16_t red, uint16_t green, uint16_t blue);

void led_anim(uint8_t mode);
void led_anim(uint8_t mode, uint16_t top);


#define LED_NO_ANIM		0
#define LED_BREATHR		1
#define LED_BREATHG		2
#define LED_BREATHB		3

#define ANIM_DEFAULT_TOP	0xFF

#endif /* LED_H_ */
