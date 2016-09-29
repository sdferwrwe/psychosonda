#include "task_nv_demo.h"

void task_nv_demo_init()
{
	DEBUG("Starting NV DEMO\n");

	ads1292_settings.ch1_gain = 3;
	ads1292_settings.resp_enabled = 1;
	ads1292_settings.resp_phase = 135;
	ads1292_settings.resp_freq = 64;
	ads1292_settings.ch1_source = BIO_SOURCE;

	ads1292_settings.ch2_gain = 4;
	ads1292_settings.ch2_source = BIO_SOURCE;

	ads1292_settings.enabled = true;
	ads1292_settings.odr = 125;

	lsm303d_settings.accOdr = 12;
	lsm303d_settings.accScale = 4;
	lsm303d_settings.magOdr = 12;
	lsm303d_settings.enabled = true;

	l3gd20_settings.odr = 95;
	l3gd20_settings.scale = 2000;
	l3gd20_settings.enabled = true;

	bmp180_settings.enabled = true;
	bmp180_settings.odr = 1;

	//bluetooth radio init
	DEBUG("Bluetooth ... ");
	bt_init();
	bt_module_init();

	MEMS_POWER_ON;
	I2C_POWER_ON;
	MEMS_I2C_PWR_ON;

	_delay_ms(10);

	ads1292.Init(ads1292_settings);
	DEBUG("ADS1292 ... ");

	if (ads1292.SelfTest())
		DEBUG("OK\n");
	else
		DEBUG("Error\n");
		

	mems_i2c.InitMaster(MEMS_I2C, 800000ul);

	mems_i2c.Scan();

	//press
	bmp180.Init(&mems_i2c, bmp180_settings);

	DEBUG("BMP180  ... ");
	if (bmp180.SelfTest())
		DEBUG("OK\n");
	else
		DEBUG("Error\n");

	//acc + mag
	lsm303d.Init(&mems_i2c, lsm303d_settings);
	DEBUG("LSM303D ... ");
	if (lsm303d.SelfTest())
		DEBUG("OK\n");
	else
		DEBUG("Error\n");

	//gyro
	l3gd20.Init(&mems_i2c, l3gd20_settings);
	DEBUG("L3GD20  ... ");
	if (l3gd20.SelfTest())
		DEBUG("OK\n");
	else
		DEBUG("Error\n");

	test_memory();

	buzzer_beep(_100ms, _100ms, _100ms);
	led_anim(LED_BREATHB, 0xFF);
}

bool meas_running = false;

void task_nv_demo_stop()
{
}


int32_t ads_ch0;
int32_t ads_ch1;

int16_t gyro_x;
int16_t gyro_y;
int16_t gyro_z;

int16_t acc_x;
int16_t acc_y;
int16_t acc_z;

int16_t mag_x;
int16_t mag_y;
int16_t mag_z;

float press;
float temp;

uint8_t meas_id;

extern uint16_t battery_adc_raw;
extern int8_t battery_per;

void nv_demo_gui_init()
{
	DEBUG("RGUI Init\n");
	RGUI_MESSAGE("RGUI Init\n");

	rgui.SetHeartbeat(250, "H");

	rgui.SetLineFromType(0, ads_ch0);
	rgui.SetLineProperties(0, 0xFF, 0, 0, "Ch 0");
	rgui.SetLineFromType(1, ads_ch1);
	rgui.SetLineProperties(1, 0, 0xFF, 0, "Ch 1");

	rgui.SetLineFromType(2, gyro_x);
	rgui.SetLineProperties(2, 0xFF, 0, 0, "gX");
	rgui.SetLineFromType(3, gyro_y);
	rgui.SetLineProperties(3, 0, 0xFF, 0, "gY");
	rgui.SetLineFromType(4, gyro_z);
	rgui.SetLineProperties(4, 0, 0, 0xFF, "gZ");

	rgui.SetLineFromType(5, acc_x);
	rgui.SetLineProperties(5, 0xFF, 0, 0, "aX");
	rgui.SetLineFromType(6, acc_y);
	rgui.SetLineProperties(6, 0, 0xFF, 0, "aY");
	rgui.SetLineFromType(7, acc_z);
	rgui.SetLineProperties(7, 0, 0, 0xFF, "aZ");

	rgui.SetLineFromType(8, mag_x);
	rgui.SetLineProperties(8, 0xFF, 0, 0, "mX");
	rgui.SetLineFromType(9, mag_y);
	rgui.SetLineProperties(9, 0, 0xFF, 0, "mY");
	rgui.SetLineFromType(10, mag_z);
	rgui.SetLineProperties(10, 0, 0, 0xFF, "mZ");

	rgui.SetLineFromType(11, press);
	rgui.SetLineProperties(11, 0xFF, 0, 0, "Pressure");
	rgui.SetLineFromType(12, temp);

	rgui.SetLineFromType(13, battery_adc_raw);
	rgui.SetLineFromType(14, battery_per);


	rgui.ClearScreen(0);

	rgui.AddComponentText(AUTO_ID, 1, 0, 10, 100, 10, "ADS CH1: ", "", "");
	rgui.AddDataSource(LAST_ID, 0);
	rgui.AddComponentText(AUTO_ID, 1, 0, 15, 100, 10, "ADS CH2: ", "", "");
	rgui.AddDataSource(LAST_ID, 1);

	rgui.AddComponentText(AUTO_ID, 1, 0, 25, 100, 10, "Gyro X: ", "", "");
	rgui.AddDataSource(LAST_ID, 2);
	rgui.AddComponentText(AUTO_ID, 1, 0, 30, 100, 10, "Gyro Y: ", "", "");
	rgui.AddDataSource(LAST_ID, 3);
	rgui.AddComponentText(AUTO_ID, 1, 0, 35, 100, 10, "Gyro Z: ", "", "");
	rgui.AddDataSource(LAST_ID, 4);

	rgui.AddComponentText(AUTO_ID, 1, 0, 45, 100, 10, "Acc X: ", "", "");
	rgui.AddDataSource(LAST_ID, 5);
	rgui.AddComponentText(AUTO_ID, 1, 0, 50, 100, 10, "Acc Y: ", "", "");
	rgui.AddDataSource(LAST_ID, 6);
	rgui.AddComponentText(AUTO_ID, 1, 0, 55, 100, 10, "Acc Z: ", "", "");
	rgui.AddDataSource(LAST_ID, 7);

	rgui.AddComponentText(AUTO_ID, 1, 0, 65, 100, 10, "Mag X: ", "", "");
	rgui.AddDataSource(LAST_ID, 8);
	rgui.AddComponentText(AUTO_ID, 1, 0, 70, 100, 10, "Mag Y: ", "", "");
	rgui.AddDataSource(LAST_ID, 9);
	rgui.AddComponentText(AUTO_ID, 1, 0, 75, 100, 10, "Mag Z: ", "", "");
	rgui.AddDataSource(LAST_ID, 10);

	rgui.AddComponentText(AUTO_ID, 1, 0, 85, 100, 10, "BMP180: ", "", " Pa");
	rgui.AddDataSource(LAST_ID, 11);
	rgui.AddComponentText(AUTO_ID, 1, 0, 90, 100, 10, " temp: ", "", "");
	rgui.AddDataSource(LAST_ID, 12);

	rgui.AddComponentText(AUTO_ID, 1, 0, 100, 100, 10, "Battery: ", "", "");
	rgui.AddDataSource(LAST_ID, 13);
	rgui.AddComponentProgressbar(AUTO_ID, 1, 0, 105, 100, 10, 100);
	rgui.AddDataSource(LAST_ID, 14);

	rgui.AddComponentButton(AUTO_ID, 1, 0, 130, 50, 15, "Status");
	rgui.AddAction(LAST_ID, "S");
	meas_id = rgui.AddComponentButton(AUTO_ID, 1, 50, 130, 50, 15, "Start");
	rgui.AddAction(LAST_ID, "M");

	rgui.SetScreenProperties(1, "Control");


	rgui.AddComponentChart(AUTO_ID, 2, 0, 0, 100, 100, -20000, 20000, 500);
	rgui.AddDataSource(LAST_ID, 0);
	rgui.AddDataSource(LAST_ID, 1);
	rgui.SetScreenProperties(2, "Bio");


	rgui.AddComponentChart(AUTO_ID, 3, 0, 0, 100, 100, -32767, 32767, 500);
	rgui.AddDataSource(LAST_ID, 2);
	rgui.AddDataSource(LAST_ID, 3);
	rgui.AddDataSource(LAST_ID, 4);
	rgui.SetScreenProperties(3, "Gyroscope");

	rgui.AddComponentChart(AUTO_ID, 4, 0, 0, 100, 100, -32767, 32767, 500);
	rgui.AddDataSource(LAST_ID, 5);
	rgui.AddDataSource(LAST_ID, 6);
	rgui.AddDataSource(LAST_ID, 7);
	rgui.SetScreenProperties(4, "Accelerometer");

	rgui.AddComponentChart(AUTO_ID, 5, 0, 0, 100, 100, -32767, 32767, 500);
	rgui.AddDataSource(LAST_ID, 8);
	rgui.AddDataSource(LAST_ID, 9);
	rgui.AddDataSource(LAST_ID, 10);
	rgui.SetScreenProperties(5, "Magnetometer");

	rgui.AddComponentChart(AUTO_ID, 6, 0, 0, 100, 100, 100300, 100600, 500);
	rgui.AddDataSource(LAST_ID, 11);
	rgui.SetScreenProperties(6, "Pressure");
}

uint8_t a=0;


void nv_demo_start_meas()
{
	DEBUG("Starting meas\n");
	led_anim(LED_BREATHG, 0xFF);
	buzzer_beep(400);

	ads1292.Start();

	l3gd20.Start();

	lsm303d.Start();
}

void nv_demo_stop_meas()
{
	ads1292.Stop();
	lsm303d.Stop();
	l3gd20.Stop();

	DEBUG("Stop meas\n");
	led_anim(LED_NO_ANIM);
	led_set(0, 0, 0xFF);

	buzzer_beep(100, 200, 100, 200, 100);
}

void nv_demo_rgui_irq()
{
	if (rgui.HelloPending())
	{
		nv_demo_gui_init();
	}

	if (rgui.ActionPending())
	{
		uint8_t c = rgui.ActionBuffer(0);

		switch(c)
		{

		case('S'):


			rgui.ClearScreen(RGUI_POPUP);

			rgui.SwitchScreen(RGUI_POPUP);

			rgui.SetScreenProperties(RGUI_POPUP, "System status");

			rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 0, 10, 80, 10, "ADS1292");
			if (ads1292.SelfTest())
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 10, 20, 10, "OK");
			else
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 10, 20, 10, "Error");

			_delay_ms(100);

			rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 0, 20, 80, 10, "BMP180");
			if (bmp180.SelfTest())
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 20, 20, 10, "OK");
			else
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 20, 20, 10, "Error");

			_delay_ms(100);

			rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 0, 30, 80, 10, "LSM303D");
			if (lsm303d.SelfTest())
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 30, 20, 10, "OK");
			else
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 30, 20, 10, "Error");

			_delay_ms(100);

			rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 0, 40, 80, 10, "L3GD20");
			if (l3gd20.SelfTest())
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 40, 20, 10, "OK");
			else
				rgui.AddComponentText(AUTO_ID, RGUI_POPUP, 80, 40, 20, 10, "Error");

			rgui.AddComponentButton(AUTO_ID, RGUI_POPUP, 20, 100, 60, 15, "Close");
			rgui.AddAction(LAST_ID, "X");

//			rgui.SwitchScreen(RGUI_POPUP);
		break;

		case('X'):
			rgui.ClearScreen(RGUI_POPUP);
		break;

		case('M'):
			if (meas_running)
			{
				meas_running = false;
				nv_demo_stop_meas();
				rgui.SetComponentProperty(meas_id, RGUI_PROPERTY_TEXT, "Start");
			}
			else
			{
				meas_running = true;
				nv_demo_start_meas();
				rgui.SetComponentProperty(meas_id, RGUI_PROPERTY_TEXT, "Stop");
			}


		break;
		}
	}
}

void nv_demo_bt_irq(uint8_t * param)
{
	DEBUG("BT IRQ %d\n", param[0]);

	switch (param[0])
	{
	case (BT_IRQ_PAIR):
		DEBUG("pair sucesfull\n");
	break;

//	case (BT_IRQ_INCOMING):
//		DEBUG("incoming connection %d\n", bt_link.AcceptConnection());
//	break;

	case (BT_IRQ_CONNECTED):
		buzzer_beep(100);
		led_anim(LED_NO_ANIM);
		led_set(0, 0, 0xFF);
	break;

	case (BT_IRQ_DISCONNECTED):
		buzzer_beep(500, 200, 50);
		DEBUG("device disconnected\n");
		nv_demo_stop_meas();
	break;

	case (BT_IRQ_ERROR):
		DEBUG("Error %d\n", param[1]);
	break;

	case (BT_IRQ_DATA):
		if (rgui.ParserStep(param[1]))
			nv_demo_rgui_irq();
	break;
	}
}

uint32_t timestamp = 0;
bool new_sample = false;

uint32_t bmp_next = 0;
bool bmp_temp = true;

void task_nv_demo_loop()
{
	uint8_t l;

	uint32_t atime = task_get_ms_tick();

	//YYY: Do NOT use RGUI in IRQ
	if (new_sample && meas_running)
	{
		SIGNAL1_HI
		new_sample = false;

		l = 0;
		l += rgui.GetLineLen(0);
		l += rgui.GetLineLen(1);
//		l += rgui.GetLineLen(2);
//		l += rgui.GetLineLen(3);
//		l += rgui.GetLineLen(4);
//		l += rgui.GetLineLen(5);
//		l += rgui.GetLineLen(6);
//		l += rgui.GetLineLen(7);
//		l += rgui.GetLineLen(8);
//		l += rgui.GetLineLen(9);
//		l += rgui.GetLineLen(10);
//		l += rgui.GetLineLen(11);
//		l += rgui.GetLineLen(12);
		l += rgui.GetLineLen(13);
		l += rgui.GetLineLen(14);

		rgui.StartStream(timestamp, l); //+RDAI=YYY,SILLT 15
		rgui.SendLine(0, ads_ch0); //INNNN 5
		rgui.SendLine(1, ads_ch1); //INNNN 5

//		rgui.SendLine(2, gyro_x);
//		rgui.SendLine(3, gyro_y);
//		rgui.SendLine(4, gyro_z);
//
//		rgui.SendLine(5, acc_x);
//		rgui.SendLine(6, acc_y);
//		rgui.SendLine(7, acc_z);
//
//		rgui.SendLine(8, mag_x);
//		rgui.SendLine(9, mag_y);
//		rgui.SendLine(10, mag_z);
//
//		rgui.SendLine(11, press);
//		rgui.SendLine(12, temp);

		rgui.SendLine(13, battery_adc_raw);
		rgui.SendLine(14, battery_per);

		rgui.SendCRC(); //C //1

		SIGNAL1_LO
	}
}

int32_t ch1_avg = 0;
int32_t ch2_avg = 0;


void task_nv_demo_irqh(uint8_t type, uint8_t * buff)
{
//	DEBUG("IRQ t:%d\n", type);

	switch (type)
	{
	case(TASK_IRQ_ADS):
		ads1292.ReadData();

		timestamp++;

//		if (timestamp % 12 == 0)
		{
			SIGNAL2_HI

			ads_ch0 = to_dec_3(ads1292.value_ch1);
			ads_ch1 = to_dec_3(ads1292.value_ch2);

			ch1_avg += (ads_ch0 - ch1_avg) / 512;
			ch2_avg += (ads_ch1 - ch2_avg) / 256;

			ads_ch0 -= ch1_avg;
			ads_ch0 *= 4;
			ads_ch0 += 10000;

			ads_ch1 -= ch2_avg + 10000;
			ads_ch1 *= -1;

//			l3gd20.ReadGyro(&gyro_x, &gyro_y, &gyro_z);
//
//
//			lsm303d.ReadAcc(&acc_x, &acc_y, &acc_z);
//
//
//			lsm303d.ReadMag(&mag_x, &mag_y, &mag_z);


			new_sample = true;

			SIGNAL2_LO
		}
	break;

	case(TASK_IRQ_BUTTON):
		if (buff[0] == BUTTON_HOLD)
		{
			buzzer_beep(_100ms, _100ms, _100ms, _100ms, _100ms);
			led_set(0xFF, 0, 0);
			_delay_ms(500);
			led_set(0, 0, 0);

			task_set(TASK_POWERDOWN);
		}
		test_memory();
	break;

	case(TASK_IRQ_BT):
		nv_demo_bt_irq(buff);
	break;
	}
}
