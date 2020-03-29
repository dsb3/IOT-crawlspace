/**********************************************************
  
  Arduino sketch to drive the ESP8266 in our the crawlspace


Refs:
* https://github.com/bram2202/mqtt-watermeter
* 



  Inputs:
  * water flow sensor
  * door/window sensor
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
  * https://www.adafruit.com/product/375   door/window sensor

  Future:
  * https://www.adafruit.com/product/189   PIR sensor
  * https://www.adafruit.com/product/385   Temp/humidity sensor



  Setup diags LED
  * None - use the onboard LED

  Setup water flow sensor
  * Connect red to +5V
  * Connect black to common ground
  * Connect yellow data to pin #13
  *   (interrupt cannot be attached to #16)

  Setup door sensor
  * Connect one end to common ground
  * Connect other to input pin #14 (use internal pullup)


  Notes
  * See https://learn.adafruit.com/adafruit-feather-huzzah-esp8266/faq
  * Don't use GPIO 0, 2, 15 as these need certain values at boot
   

**********************************************************/

// Needed for ESP8266 specific timer functions
//extern "C" {
//#include "user_interface.h"
//}



// Use built-in LED for flash/output
const int led = LED_BUILTIN;

// Which pin for flow sensor input
#define FLOWSENSORPIN 13

// Which pin for door/window sensor input
#define DOORSENSORPIN 14


// How often to show serial stats by default (in ms)
#define statusFreq 5000

// vars changed by interrupt need to be marked "volatile" so 
// as to not be optimized out by the compiler
volatile unsigned long pulses = 0;

volatile int      doorState;
volatile int      doorChanged = 0;


volatile unsigned int lastSerial = 0;






// Interrupt called on rising signal = just count them
void ICACHE_RAM_ATTR flowHandler()
{
	pulses++;
}


// Interrupt called on changing signal = note sensor changed
void ICACHE_RAM_ATTR doorHandler()
{
	doorState = digitalRead(DOORSENSORPIN);
	doorChanged = 1;
	lastSerial=0;   // flag for immediate output
	
	// debug - allow clearing counter easily
	pulses=0;
}



void setup() {
	// Setup
	Serial.begin(115200);
	Serial.println("Water Flow system starting.");


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


}




// Run indefinitely.
void loop()
{


	// Print serial status every N milliseconds
	if (millis() - lastSerial > statusFreq) {
		lastSerial=millis();
		
		Serial.println();
		Serial.println(millis());
		Serial.print("Door State: ");  Serial.println(doorState);
		Serial.print("Pulse count: "); Serial.println(pulses, DEC);
		Serial.print("Liters: ");      Serial.println(pulses / (7.5 * 60.0));
		
	}
	
	
  // if a plastic sensor use the following calculation
  // Sensor Frequency (Hz) = 7.5 * Q (Liters/min)
  // Liters = Q * time elapsed (seconds) / 60 (seconds/minute)
  // Liters = (Frequency (Pulses/second) / 7.5) * time elapsed (seconds) / 60
  // Liters = Pulses / (7.5 * 60)
  //float liters = pulses;
  //liters /= 7.5;
  //liters /= 60.0;


  yield();  // or delay(0);
  
}



