// Embed simple web server content inline to avoid SPIFFS overhead
// - Only suitable for very small files; literal R (raw string) is C++11 only

const char* all_json = R"({
  "name": "Crawlspace",
  "id": 12345678,

  "door": "%DOOR%",
  
  "luminance": "%LUMINANCE%",
    
  "flowpulses": "%FLOWPULSES%",  
  "flowcount": "%WATERFLOW%",
  "flowunits": "L",
  
  "flowstats": {
    "future-stuff": "in-here",
    "minute": "%FLOWMINUTE%",
    "hour": "%FLOWHOUR%",
    "day": "%FLOWDAY%"
  },

  "temperature": "%TEMPERATURE%",
  "temp_scale": "F",
  "humidity": "%HUMIDITY%",

  "uptime": "%MILLIS%"
}
)";


const char* flow_json = R"({
  "flowpulses": "%FLOWPULSES%",
  "flowcount":  "%WATERFLOW%",
  "flowunits":  "L",
  
  "uptime": "%MILLIS%"
}
)";
