/*
 * button.h
 *
 *  Created on: 30.7.2014
 *      Author: horinek
 */

#ifndef BUTTON_H_
#define BUTTON_H_

#include "../psychosonda.h"

void button_init();
void button_step();

#define BUTTON_IDLE		0
#define BUTTON_WAIT		1
#define BUTTON_SHORT	2
#define BUTTON_LONG		3
#define BUTTON_HOLD		4

#define BUTTON_SHORT_PRESS	(900)
#define BUTTON_LONG_PRESS	(1700)
#define BUTTON_LONG_HOLD	(2500)

#endif /* BUTTON_H_ */
