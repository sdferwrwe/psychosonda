
#include "task_test.h"

void show_error(uint8_t val)
{
	while(1)
	{
		for (uint8_t i = 0; i < val; i++)
		{
			led_set(0xFF, 0, 0);
			_delay_ms(100);
			led_set(0, 0, 0);
			_delay_ms(300);
		}
		_delay_ms(1000);
	}
}

void task_test_init()
{
	DEBUG(" *** TEST TASK ***\n\n");

	test_memory();

	buzzer_beep(_100ms, _100ms, _100ms * 5);

	led_set(0xFF, 0, 0);

	MEMS_POWER_ON;
	I2C_POWER_ON;
	MEMS_I2C_PWR_ON;

	_delay_ms(10);

	mems_i2c.InitMaster(MEMS_I2C, 400000ul);
	DUMP_REG(mems_i2c.i2c->MASTER.BAUD);

	DEBUG("Checking for i2c devices\n");
	//i2c need time to spool up
	_delay_ms(1);
	mems_i2c.Scan();


	if (!storage_init())
	{
		DEBUG("Could not mount the SD card!\n");
		show_error(1);
	}

	FIL * log;
	log = new FIL;

	uint8_t res = f_open(log, "hw.log", FA_WRITE | FA_CREATE_ALWAYS);
	if (res != FR_OK)
		show_error(2);

	ads1292_settings.enabled = true;

	lsm303d_settings.enabled = true;

	l3gd20_settings.enabled = true;

	bmp180_settings.enabled = true;

	sht21_settings.temp_enabled = true;


	char tmp[512];
	uint16_t bw;

	sprintf(tmp, "[hw]\n");
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);

	ads1292.Init(ads1292_settings);
	sprintf(tmp, "ads1292 = %d\n", ads1292.SelfTest());
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);

	lsm303d.Init(&mems_i2c, lsm303d_settings);
	sprintf(tmp, "lsm303d = %d\n", lsm303d.SelfTest());
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);

	l3gd20.Init(&mems_i2c, l3gd20_settings);
	sprintf(tmp, "l3gd20 = %d\n", l3gd20.SelfTest());
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);

	bmp180.Init(&mems_i2c, bmp180_settings);
	sprintf(tmp, "bmp180 = %d\n", bmp180.SelfTest());
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);

	sht21.Init(&mems_i2c, sht21_settings);
	sprintf(tmp, "sht21 = %d\n", sht21.SelfTest());
	f_write(log, tmp, strlen(tmp), &bw);
	f_sync(log);


//	sprintf(tmp, "bt = %d\n\n", bt_init());
//	f_write(log, tmp, strlen(tmp), &bw);
//	f_sync(log);

	f_close(log);

	free(log);
}

void task_test_stop()
{
	buzzer_beep(_100ms, _100ms, _100ms, _100ms, _100ms);
	_delay_ms(500);

	bt_module_deinit();
	ads1292.Stop();
	lsm303d.Stop();
	l3gd20.Stop();
	bmp180.Stop();
	sht21.Stop();

	MEMS_POWER_OFF;
	I2C_POWER_OFF;
	MEMS_I2C_PWR_OFF;
}

uint8_t test_cycle = 0;
uint16_t test_timeout = 0;

void task_test_loop()
{
	test_cycle = (test_cycle + 1) % 15;
	test_timeout++;

	if (test_cycle / 5 == 0)
		led_set(0xFF, 0, 0);
	if (test_cycle / 5 == 1)
		led_set(0, 0xFF, 0);
	if (test_cycle / 5  == 2)
		led_set(0, 0, 0xFF);

	if (test_timeout > 200)
		task_set(TASK_POWERDOWN);

	_delay_ms(5);
}


void task_test_irqh(uint8_t type, uint8_t * buff)
{
	switch(type)
	{

	case(TASK_IRQ_BUTTON):
		switch (*buff)
		{
			case(BUTTON_SHORT):
				buzzer_beep(_100ms * 5);
			break;

			case(BUTTON_HOLD):
				task_set(TASK_POWERDOWN);
			break;
		}
	break;


	case(TASK_IRQ_USB):
		uint8_t state = *buff;

		DEBUG("USB IRQ %d\n", state);
		if (state == 1)
			task_set(TASK_USB);
	break;
	}
}
