// Embed simple web server content inline to avoid SPIFFS overhead
// - Only suitable for very small files; literal R (raw string) is C++11 only

const char* index_html = R"(

<!DOCTYPE html>
<!-- 
  Based on original code from
    Rui Santos https://RandomNerdTutorials.com  
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
    <span class="sensor-labels">Luminance:</span>
    <span id="luminance">%LUMINANCE%</span>
    <sup class="units">lux</sup>
  </p>
  <p>
    <span class="sensor-labels">Door:</span>
    <span id="door">%DOOR%</span>
  </p>
  <p>
    <span class="sensor-labels">(uptime)</span>
    <span id="uptime">%UPTIME%</span>
  </p>
</body>

<!-- script will poll and reload values every 10s -->
<script>
  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        document.getElementById("waterflow").innerHTML = this.responseText;
      }
    };
    xhttp.open("GET", "/stat?WATERFLOW", true);
    xhttp.send();
  }, 10000 ) ;

  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4) {
        document.getElementById("luminance").innerHTML = (this.status == 200 ? this.responseText : "-");
      }
    };
    xhttp.open("GET", "/stat?LUMINANCE", true);
    xhttp.send();
  }, 10000 ) ;

  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        document.getElementById("door").innerHTML = this.responseText;
      }
    };
    xhttp.open("GET", "/stat?DOOR", true);
    xhttp.send();
  }, 10000 ) ;

  setInterval(function ( ) {
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function() {
      if (this.readyState == 4 && this.status == 200) {
        document.getElementById("uptime").innerHTML = this.responseText;
      }
    };
    xhttp.open("GET", "/stat?UPTIME", true);
    xhttp.send();
  }, 10000 ) ;


</script>
</html>

)";
