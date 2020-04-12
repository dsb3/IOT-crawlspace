// Rename this as private-config.h and fill with actual values

// Local config variables that stay outside code
// -- still gets compiled in at build time.
// -- TODO: migrate to a SPIFFS config file to read at run time.


// Details of AP to connect to on boot. 
const char* ssid = "Some-SSID";
const char* password = "12345678";


// Details of MQTT parameters
const char* mqttServer = "192.168.x.y";
const int   mqttPort = 1883;
const char* mqttprefix = "not-yet-used-";
const char* mqttuser = "crawlspace";
const char* mqttpass = "1234123412341234";

const char* mqttdoortopic = "ha/door/crawlspace/door";







