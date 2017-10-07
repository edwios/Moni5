// Environment monitor and logger
// Copyright (c) 2017 PROOVE AB
// All rights reserved.

#pragma SPARK_NO_PREPROCESSOR

#include "Particle.h"
#include "softap_http.h"
#include "application.h"
#include "moni5.h"
#include "SIM7500.h"
#include <sd-card-library-photon-compat.h>

#ifdef USESI7021
#   include "si7021.h"
#endif
#ifdef USEBME280
#   include "BME280.h"
#endif

SYSTEM_THREAD(ENABLED);
STARTUP(System.enableFeature(FEATURE_RETAINED_MEMORY));
SYSTEM_MODE(MANUAL);

retained  long stackCounter;
retained  long stackPointer;
retained  long allclearflag;
retained  long period = DEFAULT_PERIOD;
retained  bool timeSet;

// SOFTWARE SPI pin configuration - modify as required
// The default pins are the same as HARDWARE SPI
const uint8_t chipSelect = SDCS;      // Also used for HARDWARE SPI setup
const uint8_t mosiPin = SDMOSI;
const uint8_t misoPin = SDMISO;
const uint8_t clockPin = SDCLK;
// GPS Com unit
const uint8_t gpscomRst = GPSCOMRST;
const uint8_t gpscomPwr = GPSCOMPWR;

struct sensor_data_struct {
	float temperature;
	float humidity;
	uint32_t readtime;
};
sensor_data_struct sensor_data;
sensor_data_struct getUpdatedSensorReadingsForDisplay(void);

// SD-Card
char dataString[256];
char recordString[256];
char sdata[10];
int lpcount=10;

// GPS-WCDMA Unit
char gpsbuf[120];
bool gpscom_ready = false;
bool gpsEnabled = false;
unsigned int failed_mn,failed_gps, failed_gprs, failed_dcu, failed_3dfix, failed_timeSet, failed_up = 0;
bool ERR_dcu,ERR_mn,ERR_gps,ERR_gprs,ERR_3dfix,ERR_fatal = false;
bool hasSD = false;
bool hasRSSI = false;
bool hasSensorsReadings = false;
bool gpsComReady = false;
bool checkOnly = false;
bool displayOn = false;
bool wifi_on = false;
volatile bool displayButtonPressed = false;
volatile bool powerButtonPressed = false;
bool debug = false;

uint32_t wakeUpTime = 0, last_loop_millis = 0;
uint32_t ac_last = 0;
uint32_t ac = 0;
uint32_t cg = 0;
uint8_t rssi=0;
unsigned long ndelay = 10;

char devID[64];
char updateTime[20];
char jsonStr[256];
char sufx[5]="0000";
char wifimesg[22];

uint8_t state = 0, saved_state = 0, preserved_state = 0;

float latitude, longitude, speed_kph, heading, altitude;
float temperature, rhumidity = 0.0;
volatile int vbatt=0, rvbatt = 0;
int max_3dfix = MAX_3DFIX;
long sleepPeriod = 0;
unsigned long lpdur = 0, lpstart = 0;
bool abortWaitGPS = false;


#ifdef USESI7021
si7021 sensor;
#endif
#ifdef USEBME280
BME280 bme; // I2C
// bme(BME_CS); // hardware SPI
// bme(BME_CS, BME_MOSI, BME_MISO,  BME_SCK);
#endif
SIM7500 gpscom = SIM7500(gpscomRst, gpscomPwr);
Sd2Card card;
SdVolume volume;
SdFile root;
//SdFile dataFile;

void displayOnOff(void);
void powerOnOff(void);

// 1.3" 12864 OLED with SH1106 and Simplified Chinese IC for Spark Core
int Rom_CS = OLEDROMCS;
unsigned long  fontaddr=0;
char dispCS[32];

/****************************************************************************
*****************************************************************************
**************************   Wi-Fi SD Read   ********************************
*****************************************************************************
****************************************************************************/
void myPage(const char* url, ResponseCallback* cb, void* cbArg, Reader* body, Writer* result, void* reserved);
STARTUP(softap_set_application_page_handler(myPage, nullptr));

void myPage(const char* url, ResponseCallback* cb, void* cbArg, Reader* body, Writer* result, void* reserved)
{
	const char indexhtml[] = "<html><head><title>Download LOG.txt</title></head><body>Click <a href=\"/download\">here</a> to download the log file.</body></html>";
	Serial.printlnf("handling page %s", url);
	if (strcmp(url,"/download")==0) {
		// Download LOG.txt from SD card
		char header[128];
		char srec[] = RECORDFILE;
		char *curFilename;
		char *p = strrchr(srec, '/');
		curFilename = (p > 0)?p+1:srec;
		sprintf(header, "Content-Disposition: attachment; filename=%s\r\n", curFilename);
		Header h(header);
		cb(cbArg, 0, 200, "application/octet-stream", &h);
		if (debug) {
			Serial.printf("DEBUG: Read %s to Wi-Fi \r\n", RECORDFILE);
		}
		File dataFile = SD.open(RECORDFILE, FILE_READ);
		if (dataFile) {
			char rbuf[20];
			if (debug) {
				Serial.println("Dumping file to Wi-Fi...");
			}
			// if the file is available, read from it:
			while (dataFile.available()) {
				int l = dataFile.read(rbuf, 19);
				if (!l) break;      // We are done
				rbuf[l] = 0;
				if (debug) {
					Serial.print(rbuf);
				}
				result->write(rbuf);
			}
			// close the file:
			dataFile.close();
			if (debug) {
				Serial.println();
				Serial.println("DEBUG: Dumped");
			}
		} else {
			Serial.println("ERROR: Failed to open file. Nothing to send");
		}
		if (debug) {
			Serial.println("DEBUG: Closing Wi-Fi");
		}
	} else if (strcmp(url,"/index.html")==0) {
		// Present download page
		if (debug) {
			Serial.println("Serving index.html");
		}
		cb(cbArg, 0, 200, "text/html", nullptr);
		result->write(indexhtml);
	} else {
		cb(cbArg, 0, 404, nullptr, nullptr);
	}

}


/****************************************************************************
*****************************************************************************
**************************   MPU Power hack  ********************************
*****************************************************************************
****************************************************************************/

void lowPower(void) {
	RCC->CFGR &= ~0xfcf0;
	RCC->CFGR |= 0x00c0;    // RCC_CFGR_HPRE_DIV64 (SYSCLK divided by 64)

	SystemCoreClockUpdate();
	SysTick_Configuration();

	//    RGB.control(true);
	//    RGB.color(0, 0, 0);

	//    FLASH->ACR &= ~FLASH_ACR_PRFTEN;
}

void normalPower(void) {
	RCC->CFGR &= ~0xfcf0;
	// RCC->CFGR |= (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2);
	RCC->CFGR |= 0x9400;

	SystemCoreClockUpdate();
	SysTick_Configuration();

	//    RGB.control(true);
	//    RGB.color(0, 0, 0);

	//    FLASH->ACR &= ~FLASH_ACR_PRFTEN;
}

/****************************************************************************
*****************************************************************************
****************************   OLED Driver  *********************************
*****************************************************************************
****************************************************************************/


/*****************************************************************************
Funtion    :   OLED_WrtData
Description:   Write Data to OLED
Input      :   byte8 ucCmd
Output     :   NONE
Return     :   NONE
*****************************************************************************/
void transfer_data_lcd(byte ucData)
{
	Wire.beginTransmission(0x78 >> 1);
	Wire.write(0x40);      //write data
	Wire.write(ucData);
	Wire.endTransmission();
}

/*****************************************************************************
Funtion    :   OLED_WrCmd
Description:   Write Command to OLED
Input      :   byte8 ucCmd
Output     :   NONE
Return     :   NONE
*****************************************************************************/
void transfer_command_lcd(byte ucCmd)
{
	Wire.beginTransmission(0x78 >> 1);            //Slave address,SA0=0
	Wire.write(0x00);      //write command
	Wire.write(ucCmd);
	Wire.endTransmission();
}


/* OLED Initialization */
void initial_lcd()
{
	digitalWrite(Rom_CS, HIGH);
	if (!(Wire.isEnabled())) {
		Wire.setSpeed(DEFAULT_WIRE_SPEED);
		Wire.begin();
		delay(20);
	}
	transfer_command_lcd(0xAE);   //display off
	transfer_command_lcd(0x20); //Set Memory Addressing Mode
	transfer_command_lcd(0x10); //00,Horizontal Addressing Mode;01,Vertical Addressing Mode;10,Page Addressing Mode (RESET);11,Invalid
	transfer_command_lcd(0xb0); //Set Page Start Address for Page Addressing Mode,0-7
	transfer_command_lcd(0xc8); //Set COM Output Scan Direction
	transfer_command_lcd(0x00);//---set low column address
	transfer_command_lcd(0x10);//---set high column address
	transfer_command_lcd(0x40);//--set start line address
	transfer_command_lcd(0x81);//--set contrast control register
	transfer_command_lcd(0x7f);
	transfer_command_lcd(0xa1);//--set segment re-map 0 to 127
	transfer_command_lcd(0xa6);//--set normal display
	transfer_command_lcd(0xa8);//--set multiplex ratio(1 to 64)
	transfer_command_lcd(0x3F);//
	transfer_command_lcd(0xa4);//0xa4,Output follows RAM content;0xa5,Output ignores RAM content
	transfer_command_lcd(0xd3);//-set display offset
	transfer_command_lcd(0x00);//-not offset
	transfer_command_lcd(0xd5);//--set display clock divide ratio/oscillator frequency
	transfer_command_lcd(0xf0);//--set divide ratio
	transfer_command_lcd(0xd9);//--set pre-charge period
	transfer_command_lcd(0x22); //
	transfer_command_lcd(0xda);//--set com pins hardware configuration
	transfer_command_lcd(0x12);
	transfer_command_lcd(0xdb);//--set vcomh
	transfer_command_lcd(0x20);//0x20,0.77xVcc
	transfer_command_lcd(0x8d);//--set DC-DC enable
	transfer_command_lcd(0x14);//
	transfer_command_lcd(0xaf);//--turn on oled panel

}

void lcd_address(byte page,byte column)
{

	transfer_command_lcd(0xb0 + column);   /* Page address */
	transfer_command_lcd((((page + 1) & 0xf0) >> 4) | 0x10);  /* 4 bit MSB */
	transfer_command_lcd(((page + 1) & 0x0f) | 0x00); /* 4 bit LSB */
}

void clear_screen()
{
	unsigned char i,j;
	digitalWrite(Rom_CS, HIGH);
	for(i=0;i<8;i++)
	{
		transfer_command_lcd(0xb0 + i);
		transfer_command_lcd(0x00);
		transfer_command_lcd(0x10);
		for(j=0;j<132;j++)
		{
			transfer_data_lcd(0x00);
		}
	}

}

void display_128x64(byte *dp)
{
	unsigned int i,j;
	for(j=0;j<8;j++)
	{
		lcd_address(0,j);
		for (i=0;i<132;i++)
		{
			if(i>=2&&i<130)
			{
				// Write data to OLED, increase address by 1 after each byte written
				transfer_data_lcd(*dp);
				dp++;
			}
		}

	}

}


/*
Display a 5x7 dot matrix, ASCII or a 5x7 custom font, glyph, etc.
*/

void display_graphic_5x7(unsigned int page,byte column,byte *dp)
{
	unsigned int col_cnt;
	byte page_address;
	byte column_address_L,column_address_H;
	page_address = 0xb0 + page - 1;//



	column_address_L =(column&0x0f);  // -1
	column_address_H =((column>>4)&0x0f)+0x10;

	transfer_command_lcd(page_address);     /*Set Page Address*/
	transfer_command_lcd(column_address_H); /*Set MSB of column Address*/
	transfer_command_lcd(column_address_L); /*Set LSB of column Address*/

	for (col_cnt=0;col_cnt<6;col_cnt++)
	{
		transfer_data_lcd(*dp);
		dp++;
	}
}

/**** Send command to Character ROM ***/
void send_command_to_ROM( byte datu )
{
	SPI.transfer(datu);
}

/**** Read a byte from the Character ROM ***/
byte get_data_from_ROM( )
{
	byte ret_data=0;
	ret_data = SPI.transfer(255);
	return(ret_data);
}


/*
*     Read continuously from ROM DataLen's bytes and
*     put them into pointer pointed to by pBuff
*/

void get_n_bytes_data_from_ROM(byte addrHigh,byte addrMid,byte addrLow,byte *pBuff,byte DataLen )
{
	byte i;
	digitalWrite(Rom_CS, LOW);
	delayMicroseconds(100);
	send_command_to_ROM(0x03);
	send_command_to_ROM(addrHigh);
	send_command_to_ROM(addrMid);
	send_command_to_ROM(addrLow);

	for(i = 0; i < DataLen; i++ ) {
		*(pBuff+i) =get_data_from_ROM();
	}
	digitalWrite(Rom_CS, HIGH);
}


/******************************************************************/

void display_string_5x7(byte y,byte x,const char *text)
{
	unsigned char i= 0;
	unsigned char addrHigh,addrMid,addrLow ;
	while((text[i]>0x00))
	{

		if((text[i]>=0x20) &&(text[i]<=0x7e))
		{
			unsigned char fontbuf[8];
			fontaddr = (text[i]- 0x20);
			fontaddr = (unsigned long)(fontaddr*8);
			fontaddr = (unsigned long)(fontaddr+0x3bfc0);
			addrHigh = (fontaddr&0xff0000)>>16;
			addrMid = (fontaddr&0xff00)>>8;
			addrLow = fontaddr&0xff;

			get_n_bytes_data_from_ROM(addrHigh,addrMid,addrLow,fontbuf,8);/*取8个字节的数据，存到"fontbuf[32]"*/

			display_graphic_5x7(y,x+1,fontbuf);/*显示5x7的ASCII字到LCD上，y为页地址，x为列地址，fontbuf[]为数据*/
			i+=1;
			x+=6;
		}
		else
		i++;
	}

}


bool sdInit() {
	if (debug) Serial.print("\nInitializing SD card...");

	if (!SD.begin(SDCS)) {

		// we'll use the initialization code from the utility libraries
		// since we're just testing if the card is working!
		// Initialize HARDWARE SPI with user defined chipSelect
		if (!card.init(SPI_FULL_SPEED, chipSelect)) {
			// Initialize SOFTWARE SPI (uncomment below and comment out above line to use)
			//  if (!card.init(mosiPin, misoPin, clockPin, chipSelect)) {
			Serial.println("ERROR: SD card initialization failed.");
			return false;
		} else {
			Serial.println("INFO: Found SD Card.");
		}

		if (debug) {
			// print the type of card
			Serial.print("\nCard type: ");
			switch(card.type()) {
				case SD_CARD_TYPE_SD1:
				Serial.println("SD1");
				break;
				case SD_CARD_TYPE_SD2:
				Serial.println("SD2");
				break;
				case SD_CARD_TYPE_SDHC:
				Serial.println("SDHC");
				break;
				default:
				Serial.println("Unknown");
			}
		}

		// Now we will try to open the 'volume'/'partition' - it should be FAT16 or FAT32
		if (!volume.init(card)) {
			Serial.println("ERROR: Could not find FAT16/FAT32 partition.\nMake sure you've formatted the card");
			return false;
		}
		if (!root.openRoot(volume)) {
			Serial.println("ERROR: openRoot failed");
			return false;
		}
		return (SD.begin(SDCS));
	} else {
		if (debug) Serial.println("DEBUG: SD init OK");
		return true;
	}
}

void allClear(bool clearSD) {
	Serial.println("ALL CLEAR Mode!!");
	initOLEDDisplay();
	displayOn = true;
	stackPointer = 0;
	stackCounter = 0;
	timeSet = false;                                                // Dirty time in RTC!!
	gpsComReady = false;
	period = DEFAULT_PERIOD;
	allclearflag = 0xDEADBEEF;
	if (hasSD && clearSD) {
		if (SD.exists("LOG")) {
			SD.rmRfStar("LOG");
		}
		SD.mkdir("LOG");
	}
}


void setup(){
	wakeUpTime = millis();                                          // record the startup time
	pinMode(ALLCLEAR, INPUT_PULLDOWN);
	pinMode(CHECKPIN, INPUT_PULLDOWN);
	pinMode(Rom_CS, OUTPUT);
	pinMode(DISPLAYON, INPUT_PULLDOWN);
	pinMode(D7, OUTPUT);
	//    pinMode(VBAT, INPUT); // Must NOT set pin mode according to Photon reference manual
	//    setADCSampleTime(ADC_SampleTime_112Cycles);
	digitalWrite(D7, LOW);
	RGB.control(true);
	RGB.brightness(0);                                              // Turn off RGB
	WiFi.off();                                                     // We don't need Wi-Fi at phase 1
	period==0?DEFAULT_PERIOD:period;
	checkOnly = false;                                              // CHECKPIN is On, checkOnly mode
	if (digitalRead(DISPLAYON) == 1) {
		displayOn = true;
		debug = true;
	}
	Time.setFormat(TIME_FORMAT);
	Serial.begin(57600);

	#ifdef USESI7021
	sensor.begin();
	sensor.resetSettings();                                         // Reset TH sensor
	#endif
	#ifdef USEBME280
	bme.begin();
	#endif
	if (debug) delay(2000);
	gpscom.setUserAgent(USERAGENT);                                 // Set UA
	gpscom.setGPRSNetworkSettings(GPSCOMAPN,NULL,NULL);                         // Set APN

	//    byte mac[6];
	//    WiFi.macAddress(mac);
	//    sprintf(devID, "%02x%02x%02x%02x%02x%02x", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
	//    devID[12]=0;                                                    // Safe guard in case above failed
	String sdevID = System.deviceID();
	sdevID.toCharArray(devID, 63);
	devID[63]=0;                                                    // Safe guard in case above failed
	strncpy(sufx,devID,4);
	SPI.begin();
	SPI.setBitOrder(MSBFIRST);
	SPI.setDataMode(SPI_MODE3);
	SPI.setClockDivider(DEFAULT_SPI_CLOCK_DIV);
	initOLEDDisplay();
	delay(100);
	displayOn = true;

	hasSD = sdInit();
	if (allclearflag != 0xDEADBEEF) {                               // Are we started afresh?
		allClear(false);
	}
	if (hasSD) {
		if (!SD.exists("LOG")) {
			SD.mkdir("LOG");
		}
	}

	attachInterrupt(DISPLAYON, displayOnOff, RISING, 1);
	attachInterrupt(ALLCLEAR, powerOnOff, RISING, 1);

	display_line("      HYPA-T1A     ", 2);
	display_line(" (c)2016 ioStation ", 3);
	display_line("    System Ready   ", 5);
	//  display_line("IOSTATION v1.5d.002 ", 8);
	display_line(USERAGENT, 8);
	Serial.printf("INFO: HYPA-T1A System Ready %s\n\r", devID);
	state = DEFAULT_INIT_STATE;
	displayOn = false;
	delay(3000);
	clear_screen();
	System.set(SYSTEM_CONFIG_SOFTAP_PREFIX, "HYPA-T1A");
	System.set(SYSTEM_CONFIG_SOFTAP_SUFFIX, sufx);
}

void initOLEDDisplay() {
	digitalWrite(Rom_CS, HIGH);
	initial_lcd();
	clear_screen();    //clear all dots
	//    if (displayOn) display_line("(c)2016 ioStation", 8);
}

sensor_data_struct getUpdatedSensorReadingsForDisplay(void) {
	String st;
	sensor_data_struct _sensor_data;
	#ifdef USESI7021
	// Measure RH
	_sensor_data.humidity = round(sensor.getRH()*100.0)/100.0;
	// Measure Temperature
	_sensor_data.temperature = round(sensor.readTemp()*100.0)/100.0;     // 2016-06-25 changed from getTemp() to readTemp()
	#endif
	#ifdef USEBME280
	// Measure RH
	_sensor_data.humidity = round(bme.readHumidity()*100.0)/100.0;
	// Measure Temperature
	_sensor_data.temperature = round(bme.readTemperature()*100.0)/100.0;
	#endif
	_sensor_data.readtime = Time.now();
	//    st = Time.format(_sensor_data.readtime);
	//    st.toCharArray(updateTime, 20);
	//    updateTime[19]=0;       // Safe guard in case above failed
	return _sensor_data;
}

bool sendHTTP(char *s) {
	uint16_t httpstatus = 0;
	bool errflag = false;
	bool resetPeriod = false;

	if (debug) {
		Serial.print("DEBUG: HTTP Send: ");
		Serial.println(s);
	}
	if (s) {
		gpscom.HTTP_POST(SERVERFQDN, SERVERPORT, SERVERPATH, s, &httpstatus, HTTPTIMEOUT, true, jsonStr);
	}
	if (httpstatus == 200) {
		if (debug) Serial.println("POSTed");
		if (jsonStr) {
			// Parse JSON string
			if (debug) Serial.printf("DEBUG: JSON=%s\n\r",jsonStr);
			int l = strlen(JSON_PERIODSTR);
			char *start = strstr(jsonStr, JSON_PERIODSTR);
			if (start) {
				start += l;
				char *end = strchr(start, '"');     // ...{"interval":"3000",
				if (!end) {
					end = strchr(start, ',');
					if (!end) {
						// Error, cannot locate a valid JSON value, wrong string?
						if (debug) {
							Serial.println("ERROR: cannot locate a valid JSON value, wrong string?");
							Serial.printf("DEBUG: Found %s\n\r", start);
						}
					}
				}
				if (end) {
					*end=0;
					if (debug) Serial.printf("DEBUG: Got new period %s s\n\r", start);
					period = atoi(start);
					if (period < 100) period = DEFAULT_PERIOD;      // Safe guard against mal-values
				} else {
					resetPeriod = true;
				}
			} else {
				// Error, cannot locate a valid JSON value, wrong string?
				if (debug) {
					Serial.println("ERROR: cannot locate a valid JSON pair, wrong string?");
				}
				resetPeriod = true;
			}
		} else {
			resetPeriod = true;
		}
	} else {
		Serial.print("ERROR: POST failed with error ");
		Serial.println(httpstatus);
		errflag = true;
	}
	if (resetPeriod || errflag) {
		period = DEFAULT_PERIOD;                                    // Safe guard against mising values
	}
	return !errflag;
}

void clearLastGPS() {
	latitude = longitude = 0.0;
}

void gotoSleep(long t) {
	gpscom.end();
	// Deep sleep
	if (t <= 0) t = period;
	if (debug) {
		Serial.print("DEBUG: Sleeping for ");
		Serial.print(t);
		Serial.println("s");
		Serial.flush();
	}
	clear_screen();
	System.sleep(SLEEP_MODE_DEEP,t);         // 2016-06-25 no longer feasible to use DEEP_SLEEP due to button wake up
}

void display_line(char *s, int n) {
	if (!displayOn) return;                 // We don't do no display when it is not on
	int sl = strlen(s);
	if (sl>20) return;                      // Todo: Gracefully handle s>16
	strcpy(dispCS, s);
	int l = (20 - sl);
	if (l>0) strncpy(dispCS+sl, "                    ", l);
	dispCS[20]=0;
	display_string_5x7(n, 1, dispCS);
}

void display_status(char *s) {
	display_line(s, STATUSLINE);
}

void displayOnOff(void) {
	static unsigned long last_interrupt_time = 0;
	unsigned long interrupt_time = millis();
	if (interrupt_time - last_interrupt_time > 50)  // debounce time = 50ms
	{
		displayButtonPressed = true;
	}
	last_interrupt_time = interrupt_time;
}

void powerOnOff(void) {
	// detachInterrupt(ALLCLEAR);
	static unsigned long last_interrupt_time_power = 0;
	unsigned long interrupt_time = millis();
	if (interrupt_time - last_interrupt_time_power > 50)  // debounce time = 50ms
	{
		powerButtonPressed = true;
	}
	last_interrupt_time_power = interrupt_time;
}

void powerdown(bool really, uint32_t stime) {
	// ALLCLEAR holded for >10s, power down
	if (debug) {
		if (really) {
			Serial.println("DEBUG: Power down");
		} else {
			Serial.println("DEBUG: Sleep");
		}
	}
	initOLEDDisplay();
	displayOn = true;
	display_line("== SHUTDOWN ==", 4);
	sprintf(dispCS, "BATT: %d%%/%d   ", vbatt, rvbatt);
	display_line(dispCS, 5);
	gpscom.enableGPS(false);                    // stop GPS properly
	gpsEnabled = false;                         // Set GPS not enabled
	if (!((state == LOWPOWER) || (state == MINPOWER))) {
		gpscom.end();                           // Properly shutdown GPSCOM but only when it has not done so
	}
	clear_screen();
	displayOn = false;
	digitalWrite(D7,LOW);
	gpsComReady = false;                        // Without this, GPSCOM will NOT init after wakeup
	hasRSSI = false;
	hasSensorsReadings = false;                  // Clear sensor flag
	lpstart = 0;
	if (really) {
		delay(5000);
		state = WAKEUP;
		System.sleep(ALLCLEAR, RISING);
	} else {
		state = LOWPOWER;
		lpdur = stime*1000;
		lpstart = millis();
		lowPower();
		//        System.sleep(ALLCLEAR, RISING, stime);  // Sleep until ALLCLEAR (or RST) pressed
	}
}

void loop(){
	String supdateTime;
	char _updateTime[20];
	char gpstime[10]="", gpsdate[7]="";
	sensor_data_struct _sensor_data;
	int vb = 0;

	if (state != WAIT_STATE) ndelay = 10;       // Default 10ms
	sleepPeriod = period;
	if (displayButtonPressed) {
		if (debug) {
			Serial.println("Display button pressed");
		}
		while (digitalRead(DISPLAYON) == HIGH);
		displayButtonPressed = false;

		if (debug && displayOn && !WiFi.listening()) {          // Only turn off debug mode when WiFi is off
			debug = false;
		}
		if (!displayOn) {
			if ((state == LOWPOWER) || (state == MINPOWER)) {   // Not necessary but display will response faster
				normalPower();
			}
			displayOn = true;
			initOLEDDisplay();
			clear_screen();
			digitalWrite(D7, HIGH);
			if (WiFi.listening()) {                             // Otherwise screen will be very blank :(
				display_status(wifimesg);
			}
		} else {
			displayOn = false;
			clear_screen();
			digitalWrite(D7, LOW);
			if ((state == LOWPOWER) || (state == MINPOWER)) {   // Restore low power mode when done
				lowPower();
			}
		}
	}
	if (powerButtonPressed) {
		normalPower();                          // Restore processor speed
		ac_last = millis();
		RGB.control(true);
		RGB.brightness(255);
		RGB.color(0,255,0);
		while (digitalRead(ALLCLEAR) == HIGH) {
			ac = millis() - ac_last;
			if ((ac > 3000) && (ac < 6000)) {  // 3s -> Yellow light
				RGB.control(true);
				RGB.brightness(255);
				RGB.color(255,255,0);
			} else if (ac > 6000 && ac < 10000) { // 6-10s purple light
				RGB.control(true);
				RGB.brightness(255);
				RGB.color(255,0,255);
			} else if (ac > 10000) {            // 10s -> Red light
				RGB.control(true);
				RGB.brightness(255);
				RGB.color(255,0,0);
			}
		}
		powerButtonPressed = false;
		RGB.brightness(0);
		if ((ac < 1000) && (ac > 0)) {                        // <1s
			/*
			*** Not encouraged for users as it might disrupt normal operation of the device.
			*** Using 999ms as the code word for internal use. Chance of hitting
			*** exactly 999ms by physically pressing the button is slim...
			***/
			if (ac == 999) {                    // Internal call from sleep finished
				// wake up and restart normal operation
				wakeUpTime = millis();          // record the startup time
				state = WAKEUP;
				ac = 0;
			} else {
				ac = 0;

				if (((state == LOWPOWER) || (state == MINPOWER)) && !displayOn) {
					// Resume low/min power mode, only when display is not active
					lowPower();
				}

				if ((state == WAITGPS) || ((state == WAIT_STATE) && (saved_state == WAITGPS))) {
					abortWaitGPS = true;            // Abort waiting for a loooong GPS fix
				}
				if (debug) {
					if (WiFi.listening()) {
						Serial.println("Wi-Fi OFF");
						display_status("Wi-Fi OFF");

						WiFi.listen(false);
						WiFi.off();
						saved_state = preserved_state;
						state = WAIT_STATE;
						if (!hasSensorsReadings) {
							state = WAKEUP;
						}
						ndelay=1000;
						if (debug) {
							Serial.printf("Restoring state to %d\r\n", saved_state);
						}
					} else {
						WiFi.on();
						WiFi.listen();
						sprintf(wifimesg, "Wi-Fi On %s", sufx);
						Serial.println(wifimesg);
						display_status(wifimesg);
						if (state == WAIT_STATE) {
							preserved_state = saved_state;      // if system is waiting, we resume to the saved state
						} else {
							preserved_state = state;
						}
						state = WAIT_STATE;
						saved_state = state;
						ndelay=5000;
						if (debug) {
							Serial.printf("Preserving %d state\r\n", preserved_state);
						}
					}
				}
			}
		}
		if (ac > 3000 && ac < 6000) {           // 3-6s debug mode
			ac = 0;
			debug = true;
			displayOn = true;
			//            delay(300000);
			//            state=ABORT;
			/*
			ndelay=15000;
			saved_state = state;
			state=WAIT_STATE;
			*/
		}
		if ((ac > 6000) && (ac < 10000)) {      // 6-10s, power off
			ac = 0;
			powerdown(real, 0);
		}
		if (ac > 10000) {
			// ALLCLEAR not pressed or released
			ac = 0;
			initOLEDDisplay();
			displayOn = true;
			allClear(true);         // Perform a re-init and clean the SD
			display_line("== ALL CLEAR ==", 4);
			delay(5000);
			System.reset();
		}
		/*
		if (wifi_on) {
		if (debug) Serial.println("DEBUG: Wi-Fi On");
		wifi_on = false;
		ac = 0;
		RGB.brightness(255);
		RGB.control(false);                                     // Return control to system
		WiFi.on();
		waitFor(Particle.connected, 30000);
	}
	*/
}

if (((state != WAIT_STATE) && (state != LOWPOWER) && (state != MINPOWER)) || (displayOn && (state != WAIT_STATE))) {
	int vb1 = analogRead(VBATTINP);
	int vb2 = analogRead(VBATTINP);
	if (vb1 > vb2) {    // Take the larger value to avoid mis-read
		vb = vb1;
	} else {
		vb = vb2;
	}
	rvbatt = vb;
	if (debug) {
		Serial.print("DEBUG: VBatt=");
		Serial.println(vb);
	}
	vbatt = (vb - MINVBATT) * 100 / (MAXVBATT - MINVBATT);
	if (vbatt < 0) vbatt = 0;
	if (vbatt > 100) vbatt = 100;
	if ((vbatt < THRES02) && (state == WAKEUP) && !debug) {  // Low battery, only set lowbatt in start to avoid mis-fired
		state = LOWBATT;
	}
}

if ((state != WAIT_STATE) && displayOn) {
	_sensor_data = getUpdatedSensorReadingsForDisplay();
	supdateTime = Time.format(_sensor_data.readtime);
	supdateTime.toCharArray(_updateTime, 20);
	_updateTime[19]=0;       // Safe guard in case above failed
	display_line(_updateTime, 1);
	sprintf(dispCS, "%.2fC, %.2f%%    ", _sensor_data.temperature, _sensor_data.humidity);
	display_line(dispCS, 2);
	sprintf(dispCS, "Lat: %f  ", latitude);
	display_line(dispCS, 3);
	sprintf(dispCS, "Log: %f  ", longitude);
	display_line(dispCS, 4);
	sprintf(dispCS, "BATT: %d%%   ", vbatt);
	display_line(dispCS, 5);
	sprintf(dispCS, "RSSI: %d   ", rssi);
	display_line(dispCS, 6);
}

if (state == WAKEUP) {
	if (debug) Serial.println("DEBUG: State (WAKEUP)");
	display_status("Wakeup");
	state = PWRGPSCOM;
	if (!timeSet || !hasRSSI) {
		state = SYNCLK;
	} else {
		sensor_data = getUpdatedSensorReadingsForDisplay();
		// Measure RH
		rhumidity = sensor_data.humidity;
		// Measure Temperature
		temperature = sensor_data.temperature;
		supdateTime = Time.format(sensor_data.readtime);
		supdateTime.toCharArray(updateTime, 20);
		updateTime[19]=0;       // Safe guard in case above failed
		hasSensorsReadings = true;

		/*
		* SI7021 only
		* Temperature is measured every time RH is requested.
		* It is faster, therefore, to read it from previous RH
		* measurement instead with readTemp()
		* float t = sensor.readTemp();
		* To play switch on/off onboard heater use heaterOn() and heaterOff()
		* heaterOn();
		* delay(5000);
		* heaterOff();
		*/
		if (debug) {
			Serial.print("DEBUG: T=");
			Serial.print(temperature);
			Serial.print("C, RH=");
			Serial.print(rhumidity);
			Serial.println("%");
		}
	}
} else

if (state == PWRGPSCOM) {
	if (debug) Serial.println("DEBUG: State (PWRGPSCOM)");
	display_status("Power on Com Unit");
	state = INITGPSCOM;
	if (!gpsComReady) {         // Why bother if gpscom is already started?
		if (debug) Serial.println("DEBUG: Powering on COM unit");
		gpscom.begin();
		ndelay = 5000;          // Wait 5 second before we can reset (init)
	}
} else

if (state == INITGPSCOM) {
	if (debug) Serial.println("DEBUG: State (INITGPSCOM)");
	display_status("Init Com Unit");
	// Initialize GPSCOM Unit
	state = WAITGPSCOM;     // Default next state
	if (!gpsComReady) {
		if (debug) Serial.println("DEBUG: Initializing COM unit");
		/* Moved into if() so as not to disrupt the previous RSSI reading and create dead loop:
		* state = SYNCLK - timeset, ~hasRSSI, GPRS
		*/
		hasRSSI = false;       // Clear RSSI flag
		if (gpscom.init()) {
			// GPSCOM Ready
			gpsComReady = true;
		}
	}
} else

if (state == WAITGPSCOM) {
	if (debug) Serial.println("DEBUG: State (WAITGPSCOM)");
	display_status("Wait Com Unit");
	if (!gpsComReady) {
		gpsComReady = gpscom.poke();
	}
	if (gpsComReady) {
		if (hasSensorsReadings) {
			state = WAITGPS;
		} else {
			state = WAKEUP;
		}
		gpscom.clearSMS();
		if (!timeSet) state = SYNCLK;
		failed_dcu=0;
	} else {
		// GPSCOM NA
		failed_dcu++;
		if (failed_dcu > MAX_DCU) {
			failed_dcu = 0;
			state = PUSHSD;
		} else {
			ndelay = 1000;                  // Wait 1s between each unsuccessful poke
		}
	}
} else

if (state == INITGPS) {
	if (debug) Serial.println("DEBUG: State (INITGPS)");
	display_status("Init GPS");
	state = WAITGPS;        // Default next state
	gpsEnabled=gpscom.enableGPS(true);
	if (gpsEnabled) {
		int8_t stat = gpscom.GPSstatus();
		if (stat >= 2) {
			state = READGPS;    // Got 3D fix, move to next phase
		}
	} else {
		// GPS cannot enabled
		failed_gps++;
		if (failed_gps > MAX_GPS) {
			failed_gps=0;
			clearLastGPS();
			if (!checkOnly) state = PUSHSD;     // No GPS, push to SD anyway
		}
	}
} else

if (state == WAITGPS) {
	if (debug) Serial.println("DEBUG: State (WAITGPS)");
	display_status("Wait GPS");
	state = WAITGPS;        // Default next state
	if (gpsEnabled) {
		int8_t stat = gpscom.GPSstatus();
		int gpsss = gpscom.GPSsignal();
		sprintf(dispCS, "Wait GPS (%d)", gpsss);
		display_line(dispCS, STATUSLINE);
		if (debug) {
			Serial.printf("DEBUG: GPS Signal strength %d\n\r", gpsss);
		}
		if (stat < 2) {
			failed_3dfix++;
			if ((failed_3dfix > max_3dfix) || abortWaitGPS) {
				failed_3dfix = 0;
				abortWaitGPS = false;   // Reset flag
				state = PUSHSD;         // No 3D fix on GPS, push to SD anyway if not checking
			} else {
				ndelay=1000;            // Wait 1s between failed 3D fix
			}
		} else {
			failed_3dfix = 0;
			state = READGPS;
		}
	} else {
		// GPS not enabled, go back! (impossible case, may create dead loop!)
		state = INITGPS;
	}
} else

if (state == READGPS) {
	bool gps_success = false;
	if (debug) Serial.println("DEBUG: State (READGPS)");
	display_status("Read GPS");
	if (!checkOnly) {
		state = PUSHSD;         // Default next state
	} else {
		state = WAKEUP;
	}
	rssi = gpscom.getRSSI();    // 2016-06-12 Get RSSI for PUSHSD since GPRS won't init before this
	if (gpsEnabled) {
		gps_success = gpscom.getGPS(gpsbuf, &latitude, &longitude, gpsdate, gpstime, &speed_kph, &heading, &altitude);
	} else {
		// GPS not enabled, go back! (impossible case, may create dead loop!)
		state = INITGPS;
	}
	if (!gps_success) {
		if (failed_gps > MAX_GPS) {
			failed_gps=0;
			clearLastGPS();
			if (!checkOnly) {
				state = PUSHSD;     // No GPS, push to SD anyway
			} else {
				state = WAKEUP;
			}
		}
	} else {
		// gpsEnabled=gpscom.enableGPS(false);     // 2016-06-12 ONLY if battery is an issue, we don't stop GPS too early
	}
} else

if (state == PUSHSD) {
	if (debug) Serial.println("DEBUG: State (PUSHSD)");
	display_status("Write SD");
	state = ABORT;
	sprintf(dataString, SERVER_STR_FMT, devID, temperature, rhumidity, latitude, longitude, updateTime, vbatt, rssi);
	sprintf(recordString, "%s,%.2f,%.2f,%f,%f,%s,%d,%d", devID, temperature, rhumidity, latitude, longitude, updateTime, vbatt, rssi);
	if (debug) {
		Serial.print("DEBUG: DataString length ");
		Serial.println(strlen(dataString));
	}
	if (!hasSD) {
		state = INITGPRS;
		Serial.println("ERROR: SD Card not present");
	} else {
		char curFilename[8];
		sprintf(curFilename, FILENAMEFMT, SDFILEPREFIX, stackPointer);
		if (debug) {
			Serial.print("DEBUG: PUSH ");
			Serial.println(curFilename);
		}

		File dataFile = SD.open(curFilename, O_CREAT | O_TRUNC | O_RDWR );
		// if the file is available, write to it:
		if (dataFile) {
			dataFile.print(dataString);
			dataFile.close();
			stackPointer++;
			state = INITGPRS;       // On stack, now send with the rest
		} else {
			state = ABORT;
			Serial.println("FATAL: Cannot write to SD");
		}

		strcpy(curFilename, RECORDFILE);
		dataFile = SD.open(curFilename, O_CREAT | O_RDWR | O_APPEND );
		// if the file is available, write to it:
		if (dataFile) {
			dataFile.println(recordString);
			dataFile.close();
		} else {
			state = ABORT;
			Serial.println("FATAL: Cannot write to SD");
		}
	}
} else

if (state == POPSD) {
	if (debug) Serial.println("DEBUG: State (POPSD)");
	display_status("Read SD");
	state = ABORT;
	if (!hasSD) {
		state = HTTPSEND;
		Serial.println("FATAL: SD Card not present");
		//            } else if (vbatt < THRES02) {  // Low battery
		//                    state = LOWBATT;
	} else {
		char curFilename[8];
		long sp = stackPointer - 1;
		if (sp >= 0) {
			// Stack not empty
			sprintf(curFilename, FILENAMEFMT, SDFILEPREFIX, sp);
			if (debug) {
				Serial.print("DEBUG: POP ");
				Serial.println(curFilename);
			}
			File dataFile = SD.open(curFilename, FILE_READ);
			char *dsp = dataString;
			char rbuf[100];
			// if the file is available, read from it:
			state = HTTPSEND;
			while (dataFile.available()) {
				int l = dataFile.read(rbuf, 100);
				strcpy(dsp,rbuf);
				dsp+=l;
				if ((dsp-dataString) > 200) {
					Serial.println("FATAL: Overflow while reading file");
					state = ABORT;
					dsp = dataString;
				}
				*dsp = 0;
			}
			// close the file:
			dataFile.close();
		} else {
			// Stack empty
			state = ALLDONE;
		}
	}
} else

if (state == HTTPSENDOK) {
	if (debug) Serial.println("DEBUG: State (HTTPSENDOK)");
	display_status("Upload OK");
	dataString[0] = 0;  // Clear data buffer
	state = POPSD;
	if (!hasSD) {
		state = ABORT;
		Serial.println("FATAL: SD Card not present");
	} else {
		char curFilename[8];
		long sp = stackPointer - 1;
		if (sp >= 0) {
			// Stack not empty
			sprintf(curFilename, FILENAMEFMT, SDFILEPREFIX, sp);
			if (debug) {
				Serial.print("DEBUG: DEL ");
				Serial.println(curFilename);
			}
			if (SD.remove(curFilename)) {
				// Stack object removed successfully, decrease sp by 1
				if (stackPointer >= 0) {
					stackPointer--;
					// gpscom.enableGPRS(false);       // Not disable but close all sockets
					//ndelay=2000;                // wait 2s before next upload
				}
			} else {
				Serial.print("FATAL: File ");
				Serial.print(curFilename);
				Serial.println("cannot be removed");
				state = ABORT;
			}
		} else {
			// Empty stack!!
			Serial.println("FATAL: Empty Stack");
			state = ABORT;
		}
	}
} else

if (state == INITGPRS) {
	if (debug) Serial.println("DEBUG: State (INITGPRS)");
	display_status("Init GPRS");
	state = ENAGPRS;     // Default next state
	if (!gpsComReady) {
		state = PWRGPSCOM;                         // Who forgot to init the com unit?
	} else {
		rssi = gpscom.getRSSI();
		uint8_t netstatus = gpscom.getNetworkStatus();
		if (debug) {
			Serial.print("INFO: RSSI: ");
			Serial.println(rssi);
		}
		if (netstatus == 1 || netstatus == 5) {     // Has signal and has Mobile Network
			rssi = gpscom.getRSSI();
			hasRSSI= true;
			if (debug) {
				Serial.print("INFO: GPRS Connected RSSI: ");
				Serial.println(rssi);
			}
			if (rssi > MIN_RSSI) {
				netstatus = gpscom.getNetworkMode();
				if (netstatus >= 2) {                   // Mobile Data
					if (debug) Serial.println("INFO: Attached to a Mobile netowrk");
					failed_mn = 0;
				} else {
					// No mobile data available
					failed_mn++;
					state = INITGPRS;
					ndelay = 2000;
				}
			} else {
				// No reliable mobile network available (signal too weak)
				failed_mn++;
				state = INITGPRS;
				ndelay=2000;
			}
		} else {
			// No mobile network available
			failed_mn++;
			state = INITGPRS;
			ndelay=2000;
		}
		if ((failed_mn > MAX_MN)) {
			failed_mn = 0;
			state = ALLDONE;                        // No network. Next time, bye bye
		}
	}
} else

if (state == ENAGPRS) {
	if (debug) Serial.println("DEBUG: State (ENAGPRS)");
	display_status("Enable GPRS");
	state = SYNCLK;       // Default next state
	uint8_t netstatus = gpscom.getNetworkMode();
	if (netstatus >= 2) {
		gpscom.enableGPRS(true);
		if (gpscom.GPRSstate() != 1) {
			// GPRS not active
			state = ENAGPRS;
			failed_gprs++;
			if (failed_gprs > MAX_GPRS) {
				failed_gprs = 0;
				state = ALLDONE;                 // No gprs. Next time, bye bye
			}
		}
	} else {
		state = INITGPRS;
	}
} else

if (state == SYNCLK) {
	if (debug) Serial.println("DEBUG: state (SYNCLK)");
	display_status("Sync Clock");
	if (hasSensorsReadings) {
		state = POPSD;
	} else {
		state = WAKEUP;
	}
	//        rssi = gpscom.getRSSI();    // If time already set, get rssi for PUSHSD use since INITGPRS is after
	if (!timeSet || !hasRSSI) {
		state = WAKEUP;
		if (gpscom.GPRSstate() != 1) {
			if (debug) Serial.println("WARN: No GPRS");
			state = INITGPRS;
		} else if (!timeSet) {
			if (debug) Serial.println("WARN: Time not set");
			if (gpscom.syncNetworkTime(NETTIMESERVER, TIMESET_TIMEOUT_MS+failed_timeSet*5000)) {
				char dateTime[25];
				gpscom.readRTC(dateTime);
				if (debug) {
					Serial.print("DEBUG: Network time: ");
					Serial.println(dateTime);
				}
				struct tm tm;
				time_t epoch=0;
				if ( strptime(dateTime, "%y/%m/%d,%H:%M:%S", &tm) != NULL ) {
					epoch = mktime(&tm);
					Time.setTime(epoch);
					timeSet = true;
					if (debug) {
						Serial.print("DEBUG: System time: ");
						Serial.println(Time.timeStr());
					}
				}
			} else {
				failed_timeSet++;
				if (failed_timeSet > MAX_TIMESET) { // Can't sync time, meaningless, abort!
				failed_timeSet = 0;
				state = ABORT;
			}
		}
	} else {
		if (debug) Serial.println("WARN: No Signal");
		state = INITGPRS;
	}
}
if (!timeSet && (state != ABORT)) {     // Guard against initial timeset fail, must shutdown
	failed_timeSet++;
	if (failed_timeSet > MAX_TIMESET) { // Can't sync time, meaningless, abort!
	failed_timeSet = 0;
	state = ABORT;
}
}

} else

if (state == HTTPSEND) {
	if (debug) Serial.println("DEBUG: State (HTTPSEND)");
	display_status("Upload");
	state = HTTPSENDOK;        // Default next state
	if (strlen(dataString) > 0) {
		if (!sendHTTP(dataString)) {
			state = HTTPSEND;
			failed_up++;
			if (failed_up > MAX_UP) {
				failed_up = 0;
				state = ABORT;
			} else {
				ndelay=2000;
			}
		} else {
			failed_up = 0;
		}
	}
} else

if (state == ALLDONE) {
	if (debug) Serial.println("DEBUG: State (ALLDONE)");
	state = WAKEUP;
	if (hasSensorsReadings) {
		display_status("All done");
		long sleepTime = sleepPeriod - ((millis() - wakeUpTime)/1000);
		if (sleepTime <= 0) sleepTime = sleepPeriod;
		if (debug) {
			Serial.print("DEBUG: Done, Sleep for ");
			Serial.println(sleepTime);
		}
		powerdown(fake, sleepTime);
	}
} else

if (state == ABORT) {
	if (debug) Serial.println("DEBUG: State (ABORT)");
	state = WAKEUP;
	display_status("Aborted");
	long sleepTime = sleepPeriod - ((millis() - wakeUpTime)/1000);
	if (sleepTime <= MINSLEEP) sleepTime = sleepPeriod;
	if (debug) {
		Serial.print("DEBUG: Aborted, Sleep for ");
		Serial.println(sleepTime);
	}
	powerdown(fake, sleepTime);
} else

if (state == LOWBATT) {
	if (debug) Serial.println("DEBUG: State (LOWBATT)");
	initOLEDDisplay();
	displayOn = true;
	display_status("Low Battery");
	powerdown(real, 0);    // battery low, shutdown
	state = ALLDONE;
} else

if (state == LOWPOWER) {                                // Don't wake up in MINPOWER (power off)
if (millis() - lpstart > lpdur) {                   // Wake up if time's up
powerButtonPressed = true;
ac = 999;                                       // Special code to signify simulated power up
if (debug) Serial.println("DEBUG: Power up");
}
}

if ((state != LOWPOWER) && (state != MINPOWER)) {       // We don't do delay loop when in power save mode
if ((ndelay > 0) && (state != WAIT_STATE)) {
	saved_state = state;            // save state
	last_loop_millis = millis();    // Init timer
	state = WAIT_STATE;
}
if ((state == WAIT_STATE) && ((millis() - last_loop_millis) >= ndelay)) {
	state = saved_state;
	last_loop_millis = 0;
}
}

}
