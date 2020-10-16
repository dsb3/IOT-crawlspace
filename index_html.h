// Embed simple web server content inline to avoid SPIFFS overhead
// - Only suitable for very small files; literal R (raw string) is C++11 only

const char* index_html = R"(

<!DOCTYPE html>
<!-- 
  Web front end for diags.  Integration should use /all.json
-->
<html>
<head>
  <title>Crawlspace Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
<style>
html {
  /*font-family: Arial;*/
  display: inline-block;
  margin: 0px auto;
  text-align: center;
}
h1 {
  color: #0F3376;
  padding: 2vh;
}
.sensor-labels {
  vertical-align: middle;
  padding-bottom: 15px;
}
</style>


</head>
<body>
  <h1>Crawlspace Web Server</h1>

  <p>
    <span class="sensor-labels">Water Flow:</span>
    <span id="waterflow">%WATERFLOW%</span>
    <sup class="units">L</sup>
  </p>
  <p>
    <span class="sensor-labels">Temperature:</span>
    <span id="temperature">%TEMPERATURE%</span>
    <sup class="units">F</sup>
  </p>
  <p>
    <span class="sensor-labels">Humidity:</span>
    <span id="humidity">%HUMIDITY%</span>
    <sup class="units">%</sup>
  </p>
  <p>
    <span class="sensor-labels">Luminance:</span>
    <span id="luminance">%LUMINANCE%</span>
    <sup class="units">lux</sup>
  </p>
  <p>
    <span class="sensor-labels">Door:</span>
    <span id="door">%DOOR%</span>
  </p>
    <p>
    <span class="sensor-labels">GPIO 1:</span>
    <span id="door">%GPIO1%</span>
  </p>
    <p>
    <span class="sensor-labels">Motion:</span>
    <span id="door">%MOTION%</span>
  </p>
  <p>
    <span class="sensor-labels">Uptime: </span>
    <span id="uptime">%UPTIME%</span>
  </p>
</body>


</html>

)";
