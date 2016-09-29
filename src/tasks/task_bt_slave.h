/*
 * task_bt_slave.h
 *
 *  Created on: 11.2.2015
 *      Author: horinek
 */

#ifndef TASK_BT_SLAVE_H_
#define TASK_BT_SLAVE_H_

#include "tasks.h"
#include "sensors/sht21.h"

void task_bt_slave_init();
void task_bt_slave_stop();
void task_bt_slave_loop();
void task_bt_slave_irqh(uint8_t type, uint8_t * buff);

void task_bt_save_buffer();

#endif /* TASK_BT_SLAVE_H_ */
