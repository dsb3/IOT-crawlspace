// Embed simple web server content inline to avoid SPIFFS overhead
// - Only suitable for very small files; literal R (raw string) is C++11 only

const char* all_json = R"(

{
  "name": "Crawlspace",
  "id": 12345678,

  "door": "%DOOR%",
  "luminance": "%LUMINANCE%",
  "flowcount": "%WATERFLOW%",
  
  "flowstats": {
    "minute": "4",
    "hour": "15",
    "day": "52",
  },

  "temperature": "%TEMPERATURE%",
  "temp_scale": "F",
  "humidity": "%HUMIDITY%",


  "uptime", "%MILLIS%"

}

)";
