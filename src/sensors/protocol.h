/*
 * protocol.h
 *
 *  Created on: 18.2.2015
 *      Author: horinek
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_


// FILE FORMAT
// 7654 3210 |
// id   len
// id 0-15
//	0 - time sync (every 1 sec?)
//	1 - ADS  ch1, ch2
//  2 - ACC  x, y, z (FIFO?)
//  3 - GYRO x, y, z (FIFO?)
//  4 - MAG  x, y, z
//  5 - BMP

// len 0-14
//  0xf - next byte set length

#define make_head(id, len)	(id << 4 | (len & 0x0F))

#define id_time		0
#define id_ads		1
#define id_acc		2
#define id_gyro		3
#define id_mag		4
#define id_bmp		5
#define id_event	6
#define id_bat		7
#define id_temp		8
#define id_humi		9

#define event_mark		0
#define event_end		1
#define event_signal	2

#endif /* PROTOCOL_H_ */
