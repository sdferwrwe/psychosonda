/*
 * task_nv_demo.h
 *
 *  Created on: 24.9.2014
 *      Author: horinek
 */

#ifndef TASK_NV_DEMO_H_
#define TASK_NV_DEMO_H_

#include "tasks.h"

void task_nv_demo_init();
void task_nv_demo_stop();
void task_nv_demo_loop();
void task_nv_demo_irqh(uint8_t type, uint8_t * buff);


#endif /* TASK_NV_DEMO_H_ */
