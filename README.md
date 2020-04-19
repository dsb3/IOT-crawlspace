# IOT-crawlspace

IOT for crawlspace monitor

Created with the Adafruit Feather Huzzah in mind.  Probably works with a bunch of other stuff as well, but I know for a fact the code needs editing to work with ESP32.

## Getting Started

Copy example-config.h to private-config.h and fill in values with whatever is appropriate for your environment.

I create a permanent DHCP mapping for my device so I can reach it by the same address every time.  If you can do the same, do so, otherwise look into mDNS or assigning a static IP in the code.

Compile and upload the code to a Feather Huzzah.  Plug in the components into the pins mentioned.  Check the output over serial console is sensible.  Check the output over the URLs is sensible.  Then plug it in wherever your water meter is.


## Sample Integration - Basics

Created with Home Assistant integration in mind.

**customize.yaml:**

For easy of understanding, I keep my customize.yaml empty and include other files that contain the meat of the configuration, as shown here:
>...
sensor: !include sensor.yaml
binary_sensor: !include binary_sensor.yaml
utility_meter: !include utility_meter.yaml
...


## Sample Integration - Polling

We can poll the device for updates, choosing to either read the value in and read 

**sensor.yaml:**

Create a RESTful sensor that polls the flow.json output and saves it locally.

Scanning every 15 seconds offers reasonable responsiveness to when water starts to flow.  We must have "force_update: true" to cause the sensor to update with the same value when water stops flowing, else our trend sensor (below) will not detect the fact water flow stopped.

```yaml
...
- platform: rest
  resource: http://192.168.x.y/flow.json
  name: garage_waterflow_l
  value_template: '{{ ( value_json.flowpulses | float / 450.0 ) | round(2) }}'
  unit_of_measurement: "L"
  scan_interval: 15
  force_update: true
...
```

**binary_sensor.yaml:**

Create a "trend" sensor.  This simply reports dry/wet for whether water is flowing or not.

```yaml
...
- platform: trend
    sensors:
      garage_water_flowing:
      device_class: moisture
      entity_id: sensor.garage_waterflow_l
      min_gradient: 0.00001    # squelch e-18 type rounding errors
...
```

**utility_meter.yaml:**

Create two utility meters - daily and monthly.  These make for clear output and manage water usage across device restarts (which cause garage_waterflow_l to reset to zero).

To-do: the utility meter integration claims to have a "reset" service call but I've not yet gotten it to work.

```yaml
...
garage_water_daily:
  source: sensor.garage_waterflow_l
  cycle: daily

garage_water_monthly:
  source: sensor.garage_waterflow_l
  cycle: monthly
...
```


## Sample Integration - MQTT

We can also have the device push updates via MQTT.  This permits immediate notification on start of water flow, but more logic is required behind the scenes.

The main problems to address:

* Additional logic required on device to send updates immediately, and then send updates at an appropriate pace, including rapid updates with the same value when water stops flowing, in order for our flow detection sensor to recognize that water has stopped.
* When home-assistant restarts, the MQTT sensor will, by default, easily be mis-assumed to be zero.  As soon as the real value comes in, we will double-count it in the utility meters.
* Method one to address this is the availability_template - if we don't have an mqtt value, turn the interpretation unavailable so as to ignore it until we have a value
* Method two to address this is to send the "retain: true" flag as part of the MQTT message - this gives HA an additional chance to start with the value pre-populated rather than having the sensor unavailable until the next update.
* Regardless, there will always be some specific sequences of device, mqtt broker, home assistant restarting that lead to data mis-interpretation.  This tries to reduce it as much as possible.


**sensor.yaml:**

We create two sensors - one for the MQTT data feed itself.  The other interprets it in units of measurement and drives the utility meters / binary sensor.


```yaml
...
# read raw pulse data via MQTT updates
- platform: mqtt
  name: mqtt_water_pulse
  unit_of_measurement: "pulses"
  force_update: true
  state_topic: "ha/sensor/BCDDC2aabbcc/waterflow/state"

- platform: template
  sensors:
    mqtt_water_liter:
      unit_of_measurement: "L"
      availability_template: '{{ states("sensor.mqtt_water_pulse") != "unknown" }}'
      value_template: '{{ (states("sensor.mqtt_water_pulse") | float / 450.0 ) | round(2)  }}'

...
```

In this example, our binary_sensor to detect water flowing must monitor the mqtt_water_pulse sensor, NOT mqtt_water_liter.  The MQTT sensor easily accepts a "force_update: true" to refresh the sensor with the same value, as happens when water stops flowing.


