// Rename this as private-config.h and fill with actual values

// Local config variables that stay outside code
// -- still gets compiled in at build time.
// -- TODO: migrate to a SPIFFS config file to read at run time.


// Details of AP to connect to on boot. 
const char* ssid = "Some-SSID";
const char* password = "12345678";


// Details of MQTT parameters
#define USEMQTT 1
const char* mqttServer = "192.168.x.y";
const int   mqttPort = 1883;
const char* mqttprefix = "not-yet-used-";
const char* mqttuser = "crawlspace";
const char* mqttpass = "1234123412341234";


// Hard-coded hackery to avoid my development board from updating the
// production MQTT topic(s)
const char* prodMac = "BC:DD:C2:..:..:..";
const char* devMac =  "BC:DD:C2:..:..:..";

const char* mqttDoorTopicProd    = "ha/door/crawlspace/door";
const char* mqttDoorTopicDev     = "ha/door/crawlspace-dev/door";
const char* mqttDoorTopicDefault = "ha/door/crawlspace-other/door";




// Enable DHT22 sensor code
#define TEMPHUMIDITY 1


