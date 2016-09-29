#include "task_devel.h"



DataBuffer * devel_sd_buff;
FIL * devel_file;

volatile bool devel_meas = false;
volatile bool devel_flush = false;
volatile uint32_t devel_meas_start;

void task_devel_meas_stop();
void task_devel_meas_start();

uint16_t EEMEM devel_file_cnt;

void task_devel_init()
{
	DEBUG(" *** DEVEL TASK INIT ***\n\n");

	test_memory();


	bool init_fail = false;

	MEMS_POWER_ON;
	I2C_POWER_ON;
	MEMS_I2C_PWR_ON;

	mems_i2c.InitMaster(MEMS_I2C, 800000ul);

	//i2c need time to spool up
	_delay_ms(1);
	mems_i2c.Scan();

	devel_sd_buff = new DataBuffer(512 * 20); //10K
	if (devel_sd_buff->size == 0)
	{
		DEBUG("Could not allocate memory for sd buffer!\n");
		init_fail = true;
	}

	devel_file = new FIL;

	if (!storage_init())
	{
		DEBUG("Could not mount the SD card!\n");
		init_fail = true;
	}

	FIL * cfg;
	cfg = new FIL;

	DEBUG("\nReading cfg file:\n");

	uint8_t res = f_open(cfg, "cfg/devel/main.cfg", FA_READ);

	if (res == FR_OK)
	{
		//Bio section
		DEBUG("\n");
		DEBUG("ADS1292:\n");

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
			DEBUG(" disabled\n");

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
			lsm303d_settings.accScale = cfg_get_int(cfg, "acc", "scale", 2);

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

	}
	else
	{
		init_fail = true;
		DEBUG(" Error opening cfg file %02X\n", res);
	}

	delete cfg;

	test_memory();

	if (init_fail)
		task_set(TASK_POWERDOWN);
	else
		task_devel_meas_start();
}


void task_devel_stop()
{
	DEBUG(" *** DEVEL DEINIT *** \n");

	if (devel_meas)
		task_devel_meas_stop();

	storage_deinit();

	l3gd20.Deinit();
	lsm303d.Deinit();
	ads1292.Deinit();
	bmp180.Deinit();

	MEMS_POWER_OFF;
	I2C_POWER_OFF;
	MEMS_I2C_PWR_OFF;

	delete devel_sd_buff;
	delete devel_file;
	DEBUG("done\n");
}

void task_devel_loop()
{
	//write data
	#define WRITE_SIZE	(512 * 14)
	if (devel_sd_buff->Length() > WRITE_SIZE || devel_flush)
	{
		uint8_t * data_ptr;
		uint16_t wrt;
		uint8_t res;

		uint16_t size = f_size(devel_file) / 1024;

		DEBUG("WRITING %dkB\n", size);

		uint16_t real_write_size = devel_sd_buff->Read(WRITE_SIZE, &data_ptr);

		res = f_write(devel_file, data_ptr, real_write_size, &wrt);

		res = f_sync(devel_file);
	}

}

void task_devel_meas_start()
{
	char path[128];
	uint8_t res;

	devel_meas = true;

	eeprom_busy_wait();
	uint16_t file_number = eeprom_read_word(&devel_file_cnt);

	sprintf(path, "/meas");
	res = f_mkdir(path);
	if (res != FR_OK)
		DEBUG("Could not create dir Error %02X\n", res);

	DEBUG("Storing configuration\n", path);
	sprintf(path, "/meas/PS%05u.cfg", file_number);
	storage_copy_file((char *)"/cfg/devel/main.cfg", path);

	DEBUG("Creating file %s\n", path);
	sprintf(path, "/meas/PS%05u.raw", file_number);
	res = f_open(devel_file, path, FA_WRITE | FA_CREATE_ALWAYS);

	if (res != FR_OK)
	{
		DEBUG("Could not create raw file Error %02X\n", res);
		task_set(TASK_POWERDOWN);
		_delay_ms(100);
	}
	else
	{
		file_number++;

		eeprom_update_word(&devel_file_cnt, file_number);
		eeprom_busy_wait();

		buzzer_beep(_100ms * 5, _100ms, _100ms);
		led_anim(LED_BREATHG, 0xFF);

		devel_meas_start = task_get_ms_tick();

		ads1292.Start();
		lsm303d.Start();
		l3gd20.Start();
		bmp180.Start();
	}
}

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

#define event_mark	0
#define event_end	1


void task_devel_meas_stop()
{
	devel_meas = false;

	ads1292.Stop();
	lsm303d.Stop();
	l3gd20.Stop();
	bmp180.Stop();

	buzzer_beep(_100ms, _100ms, _100ms, _100ms, _100ms);
	led_anim(LED_NO_ANIM);

	DEBUG("Writing end time...\n");

	//write end event
	uint8_t event_data[1 + 1];
	event_data[0] = make_head(id_event, 5);
	event_data[1] = event_end;
	uint32_t act_time = task_get_ms_tick() - devel_meas_start;
	devel_sd_buff->Write(2, event_data);
	devel_sd_buff->Write(4, (uint8_t*)&act_time);

	DEBUG("Flushing SD buffer ...");

	devel_flush = true;
	while(devel_sd_buff->Length() > 0)
	{
		DEBUG("to write %d\n", devel_sd_buff->Length());
		task_devel_loop();
	}
	devel_flush = false;
	DEBUG("OK\n");

	f_close(devel_file);

	//XXX: just for the buzzer sound
	_delay_ms(500);
}

void task_devel_button_irqh(uint8_t state)
{
	switch (state)
	{
	case(BUTTON_SHORT):
		if (devel_meas)
		{
			//MAKE marker
			uint8_t event_data[1 + 1];
			event_data[0] = make_head(id_event, 5);
			event_data[1] = event_mark;
			uint32_t act_time = task_get_ms_tick() - devel_meas_start;
			devel_sd_buff->Write(2, event_data);
			devel_sd_buff->Write(4, (uint8_t*)&act_time);

			buzzer_beep(_100ms * 10);
		}
	break;

	case(BUTTON_HOLD):
		if (devel_meas)
		{
			task_set(TASK_POWERDOWN);
		}
	break;

	}
}



void task_devel_irqh(uint8_t type, uint8_t * buff)
{
//DEBUG("IRQ %d\n", type);

	int16_t fifo_buffer[16 * 3];

	switch (type)
	{
	case(TASK_IRQ_ADS):
		ads1292.ReadData();
		uint8_t ads_data[7];
		ads_data[0] = make_head(id_ads, 6);

		//3 bytes are enough
		memcpy(ads_data + 1, &ads1292.value_ch1, 3);
		memcpy(ads_data + 4, &ads1292.value_ch2, 3);

		devel_sd_buff->Write(7, ads_data);
	break;

	case(TASK_IRQ_ACC):
		lsm303d.ReadAccStream(fifo_buffer, 16);

		uint8_t lsm_data[2];
		lsm_data[0] = make_head(id_acc, 0); //0 means that length is next
		lsm_data[1] = 16 * 3 * 2;

		devel_sd_buff->Write(2, lsm_data);
		devel_sd_buff->Write(16 * 3 * 2, (uint8_t*)fifo_buffer);
	break;

	case(TASK_IRQ_MAG):
		uint8_t mag_data[1 + 3 * 2];
		mag_data[0] = make_head(id_mag, 3 * 2);

		lsm303d.ReadMag((int16_t*)(mag_data + 1), (int16_t*)(mag_data + 3), (int16_t*)(mag_data + 5));

		devel_sd_buff->Write(7, mag_data);
	break;

	case(TASK_IRQ_GYRO):
		l3gd20.ReadGyroStream(fifo_buffer, 16);

		uint8_t l3g_data[2];
		l3g_data[0] = make_head(id_gyro, 0); //0 means that length is next
		l3g_data[1] = 16 * 3 * 2;

		devel_sd_buff->Write(2, l3g_data);
		devel_sd_buff->Write(16 * 3 * 2, (uint8_t*)fifo_buffer);
	break;

	case(TASK_IRQ_BARO):
		uint8_t bmp_data[5];
		bmp_data[0] = make_head(id_bmp, 4);

		//3 bytes are enough
		memcpy(bmp_data + 1, &bmp180.pressure, 4);

		devel_sd_buff->Write(5, bmp_data);
	break;

	case(TASK_IRQ_BAT):
		uint8_t bat_data[2];

		bat_data[0] = make_head(id_bat, 1);
		bat_data[1] = *buff;

		devel_sd_buff->Write(2, bat_data);
	break;

	case(TASK_IRQ_BUTTON):
		task_devel_button_irqh(*buff);
	break;

	case(TASK_IRQ_USB):
		uint8_t state = *buff;

		DEBUG("USB IRQ %d\n", state);
		if (state == 1)
			task_set(TASK_USB);
	break;

	}
}
