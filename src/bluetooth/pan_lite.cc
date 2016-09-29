#include "pan_lite.h"

#include "bt.h"
#include "../../tasks/tasks.h"
#include <string.h>

extern pan1322 bt_pan1322;



CreateStdOut(bt_pan1322_out, bt_pan1322.StreamWrite);

enum pan1322_cmd_e
{
	pan_cmd_none 				= 0,
	pan_cmd_set_name			= 1,
	pan_cmd_create_service		= 2,
	pan_cmd_set_discoverable	= 3,
	pan_cmd_accept_connection	= 4,
	pan_cmd_reset 				= 5,
	pan_cmd_set_baud			= 6,
	pan_cmd_disable_sniff		= 7,
};

//ERR=-
//+RSLE 					pair sucesfull
//+RCOI=XXXXXXXXXXXX		incoming connection (12 HEX bt address)
//+RCCRCNF					connected
//+RDII						disconnected
//+RDAI=DDD,data			data

#define BT_STATE_START		0
#define BT_STATE_ERR		1
#define BT_STATE_RESP		2
#define BT_STATE_DATA		3
#define BT_STATE_OK			4
#define BT_STATE_ROK		5
#define BT_STATE_FIND_RN	6
#define BT_STATE_GET_MTU	7
#define BT_STATE_RCCRCNF	8

#define BT_TIMEOUT			3000
#define BT_NO_TIMEOUT		0

void pan1322::TxResume()
{
	this->usart->TxComplete();
}

void pan1322::Init(Usart * uart)
{
	this->usart = uart;
	this->baud = 115200ul;

	bt_irqh(BT_IRQ_INIT, 0);

	this->Restart();

	this->connected = false;
}

void pan1322::SetBaudrate(uint32_t baud)
{
	this->baud = baud;
	fprintf_P(bt_pan1322_out, PSTR("AT+JSBR=%07lu\r\n"), this->baud);

	this->WaitForOK();
}

void pan1322::SetSniff(bool mode)
{
	//N * 0.625 ms
	//Range: 1.25 ms to 40.9 s
	#define sniff_Max	200
	#define sniff_Min	2
	#define sniff_attempt	100
	#define sniff_tmo	200

	fprintf_P(bt_pan1322_out, PSTR("AT+JSNF=%04X,%04X,%04X,%04X,%i\r\n"), sniff_Max, sniff_Min, sniff_attempt, sniff_tmo, mode);
}

void pan1322::Restart()
{
	this->connected = false;
	this->p_state = BT_STATE_START;
	this->p_cmd = pan_cmd_reset;

	bt_module_reset();
	this->WaitForOK();
}

void pan1322::StreamWrite(uint8_t data)
{
//	DEBUG("%02X .. %c\n", data, data);
	this->usart->Write(data);
}

void pan1322::SendString(char *str)
{
	uint16_t len = strlen(str);

	this->StreamHead(len);
	fprintf_P(bt_pan1322_out, PSTR("%s"), str);
	this->StreamTail();
	this->WaitForOK();
}

void pan1322::SetName(const char * name)
{
	uint8_t nameLength = strlen_P(name);

	assert(nameLength <= 18);
	if (nameLength > 18)
	{
		DEBUG("Wrong name length");
		return;
	}

	fprintf_P(bt_pan1322_out, PSTR("AT+JSLN=%02i,%S\r\n"), nameLength, name);
	this->WaitForOK();
}

void pan1322::SetDiscoverable(bool discoverable)
{
	fprintf_P(bt_pan1322_out, PSTR("AT+JDIS=%i\r\n"), (discoverable) ? 3 : 0);
	this->WaitForOK();
}

void pan1322::CreateService(const char * uuid, const char * name, uint8_t channel, const char * deviceClass)
{
	uint8_t nameLength = strlen_P(name);

	assert(nameLength <= 16);
	if (nameLength > 16)
	{
		DEBUG("Wrong name length");
		return;
	}

	uint8_t uuidLength = strlen_P(uuid);
	assert(uuidLength <= 32 && uuidLength >= 4);
	if (uuidLength > 32 || uuidLength < 4)
	{
		DEBUG("Wrong uuid length");
		return;
	}

	fprintf_P(bt_pan1322_out, PSTR("AT+JRLS=%02d,%02d,%S,%S,%02d,%S\r\n"), uuidLength, nameLength, uuid, name, channel, deviceClass);
	this->WaitForOK();
}

void pan1322::Step()
{
	while (!this->usart->isRxBufferEmpty())
	{
//		uint8_t c = this->usart->Peek();
//		DEBUG("1322>%02X %c (%d)\n", c, c, this->usart->rx_len);
		this->Parse(this->usart->Read());
	}

	if (this->p_cmd != pan_cmd_none)
	{
		DEBUG("cmd %d\n", this->p_cmd);

		switch(this->p_cmd)
		{
			case(pan_cmd_set_baud):
				this->SetBaudrate(464285ul);
			break;

			case(pan_cmd_set_name):
				this->SetName(PSTR("Bioprobe"));
			break;

			case(pan_cmd_create_service):
				this->CreateService(PSTR("0000110100001000800000805f9b34fb"), PSTR("SPP"), 01, PSTR("080510"));
			break;

			case(pan_cmd_set_discoverable):
				this->SetDiscoverable(true);
			break;

			case(pan_cmd_accept_connection):
				this->AcceptConnection();
			break;

			case(pan_cmd_disable_sniff):
				this->SetSniff(false);
			break;
		}
		this->p_last_cmd = this->p_cmd;
		this->p_cmd = pan_cmd_none;
	}

	if (this->timer != BT_NO_TIMEOUT && this->timer < task_get_ms_tick())
	{
		DEBUG("PAN1322 timeout, last cmd %d\n", this->p_last_cmd);
		this->timer = BT_NO_TIMEOUT;

		switch (this->p_last_cmd)
		{
			case(pan_cmd_set_baud):
					//XXX
				this->p_cmd = pan_cmd_set_baud;
			break;

			case(pan_cmd_reset):
				//ROK not recieved
				bt_irqh(BT_IRQ_INIT_FAIL, 0);
			break;

			case(pan_cmd_set_name):
				this->p_cmd = pan_cmd_set_name;
			break;

			case(pan_cmd_accept_connection):
				this->p_cmd = pan_cmd_set_name;
			break;
		}

	}


}

void pan1322::AcceptConnection()
{
	fprintf_P(bt_pan1322_out, PSTR("AT+JACR=1\r\n"));
}

void pan1322::StreamHead(uint16_t len)
{
	bool first = true;
	while (!this->isIdle())
	{
//		if (first)
//		{
//			DEBUG("---tx lock\n");
//			first = false;
//		}
//
//		DEBUG("SH %d,%d,%d,%d\n", this->p_state, this->p_len, this->usart->rx_len, this->data_len);

		this->Step();
	}
//	if (!first)
//	{
//		DEBUG("---tx released\n");
//	}

	fprintf_P(bt_pan1322_out, PSTR("AT+JSDA=%03d,"), len);
//	DEBUG("AT+JSDA=%03d,", len);
}

void pan1322::StreamTail()
{
	fprintf_P(bt_pan1322_out, PSTR("\r\n"));
	this->usart->FlushTxBuffer();
}

bool pan1322::isIdle()
{
	return this->p_state == BT_STATE_START;
}

bool pan1322::isConnected()
{
	return this->connected;
}

void pan1322::WaitForOK()
{
//	DEBUG("Wait for ok\n");
	this->timer = task_get_ms_tick() + BT_TIMEOUT;
}

void pan1322::Parse(uint8_t c)
{
	uint8_t param;
	uint8_t tmp;
	uint8_t n;

//	DEBUG("P %c %d,%d\n", c, this->p_state, this->p_len);

	uint8_t state = this->p_state;
	switch (state)
	{
	case(BT_STATE_START):
		if (c == '+')
		{
			this->p_state = BT_STATE_RESP;
			this->p_len = 4;
			this->p_index = 0;
			break;
		}
		if (c == 'E')
		{
			this->p_state = BT_STATE_ERR;
			this->p_len = 4;
			this->p_index = 0;
			break;
		}
		if (c == 'O')
		{
			this->p_state = BT_STATE_OK;
			break;
		}
		if (c == 'R')
		{
			this->p_state = BT_STATE_ROK;
			this->p_len = 2;
			this->p_index = 0;
			break;
		}
	break;

	case(BT_STATE_ROK):
		if (this->p_len > this->p_index)
		{
			this->p_buff[this->p_index] = c;
			this->p_index++;
		}
		else
		{
			if (cmpn(this->p_buff, "OK", 2))
			{
				DEBUG("pan1322 reset ok\n");
				_delay_ms(10);
				this->p_cmd = pan_cmd_set_baud;
				this->p_state = BT_STATE_FIND_RN;
			}
		}
	break;

	case(BT_STATE_OK):
		if (c == 'K')
		{
			this->timer = BT_NO_TIMEOUT;

//			DEBUG("OK %d\n", this->p_last_cmd);

			switch(this->p_last_cmd)
			{
				case(pan_cmd_set_baud):
					DEBUG("SET BAUD OK\n");

					this->usart->Stop();
					while(GpioRead(BT_CTS) == HIGH);
					_delay_ms(150);
					this->usart->Init(BT_UART, this->baud);
					this->usart->SetInterruptPriority(MEDIUM);


					this->p_cmd = pan_cmd_set_name;
				break;

				case(pan_cmd_set_name):
					this->p_cmd = pan_cmd_create_service;
				break;

				case(pan_cmd_create_service):
					this->p_cmd = pan_cmd_set_discoverable;
				break;

				case(pan_cmd_set_discoverable):
					DEBUG("pan1322 init ok\n");

					bt_irqh(BT_IRQ_INIT_OK, 0);
				break;
			}
		}
		this->p_state = BT_STATE_FIND_RN;
	break;

	case(BT_STATE_FIND_RN):
		if (c == '\n')
		{
			this->p_state = BT_STATE_START;
		}
	break;

	case(BT_STATE_ERR):
		if (this->p_len > this->p_index)
		{
			this->p_buff[this->p_index] = c;
			this->p_index++;
		}
		else
		{
			if (cmpn(this->p_buff, "RR=-", 4))
			{
				param = c - '0';
				this->error = true;
				this->p_state = BT_STATE_FIND_RN;
				bt_irqh(BT_IRQ_ERROR, &param);
			}
		}
	break;

	case(BT_STATE_RESP):
		if (this->p_len > this->p_index)
		{
			this->p_buff[this->p_index] = c;
			this->p_index++;
		}
		else
		{
			if (cmpn(this->p_buff, "RSLE", 4))
			{
				//pair sucesfull
				this->p_state = BT_STATE_FIND_RN;
				bt_irqh(BT_IRQ_PAIR, 0);
				break;
			} else
			if (cmpn(this->p_buff, "RCOI", 4))
			{
				//TODO: decode btaddress
				this->p_state = BT_STATE_FIND_RN;

				this->p_cmd = pan_cmd_accept_connection;
				break;
			} else
			if (cmpn(this->p_buff, "RCCR", 4))
			{
				//device connected
				this->p_state = BT_STATE_RCCRCNF;

				break;
			} else
			if (cmpn(this->p_buff, "RDII", 4))
			{
				//device disconnected
				this->p_state = BT_STATE_FIND_RN;

				this->connected = false;
				bt_irqh(BT_IRQ_DISCONNECTED, 0);
				break;
			} else
			if (cmpn(this->p_buff, "RDAI", 4))
			{
				//incoming data

				this->p_state = BT_STATE_DATA;
				this->p_len = 0;
				break;
			}

			this->p_state = BT_STATE_FIND_RN;
		}


	break;

	case(BT_STATE_RCCRCNF):
		if (c == '=')
		{
			this->p_state = BT_STATE_GET_MTU;
			this->p_len = 0;
			this->mtu_size = 0;
		}
	break;

	case(BT_STATE_GET_MTU):
		tmp = this->p_len;

		switch(tmp)
		{
		case(0): // first digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->mtu_size += 100 * n;

			break;
		case(1): // second digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->mtu_size += 10 * n;

			break;
		case(2): // third digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->mtu_size += 1 * n;
			DEBUG("mtu_size %d\n", this->mtu_size);

			this->p_state = BT_STATE_FIND_RN;
			this->connected = true;
			bt_irqh(BT_IRQ_CONNECTED, 0);

//			this->p_cmd = pan_cmd_disable_sniff;

			break;
		}
	break;


	case(BT_STATE_DATA):
		tmp = this->p_len;

		switch(tmp)
		{
		case(0): // first digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->data_len += 100 * n;

			break;
		case(1): // second digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->data_len += 10 * n;

			break;
		case(2): // third digit
			this->p_len++;

			n = c - '0';
			assert(n <= 9);
			if (n > 9)
			{
				this->p_state = BT_STATE_START;
				break;
			}
			this->data_len += 1 * n;
//			DEBUG("DL %d\n", this->data_len);

			break;
		case(3): // semicolon
			this->p_len++;

			assert(c == ',');
			if (c != ',')
			{
				this->p_state = BT_STATE_START;
				break;
			}
			break;
		case(4): // data
			this->data_len -= 1;

			if (this->data_len == 0)
			{
				this->p_state = BT_STATE_FIND_RN;
			}

			//HANDLE INCOMING DATA HERE
			bt_irqh(BT_IRQ_DATA, &c);
			break;

		}
		break;
	}
}
