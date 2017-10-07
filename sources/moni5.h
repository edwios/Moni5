#define DEBUGBUILD
#undef RELEASE

#define fake    false
#define real    true

#undef USEBME280
#undef USESI7021

// Features
#undef GPSXTRA					// No GPS

// Timeouts
#define MAX_MN      10
#define MAX_GPS     3
#define MAX_GPRS    3
#define MAX_DCU     3
#define MAX_TIMESET 3
#define MAX_UP      3
#define TIMESET_TIMEOUT_MS 15000
#define HTTPTIMEOUT 15000
#ifdef DEBUGBUILD
#   define MAX_3DFIX    3       // Don't care much in testing
#else
#   define MAX_3DFIX    40      // At most 30s for a fix
#endif
#define MAXVBATT    3873        // 3924 measured @ fully charged on Nexcell
#define MINVBATT    3080        // 3080 @ 3.3V (Li-ion corner pos), min Vin = 2.9V, 2391 measured @ log stopped
#define THRES01     10          // Threshold 1 at 10% battery
#define THRES02     5           // Threshold 1 at 5% battery
#define THRES03     1           // Threshold 1 at 1% battery
#define MIN_RSSI    6           // Minimum signal strength to start GPRS
#define MINSLEEP    50          // Minimum sleep duration is 50s

#define STATUSLINE 8            // Status line on OLED display

// Deep sleep period
#ifdef DEBUGBUILD
#   define DEFAULT_PERIOD       120      // 120s (Testing)
#else
#   define DEFAULT_PERIOD       300     // 5 minutes
#endif

// #define DEFAULT_SPI_CLOCK_DIV SPI_CLOCK_DIV8
// #define DEFAULT_WIRE_SPEED CLOCK_SPEED_100KHZ
#define DEFAULT_SPI_CLOCK_DIV SPI_CLOCK_DIV8
#define DEFAULT_WIRE_SPEED CLOCK_SPEED_100KHZ

// Time format (for server upload use)
#define TIME_FORMAT "%F %T"

#define SERVER_STR_FMT "jsonStr={\"deviceid\":\"%s\",\"temperature\":%.2f,\"humidity\":%.2f,\"latitude\":%f,\"longitude\":%f,\"updateTime\":\"%s\",\"vbatt\":%d,\"rssi\":%d}"
//#define SERVER_STR_FMT "jsonStr={\"deviceid\":\"%s\",\"temperature\":%.2f,\"humidity\":%.2f,\"latitude\":%f,\"longitude\":%f,\"updatetime\":\"%s\",\"battery\":%d,\"signal\":%d}"


#ifdef RELEASE
#   define USEBME280
// Client UA
#   define USERAGENT "PROOVE v1.0.000"
// Data sink information
#   define SERVERFQDN  "datasink.iostation.com"
#   define SERVERPORT  8080
#   define SERVERPATH  "/v1/thermo"
#else
#   define USESI7021
// Client UA
#   define USERAGENT "PROOVE v1.0.000d"
// Test Server information
#   define SERVERFQDN  "datasink.iostation.com"
#   define SERVERPORT  8080
#   define SERVERPATH  "/v1/thermo"
#endif

// Time server
#define NETTIMESERVER "www.baidu.com"

// APN
#define GPSCOMAPN "3gnet"
// SD Files
#define RECORDFILE  "LOG/LOG.TXT"
#define FILENAMEFMT "LOG/%s%05ld.LOG"
#define SDFILEPREFIX  "M"

// "interval":"3000"
#define JSON_PERIODSTR "\"interval\":\""

// Various GPIO definitions
#define SDCS        A2
#define SDMOSI      A5
#define SDMISO      A4
#define SDCLK       A3
#define ALLCLEAR    D6
#define CHECKPIN    D5
#define OLEDROMCS   A1
#define GPSCOMRST   D2
#define VBATTINP    A0
#define GPSCOMPWR   D4
#define DISPLAYON   D3

// State machine states
#define WAKEUP      1
#define READENV     2
#define INITGPSCOM  3
#define WAITGPSCOM  4
#define INITGPS     5
#define WAITGPS     6
#define READGPS     7
#define INITGPRS    8
#define ENAGPRS     9
#define INITSD      10
#define POPSD       11
#define PUSHSD      12
#define HTTPSEND    13
#define HTTPSENDOK  14
#define ALLDONE     15
#define ABORT       16
#define SYNCLK      17
#define LOWBATT     18
#define WAIT_STATE  19
#define LOWPOWER    20
#define MINPOWER    21
#define PWRGPSCOM   22

// Default state when startup / wake up from Deep Sleep
#define DEFAULT_INIT_STATE WAKEUP

// Status codes, reserved for future expansion
#define GPSCOMNA    300
#define GPSCOMOK    301
#define GPSNA       400
#define GPSOK       401
#define GPRSNA      500
#define GPRSOK      501
#define SDNA        600
#define SDOK        601
#define POPSDNA     700
#define POPSDOK     701
#define PUSHSDNA    800
#define PUSHSDOK    801
#define HTTPSENDNA  91

void initOLEDDisplay();
void display_line(char *s, int n);
void display_status(char *s);
