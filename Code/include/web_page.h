#pragma once
#include <Arduino.h>

const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Stepper Controller</title>

  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background: #111;
      color: white;
      margin-top: 50px;
    }

    h2 {
      margin-bottom: 30px;
    }

    button {
      font-size: 22px;
      padding: 16px 35px;
      margin: 10px;
      border: none;
      border-radius: 10px;
      cursor: pointer;
      color: white;
    }

    .start {
      background-color: #27ae60;
    }

    .stop {
      background-color: #c0392b;
    }

    .home {
      background-color: #2980b9;
    }

    .statusBox {
      margin-top: 25px;
      font-size: 20px;
      padding: 15px;
      background: #222;
      border-radius: 10px;
      display: inline-block;
      min-width: 260px;
    }
  </style>
</head>

<body>
  <h2>ESP32 Dual Stepper Controller</h2>

  <button class="home" onclick="sendCommand('/home', 'Homing... please wait')">HOME</button>
  <button class="start" onclick="sendCommand('/start', 'Starting motors...')">START</button>
  <button class="stop" onclick="sendCommand('/stop', 'Stopping motors...')">STOP</button>

  <br>

  <button onclick="getStatus()">CHECK STATUS</button>

  <div class="statusBox" id="status">
    Status: Ready
  </div>

  <script>
    function sendCommand(path, waitingText) {
      document.getElementById("status").innerText = "Status: " + waitingText;

      fetch(path)
        .then(response => response.text())
        .then(text => {
          document.getElementById("status").innerText = "Status: " + text;
        })
        .catch(error => {
          document.getElementById("status").innerText = "Status: Connection error";
        });
    }

    function getStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          document.getElementById("status").innerText =
            "Running: " + data.motorsRunning + " | Homed: " + data.homed;
        })
        .catch(error => {
          document.getElementById("status").innerText = "Status: Connection error";
        });
    }
  </script>
</body>
</html>
)rawliteral";