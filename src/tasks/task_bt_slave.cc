#include "task_bt_slave.h"
#include "../xlib/stream.h"

//Stream control data flow form and to the slave
Stream bt_slave_stream;
//hander for all file operations
FIL * slave_file;
//data buffer for file operation
char * slave_file_buffer;
//time when system start measuring
uint32_t slave_meas_start;
//is system in measurement mode?
bool slave_meas = false;
//do we need to flash all the data from sd_buffer?
bool slave_flush = false;
//end time
uint32_t slave_meas_end = 0;
//bt_enabled after end
uint32_t slave_bt_timeout = 0;
bool slave_bt_timeout_enabled = false;

volatile bool live_meas_on = false;
volatile bool send_data = false;
volatile bool t_send_data = false;
volatile bool ads_started = false;
volatile bool starting_meas_ignore_bt_errors = false;

SleepLock slave_bt_lock;
SHT21 * adas;

#define SLAVE_SIGNAL_MAX	5
//markers
uint32_t slave_signals[SLAVE_SIGNAL_MAX];

EEMEM uint16_t ee_slave_meas_cnt;

DataBuffer * bluetooth_buffer;

volatile uint8_t * buffer;
volatile uint32_t buffer_pos = 0;

void bt_send_live_data_packet();

#define CMD_HELLO					0
#define CMD_PUSH_FILE				1
#define CMD_PULL_FILE				2
#define CMD_PUSH_PART				3
#define CMD_PULL_PART				4
#define CMD_CLOSE_FILE				5
#define CMD_MEAS					6
#define CMD_REBOOT					7
#define CMD_REBOOT					7
#define CMD_OFF						8
#define CMD_START_LIVE_MEAS			9
#define CMD_STOP_LIVE_MEAS			10
#define CMD_LIVE_MEAS				11

#define CMD_RET_OK			0
#define CMD_RET_FAIL		1
#define CMD_ID				2
#define CMD_PART			3
#define LIVE_PACKET			4


#define FAIL_FILE			0
#define FAIL_MEAS			1

#define FAIL_MEAS_OK		0
#define FAIL_MEAS_BUFFER	1
#define FAIL_MEAS_CFG		2
#define FAIL_MEAS_RAW		3


#define SD_BUFFER_SIZE		512 * 16
#define SD_BUFFER_WRITE		512 * 8

#define LIVE_MEAS_TIMEOUT 10000

volatile uint32_t time_since_last_live_request = 0;

//Slave return OK
void bt_slave_ok()
{
	DEBUG(">bt_slave_ok\n");

	bt_slave_stream.StartPacket(1);
	bt_slave_stream.Write(CMD_RET_OK);
}

//Slave return OK + size
void bt_slave_ok(uint32_t size)
{
	DEBUG(">bt_slave_ok %lu\n", size);

	bt_slave_stream.StartPacket(1 + 4);
	bt_slave_stream.Write(CMD_RET_OK);

	byte4_u b4;

	b4.uint32 = size;

	bt_slave_stream.Write(b4.bytes[0]);
	bt_slave_stream.Write(b4.bytes[1]);
	bt_slave_stream.Write(b4.bytes[2]);
	bt_slave_stream.Write(b4.bytes[3]);
}


//Slave indicate error
void bt_slave_fail(uint8_t type, uint8_t code)
{
	DEBUG(">bt_slave_fail %d %d\n", type, code);

	bt_slave_stream.StartPacket(3);
	bt_slave_stream.Write(CMD_RET_FAIL);
	bt_slave_stream.Write(type);
	bt_slave_stream.Write(code);
}

void bt_slave_hello()
{
	byte2_u mtu;

	DEBUG(">bt_slave_hello\n");

	bt_slave_stream.StartPacket(12);
	bt_slave_stream.Write(CMD_ID);
	bt_slave_stream.Write('b');
	bt_slave_stream.Write('i');
	bt_slave_stream.Write('o');
	bt_slave_stream.Write('p');
	bt_slave_stream.Write('r');
	bt_slave_stream.Write('o');
	bt_slave_stream.Write('b');
	bt_slave_stream.Write('e');

	bt_slave_stream.Write(battery_per);

	mtu.uint16 = bt_pan1322.mtu_size;
	bt_slave_stream.Write(mtu.bytes[0]);
	bt_slave_stream.Write(mtu.bytes[1]);
}

//Start measuring
uint8_t bt_slave_meas_start()
{
	DEBUG("Start: Free RAM %d", freeRam());
	uint8_t res;

	DEBUG("Starting meas\n");

	MEMS_POWER_ON;
	I2C_POWER_ON;
	MEMS_I2C_PWR_ON;

	starting_meas_ignore_bt_errors = true;

	_delay_ms(10);

	mems_i2c.InitMaster(MEMS_I2C, 400000ul, 120, 20);
	DUMP_REG(mems_i2c.i2c->MASTER.BAUD);

	DEBUG("Checking for i2c devices\n");
	//i2c need time to spool up
	_delay_ms(1);
	mems_i2c.Scan();

	sd_buffer = new DataBuffer(SD_BUFFER_SIZE); //10K
	if (sd_buffer->size == 0)
	{
		DEBUG("Could not allocate memory for sd buffer!\n");
		return FAIL_MEAS_BUFFER;
	}

	FIL * cfg;
    FILINFO fno;
	cfg = new FIL;

	DEBUG("\nReading cfg file:\n");

    if (f_stat("MEAS.CFG", &fno) == FR_OK)
    	res = f_open(cfg, "MEAS.CFG", FA_READ);
    else
    	res = f_open(cfg, "BT.CFG", FA_READ);



	char meas_filebase[32];
	char meas_measname[128];
	char meas_cfgname[128];
	char meas_outputdir[128];

	if (res == FR_OK)
	{
		//Meas section
		DEBUG("\n");
		DEBUG("Measurement\n");

		bool default_name, default_path;

		if (!cfg_get_str(cfg, "meas", "filename", meas_filebase))
		{
			strcpy_P(meas_filebase, PSTR("MEAS"));
			default_name = true;
		}
		else
		{
			default_name = false;

			if (strchr(meas_filebase, '%'))
			{
				char tmp[32];
				uint16_t ram_meas_cnt;

				eeprom_busy_wait();
				ram_meas_cnt = eeprom_read_word(&ee_slave_meas_cnt);

				ram_meas_cnt++;

				eeprom_update_word(&ee_slave_meas_cnt, ram_meas_cnt);

				strcpy(tmp, meas_filebase);
				sprintf(meas_filebase, tmp, ram_meas_cnt);
			}
		}

		DEBUG(" filebase: %s\n", meas_filebase);

		if (!cfg_get_str(cfg, "meas", "outputdir", meas_outputdir))
		{
			strcpy_P(meas_outputdir, PSTR(""));
			default_path = true;
		}
		else
		{
			default_path = false;

			res = f_mkdir(meas_outputdir);
			if (res != FR_OK)
				DEBUG("Could not create dir Error %02X\n", res);
		}

		sprintf(meas_measname, "%s/%s.RAW", meas_outputdir, meas_filebase);
		DEBUG(" meas file: %s\n", meas_measname);

		sprintf(meas_cfgname, "%s/%s.CFG", meas_outputdir, meas_filebase);

		//copy cfg file
		if (!default_path or !default_name)
			storage_copy_file((char *)"MEAS.CFG", meas_cfgname);

		DEBUG(" cfg file: %s\n", meas_cfgname);


		slave_meas_end = cfg_get_int(cfg, "meas", "duration", 0);
		if (slave_meas_end)
			DEBUG(" duration: %lu ms\n", slave_meas_end);

		slave_bt_timeout = cfg_get_int(cfg, "meas", "bt_timeout", 0);
		if (slave_bt_timeout)
		{
			DEBUG(" bt_timeout: %lu ms\n", slave_bt_timeout);
		}

		for (uint8_t i=0; i < SLAVE_SIGNAL_MAX; i++)
		{
			char tmp[8];
			sprintf(tmp, "signal%d", i + 1);
			slave_signals[i] = cfg_get_int(cfg, "meas", tmp, 0);
			if (slave_signals[i])
				DEBUG(" %s: %lu ms\n", tmp, slave_signals[i]);
		}

		//Bio section
		DEBUG("\n");
		DEBUG("ADS1292:\n");

		if ( ads_started ){
			DEBUG( "RESET\n" );
			ads1292.Deinit();
			_delay_ms(500);
		}

		ads1292_settings.enabled = cfg_have_section(cfg, "bio");
		if (ads1292_settings.enabled)
		{
			ads1292_settings.odr = cfg_get_int(cfg, "bio", "odr", 125);
			ads1292_settings.ch1_gain = cfg_get_int(cfg, "bio", "ch1_gain", 1);
			ads1292_settings.ch1_source = cfg_get_int(cfg, "bio", "ch1_source", BIO_SOURCE);

			ads1292_settings.resp_enabled = cfg_get_int(cfg, "bio", "resp_enabled", 0);
			ads1292_settings.resp_phase = cfg_get_int(cfg, "bio", "resp_phase", 125);
			ads1292_settings.resp_freq = cfg_get_int(cfg, "bio", "resp_freq", 64);

			ads1292_settings.ch2_gain = cfg_get_int(cfg, "bio", "ch2_gain", 1);
			ads1292_settings.ch2_source = cfg_get_int(cfg, "bio", "ch2_source", BIO_SOURCE);

			DEBUG(" odr: %d\n", ads1292_settings.odr);
			DEBUG(" ch1_gain: %d\n", ads1292_settings.ch1_gain);
			DEBUG(" ch1_source: %d\n", ads1292_settings.ch1_source);
			DEBUG(" ch2_gain: %d\n", ads1292_settings.ch2_gain);
			DEBUG(" ch2_source: %d\n", ads1292_settings.ch2_source);
		}
		else
		{
			DEBUG(" disabled\n");
		}



		ads1292.Init(ads1292_settings);

		//Gyro section
		DEBUG("\n");
		DEBUG("L3GD20:\n");

		l3gd20_settings.enabled = cfg_have_section(cfg, "gyro");
		if (l3gd20_settings.enabled)
		{
			l3gd20_settings.odr = cfg_get_int(cfg, "gyro", "odr", 95);
			l3gd20_settings.scale = cfg_get_int(cfg, "gyro", "scale", 250);
			l3gd20_settings.bw = cfg_get_int(cfg, "gyro", "bw", 12);

			DEBUG(" odr: %d\n", l3gd20_settings.odr);
			DEBUG(" scale: %d\n", l3gd20_settings.scale);
			DEBUG(" bw: %d\n", l3gd20_settings.bw);
		}
		else
			DEBUG("disabled\n");

		l3gd20.Init(&mems_i2c, l3gd20_settings);

		//Magnetometer & Accelerometer section
		DEBUG("\n");
		DEBUG("LSM303D:\n");

		lsm303d_settings.enabled = cfg_have_section(cfg, "acc") || cfg_have_section(cfg, "mag");
		if (lsm303d_settings.enabled)
		{
			lsm303d_settings.accOdr = cfg_get_int(cfg, "acc", "odr", 0);
			lsm303d_settings.accScale = cfg_get_int(cfg, "acc", "scale", 0);

			lsm303d_settings.magOdr = cfg_get_int(cfg, "mag", "odr", 0);
			lsm303d_settings.magScale = cfg_get_int(cfg, "mag", "scale", 2);

			DEBUG(" ACC ODR: %d\n", lsm303d_settings.accOdr);
			DEBUG(" ACC scale: %d\n", lsm303d_settings.accScale);
			DEBUG(" MAG ODR: %d\n", lsm303d_settings.magOdr);
			DEBUG(" MAG scale: %d\n", lsm303d_settings.magScale);
		}
		else
			DEBUG("disabled\n");

		lsm303d.Init(&mems_i2c, lsm303d_settings);

		//Barometer section
		DEBUG("\n");
		DEBUG("BMP180:\n");

		bmp180_settings.enabled = cfg_have_section(cfg, "baro");
		if (bmp180_settings.enabled)
		{
			bmp180_settings.odr = cfg_get_int(cfg, "baro", "odr", 2);

			DEBUG(" BARO ODR: %d\n", bmp180_settings.odr);
		}
		else
			DEBUG("disabled\n");

		bmp180.Init(&mems_i2c, bmp180_settings);

		//Enviromental
		DEBUG("\n");
		DEBUG("SHT21:\n");

		if (cfg_have_section(cfg, "enviro"))
		{
			sht21_settings.rh_enabled = cfg_get_int(cfg, "enviro", "humidity", 0);
			sht21_settings.temp_enabled = cfg_get_int(cfg, "enviro", "temperature", 0);

			DEBUG(" humidity: %d\n", sht21_settings.rh_enabled);
			DEBUG(" temperature: %d\n", sht21_settings.temp_enabled);
		}
		else
		{
			DEBUG("disabled\n");
		}
		sht21_settings.temp_enabled = true;
		sht21.Init(&mems_i2c, sht21_settings);
		sht21.Start();


	}
	else
	{
		return FAIL_MEAS_CFG;
		DEBUG(" Error opening cfg file %02X\n", res);
	}

	DEBUG("\n\n");

	delete cfg;

	res = f_open(slave_file, meas_measname, FA_WRITE | FA_CREATE_ALWAYS);

	if (res != FR_OK)
	{
		DEBUG("Could not create raw file. Error %02X\n", res);
		return FAIL_MEAS_RAW;
	}
	else
	{
		slave_meas_start = task_get_ms_tick();
		DEBUG("slave_meas_start %lu\n", slave_meas_start);
		if (slave_meas_end)
			slave_meas_end = slave_meas_start + slave_meas_end;

		DEBUG("slave_meas_end %lu\n", slave_meas_end);


		for (uint8_t i=0; i < SLAVE_SIGNAL_MAX; i++)
		{
			if (slave_signals[i])
				slave_signals[i] += slave_meas_start;
			DEBUG("slave_signals[%d] %lu\n", i, slave_signals[i]);
		}

		slave_meas = true;

		ads1292.Start();
		lsm303d.Start();
		l3gd20.Start();
		bmp180.Start();
		sht21.Start();
		DEBUG( "Temperature starting" );


		DEBUG("Free RAM %d", freeRam());

		buzzer_beep(_100ms * 5, _100ms, _100ms);
		led_anim(LED_BREATHG, 0xFF);

		return FAIL_MEAS_OK;
	}
}

uint8_t bt_slave_start_live_meas_start()
{
	DEBUG("Start: Free RAM %d", freeRam());
	uint8_t res;

	DEBUG("Starting meas\n");

	MEMS_POWER_ON;
	I2C_POWER_ON;
	MEMS_I2C_PWR_ON;

	_delay_ms(10);
	DEBUG("1:Free RAM %d", freeRam());

	mems_i2c.InitMaster(MEMS_I2C, 400000ul, 120, 20);

	DEBUG("Checking for i2c devices\n");
	//i2c need time to spool up
	_delay_ms(1);
	mems_i2c.Scan();



	buffer = (uint8_t *) malloc( sizeof( uint8_t ) * 1000 );

	if ( buffer == 0 ){
		DEBUG("Error allocating memory buffer\n");
		return FAIL_MEAS_BUFFER;
	}


	if ( !ads_started ) {

		ads1292_settings.enabled = true;
		ads1292_settings.odr = 125;
		ads1292_settings.ch1_gain = 3;
		ads1292_settings.ch1_source = 1;

		ads1292_settings.resp_enabled = false;
		ads1292_settings.resp_phase = 135;
		ads1292_settings.resp_freq = 64;

		ads1292_settings.ch2_gain = 4;
		ads1292_settings.ch2_source = 1;

		DEBUG("3:Free RAM %d", freeRam());
		DEBUG(" odr: %d\n", ads1292_settings.odr);
		DEBUG(" ch1_gain: %d\n", ads1292_settings.ch1_gain);
		DEBUG(" ch1_source: %d\n", ads1292_settings.ch1_source);
		DEBUG(" ch2_gain: %d\n", ads1292_settings.ch2_gain);
		DEBUG(" ch2_source: %d\n", ads1292_settings.ch2_source);

		ads1292.Init(ads1292_settings);

		DEBUG("\n\n");

		//slave_meas = true;
		ads_started = true;
		//bt_module_deinit();

	}

	ads1292.Start();

	buzzer_beep(_100ms * 5, _100ms, _100ms);
	led_anim(LED_BREATHG, 0xFF);
	live_meas_on = true;


	time_since_last_live_request = task_get_ms_tick();
	buffer_pos = 0;

	return FAIL_MEAS_OK;

}

void bt_slave_stop_live_meas_start() {
	//starting_meas_ignore_bt_errors = false;
	live_meas_on = false;
	send_data = false;
	ads1292.Stop();

	free( (uint8_t*)buffer );

	buzzer_beep(_100ms, _100ms, _100ms, _100ms, _100ms);
	led_anim(LED_NO_ANIM);
	led_set(0, 0, 0xFF);

	DEBUG("Stoping live meas\n");
	DEBUG("Free RAM %d", freeRam());
	//XXX: just for the buzzer sound
	//_delay_ms(500);

	//free( bluetooth_buffer );
	buffer_pos = 0;
	MEMS_POWER_OFF;
	I2C_POWER_OFF;
	MEMS_I2C_PWR_OFF;
}

void bt_slave_meas_stop()
{
	slave_meas = false;
	starting_meas_ignore_bt_errors = false;
	ads1292.Stop();
	lsm303d.Stop();
	l3gd20.Stop();
	bmp180.Stop();
	sht21.Stop();

	buzzer_beep(_100ms, _100ms, _100ms, _100ms, _100ms);
	led_anim(LED_NO_ANIM);

	DEBUG("Writing end time...\n");

	//write end event
	uint8_t event_data[1 + 1];
	event_data[0] = make_head(id_event, 5);
	event_data[1] = event_end;
	uint32_t act_time = task_get_ms_tick() - slave_meas_start;
	sd_buffer->Write(2, event_data);
	sd_buffer->Write(4, (uint8_t*)&act_time);

	DEBUG("Flushing SD buffer ...");

	slave_flush = true;
	task_bt_save_buffer();
	slave_flush = false;
	DEBUG("OK\n");

	f_close(slave_file);

	//XXX: just for the buzzer sound
	_delay_ms(500);

	MEMS_POWER_OFF;
	I2C_POWER_OFF;
	MEMS_I2C_PWR_OFF;
}

void bt_slave_rxpacket()
{
//	DEBUG("bt_slave_stream.Debug();\n");
//	bt_slave_stream.Debug();

	char file_path[64];
	uint8_t ret;
	uint8_t cmd = bt_slave_stream.Read();
	uint16_t len;
	byte2_u b2;
	byte4_u b4;
	uint16_t bw;

	switch (cmd)
	{

	case(CMD_HELLO):
		bt_slave_hello();
	break;

	case(CMD_PUSH_FILE):
		len = bt_slave_stream.Read();
		for(uint8_t i=0; i < len; i++)
			file_path[i] = bt_slave_stream.Read();
		file_path[len] = 0;

		DEBUG("CMD_PUSH_FILE %s, %u\n", file_path, len);

		ret = f_open(slave_file, file_path, FA_WRITE | FA_CREATE_ALWAYS);
		if (ret != FR_OK)
			bt_slave_fail(FAIL_FILE, ret);
		else
			bt_slave_ok();
	break;

	case(CMD_PULL_FILE):
		len = bt_slave_stream.Read();
		for(uint8_t i=0; i < len; i++)
			file_path[i] = bt_slave_stream.Read();
		file_path[len] = 0;

		DEBUG("CMD_PULL_FILE %s, %u\n", file_path, len);

		ret = f_open(slave_file, file_path, FA_READ);
		if (ret != FR_OK)
			bt_slave_fail(FAIL_FILE, ret);
		else
			bt_slave_ok(f_size(slave_file));
	break;

	case(CMD_PUSH_PART):
		b2.bytes[0] = bt_slave_stream.Read();
		b2.bytes[1] = bt_slave_stream.Read();
		len = b2.uint16;
		b4.bytes[0] = bt_slave_stream.Read();
		b4.bytes[1] = bt_slave_stream.Read();
		b4.bytes[2] = bt_slave_stream.Read();
		b4.bytes[3] = bt_slave_stream.Read();

		DEBUG("CMD_PUSH_PART %u, %lu\n", len, b4.uint32);


		ret = f_lseek(slave_file, b4.uint32);
		if (ret != FR_OK)
			bt_slave_fail(FAIL_FILE, ret);

		for (uint16_t i=0; i < len; i++)
			slave_file_buffer[i] = bt_slave_stream.Read();

		ret = f_write(slave_file, slave_file_buffer, len, &bw);
		if (ret != FR_OK)
		{
			bt_slave_fail(FAIL_FILE, ret);
			break;
		}

		if (bw != len)
		{
			bt_slave_fail(FAIL_FILE, 0xFF);
			break;
		}
		bt_slave_ok();
	break;

	case(CMD_PULL_PART):
		b2.bytes[0] = bt_slave_stream.Read();
		b2.bytes[1] = bt_slave_stream.Read();
		len = b2.uint16;
		b4.bytes[0] = bt_slave_stream.Read();
		b4.bytes[1] = bt_slave_stream.Read();
		b4.bytes[2] = bt_slave_stream.Read();
		b4.bytes[3] = bt_slave_stream.Read();

		DEBUG("CMD_PULL_PART %u, %lu\n", len, b4.uint32);

		ret = f_lseek(slave_file, b4.uint32);
		if (ret != FR_OK)
			bt_slave_fail(FAIL_FILE, ret);

		uint16_t bw;
		ret = f_read(slave_file, slave_file_buffer, len, &bw);
		if (ret != FR_OK)
		{
			bt_slave_fail(FAIL_FILE, ret);
			break;
		}

		bt_slave_stream.StartPacket(3 + len);
		bt_slave_stream.Write(CMD_PART);

		b2.uint16 = bw;
		bt_slave_stream.Write(b2.bytes[0]);
		bt_slave_stream.Write(b2.bytes[1]);

		for (uint16_t i=0; i < len; i++)
			bt_slave_stream.Write(slave_file_buffer[i]);
			//bt_slave_stream.Write(slave_file_buffer[i]);

	break;

	case(CMD_CLOSE_FILE):
		DEBUG("CMD_CLOSE_FILE\n");

		ret = f_close(slave_file);
		if (ret != FR_OK)
		{
			bt_slave_fail(FAIL_FILE, ret);
			break;
		}

		bt_slave_ok();
	break;

	case(CMD_START_LIVE_MEAS):

		DEBUG("CMD_START LIVE MEASURMENT\n");
		ret = bt_slave_start_live_meas_start();

		if (ret == FAIL_MEAS_OK)
			bt_slave_ok();
		else
			bt_slave_fail(FAIL_MEAS, ret);
	break;
	case(CMD_STOP_LIVE_MEAS):
		DEBUG("CMD STOPING_MEAS\n");

		bt_slave_stop_live_meas_start();

		bt_slave_ok();

	break;

	case(CMD_MEAS):
		DEBUG("CMD_MEAS\n");

		ret = bt_slave_meas_start();

		if (ret == FAIL_MEAS_OK){
			bt_slave_ok();
			_delay_ms( 300 );
			bt_module_deinit();
		}else{
			bt_slave_fail(FAIL_MEAS, ret);
			task_set(TASK_POWERDOWN);
		}
	break;


	case(CMD_REBOOT):
		DEBUG("CMD_REBOOT\n");

		bt_slave_ok();
		SystemReset();
	break;

	case(CMD_OFF):
		DEBUG("CMD_OFF\n");

		bt_slave_ok();
		task_set(TASK_POWERDOWN);
	break;
	case( CMD_LIVE_MEAS ):

		break;

	}
}

void bt_send_live_data_packet() {

	if ( live_meas_on ) {

		uint16_t act_len;

		if ( buffer_pos > 3 ) {
			act_len = buffer_pos;
			bt_slave_stream.StartPacket( act_len + 1 );
			bt_slave_stream.Write( LIVE_PACKET );

			for( int x = 0; x < act_len; x++ ) {
				//DEBUG( "Sending %d\n", buffer[ x ] );
				bt_slave_stream.Write( buffer[ x ] );

			}

			buffer_pos = 0;

		}

	}

}

void task_bt_slave_init()
{
	bool init_fail = false;

	DEBUG("Starting BT Slave task\n");

	slave_file = new FIL;
	slave_file_buffer = new char[512];

	bt_slave_stream.Init(bt_pan1322_out, 255);
	bt_slave_stream.RegisterOnPacket(bt_slave_rxpacket);

	//Storage init
	if (!storage_init())
	{
		DEBUG("Could not mount the SD card!\n");
		init_fail = true;
	}
	else
	{
	    FILINFO fno;

	    if (f_stat("RST_CNT", &fno) == FR_OK)
		{
	    	f_unlink("RST_CNT");
	    	eeprom_busy_wait();
	    	eeprom_update_word(&ee_slave_meas_cnt, 0xFFFF);
		}

	    if (f_stat("MEAS.CFG", &fno) == FR_OK)
		{
			bt_slave_meas_start();
			return;
		}
	}


	//bluetooth radio init
	DEBUG("Bluetooth Init\n");
	bt_init();
	bt_module_init();

	if (init_fail)
	{
		DEBUG("HORIBLE error!\n");
	}
}

void task_bt_slave_stop()
{
	if (slave_meas)
		bt_slave_meas_stop();

	DEBUG("\n\n");

	led_anim(LED_NO_ANIM);
	bt_module_deinit();
	storage_deinit();

//	delete slave_file;
//	delete slave_file_buffer;
}

uint16_t luc = 0;

void task_bt_save_buffer()
{
	while (sd_buffer->Length() > SD_BUFFER_WRITE || (slave_flush && sd_buffer->Length() > 0))
	{
		uint8_t * data_ptr;
		uint16_t wrt;
		uint8_t res;

		uint16_t size = f_size(slave_file) / 1024;

		DEBUG("BL%d\n", sd_buffer->Length());

		DEBUG("%lu ms\n", task_get_ms_tick());
		DEBUG("WRITING %dkB..", size);

		uint16_t real_write_size = sd_buffer->Read(SD_BUFFER_WRITE, &data_ptr);

		res = f_write(slave_file, data_ptr, real_write_size, &wrt);

		res = f_sync(slave_file);

		DEBUG("done\n");
	}
}

void task_bt_slave_loop()
{
	uint32_t ms_time = task_get_ms_tick();

	//live meas
	if ( buffer_pos > 100 && live_meas_on == true ){
		bt_send_live_data_packet();
	}


	if (slave_meas)
	{
		if (slave_meas_end)
			//DEBUG("Checking time!\n");
			if (slave_meas_end < ms_time)
			{
				//do not recurse
				slave_meas_end = 0;

				DEBUG("Time is up!\n");

				bt_slave_meas_stop();

				if (slave_bt_timeout)
				{
					DEBUG("BT_timout active\n");
					bt_module_init();

					slave_bt_timeout += task_get_ms_tick();
					slave_bt_timeout_enabled = true;
				}
				else
					task_set(TASK_POWERDOWN);
			}

		for (uint8_t i=0; i < SLAVE_SIGNAL_MAX; i++)
		{
			if (slave_signals[i])
				if (slave_signals[i] < ms_time)
				{
					slave_signals[i] = 0;
					buzzer_beep(_100ms * 3);

					//MAKE marker
					uint8_t event_data[1 + 1];
					event_data[0] = make_head(id_event, 5);
					event_data[1] = event_signal;
					uint32_t act_time = task_get_ms_tick() - slave_meas_start;
					sd_buffer->Write(2, event_data);
					sd_buffer->Write(4, (uint8_t*)&act_time);
				}
		}

		task_bt_save_buffer();
	}
	else
	{
		if (slave_bt_timeout_enabled)
			if (slave_bt_timeout < ms_time)
				task_set(TASK_POWERDOWN);
	}

}

void bt_slave_button_irqh(uint8_t state)
{
	switch (state)
	{
		case(BUTTON_SHORT):
			if (slave_meas)
			{
				//MAKE marker
				uint8_t event_data[1 + 1];
				event_data[0] = make_head(id_event, 5);
				event_data[1] = event_mark;
				uint32_t act_time = task_get_ms_tick() - slave_meas_start;
				sd_buffer->Write(2, event_data);
				sd_buffer->Write(4, (uint8_t*)&act_time);

				buzzer_beep(_100ms * 10);
			}
		break;

		case(BUTTON_HOLD):
			task_set(TASK_POWERDOWN);
		break;
	}
}

void bt_slave_bt_irq(uint8_t * param)
{
//	DEBUG("BT IRQ %d\n", param[0]);

	switch (param[0])
	{
	case (BT_IRQ_PAIR):
		DEBUG("pair sucesfull\n");
	break;

//	case (BT_IRQ_INCOMING):
//		DEBUG("incoming connection %d\n", bt_link.AcceptConnection());
//	break;

	case (BT_IRQ_CONNECTED):
		DEBUG( "Hello received\n" );

		buzzer_beep(100);
		led_anim(LED_NO_ANIM);
		led_set(0, 0, 0xFF);
		bt_slave_hello();
		slave_bt_lock.Lock();
		if (slave_bt_timeout_enabled)
			slave_bt_timeout_enabled = false;
	break;

	case (BT_IRQ_DISCONNECTED):

		if ( live_meas_on == true )
			bt_slave_stop_live_meas_start();


		slave_bt_lock.Unlock();
		DEBUG("disconnected\n");
		led_anim(LED_BREATHB);

	break;

	case (BT_IRQ_ERROR):
		if( starting_meas_ignore_bt_errors == false ){
			led_anim(LED_NO_ANIM);
			led_set(0xFF, 0, 0);
		}
		DEBUG("BT Error %d\n", param[1]);
	break;

	case (BT_IRQ_INIT):
		led_anim(LED_BREATHB);
	break;

	case (BT_IRQ_DATA):
		bt_slave_stream.Decode(param[1]);
	break;
	}
}

void task_bt_slave_irqh(uint8_t type, uint8_t * buff)
{
	int16_t fifo_buffer[16 * 3];

//	DEBUG("IRQ %d %d\n", type, *buff);

	switch (type)
	{
	//SPI - gpio IRQ
	case(TASK_IRQ_ADS):
		ads1292.ReadData();

		if ( live_meas_on ) {
			uint8_t ads_data[4];

			if ( buffer_pos < 1000 ) {

				memcpy(ads_data, &ads1292.value_ch2, 3);

				buffer[ buffer_pos ] = ads_data[ 0 ];
				buffer[ buffer_pos + 1 ] = ads_data[ 1 ];
				buffer[ buffer_pos + 2 ] = ads_data[ 2 ];

				buffer_pos += 3;

			}else{
				DEBUG( "Overflowing" );
			}

			//bluetooth_buffer->Write( 3, ads_data );

		}else {
			uint8_t ads_data[7];
			memcpy(ads_data + 1, &ads1292.value_ch1, 3);
			memcpy(ads_data + 4, &ads1292.value_ch2, 3);

			ads_data[0] = make_head(id_ads, 6);
			sd_buffer->Write(7, ads_data);
		}
	break;

	//I2C - gpio IRQ
	case(TASK_IRQ_ACC):
		SIGNAL2_HI;
		lsm303d.ReadAccStream(fifo_buffer, 16);
		SIGNAL1_HI;

		uint8_t lsm_data[2];
		lsm_data[0] = make_head(id_acc, 0); //0 means that length is next
		lsm_data[1] = 16 * 3 * 2;

		sd_buffer->Write(2, lsm_data);
		sd_buffer->Write(16 * 3 * 2, (uint8_t*)fifo_buffer);
		SIGNAL1_LO;
		SIGNAL2_LO;
	break;

	//I2C - gpio IRQ
	case(TASK_IRQ_MAG):
		uint8_t mag_data[1 + 3 * 2];
		mag_data[0] = make_head(id_mag, 3 * 2);

		lsm303d.ReadMag((int16_t*)(mag_data + 1), (int16_t*)(mag_data + 3), (int16_t*)(mag_data + 5));

		sd_buffer->Write(7, mag_data);
	break;

	//I2C - gpio IRQ
	case(TASK_IRQ_GYRO):
		l3gd20.ReadGyroStream(fifo_buffer, 16);

		uint8_t l3g_data[2];
		l3g_data[0] = make_head(id_gyro, 0); //0 means that length is next
		l3g_data[1] = 16 * 3 * 2;

		sd_buffer->Write(2, l3g_data);
		sd_buffer->Write(16 * 3 * 2, (uint8_t*)fifo_buffer);
	break;

	//I2C - Timer IRQ
	case(TASK_IRQ_BARO):
		uint8_t bmp_data[5];
		bmp_data[0] = make_head(id_bmp, 4);

		//3 bytes are enough
		memcpy(bmp_data + 1, &bmp180.pressure, 4);

		sd_buffer->Write(5, bmp_data);
	break;

	//ADC - Main loop
	case(TASK_IRQ_BAT):
		uint8_t bat_data[2];

		if (slave_meas)
		{
			bat_data[0] = make_head(id_bat, 1);
			bat_data[1] = *buff;

			sd_buffer->Write(2, bat_data);
		}
	break;

	//I2C - Timer IRQ
	case(TASK_IRQ_TEMPERATURE):
		uint8_t temp_data[3];
		DEBUG( "Temperature reading" );

		temp_data[0] = make_head(id_temp, 2);
		memcpy(temp_data + 1, &sht21.temperature, 2);

		sd_buffer->Write(3, temp_data);
	break;

	//I2C - Timer IRQ
	case(TASK_IRQ_HUMIDITY):
		uint8_t humi_data[3];

		humi_data[0] = make_head(id_humi, 2);
		memcpy(humi_data + 1, &sht21.humidity, 2);

		sd_buffer->Write(3, humi_data);
	break;


	//Gpio - mixed IRQ and main loop
	case(TASK_IRQ_BUTTON):
		bt_slave_button_irqh(*buff);
	break;

	//Gpio - gpio IRQ
	case(TASK_IRQ_USB):
		DEBUG("USB IRQ %d\n", *buff);
		if (*buff == 1)
			task_set(TASK_USB);
	break;

	//Uart - uart IRQ
	case(TASK_IRQ_BT):
		bt_slave_bt_irq(buff);
	break;
	}
}
