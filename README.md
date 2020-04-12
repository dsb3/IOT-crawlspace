# IOT-crawlspace

IOT for crawlspace monitor

Created with the Adafruit Feather Huzzah in mind.  Probably works with a bunch of other stuff as well, but I know for a fact the code needs editing to work with ESP32.

## Getting Started

Copy example-config.h to private-config.h and fill in values with whatever is appropriate for your environment.

I create a permanent DHCP mapping for my device so I can reach it by the same address every time.  If you can do the same, do so, otherwise look into mDNS or assigning a static IP in the code.

Compile and upload the code to a Feather Huzzah.  Plug in the components into the pins mentioned.  Check the output over serial console is sensible.  Check the output over the URLs is sensible.  Then plug it in wherever your water meter is.


## Sample Integration

Created with Home Assistant integration in mind.

**customize.yaml:**

For easy of understanding, I keep my customize.yaml empty and include other files that contain the meat of the configuration, as shown here:
>...
sensor: !include sensor.yaml
binary_sensor: !include binary_sensor.yaml
utility_meter: !include utility_meter.yaml
...

**sensor.yaml:**

Create a RESTful sensor that, for now, just polls the flow.json output and saves it locally.  We scan every 15 seconds, with force_update set in order to allow our trend sensor to turn off once the sensor stops updating.

```yaml
...
- platform: rest
  resource: http://192.168.x.y/flow.json
  name: garage_waterflow_l
  value_template: '{{ value_json.flowcount }}'
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

