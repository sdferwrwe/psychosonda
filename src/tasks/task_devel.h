/*
 * task_devel.h
 *
 *  Created on: 21.10.2014
 *      Author: horinek
 */

#ifndef TASK_DEVEL_H_
#define TASK_DEVEL_H_

#include "../psychosonda.h"
#include "tasks.h"
#include "../storage/cfg.h"

void task_devel_init();
void task_devel_stop();
void task_devel_loop();
void task_devel_irqh(uint8_t type, uint8_t * buff);


#endif /* TASK_DEVEL_H_ */
