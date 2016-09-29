/*
 * task_test.h
 *
 *  Created on: 30.3.2015
 *      Author: horinek
 */

#ifndef TASK_TEST_H_
#define TASK_TEST_H_

#include "tasks.h"

void task_test_init();
void task_test_stop();
void task_test_loop();
void task_test_irqh(uint8_t type, uint8_t * buff);



#endif /* TASK_TEST_H_ */
