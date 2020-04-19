// Embed simple web server content inline to avoid SPIFFS overhead
// - Only suitable for very small files; literal R (raw string) is C++11 only

const char* all_json = R"({
  "name": "Crawlspace",
  "macaddr": "%MACADDR%",

  "door": "%DOOR%",
  
  "flowpulses": "%FLOWPULSES%",  
  "flowcount": "%WATERFLOW%",
  "flowunits": "L",

  "luminance": "%LUMINANCE%",

  "temperature": "%TEMPERATURE%",
  "temperature_scale": "F",
  "humidity": "%HUMIDITY%",

  "motion": "%MOTION%",
  
  "uptime": "%UPTIME%",
  "millis": "%MILLIS%"
}
)";


/* future stuff for all.json
 *

  "flowstats": {
    "future-stuff": "in-here",
    "flowformin": "%FLOWFORMIN%",
    "minute": "%FLOWMINUTE%",
    "hour": "%FLOWHOUR%",
    "day": "%FLOWDAY%"
  },

  "luxstats": {
    "future-stuff": "in-here",
    "luxhighonemin":     "%LUXHIGHONEMIN%",
    "luxhighfivemin":    "%LUXHIGHFIVEMIN%",
    "luxhighfifteenmin": "%LUXHIGHFIFTEENMIN%",
    "luxlowonemin":      "%LUXLOWONEMIN%",
    "luxlowfivemin":     "%LUXLOWFIVEMIN%",
    "luxlowfifteenmin":  "%LUXLOWFIFTEENMIN%"
  },

*/


const char* flow_json = R"({
  "flowpulses": "%FLOWPULSES%",
  "flowcount":  "%WATERFLOW%",
  "flowunits":  "L",
  
  "millis": "%MILLIS%"
}
)";


// TODO: very much a future-looking set of config parameters
//
const char* config_json = R"({
  "__file__": "__FILE__",
  "__date__": "__DATE__",
  "__time__": "__TIME__",
  "author":   "%AUTHOR%",
  "version":  "%VERSION%",

  "waterflow": {
    "enabled":   "%FLOWENABLED%",
    "pulseperl": "%FLOWPERL%"
  }

  "temperature": {
    "enabled": "%TEMPENABLED%",
    "present": "%TEMPPRESENT%"
  },
  
  "luminance": {
    "enabled": "%LUXENABLED%",
    "present": "%LUXPRESENT%"
  },
  
  "doors": {
    "enabled": "%DOORENABLED%",
    "count":   "%DOORCOUNT%"
  }
  
  "network": {
    "OTA": "false",
    
    "mqtt": {
      "enabled":  "...",
      "active":   "...",
      "hostname": "%MQTTHOST%",
      "ident":    "%MQTTIDENT%"
    },

    "ntp": {
      "enabled": "...",
      "active":  "...",
      "stats": "..., ..."
    }
  },
  
  "millis": "%MILLIS%"
}
)";

