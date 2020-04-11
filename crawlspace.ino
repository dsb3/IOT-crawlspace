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


  Notes
  * See https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/faq
  * Don't use GPIO 0, 2, 15 (these need certain values at boot)
  * 
  * Also: don't use GPIO 16 (cannot attach it to interrupt)
  * 
   

**********************************************************/





#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// Config held in separate file
// const char* ssid = "SSID";
// const char* password = "password";
#include "private-config.h"

// static webserver content (avoids SPIFFS overhead for now)
#include "index_html.h"
#include "favicon_ico.h"



// Use built-in LED for flash/output (not yet used)
const int led = LED_BUILTIN;


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
int have_lux = 1;
volatile float lux = 0;


volatile int doorState;
volatile int doorChanged = 0;

// Used for serial output
volatile int printNow = 0;

// Timer to print serial output regardless of activity
unsigned long lastSerial = 0;


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);





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
String processor(const String& var){
	char buffer[50];
	
	if(var == "MILLIS"){
		sprintf(buffer, "%d", millis());
	}
	else if (var == "WATERFLOW"){
		sprintf(buffer, "%.2f", pulses / 450.0);
	}
	else if (var == "LUMINANCE"){
		sprintf(buffer, "%0.2f", lux);
	}
	else if (var == "DOOR"){
		sprintf(buffer, "%s", doorState ? "OPEN" : "CLOSED");
	}  
	
	return buffer;
}

// 404 handler
void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}


void setup() {
	// Setup
	Serial.begin(115200);
	Serial.println("Crawlspace monitoring system starting.");



	// Setup VEML7700 lux sensor
	// TODO: in testing, if I unplug the lux sensor it starts
	// giving unreasonably high readings.  If we see this, we can
	// just disable it - flag as "unavailable".
	if (!veml.begin()) {
		Serial.println("Lux sensor not found");
		have_lux=0;
	}
	else {
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
		// For ICO format icon (which can be smaller, use this)
		//AsyncWebServerResponse *response = request->beginResponse_P(200, "image/x-icon", favicon_ico_gz, favicon_ico_gz_len);
		//response->addHeader("Content-Encoding", "gzip");
		//request->send(response);
	});
	
	
	//// Examples with SPIFFS to read a data file from flash, with/without processing
	// 
	//server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
	//	request->send(SPIFFS, "/index.html", String(), false, processor);
	//});
	//server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
	//	request->send(SPIFFS, "/style.css", "text/css");
	//});

	// Route to set GPIO to HIGH
	// TODO: this is wrong; these should be POST, not GET
	// Keep this for now as a template for our /json query to follow
	//server.on("/on", HTTP_GET, [](AsyncWebServerRequest *request){
	//	//digitalWrite(ledPin, HIGH);    
	//	request->send(SPIFFS, "/index.html", String(), false, processor);
	//});

	
	server.on("/millis", HTTP_GET, [](AsyncWebServerRequest *request){
		char textplain[50];
		sprintf(textplain, "%d", millis());
		request->send_P(200, "text/plain", textplain);
	});
  
	server.on("/waterflow", HTTP_GET, [](AsyncWebServerRequest *request){
		char textplain[50];
		sprintf(textplain, "%.2f", pulses / 450.0);
		request->send_P(200, "text/plain", textplain);
	});
	
	server.on("/luminance", HTTP_GET, [](AsyncWebServerRequest *request){
		char textplain[50];
		lux = veml.readLux();
		sprintf(textplain, "%0.2f", lux);
		request->send_P(200, "text/plain", textplain);
	});
  
	server.on("/door", HTTP_GET, [](AsyncWebServerRequest *request){
		request->send_P(200, "text/plain", (doorState ? "OPEN" : "CLOSED"));
	});

	// 404 handler
	server.onNotFound(notFound);
	
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

}



// Run indefinitely.
void loop()
{

	// Print serial status when prompted, or every N milliseconds
	if (printNow || millis() - lastSerial > statusFreq) {
		lastSerial=millis();
		printNow = 0;
		
		if (have_lux) {
			lux = veml.readLux();
			
			if (lux == 989560448.00) {   // from observation
				Serial.println("Lux sensor out of range - disabling");
				have_lux = 0;
			}
				
		}
		
		Serial.println();
		Serial.println(millis());
		Serial.print("Door State: ");  Serial.println(doorState);
		Serial.print("Pulse count: "); Serial.println(pulses, DEC);
		Serial.print("Liters: ");      Serial.println(pulses / (7.5 * 60.0));
		
		if (have_lux) {
			Serial.println();
			Serial.print("Lux: ");         Serial.println(lux);
		}
	}
	

  yield();  // or delay(0);
  
}



