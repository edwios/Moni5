#include "SIM7500.h"
#include <math.h>

//#define DEBUGBUILD
//#define MODEM_DEBUG
extern char* itoa(int a, char* buffer, unsigned char radix);

SIM7500::SIM7500(uint8_t rst, uint8_t pwr)
{
	apnusername = 0;
	apnpassword = 0;
	httpsredirect = false;
	useragent = "IOSTATION v1.0";
	ok_reply = "OK";
	_rstpin = rst;
	_pwrpin = pwr;
	nonsms_reply = "+CMS ERROR";

}

bool SIM7500::begin()
{
	pinMode(_rstpin, OUTPUT);
	pinMode(_pwrpin, OUTPUT);
	#ifdef DEBUGBUILD
	Serial.println("DEBUG: Preparing power on com unit");
	#endif
	digitalWrite(_pwrpin, HIGH);
	digitalWrite(_rstpin, HIGH);
	delay(100);
	digitalWrite(_pwrpin, LOW);       // Power on Comm unit
	delay(80);
	digitalWrite(_pwrpin, HIGH);
	return true;
}

bool SIM7500::init()
{
	//  delay(5000);                      // Need to minimize...
	digitalWrite(_rstpin, LOW);       // Reset Comm Unit
	delay(180);
	digitalWrite(_rstpin, HIGH);
	delay(100);
	#ifdef DEBUGBUILD
	Serial.println("DEBUG: Preparing serial for com unit");
	#endif
	Serial1.begin(115200);
	int16_t timeout = 5000;

	while (timeout > 0) {
		while (Serial1.available()) Serial1.read();
		if (sendCheckReply("AT", ok_reply))
		break;
		while (Serial1.available()) Serial1.read();
		if (sendCheckReply("AT", "AT"))
		break;
		delay(200);
		timeout-=200;
	}

	if (timeout <= 0) {
		#ifdef DEBUGBUILD
		Serial.println("Timeout: No response to AT... last ditch attempt.");
		#endif
		sendCheckReply("AT", ok_reply);
		delay(100);
		sendCheckReply("AT", ok_reply);
		delay(100);
		sendCheckReply("AT", ok_reply);
		delay(100);
	}

	// turn off Echo!
	sendCheckReply("ATE0", ok_reply);
	delay(100);

	if (! sendCheckReply("ATE0", ok_reply)) {
		return false;
	}
	sendCheckReply("AT+CNMI=1,0,0,0,0", ok_reply);  // Do not receive URC
	sendCheckReply("AT+AUTOCSQ=0", ok_reply);       // Do not auto report RSSI
	//Serial1.println("AT+STGR=83");					// End whatever session was there
	//Serial1.println("AT+STK=0");                    // Disable STK
	sendCheckReply("AT+CGPSURL=\"sh.iostation.com.com:7276\"", ok_reply); // 2016-06-12 Set AGPS server url
	sendCheckReply("AT+CGPSSSL=0", ok_reply);       // 2016-06-12 Not use certificate (if use, must supply by carrier)
	sendCheckReply("AT+CGPSAUTO=0", ok_reply);      // 2016-06-12 Disable auto GPS start
	//#ifdef GPSXTRA
	sendCheckReply("AT+CGPSXE=1", ok_reply);        // 2016-07-20 Enable GPS XTRA, require SIM7500 restart
	sendCheckReply("AT+CGPSXDAUTO=1", ok_reply);    // 2016-07-20 Automatically download GPS XTRA data (GPRS required)
	//#endif
	enableNetworkTimeSync(true);
	flushInput();

	return true;
}

bool SIM7500::poke()
{
	sendCheckReply("AT", ok_reply);
	delay(100);
	sendCheckReply("AT", ok_reply);
	delay(100);
	sendCheckReply("AT", ok_reply);
	delay(100);

	// turn off Echo!
	sendCheckReply("ATE0", ok_reply);
	delay(100);

	if (! sendCheckReply("ATE0", ok_reply)) {
		return false;
	}

	return true;
}

void SIM7500::end() {
	//    Using PWRKEY or AT+CPOF is encouraged
	//    However we don't have access to PWRKEY on the dev board
	//        digitalWrite(_pwrpin, LOW);
	//        delay(1500);
	//        digitalWrite(_pwrpin, HIGH);
	/*
	// Dev board does not provide PowerON so we don't power off

	if (!sendCheckReply("AT+CPOF", ok_reply)) {     // Power off
	Serial.println("ERROR: Cannot power off GPSCOM!! Go the hard way.");
	digitalWrite(_rstpin, LOW);       // Reset Comm Unit
	delay(180);
	digitalWrite(_rstpin, HIGH);
	delay(1000);
	digitalWrite(_pwrpin, LOW);
	delay(1500);
	digitalWrite(_pwrpin, HIGH);
}
delay(100);
*/
}

/********* SIM ***********************************************************/

uint8_t SIM7500::unlockSIM(char *pin)
{
	char sendbuff[14] = "AT+CPIN=";
	sendbuff[8] = pin[0];
	sendbuff[9] = pin[1];
	sendbuff[10] = pin[2];
	sendbuff[11] = pin[3];
	sendbuff[12] = '\0';

	return sendCheckReply(sendbuff, ok_reply);
}

/********* NETWORK *******************************************************/

uint8_t SIM7500::getNetworkStatus(void) {
	uint16_t status;

	if (! sendParseReply("AT+CREG?", "+CREG: ", &status, ',', 1)) return 0;

	return status;
}


uint8_t SIM7500::getRSSI(void) {
	uint16_t reply;
	// +CSQ: 5,99
	if (! sendParseReply("AT+CSQ", "+CSQ: ", &reply, ',', 0) ) return 0;

	return reply;
}

uint8_t SIM7500::getNetworkMode(void) {
	uint16_t status;

	if (! sendParseReply("AT+CNSMOD?", "+CNSMOD: ", &status, ',', 1)) return 0;

	return status;
}

void SIM7500::clearSMS() {
	sendCheckReply("AT+CMGD=,4", ok_reply);
}

/********* TIME **********************************************************/

bool SIM7500::readRTC(char *datetime) {
	if (!datetime) return false;
	getReply("AT+CCLK?");
	if (replybuffer[0] == '+') {
		strncpy(datetime, replybuffer+8,17);
		datetime[17]=0;
	} else {
		return false;
	}
	readline();
	return true;
}

bool SIM7500::syncNetworkTime(char *s, uint16_t timeout) {
	char send[100] = "AT+CHTPSERV=\"ADD\",\"";
	char *sendp = send + strlen(send);
	memset(sendp, 0, 100 - strlen(send));
	bool OKstatus = true;

	if (!s) return false;
	strcpy(sendp, s);
	sendp+=strlen(s);
	sendp[0] = '\"';
	sendp++;
	strcpy(sendp,",80,1");         // Only port 80 is supported
	if (!sendCheckReply(send, ok_reply)) {
		Serial.print("ERROR: Add time server");
		Serial.println(replybuffer);
		OKstatus = false;
	}
	#ifdef DEBUGBUILD
	Serial.println("DEBUG: Syncing time");
	#endif
	if (!sendCheckReply("AT+CHTPUPDATE", ok_reply)) {
		Serial.print("ERROR: Sync time ");
		Serial.println(replybuffer);
		OKstatus = false;
	}
	#ifdef DEBUGBUILD
	Serial.println("DEBUG: Finishing time sync");
	#endif
	if (!expectReply("+CHTPUPDATE: 0", timeout)) {         // 10s timeout
		Serial.print("ERROR: Finishing time sync ");
		Serial.println(replybuffer);
		OKstatus = false;
	}
	readline();     // eat OK
	sendCheckReply("AT+CHTPSERV=\"DEL\",0", ok_reply);
	return OKstatus;
}

bool SIM7500::enableNetworkTimeSync(bool onoff) {
	if (onoff) {
		if (! sendCheckReply("AT+CTZU=1", ok_reply))
		return false;
	} else {
		if (! sendCheckReply("AT+CTZU=0", ok_reply))
		return false;
	}

	flushInput(); // eat any 'Unsolicted Result Code'

	return true;
}


// IMEI
//uint8_t SIM7500::getIMEI(char *imei);

/********* GPRS **********************************************************/

bool SIM7500::enableGPRS(bool onoff) {
	if (onoff) {
		// disconnect all sockets
		//sendCheckReply(F("AT+CIPSHUT"), F("SHUT OK"), 5000);

		if (! sendCheckReply("AT+CGATT=1", ok_reply, 10000))
		return false;


		// set bearer profile access point name
		if (this->apn) {
			// Send command AT+CGSOCKCONT=1,"IP","<apn value>" where <apn value> is the configured APN name.
			if (! sendCheckReplyQuoted("AT+CGSOCKCONT=1,\"IP\",", this->apn, ok_reply, 10000))
			return false;

			// set username/password
			if (this->apnusername) {
				char authstring[100] = "AT+CGAUTH=1,1,\"";
				char *strp = authstring + strlen(authstring);
				strcpy(strp, this->apnusername);
				strp+=strlen(this->apnusername);
				strp[0] = '\"';
				strp++;
				strp[0] = 0;

				if (this->apnpassword) {
					strp[0] = ','; strp++;
					strp[0] = '\"'; strp++;
					strcpy(strp, this->apnpassword);
					strp+=strlen(this->apnpassword);
					strp[0] = '\"';
					strp++;
					strp[0] = 0;
				}

				if (! sendCheckReply(authstring, ok_reply, 10000))
				return false;
			}
		} else {
			#ifdef DEBUGBUILD
			Serial.println("WARN: No APN set");
			#endif
		}

		// connect in transparent
		//    if (! sendCheckReply("AT+CIPMODE=1", ok_reply, 10000))
		//      return false;
		// open network (?)
		//    if (! sendCheckReply("AT+NETOPEN=,,1", "Network opened", 10000))
		//      return false;

		//    readline(); // eat 'OK'
	} else {
		// close GPRS context
		if (! sendCheckReply("AT+NETCLOSE", "Network closed", 10000))
		return false;
		readline(); // eat 'OK'
	}

	return true;
}


uint8_t SIM7500::GPRSstate(void) {
	uint16_t state;

	if (! sendParseReply("AT+CGATT?", "+CGATT: ", &state) )
	return -1;

	return state;
}

void SIM7500::setGPRSNetworkSettings(char *apn, char *username, char *password) {
	this->apn = apn;
	this->apnusername = username;
	this->apnpassword = password;
	#ifdef DEBUGBUILD
	Serial.print("APN: ");
	Serial.println(this->apn);
	#endif
}

void SIM7500::setUserAgent(char *useragent) {
	this->useragent = useragent;
}

void SIM7500::setHTTPSRedirect(bool onoff) {
	httpsredirect = onoff;
}

/********* GPS **********************************************************/

uint8_t SIM7500::getGPS(uint8_t arg, char *buffer, uint8_t maxbuff) {
	int32_t x = arg;

	getReply(F("AT+CGPSINFO"));


	char *p = strstr(replybuffer, (char *)"SINF");
	if (p == 0) {
		buffer[0] = 0;
		return 0;
	}

	p+=6;

	uint8_t len = max(maxbuff-1, strlen(p));
	strncpy(buffer, p, len);
	buffer[len] = 0;

	readline(); // eat 'OK'
	return len;
}


bool SIM7500::getGPS(char *gpsbuffer, float *lat, float *lon, char *gpsdate, char *gpstime, float *speed_kph, float *heading, float *altitude) {

	// char gpsbuffer[120];

	// we need at least a 2D fix
	if (GPSstatus() < 2)
	return false;

	// grab the mode 2^5 gps csv from the sim808
	uint8_t res_len = getGPS(32, gpsbuffer, 120);

	// make sure we have a response
	if (res_len == 0)
	return false;

	// Parse 3G respose
	// +CGPSINFO:4043.000000,N,07400.000000,W,151015,203802.1,-12.0,0.0,0
	// skip beginning
	char *tok;

	// grab the latitude
	char *latp = strtok(gpsbuffer, ",");
	if (! latp) return false;

	// grab latitude direction
	char *latdir = strtok(NULL, ",");
	if (! latdir) return false;

	// grab longitude
	char *longp = strtok(NULL, ",");
	if (! longp) return false;

	// grab longitude direction
	char *longdir = strtok(NULL, ",");
	if (! longdir) return false;

	// skip date & time
	//tok = strtok(NULL, ",");
	//tok = strtok(NULL, ",");

	// grab date
	char *gdate = strtok(NULL, ",");
	if (! gdate) return false;
	strcpy(gpsdate, gdate);

	// grab time
	char *gtime = strtok(NULL, ",");
	if (! gtime) return false;
	strcpy(gpstime, gtime);

	// only grab altitude if needed
	if (altitude != NULL) {
		// grab altitude
		char *altp = strtok(NULL, ",");
		if (! altp) return false;
		*altitude = atof(altp);
	}

	// only grab speed if needed
	if (speed_kph != NULL) {
		// grab the speed in km/h
		char *speedp = strtok(NULL, ",");
		if (! speedp) return false;

		*speed_kph = atof(speedp);
	}

	// only grab heading if needed
	if (heading != NULL) {

		// grab the speed in knots
		char *coursep = strtok(NULL, ",");
		if (! coursep) return false;

		*heading = atof(coursep);
	}

	double latitude = atof(latp);
	double longitude = atof(longp);

	// convert latitude from minutes to decimal
	float degrees = floor(latitude / 100);
	double minutes = latitude - (100 * degrees);
	minutes /= 60;
	degrees += minutes;

	// turn direction into + or -
	if (latdir[0] == 'S') degrees *= -1;

	*lat = degrees;

	// convert longitude from minutes to decimal
	degrees = floor(longitude / 100);
	minutes = longitude - (100 * degrees);
	minutes /= 60;
	degrees += minutes;

	// turn direction into + or -
	if (longdir[0] == 'W') degrees *= -1;

	*lon = degrees;


}

// GPS handling
bool SIM7500::enableGPS(bool onoff) {
	uint16_t state;

	// first check if its already on or off
	if (! sendParseReply("AT+CGPS?", "+CGPS: ", &state) )
	return false;

	if (onoff && !state) {
		if (! sendCheckReply("AT+CGPS=1,2", ok_reply))      // AT+CGPS=1,2 AGPS MS-based Mode (not to use MSA)
		return false;
	} else if (!onoff && state) {
		if (! sendCheckReply("AT+CGPS=0", ok_reply))
		return false;
		// this takes a little time
		readline(2000); // eat '+CGPS: 0'
	}
	return true;
}

int8_t SIM7500::GPSstatus(void) {
	getReply(F("AT+CGPSINFO"));
	char *p = strstr(replybuffer, "+CGPSINFO:");
	if (p == 0) return -1;
	if (p[10] != ',') return 3; // if you get anything, its 3D fix
	return 0;
}

int SIM7500::GPSsignal(void) {
	uint16_t ampi=0, ampq=0;
	if (sendExpectReply(F("AT+CGPSINFO"), (char *)"AmpI", 2000)) {
		parseReply("AmpI/AmpQ:", &ampi, '/', 0);
		parseReply("AmpI/AmpQ:", &ampq, '/', 1);
		return (int)(sqrt(ampi*ampi+ampq*ampq));
	} // else nothing or not found
}
// HTTP high level interface (easier to use, less flexible).
//bool SIM7500::HTTP_GET_start(char *url, uint16_t *status, uint16_t *datalen);
//void SIM7500::HTTP_GET_end(void);
bool SIM7500::HTTP_GET(char *host, uint16_t port, char *path, uint16_t *status, uint16_t *datalen) {
	bool OKstatus = false;
	char send[100] = "AT+CHTTPACT=\"";
	char *sendp = send + strlen(send);
	memset(sendp, 0, 100 - strlen(send));

	strcpy(sendp, host);
	sendp+=strlen(host);
	sendp[0] = '\"';
	sendp++;
	sendp[0] = ',';
	itoa(port, sendp, 10);
	if (sendCheckReply(send, "+CHTTPACT: REQUEST", 2000)) {
		// Proceed to next POST seq
		Serial1.print("GET http://");
		Serial1.print(host);
		Serial1.print(":");
		Serial1.print(port);
		Serial1.print(path);
		Serial1.println(" HTTP/1.1");
		Serial1.print("Host: ");
		Serial1.println(host);
		Serial1.print("User-Agent: ");
		Serial1.println(useragent);
		Serial1.println("Content-Length: 0");
		Serial1.print(0x26);    // print ^Z
		OKstatus = expectReply(ok_reply);
	}
	return OKstatus;
}

//bool SIM7500::HTTP_POST(char *host, uint16_t port, char *path, char *body, uint16_t *status, uint32_t timeout) {
//    return (HTTP_POST(char *host, uint16_t port, char *path, char *body, uint16_t *status, uint32_t timeout, false, NULL));
//}

// path must start with '/'
bool SIM7500::HTTP_POST(char *host, uint16_t port, char *path, char *body, uint16_t *status, uint32_t timeout, bool getJSON, char *json) {
	bool OKstatus = false;
	char ctrZ = 26;
	char send[100] = "AT+CHTTPACT=\"";
	char *sendp = send + strlen(send);
	size_t bodylen = 0;
	memset(sendp, 0, 100 - strlen(send));

	if (!host) return false;
	if (port < 80) return false;
	if (body) {
		bodylen = strlen(body);
	}
	strcpy(sendp, host);
	sendp+=strlen(host);
	sendp[0] = '\"';
	sendp++;
	sendp[0] = ',';
	sendp++;
	itoa(port, sendp, 10);
	flushInput();
	#ifdef DEBUGBUILD
	Serial.println("DEBUG: Connecting to");
	Serial.println(send);
	#endif

	if (sendExpectReply(send, "+CHTTPACT: REQUEST", timeout)) {
		// Proceed to next POST seq
		#ifdef DEBUGBUILD
		Serial.println("DEBUG: Connected to server");
		#endif
		Serial1.printf("POST %s HTTP/1.1\r\n", path);
		Serial1.printf("Host: %s\r\n", host);
		Serial1.printf("User-Agent: %s\r\n",useragent);
		Serial1.printf("Connection: close\r\n");
		//    Serial1.printf("Accept: application/json\r\n");
		//    Serial1.printf("Content-type: application/json\r\n");
		Serial1.printf("Content-type: application/x-www-form-urlencoded\r\n");
		Serial1.printf("Content-Length: %lu\r\n", bodylen);
		Serial1.printf("\r\n");
		Serial1.printf("%s", body);
		Serial1.print(ctrZ);    // print ^Z
		OKstatus = expectReply(ok_reply, 10000);        // 10s timeout
		if (OKstatus) {
			#ifdef DEBUGBUILD
			Serial.println("DEBUG: Posted to server");
			Serial.println(body);
			#endif
			if (expectReplyStartsWith("+CHTTPACT:", 10000)) {     // 10s timeout
				uint16_t rplyLen = 0;
				if (parseReply("+CHTTPACT:",&rplyLen,' ',1)) {
					uint8_t l = readline();
					if (l > 0) {
						OKstatus = strstr(replybuffer, "200");
						char *hst = strtok(replybuffer, " ");
						hst = strtok(NULL, " ");
						#ifdef DEBUGBUILD
						Serial.print("DEBUG: HTTP Status ");
						Serial.println(hst);
						#endif
						*status = (uint16_t)atoi(hst);
						if (getJSON) {
							/*
							* get JSON string and return to the caller
							*/
							bool gotJSON = false;
							char *jsonStr, *tmp;
							while((l = readline()) && !gotJSON) {
								#ifdef DEBUGBUILD
								Serial.printf("DEBUG: Line %s\n\r", replybuffer);
								#endif
								if (jsonStr = strchr(replybuffer, '{')) {
									#ifdef DEBUGBUILD
									Serial.println("DEBUG: Found {");
									#endif
									if ((tmp = strchr(replybuffer, '}'))) {
										#ifdef DEBUGBUILD
										Serial.println("DEBUG: Found }");
										#endif
										if (tmp - jsonStr > 0) {
											// We have the JSON string from part of the replybuffer
											*(tmp+1) = 0;
											if (json) strcpy(json, jsonStr);
											gotJSON = true;
										}
									}
								}
							}
						}
					}
				} else {
					Serial.println("ERROR: Server responsed with no data");
				}
			} else {
				Serial.println("ERROR: +CHTTPACT not received");
			}
		} else {
			Serial.println("ERROR: OK absent after POST");
		}
	} else {
		Serial.print("ERROR: Server timed out with reply ");
		Serial.println(replybuffer);

	}
	flushInput();
	return OKstatus;
}

uint8_t SIM7500::readline(uint16_t timeout, bool multiline) {
	uint16_t replyidx = 0;
	byte xy = 0;
	while (timeout--) {
		if (replyidx >= 254) {
			//DEBUG_PRINTLN(F("SPACE"));
			break;
		}
		//            xy = Serial1.available();
		#ifdef MODEM_DEBUG
		//    Serial.print("#");
		//    Serial.print(xy);
		//    Serial.print("#");
		#endif

		while(Serial1.available()) {
			char c =  Serial1.read();
			#ifdef MODEM_DEBUG
			//    Serial.print(c);
			//    Serial.println("#");
			#endif
			if (c == '\r') continue;
			if (c == 0xA) {
				if (replyidx == 0)   // the first 0x0A is ignored
				continue;

				if (!multiline) {
					timeout = 0;         // the second 0x0A is the end of the line
					break;
				}
			}
			replybuffer[replyidx] = c;
			//DEBUG_PRINT(c, HEX); DEBUGBUILD_PRINT("#"); DEBUGBUILD_PRINTLN(c);
			replyidx++;
		}

		if (timeout == 0) {
			//DEBUG_PRINTLN(F("TIMEOUT"));
			break;
		}
		delay(1);
	}
	replybuffer[replyidx] = 0;  // null term
	#ifdef MODEM_DEBUG
	Serial.print("[");
	Serial.print(replybuffer);
	Serial.println("]");
	#endif
	return replyidx;
}



// Helper functions

void SIM7500::flushInput() {
	// Read all available serial input to flush pending data.
	uint16_t timeoutloop = 0;
	while (timeoutloop++ < 40) {
		while(Serial1.available()) {
			Serial1.read();
			timeoutloop = 0;  // If char was received reset the timer
		}
		delay(1);
	}
}

uint8_t SIM7500::getReply(char *send, uint16_t timeout) {
	flushInput();

	#ifdef MODEM_DEBUG
	Serial.print(">>");
	Serial.print(send);
	Serial.println("<<");
	#endif

	Serial1.println(send);

	uint8_t l = readline(timeout);

	#ifdef MODEM_DEBUG
	Serial.print("<<");
	Serial.print(replybuffer);
	Serial.println("<<");
	#endif

	//Serial.println(replybuffer);
	return l;
}

// Send prefix, ", suffix, ", and newline. Return response (and also set replybuffer with response).
uint8_t SIM7500::getReplyQuoted(char *prefix, char *suffix, uint16_t timeout) {
	flushInput();
	Serial1.print(prefix);
	Serial1.print('"');
	Serial1.print(suffix);
	Serial1.println('"');

	#ifdef MODEM_DEBUG
	Serial.print("<<");
	Serial.print(prefix);
	Serial.print('"');
	Serial.print(suffix);
	Serial.print('"');
	Serial.println("<<");
	#endif

	uint8_t l = readline(timeout);

	#ifdef MODEM_DEBUG
	Serial.print("<<");
	Serial.print(replybuffer);
	Serial.println("<<");
	#endif

	return l;
}

bool SIM7500::parseReply(char *toreply, uint16_t *v, char divider, uint8_t index) {
	char *p = strstr(replybuffer, toreply);     // get buffer pointer
	if (p == 0) return false;
	p+=strlen(toreply);
	//DEBUG_PRINTLN(p);
	for (uint8_t i=0; i<index;i++) {
		// increment dividers
		p = strchr(p, divider);
		if (!p) return false;
		p++;
		//DEBUG_PRINTLN(p);

	}
	*v = atoi(p);

	return true;
}

bool SIM7500::parseReply(char *toreply, float *f, char divider, uint8_t index) {
	char *p = strstr(replybuffer, toreply);     // get buffer pointer
	if (p == 0) return false;
	p+=strlen(toreply);
	//DEBUG_PRINTLN(p);
	for (uint8_t i=0; i<index;i++) {
		// increment dividers
		p = strchr(p, divider);
		if (!p) return false;
		p++;
		//DEBUG_PRINTLN(p);

	}
	*f = atof(p);

	return true;
}

bool SIM7500::sendParseReply(char *tosend, char *toreply, uint16_t *v, char divider, uint8_t index) {
	getReply(tosend);

	if (! parseReply(toreply, v, divider, index)) return false;

	readline();       // discard "OK"

	return true;
}



// Parse a quoted string in the response fields and copy its value (without quotes)
// to the specified character array (v).  Only up to maxlen characters are copied
// into the result buffer, so make sure to pass a large enough buffer to handle the
// response.
bool SIM7500::parseReplyQuoted(char *toreply,
	char *v, int maxlen, char divider, uint8_t index) {
		uint8_t i=0, j;
		// Verify response starts with toreply.
		char *p = strstr(replybuffer, toreply);
		if (p == 0) return false;
		p+=strlen(toreply);

		// Find location of desired response field.
		for (i=0; i<index;i++) {
			// increment dividers
			p = strchr(p, divider);
			if (!p) return false;
			p++;
		}

		// Copy characters from response field into result string.
		for(i=0, j=0; j<maxlen && i<strlen(p); ++i) {
			// Stop if a divier is found.
			if(p[i] == divider)
			break;
			// Skip any quotation marks.
			else if(p[i] == '"')
			continue;
			v[j++] = p[i];
		}

		// Add a null terminator if result string buffer was not filled.
		if (j < maxlen)
		v[j] = '\0';

		return true;
	}

	bool SIM7500::sendCheckReply(char *send, char *reply, uint16_t timeout) {
		if (! getReply(send, timeout) )
		return false;
		//  Serial.print("DEBUG: replybuffer is ");
		//  Serial.println(replybuffer);
		return (strcmp(replybuffer, reply) == 0);
	}

	bool SIM7500::sendCheckReplyStartsWith(char *send, char *reply, uint16_t timeout) {
		if (! getReply(send, timeout) )
		return false;
		//  Serial.print("DEBUG: replybuffer is ");
		//  Serial.println(replybuffer);
		return (strstr(replybuffer, reply) == replybuffer);
	}

	// Send prefix, ", suffix, ", and newline.  Verify FONA response matches reply parameter.
	bool SIM7500::sendCheckReplyQuoted(char *prefix, char *suffix, char *reply, uint16_t timeout) {
		getReplyQuoted(prefix, suffix, timeout);
		return (strcmp(replybuffer, reply) == 0);
	}

	bool SIM7500::expectReply(char *reply, uint16_t timeout) {
		unsigned int t0 = millis();
		readline(timeout);
		while (!(strcmp(replybuffer, reply) == 0) && (millis()-t0) < timeout) {
			readline(timeout);
		}
		#ifdef DEBUGBUILD
		if (!(strcmp(replybuffer, reply) == 0) && (millis() - t0 >= timeout)) {
			Serial.println("WARN: expectReply timed out");
		}
		#endif
		return (strcmp(replybuffer, reply) == 0);
	}

	bool SIM7500::expectReplyStartsWith(char *reply, uint16_t timeout) {
		unsigned int t0 = millis();
		readline(timeout);
		while (!(strstr(replybuffer, reply) == replybuffer) && (millis()-t0) < timeout) {
			readline(timeout);
		}
		#ifdef DEBUGBUILD
		if (millis() - t0 >= timeout) {
			Serial.println("ERROR: expectReplyStartswith timed out");
		}
		#endif
		return (strstr(replybuffer, reply) == replybuffer);
	}

	bool SIM7500::sendExpectReply(char *send, char *reply, uint16_t timeout) {
		unsigned int t0 = millis();
		flushInput();
		#ifdef MODEM_DEBUG
		Serial.print(">>");
		Serial.print(send);
		Serial.println("<<");
		Serial.print("==");
		Serial.print(reply);
		Serial.println("==");
		#endif

		Serial1.println(send);
		t0 = millis();
		readline(timeout);

		#ifdef MODEM_DEBUG
		Serial.print("<<");
		Serial.print(replybuffer);
		Serial.println("<<");
		#endif

		//    while (!(strcmp(replybuffer, reply) == 0) && ((millis()-t0) < timeout) && !(strcmp(replybuffer, "ERROR"))) {
		while (!(strstr(replybuffer, reply) == replybuffer) && ((millis()-t0) < timeout) && (strcmp(replybuffer, "ERROR"))) {
			readline(timeout);
			#ifdef MODEM_DEBUG
			Serial.print("<<");
			Serial.print(replybuffer);
			Serial.println("<<");
			#endif
		}
		#ifdef DEBUGBUILD
		if (millis() - t0 >= timeout) {
			Serial.println("ERROR: sendExpectReply timed out");
		}
		#endif
		return (strstr(replybuffer, reply) == replybuffer);
	}
