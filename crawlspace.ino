/**********************************************************
  
  Arduino sketch to drive the ESP8266 in our the crawlspace

  Inputs:
  * water flow sensor
  * door/window sensor(s)
  * others to follow (PIR / temp / humidity etc)

  Outputs:
  * Initially - copy water flow sensor to onboard LED
  * Publish to serial output for diagnostics
  * Publish to MQTT topics for integration with home assistant

  Future:
  * Create queue in SPIFFS to handle periods of disconnectedness


  Developed for:
  * https://www.adafruit.com/product/2821  feather huzzah
  * https://www.adafruit.com/product/828   water flow sensor
            For this sensor: Liters = Pulses / (7.5 * 60)
  * https://www.adafruit.com/product/4162  light sensor
  * https://www.adafruit.com/product/375   door/window sensor  

  Future:
  * https://www.adafruit.com/product/189   PIR sensor
  * https://www.adafruit.com/product/385   Temp/humidity sensor



  Setup diags LED
  * None - use the onboard LED

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
  * Connect other to input pin #14 (use internal pullup)

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





#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// MQTT client
#include <PubSubClient.h>

// Config held in separate file
// const char* ssid = "SSID";
// const char* password = "password";
#include "private-config.h"

// static webserver content (avoids SPIFFS overhead for now)
#include "index_html.h"
#include "all_json.h"
#include "favicon_ico.h"


// Temp/Humidity stuff - based on sample sketch
//   https://learn.adafruit.com/dht/overview
//
// In development I had some issues with sensors interacting with
// each other, so this is here to easily turn off/on separate sections.
//
#ifdef TEMPHUMIDITY

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

#define DHTPIN 12     // Digital pin connected to the DHT sensor 
#define DHTTYPE DHT22

DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayTempHumidity;

#endif

// these vars outside the IFDEF since ..Good will stay bad and not print
volatile float temp = 0.0;
volatile boolean tempGood = 0;  // not good before first reading
volatile float humidity = 0.0;
volatile boolean humidityGood = 0;




// Use built-in LED for flash/output (not yet used)
const int led = LED_BUILTIN;


// read MAC address once and save it
char macaddr[32] = "";

// Which pin for flow sensor input
#define FLOWSENSORPIN 13

// Which pin for door/window sensor input
#define DOORSENSORPIN 14


// How often to show serial stats by default (in ms)
#define statusFreq 60000

// Print stats based on volume of water flow
// At 450 pulses per liter, a value of 45 means print every 0.1 l
#define flowFreq 45


// vars changed by interrupt need to be marked "volatile" so 
// as to not be optimized out by the compiler
volatile unsigned long pulses = 0;

// VEML7700 produces actual lux value
// if we fail to initialize, we just disable the sensor
#include "Adafruit_VEML7700.h"

Adafruit_VEML7700 veml = Adafruit_VEML7700();
volatile boolean luxPresent = 0;   // sensor initialized ok
volatile boolean luxGood = 0;
volatile float lux = 0.0;
uint32_t delayLuminance = 1000;

volatile int doorState = 0;
volatile int doorChanged = 0;

// Used for serial output
volatile int printNow = 0;

// Timer to print serial output regardless of activity
unsigned long lastSerial = 0;

// Timer to capture last poll of sensors
// TODO: sanity check for millis() rolling over
unsigned long lastTempHumidity = 0;
unsigned long lastLuminance = 0;



void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // empty - only used for subscribed topics
}




// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create MQTT Client
#ifdef USEMQTT
WiFiClient espClient;
PubSubClient mqttclient(mqttServer, mqttPort, mqttCallback, espClient);
#endif


// Interrupt called on rising signal = just count them
void ICACHE_RAM_ATTR flowHandler()
{
	pulses++;
	if (pulses % flowFreq == 0) { printNow = 1; }
	
}


// Interrupt called on changing signal = note sensor changed
// TODO: debounce input
void ICACHE_RAM_ATTR doorHandler()
{
	doorState = digitalRead(DOORSENSORPIN);
	doorChanged = 1;
	printNow = 1;
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
			sprintf(buffer, "%0d day%s, %02d:%02ds",
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
// - TODO: name must be only alpha (and uppercase it)
//
void webStat(AsyncWebServerRequest *request) {
	// Static error page; we overwrite this with a template string, or
	// pass it through "as is" if parameters weren't read correctly.
	
	char buff[64] = "Usage: /stat?PARAMETER\n";
	// TODO: other sanity checks.
	if (request->params() == 1  &&
			request->getParam(0)->name().length() <= 32 &&
			request->getParam(0)->value().length() == 0) {
		sprintf(buff, "%%%s%%", request->getParam(0)->name().c_str());
	}
	request->send_P(200, "text/plain", buff, processor);
	
}


// 404 handler
void webNotFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}




// mqtt publish update
//
// TODO: add QoS to try to ensure send/resend on disconnectedness
//
#ifdef USEMQTT
void mqttSendDoor() {
		
	Serial.print("MQTT Connecting... ");
	// TODO: embed macaddr in client identifier
	if (mqttclient.connect("esp8266", mqttuser, mqttpass)) {
		Serial.print(" sending ...");
		if (mqttclient.publish(mqttdoortopic, doorState ? "ON" : "OFF")) {
			Serial.println("... successfully sent.");
		}
		else { Serial.println("... failed to send."); }
	}
	else {
		Serial.println("... failed to connect.");
	}
}
#else
void mqttSendDoor() {
	return;
}
#endif


void setup() {
	// Setup
	Serial.begin(115200);
	Serial.println("Crawlspace monitoring system starting.");




#ifdef TEMPHUMIDITY
  // Initialize device.
  dht.begin();
  // Print temperature sensor details.
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println(F("------------------------------------"));
  Serial.println(F("Temperature Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));
  Serial.println(F("------------------------------------"));
  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  Serial.println(F("Humidity Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
  Serial.println(F("------------------------------------"));
  // Set delay between sensor readings based on sensor details.
  Serial.print  (F("Min Delay:   ")); Serial.println(sensor.min_delay);
  Serial.println(F("------------------------------------"));
  delayTempHumidity = sensor.min_delay / 1000;   // given in us; convert to ms.

  // From observation on the DHT22 this is 2000ms.  We'll poll slower.
  delayTempHumidity = 5000;
  
#endif


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
		veml.setLowThreshold(10000);
		veml.setHighThreshold(20000);
		veml.interruptEnable(true);
	
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
	server.onNotFound(webNotFound);
	
	
	// Start server
	server.begin();
  
  
  
  
	Serial.println("Setting input/output pin parameters.");

	// onboard LED pin is for output
	pinMode(LED_BUILTIN, OUTPUT);

	// door sensors need pullup; read initial value
	pinMode(DOORSENSORPIN, INPUT_PULLUP);
	doorState = digitalRead(DOORSENSORPIN);
	
	// Flow sensor is for input - no pullup needed
	pinMode(FLOWSENSORPIN, INPUT);



	Serial.println("Setting interrupts.");
	// Set interrupts on rising signal for flow meter
	attachInterrupt(FLOWSENSORPIN, flowHandler, RISING);
	
	// Set interrupt for any signal change for door sensor
	attachInterrupt(DOORSENSORPIN, doorHandler, CHANGE);


	Serial.println("Connecting to wifi ....");
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
	sprintf(macaddr, WiFi.macAddress().c_str());
	Serial.print("MAC address: ");
	Serial.println(macaddr);


#ifdef USEMQTT
	// Connect to MQTT for initial update 
	Serial.println("Connecting to MQTT server: ");
	mqttSendDoor();
#endif
	
}



// Run indefinitely.
void loop()
{

#ifdef TEMPHUMIDITY
	// refresh sensors, avoiding refreshing "too often"
	if (millis() - lastTempHumidity > delayTempHumidity) {
		lastTempHumidity = millis();
		
		sensors_event_t event;
		dht.temperature().getEvent(&event);
		if (isnan(event.temperature)) {
			tempGood = 0;
		}
		else {
			temp = (event.temperature * 1.8) + 32;
			tempGood = 1;
		}
		
		// repeat for humidity
		dht.humidity().getEvent(&event);
		if (isnan(event.relative_humidity)) {
			humidityGood = 0;
		}
		else {
			humidity = event.relative_humidity;
			humidityGood = 1;
		}
	}
#endif


	// Note: trying to readLux() with the board absent will cause
	// the entire platform to crash.
	if (millis() - lastLuminance > delayLuminance && luxPresent) {
		lastLuminance = millis();
		lux = veml.readLux();
		
		if (lux == 989560448.00) {   // from observation
			Serial.println("Lux sensor out of range - disabling");
			luxGood = 0;
		} else {
			luxGood = 1;
		}
		
	}
	
	
	
	
	
	// TODO: deprecate serial output.
	// TODO: print timestamp (clock time) on serial to better correlate with
	// unexpected system crashes.
	
	// Print serial status when prompted, or every N milliseconds
	if (printNow || millis() - lastSerial > statusFreq) {
		lastSerial=millis();
		printNow = 0;
		
				Serial.println();
		Serial.println(millis());
		Serial.print("Door State: ");  Serial.println(doorState ? "OPEN" : "CLOSED");
		Serial.print("Pulse count: "); Serial.println(pulses, DEC);
		Serial.print("Liters: ");      Serial.println(pulses / (7.5 * 60.0));

#ifdef TEMPHUMIDITY
		if (tempGood) {
			Serial.print("Temperature: "); Serial.print(temp); Serial.println(" F");
		}
		if (humidityGood) {
			Serial.print("Humidity:    "); Serial.print(humidity); Serial.println(" %");
		}
#endif

		if (luxGood) {
			Serial.println();
			Serial.print("Luminance:   "); Serial.println(lux);
		}
	}
	

	// if door status changed - send update
	if (doorChanged) {
		Serial.print("Sending MQTT update for door: ");
		Serial.println(doorState ? "ON" : "OFF");
		mqttSendDoor();
		doorChanged = 0;
	}
	
	
	
#ifdef USEMQTT
	// TODO: do we need mqttclient.loop() when we're not subscribed to anything?
	mqttclient.loop();
#endif

	yield();  // or delay(0);
  
}



