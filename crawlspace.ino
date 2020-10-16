/**********************************************************
  
  Arduino sketch to drive the ESP8266 in our the crawlspace

  Inputs:
  * various sensors
  * 

  Outputs:
  * Publish stats serial output for diagnostics
  * Publish to MQTT topics for integration with home assistant
  * Publish JSON over web interface for polling data / monitoring

  Future:
  * Create queue in SPIFFS to handle periods of disconnectedness


  Developed for:
  * https://www.adafruit.com/product/2821  feather huzzah
  * https://www.adafruit.com/product/375   door/window sensor
  * https://www.adafruit.com/product/828   water flow sensor
            For this sensor: Liters = Pulses / 450
  * https://www.adafruit.com/product/4162  light sensor
  * https://www.adafruit.com/product/385   temp/humidity sensor
  
  Future:
  * https://www.adafruit.com/product/189   PIR sensor



  Setup water flow sensor
  * Connect red to +V
  * Connect black to common ground
  * Connect yellow data to pin #13

  Setup Lux sensor
  * Connect to +V, ground
  * Connect SDA, SDL to same pins
  * Last pin (+v supply) left unused
 
  Setup first door sensor
  * Connect one end to common ground
  * Connect other to input pin #14 (uses internal pullup)

  Setup DHT22 temp/humidity sensor
  * Connect to +v, gnd (docs say it might need 5v, but 3.3v tests fine)
  * I tested with/without the 10k resistor; seems to not be needed
  * Data pin goes to #12


  Notes
  * See https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/faq
  * Avoid using GPIO 0, 2, 15 (these need certain values at boot)
  * 
  * Also: don't use GPIO 16 for door sensor, etc; cannot attach it to interrupt
  * 
  
  
  
  TODO:
  * Handle millis() rollover.  Perhaps NTP time instead?
  * Split .ino into multiple files for management - http://www.gammon.com.au/forum/?id=12625
  * OTA updates
  * Test against cheaper lux sensor (Candidate: MAX44009)
  * Add GPIO extender via i2c to open more pins for use.
  * Add code for PIR sensor
  * Push mqtt event on water starting to flow -- permit polling to
       be less frequent, but still be alerted immediately on change.
  * ...
  * Because luminance can change far more rapidly than it's polled:
    * report spot-luminance -- i.e. absolute last value
    * report high-lum -- highest it's been in the past minute/15m/30m/...
    * report low-lum  -- lowest it's been in the same period
  *


**********************************************************/


#define VERSION "0.6.0"


// Private config held in separate file
// const char* ssid = "SSID";
// const char* password = "password";
#include "private-config.h"


#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// save macaddr during setup()
char macaddr[15] = "";


// static webserver content (avoids SPIFFS overhead for now)
#include "index_html.h"
#include "all_json.h"
#include "favicon_ico.h"








// MQTT client
// MQTT topics are created to be suitable for use with mqtt autodiscovery
// even though we don't yet expect to use that feature.
//
// For auto-discovery:
//   <discovery_prefix>/<component>/[<node_id>/]<object_id>/config
//
//   ha/binary_sensor/[MAC]/door/state
//   ha/sensor/[MAC]/waterflow/state
//   etc
//
//   MAC is 12 hex chars only (colons removed)
//
//   TODO: consider JSON data sets for combined elements like
//    temp+humidity, or lux + historic lux
//

#include <PubSubClient.h>

// * these must be defined in private-config.h 
//const char* mqttServer = "mqtt.example.com";
//const int   mqttPort = 1883;
//const char* mqttuser = "username";
//const char* mqttpass = "password";

// generate and save during setup()
char mqttIdent[64] = "";







// Temp/Humidity stuff - based on sample sketch
//   https://learn.adafruit.com/dht/overview
//

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

// per docs, must be one of 3, 4, 5, 12, 13 or 14
// can be on #15 but must be disconnected during programming
// + from observation, *can* be on 2 if gnd is disconnected at boot
#define DHTPIN 12
#define DHTTYPE DHT22

DHT_Unified dht(DHTPIN, DHTTYPE);
boolean dhtPresent = 0;   // assume hardware present, unless fails to init


uint32_t delayTempHumidity;       // delay between re-reading value


volatile float temp = 0.0;
volatile boolean tempGood = 0;    // value is not good before first reading
volatile float humidity = 0.0;
volatile boolean humidityGood = 0;




// Which pin for flow sensor input
#define FLOWSENSORPIN 13

// Which pin for door/window sensor input
#define DOORSENSORPIN 14

unsigned long lastDoorTime = 0;  // timestamp of last door update
uint32_t doorForceFreq = 900000; // force door refresh this often


// Future: PIR will just use a regular data pin
#define PIRPIN undef




// vars changed by interrupt need to be marked "volatile" so 
// as to not be optimized out by the compiler
volatile unsigned long pulses = 0;

unsigned long lastFlowTime = 0; // timestamp of last flow update
unsigned long lastFlowVal = 0;  // value of last flow update

uint32_t flowFreq = 2500;        // when water is flowing, update this often - 2.5s
uint32_t flowEndFreq = 5000;     // when water stops flowing, update this often - 5.0s
uint32_t flowForceFreq = 900000; // when water isn't flowing, update this often - 15min

// logFlowing is curious.  We set this to something like 2 or 3 inside
// loop() whenever we log flow data that changed.  Now, if flow data has
// NOT changed, and this is non-zero, we decrement the counter and send a
// log (of the unchanged flow data) anyway on the slower flowEndFreq timeline.
//
// The result of this is that for "x" seconds after water stops flowing, we
// send duplicate "no change" data, which allows the monitor to detect that
// it actually stopped, versus didn't get an update yet.
//
int logFlowing = 0;



// VEML7700 produces actual lux value
// if we fail to initialize, we just disable the sensor
#include "Adafruit_VEML7700.h"

Adafruit_VEML7700 veml = Adafruit_VEML7700();
volatile boolean luxPresent = 0;   // hardware is present?
volatile boolean luxGood = 0;      // value read is good?
volatile float lux = 0.0;
uint32_t delayLuminance = 1000;    // read value every second


// Update environmental stats this many ms, unless prompted to update sooner
uint32_t envFreq = 2500;          // when environmentals are changing, send this often
uint32_t envForceFreq = 300000;   // force an update this often, even if not changing

unsigned long lastEnvTime = 0;    // timestamp of last env update
int sendEnvNow = 0;               // env stats changed, and should be sent asap


// we need separate doorChangeTo and doorChanged so that we don't
// re-trigger when millis() rolls over
volatile int  doorState = 0;     // what the door state *IS*
volatile int  doorChangeTo = 0;  // debounced future state
volatile int  doorChanged = 0;   // 1 - doorChangeTo value is valid
volatile unsigned long doorChangeAt = 0;  // millis() 

// TODO: I'm starting to think that debouncing is causing more problems than
// it's solving.  Might be worth taking it back out and just sending jittery
// updates, or being sure to use better switches.
uint32_t delayDoorCheck = 2500;  // sanity check timer for debounce errors
unsigned long lastDoorCheck = 0;



// Timer to capture last poll of sensors
// Set to 30s to avoid polling for 30s after booting -- the humidity 
// sensor in particular seems to take a few seconds to settle down
// so this might help avoid some fluctuations.

// TODO: sanity check for millis() rolling over
unsigned long lastTempHumidity = 30000;
unsigned long lastLuminance = 30000;




void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // empty - only used for subscribed topics
}




// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create MQTT Client
WiFiClient espClient;
PubSubClient mqttclient(mqttServer, mqttPort, mqttCallback, espClient);




// Interrupt called on rising signal = just count them
// TODO: squelch the first pulse every hour to quieten noise?

void ICACHE_RAM_ATTR flowHandler()
{
	// literally count the pulses; all logic is in loop()
	pulses++;
	
}


// Interrupt called on changing signal
//
// We "debounce" the input by noting the new state, flag that a change
// happened, and set a counter so that we'll take a look 100ms in the future.
//
// If, at that future point in time, the state has changed, we process it
// as such.  If it didn't, we can avoid sending spurious "no-op" updates.
//
// However, this falls short of being perfect - we can debounce incorrectly
// leaving the door in the wrong state, so a separate check comes through 
// to sanity check and correct.  Some of the problem may stem from interrupts
// coming in while the logic is processing, changing the underlying variables.
//

void ICACHE_RAM_ATTR doorHandler()
{
	doorChanged  = 1;
	doorChangeTo = digitalRead(DOORSENSORPIN);
	doorChangeAt = millis() + 100;
}




// processor function is called for our HTML file and
// replaces tags (e.g. %STATE%) with the value returned here
//
// TODO: Update template to remove quotes
//       and fill in literal "null" in place of -
// ^- but would break same format inserted into json and text/plain

String processor(const String& var){
	char buffer[50];
	
	if(var == "MILLIS") {
		sprintf(buffer, "%d", millis());
	}
	// TODO: millis() will rollover every ~50 days.  Use NTP here.
	else if (var == "UPTIME") {
		unsigned long m = millis();
		
		// > 1 day
		if (m > (1000 * 60 * 60 * 24)) {
			sprintf(buffer, "%0d day%s, %02d:%02dm",
				(int) (m / (1000 * 60 * 60 * 24)),
				(m >= (2 * 1000 * 60 * 60 * 24) ? "s" : ""),  // plural?
				(int) (m / (1000 * 60 * 60) % 24),
				(int) (m / (1000 * 60) % 60));
		}
		// > 1 hour
		else if (m > (1000 * 60 * 60)) {
			sprintf(buffer, "%02d:%02d:%02ds",
				(int) (m / (1000 * 60 * 60) % 24),
				(int) (m / (1000 * 60) % 60),
				(int) (m / 1000 % 60));
		}
		else {
			sprintf(buffer, "%02d:%02ds",
				(int) (m / (1000 * 60) % 60),
				(int) (m / 1000 % 60));
		}
	}
	// TODO: since these are String(), can they be in a case statement for easier reading?
	else if (var == "WATERFLOW") {
		sprintf(buffer, "%.2f", pulses / 450.0);
	}
	else if (var == "FLOWPULSES") {
		sprintf(buffer, "%d", pulses);
	}
	else if (var == "LUMINANCE" && luxGood) {
		sprintf(buffer, "%0.2f", lux);
	}
	else if (var == "TEMPERATURE" && tempGood) {
		sprintf(buffer, "%0.2f", temp);
	}
	else if (var == "HUMIDITY" && humidityGood) {
		sprintf(buffer, "%0.2f", humidity);
	}
	else if (var == "DOOR") {
		sprintf(buffer, "%s", doorState ? "OPEN" : "CLOSED");
	}
	else if (var == "MACADDR") {
		sprintf(buffer, macaddr);
	}
	
	// future use for a config.json type entry.
	// also want to capture build time (USEMQTT etc) vars
	else if (var == "VERSION") {
		sprintf(buffer, VERSION);
	}
	else if (var == "BUILDDATE") {
		sprintf(buffer, __DATE__);
	}
	else if (var == "BUILDTIME") {
		sprintf(buffer, __TIME__);
	}
	else if (var == "BUILDFILE") {
		sprintf(buffer, __FILE__);
	}
	// Catch all for any unrecognized variable name
	else {
		sprintf(buffer, "-");
	}  
	
	return buffer;
}


// Provide /stat? URL with dynamic args - this avoids us needing to
// create a dozen handlers for each individual stat.  Any param is
// passed into the processor() function which just sets to "-" if the
// parameter isn't recognized.
//
// Sanity check input:
// - must be only single param.
// - only name present, not value (note: /stat?FOO= will still work here)
// - name must be <= 32 chars
// - TODO: name must be only uppercase alpha
//
void webStat(AsyncWebServerRequest *request) {
	// Static error page; we overwrite this with a template string, or
	// pass it through "as is" if parameters weren't read correctly.
	
	char buff[64] = "Usage: /stat?PARAMETER\n";
	char param[64];
	
	// TODO: other sanity checks.
	if (request->params() == 1  &&
			request->getParam(0)->name().length() <= 32 &&
			request->getParam(0)->value().length() == 0) {
		// already checked len <= 32 so we can be less paranoid here
		
		// Read and save incoming name prior to processing
		sprintf(param, request->getParam(0)->name().c_str());
		
		// Copy chars over to buff to create literally:
		//   PARAM => %PARAM%
		//   PaRaM => %PARAM%
		//   PA12M => %PAM%
		//
		int i = 0; int j = 1; char c;
		buff[0] = '%';
		
		// so rudimentary, but avoid char encoding quirks
		while ((c = param[i++]) != '\0' && i <= 32)
			if (c >= 'A' && c <= 'Z') 
				buff[j++] = c;
			else if (c >= 'a' && c <= 'z')
				buff[j++] = c - 32;
			//else
			//	break;
		
		// hackery: if all chars invalid, we turn what would 
		// otherwise be "%%" into "%n%" for output uniformity
		if (j == 1)
			buff[j++] = 'n';
		
		buff[j] = '%';
		buff[j+1]='\0';
		
	}
	request->send_P(200, "text/plain", buff, processor);
	//Serial.println(buff); // debug
}


// 404 handler
void webNotFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}




// mqtt publish update
//
// TODO: add QoS to try to ensure send/resend on disconnectedness
//
// We always send "retain: true" with messages; the last seen state
// will be an appropriate value to use if a system restarts
//
void mqttSendDoor() {
	
	Serial.print("MQTT Connecting for Door ... ");
	
	char buff[64];
	char msg[10];
	
	sprintf(msg, "%s", doorState ? "ON" : "OFF");
	
	if (mqttclient.connect(mqttIdent, mqttuser, mqttpass)) {
		sprintf(buff, "ha/binary_sensor/%s/door/state", macaddr);
		Serial.print("sending ... ");
		if (mqttclient.publish(buff, msg, true)) {
			Serial.print(msg);
			Serial.println(" sent.");
			lastDoorTime = millis();
		}
		else { Serial.println("failed to send."); }
	}
	else {
		Serial.println("failed to connect.");
	}
}


// When polling for updates, we present data in various forms, such
// as raw pulses, and pulses converted into liters.
//
// For mqtt push, we only send the raw pulse count.
//
void mqttSendFlow() {
	
	Serial.print("MQTT Connecting for Flow ... ");
	
	// updated via interrupt; save it before we use it
	unsigned long saveP = pulses;
	
	char buff[64];
	char msg[32];
	sprintf(msg, "%i", saveP);
	if (mqttclient.connect(mqttIdent, mqttuser, mqttpass)) {
		sprintf(buff, "ha/sensor/%s/waterflow/state", macaddr);
		Serial.print("sending ... ");
		if (mqttclient.publish(buff, msg, true)) {
			Serial.print(msg);
			Serial.println(" sent.");
			
			lastFlowVal = saveP;
			lastFlowTime = millis();
			
		}
		else { Serial.println("failed to send."); }
	}
	else {
		Serial.println("failed to connect.");
	}
}

// Generic function to send all environment values
// - temp, humidity, lux
//
void mqttSendEnviron() {

	
	Serial.print("MQTT Connecting for Environmental ... ");
	
	char buff[64];
	char msg[32];
	
	if (! mqttclient.connect(mqttIdent, mqttuser, mqttpass)) {
		Serial.println("failed.");
	}
	else {
		Serial.println("ok.");
		
		// send lux?
		// TODO: make sure that luxPresent =0 always sets luxGood = 0
		if (luxGood) {
			sprintf(msg, "%0.2f", lux);
			sprintf(buff, "ha/sensor/%s/luminance/state", macaddr);
			Serial.print("Sending lux ... ");
			
			if (mqttclient.publish(buff, msg, true)) {
				Serial.print(msg);
				Serial.println(" sent.");
			}
			else { Serial.println("failed to send."); }
		}
		
		if (tempGood) {
			sprintf(msg, "%0.2f", temp);
			sprintf(buff, "ha/sensor/%s/temperature/state", macaddr);
			Serial.print("Sending temp ... ");
			
			if (mqttclient.publish(buff, msg, true)) {
				Serial.print(msg);
				Serial.println(" successfully sent.");
			}
			else { Serial.println("failed to send."); }
		}
		
		if (humidityGood) {
			sprintf(msg, "%0.2f", humidity);
			sprintf(buff, "ha/sensor/%s/humidity/state", macaddr);
			Serial.print("Sending humidity ... ");
			
			if (mqttclient.publish(buff, msg, true)) {
				Serial.print(msg);
				Serial.println(" successfully sent.");
			}
			else { Serial.println("failed to send."); }
		}
		
	}
	

}



void setup() {
	// Setup
	Serial.begin(115200);
	Serial.println("Crawlspace monitoring system starting.");




	// Initialize device.
	dht.begin();
	// Print temperature sensor details.
	sensor_t sensor;
	dht.temperature().getSensor(&sensor);
 
	// Try to read a value (== determine if it's present)
	sensors_event_t event;
	dht.temperature().getEvent(&event);
	if (isnan(event.temperature)) {
		Serial.println("Reading from temperature sensor failed; disabling.");
	}
	else {
		// Note - we throw away the first temperature reading
		
		dhtPresent = 1;  // enable future checks
		
		// NOTE the F() macro pushes these strings into flash, not ram to save
		// working space.  See https://www.arduino.cc/reference/en/language/variables/utilities/progmem/
		//
		// These values are all generic values for the DHT22 device, and are not read from 
		// the sensor itself.
		Serial.println(F("------------------------------------"));
		Serial.println(F("Temperature Sensor"));
		Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
		//Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
		//Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
		Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
		Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
		Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
		Serial.println(F("------------------------------------"));
		// Print humidity sensor details.
		dht.humidity().getSensor(&sensor);
		Serial.println(F("Humidity Sensor"));
		Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
		//Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
		//Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
		Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
		Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
		Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
		
		Serial.println(F("------------------------------------"));
		// Set delay between sensor readings based on sensor details.
		Serial.print  (F("Min Delay:   ")); Serial.println(sensor.min_delay);
		Serial.println(F("------------------------------------"));
		
		// From observation on the DHT22 this is 2000ms, but we will poll slower.
		delayTempHumidity = 5000;
	
	
	}
	
	
	// Setup VEML7700 lux sensor
	// TODO: in testing, if I unplug the lux sensor it starts
	// giving unreasonably high readings.  If we see this, we can
	// just disable it - flag as "unavailable".
	if (!veml.begin()) {
		Serial.println("Lux sensor not found");
	}
	else {
		luxPresent = 1;
		Serial.println("Initializing Lux sensor");
		veml.setGain(VEML7700_GAIN_1);
		veml.setIntegrationTime(VEML7700_IT_800MS);
		
		Serial.print(F("Gain: "));
		switch (veml.getGain()) {
			case VEML7700_GAIN_1: Serial.println("1"); break;
			case VEML7700_GAIN_2: Serial.println("2"); break;
			case VEML7700_GAIN_1_4: Serial.println("1/4"); break;
			case VEML7700_GAIN_1_8: Serial.println("1/8"); break;
		}
		
		Serial.print(F("Integration Time (ms): "));
		switch (veml.getIntegrationTime()) {
			case VEML7700_IT_25MS: Serial.println("25"); break;
			case VEML7700_IT_50MS: Serial.println("50"); break;
			case VEML7700_IT_100MS: Serial.println("100"); break;
			case VEML7700_IT_200MS: Serial.println("200"); break;
			case VEML7700_IT_400MS: Serial.println("400"); break;
			case VEML7700_IT_800MS: Serial.println("800"); break;
		}
		
		//veml.powerSaveEnable(true);
		//veml.setPowerSaveMode(VEML7700_POWERSAVE_MODE4);
		
		// Disabling these - we just poll; don't need interrupts on thresholds
		//veml.setLowThreshold(10000);
		//veml.setHighThreshold(20000);
		//veml.interruptEnable(true);
	
	} // setup lux



	// Very long -- set up Async Web server
	// Route for root / web page
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/html", index_html, processor);
	});

	server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "image/png", favicon_ico_gz, favicon_ico_gz_len);
	});
	
	server.on("/all.json", HTTP_GET, [](AsyncWebServerRequest *request){
		// Serial.println("GET /all.json");
		request->send_P(200, "text/json", all_json, processor);
	});
	
	server.on("/flow.json", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/json", flow_json, processor);
	});
	
	server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/json", config_json, processor);
	});

	// TODO: can we read the contents of serial output and offer it over http?


	//// Examples with SPIFFS to read a data file from flash, with/without processing
	// 
	//server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
	//	request->send(SPIFFS, "/index.html", String(), false, processor);
	//});
	//server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
	//	request->send(SPIFFS, "/style.css", "text/css");
	//});
	
	
	// Example of simple URL with parameter substitution
	// TODO - obvs web server is online to return a result; could check
	// mqtt client/ntp client status and report.  Could also check for
	// hardware (e.g. if lux sensor disabled) and report.
	server.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *request) {
		request->send_P(200, "text/plain", (const char*)"OK. Up %UPTIME% (%MILLIS%ms).\n", processor);
	});
	
	

	// Other handlers
	server.on("/stat", HTTP_GET, webStat);
	
	// TODO: add handler to self-reboot (e.g. restart a sensor)
	
	// otherwise, 404
	server.onNotFound(webNotFound);
	
	
	// Start server
	server.begin();
  
  
  
  
	Serial.println("Setting input/output pin parameters.");


	// Door Sensor pin
	// simple switch between input pin and ground = needs internal pullup
	// read initial value; attach interrupt for any change in state
	pinMode(DOORSENSORPIN, INPUT_PULLUP);
	doorState = digitalRead(DOORSENSORPIN);
	attachInterrupt(DOORSENSORPIN, doorHandler, CHANGE);


	// Flow Sensor pin
	// purely input, no pullup needed; set interrupt for rising change only
	pinMode(FLOWSENSORPIN, INPUT);
	attachInterrupt(FLOWSENSORPIN, flowHandler, RISING);





	Serial.println("Connecting to wifi ....");
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.println("");

	// Wait for connection to finish and print details.
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.print("Connected to ");
	Serial.println(ssid);
	Serial.print("IP address: ");
	Serial.println(WiFi.localIP());

	// Read and save mac address one time since it won't change
	char longmac[32];  // will contain colons, etc.
	sprintf(longmac, WiFi.macAddress().c_str());
	int i = 0; int j = 0; char c;
	while ((c = longmac[i++]) != '\0' && j <= 12)
		if (isalnum(c)) 
			macaddr[j++] = c;
	macaddr[j] = '\0';
	
	
	Serial.print("MAC address: ");
	Serial.println(macaddr);
	
	sprintf(mqttIdent, "esp8266-%s", macaddr);
	Serial.print("MQTT Client Ident: ");
	Serial.println(mqttIdent);
	Serial.println("");

	// Connect to MQTT for initial updates 
	Serial.println("Connecting to MQTT server for initial updates: ");
	mqttSendDoor();
	mqttSendFlow();

}



// Run indefinitely.
void loop()
{

	// Process a debounced door change; if it's time to process and an actual
	// change happened.
	//
	// TODO: This seems to debounce adequately.  We might also have a every few
	// seconds read of doorState and if it is ever out of sync, report it to
	// alert to debouncing errors -- an error case exists that if this section
	// of code is interrupted by a door state change, behaviour may be unexpected.
	if (doorChanged && millis() >= doorChangeAt) {
		// did our debounce result in no change?
		if (doorState == doorChangeTo) {
			doorChanged = 0;
		}
		else {
			doorState = doorChangeTo;
			
			// Mark "changed" to off immediately.  Otherwise, the mqtt 
			// calls further may take many ms and cause the debounce to
			// squelch a real future event
			doorChanged = 0;
			
			// something changed - so send stats updates
			mqttSendDoor();
			
		}
	
	}
	
	
	// Sanity check
	if (millis() - lastDoorCheck > delayDoorCheck) {
		lastDoorCheck = millis();
		int doorCheck = digitalRead(DOORSENSORPIN);
		if (doorCheck != doorState) {
			Serial.println("****");
			Serial.println("Sanity check failed - door debounce failed");
			Serial.println("****");
			doorState=doorCheck;
			
			// stats updates needed
			mqttSendDoor();
			
		}
	}
	
	// else, send door update regardless on this cadence
	if (millis() - lastDoorTime > doorForceFreq) {
		mqttSendDoor();
	}
	
		
	// refresh sensors, avoiding refreshing "too often"
	if (millis() - lastTempHumidity > delayTempHumidity && dhtPresent) {
		lastTempHumidity = millis();
		
		sensors_event_t event;
		dht.temperature().getEvent(&event);
		if (isnan(event.temperature)) {
			// TODO: on too many failures, disable future reads (dhtPresent = 0)
			// Serial.println("Reading temp failed; disabling");
			tempGood = 0;
		}
		else {
			float lastT = temp;
			temp = (event.temperature * 1.8) + 32;
			tempGood = 1;
			
			// more than 1 degree change?
			if (abs(temp - lastT) > 1.0) {
				sendEnvNow = 1;
			}
		}
		
		// repeat for humidity
		dht.humidity().getEvent(&event);
		if (isnan(event.relative_humidity)) {
			// Serial.println("Reading humidity failed; disabling");
			humidityGood = 0;
		}
		else {
			float lastH = humidity;
			
			humidity = event.relative_humidity;
			humidityGood = 1;
			
			// more than 5 percent change?
			if (abs(humidity - lastH) > 5.0) {
				sendEnvNow = 1;
			}

		}
	}


	// Note: trying to readLux() with the board absent will cause
	// the entire platform to crash.  So only poll if it's there
	if (millis() - lastLuminance > delayLuminance && luxPresent) {
		float lastL = lux;
		
		lastLuminance = millis();
		lux = veml.readLux();
		
		if (lux == 989560448.00) {   // from observation
			Serial.println("Lux sensor out of range - disabling");
			luxPresent = 0;
			luxGood = 0;
		} else {
			// more than 5 point change?
			if (abs(lux - lastL) > 5.0) {
				sendEnvNow = 1;
			}
			
			luxGood = 1;
		}

	}
	
	
	
	
	
	// send pulses if they've changed + it's been long enough since last update
	if (pulses != lastFlowVal && millis() - lastFlowTime > flowFreq) {
		Serial.println("Water is flowing - sending data update");
		logFlowing = 2;   // reset the counter for logging after flow stops
		mqttSendFlow();
	}
	else if (logFlowing > 0 && pulses == lastFlowVal && millis() - lastFlowTime > flowEndFreq) {
		Serial.print("Water is not flowing - logFlowing is ");
		Serial.print(logFlowing);
		Serial.println(" - sending data update anyway");
		logFlowing--;
		mqttSendFlow();
	}
	// else, send update regardless on this cadence
	else if (millis() - lastFlowTime > flowForceFreq) {
		Serial.println("Sending a forced water data update regardless of change");
		mqttSendFlow();
	}
	
	
	
	// TODO: deprecate serial output.
	// TODO: print timestamp (clock time) on serial to better correlate with
	// unexpected system crashes.
	
	// Update stats when prompted, or every N milliseconds
	//
	// TODO: separate flags to update stats for different measurements
	// -- just because door opened doesn't mean we should push a temp update
	// -- ^ this is done; now need to split out temp/humid/lux to separate updates
	//
	if ( ( sendEnvNow && millis() - lastEnvTime > envFreq) ||
	     ( millis() - lastEnvTime > envForceFreq) ) {
		
		lastEnvTime=millis();
		sendEnvNow = 0;
		
		mqttSendEnviron();
		
	}
	

	// TODO: do we need mqttclient.loop() when we're not subscribed to anything?
	mqttclient.loop();

	yield();  // or delay(0);
  
}



