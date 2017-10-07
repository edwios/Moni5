#include "application.h"
#include "moni5.h"
#define _BV(bit) (1 << (bit))

#ifndef si7021_h
#define si7021_h

#define SI7021      0x40
#define RH_HOLD     0xE5
#define RH_NOHOLD   0xF5
#define TEMP_HOLD   0xE3
#define TEMP_NOHOLD 0xF3
#define TEMP_PREV   0xE0
#define RESET_SI    0xFE
#define WREG        0xE6
#define RREG        0xE7
#define HTRE        0x04

class si7021
{
public:
	si7021();
	void  begin();
	float getRH();
	float readTemp();
	float getTemp();
	void  heaterOn();
	void  heaterOff();
	void  changeResolution(uint8_t i);
	void  resetSettings();
private:
	uint16_t makeMeasurment(uint8_t command);
	void     writeReg(uint8_t value);
	uint8_t  readReg();
};

#endif
