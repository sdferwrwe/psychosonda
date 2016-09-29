/*
 * pan_lite.h
 *
 *  Created on: 25.9.2014
 *      Author: horinek
 */

#ifndef PAN_LITE_H_
#define PAN_LITE_H_

#include "../../common.h"
#include "../../drivers/uart.h"
#include <xlib/core/usart.h>


class pan1322
{
	volatile bool connected;
	volatile bool error;

	volatile uint8_t p_state;
	volatile uint8_t p_len;
	volatile uint8_t p_type;
	char p_buff[16];
	volatile uint8_t p_index;
	volatile uint8_t p_cmd;
	volatile uint8_t p_last_cmd;

	volatile uint16_t data_len;

	volatile uint32_t timer;

public:

	uint16_t mtu_size;
	Usart * usart;

	uint32_t baud;

	void Init(Usart * uart);
	void TxResume();
	void Restart();

	void SetName(const char * name);
	void SetDiscoverable(bool discoverable);
	void CreateService(const char * uuid, const char* name, uint8_t channel, const char * deviceClass);

	void SetBaudrate(uint32_t baud);
	void SetSniff(bool mode);

	void Step();
	void Parse(uint8_t c);
	void AcceptConnection();
	void WaitForOK();

	bool isConnected();

	void StreamHead(uint16_t len);
	void StreamWrite(uint8_t data);
	void StreamTail();

	void SendString(char * str);

	bool isIdle();
};

extern FILE * bt_pan1322_out;


#endif /* PAN_LITE_H_ */
