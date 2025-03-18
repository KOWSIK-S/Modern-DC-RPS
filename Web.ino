#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// WiFi SoftAP credentials
const char* ssid = "OpenLabG14";
const char* password = "OpenLabGroup14";

// Create web server on port 80 and a DNS server on port 53
WebServer server(80);
DNSServer dnsServer;

// Global storage for sensor (measured) values
// Indices: 0: Voltage1, 1: Current1, 2: Voltage2, 3: Current2, 4: Voltage3, 5: Current3
String sensorData[6] = { "0.00", "0.00", "0.00", "0.00", "0.00", "0.00" };

//----------------------------------------------------------------------
// Structure to hold UI state for each power source.
// Index 0 is unused; indices 1-3 correspond to power sources.
struct UIState {
  int sourceType;    // 0 = Voltmeter, 1 = RPS mode
  float voltageSet;  // Voltage setpoint (0-48V)
  float currentSet;  // Current setpoint (0-5A)
  int ground;        // 0 = Commonly Grounded, 1 = Not Commonly Grounded
  String mode;       // "voltage", "current", or "power"
};

UIState uiState[4] = {
  { 0, 0, 0, 0, "voltage" },  // dummy entry for index 0
  { 0, 24, 2.5, 0, "voltage" },
  { 0, 24, 2.5, 0, "voltage" },
  { 0, 24, 2.5, 0, "voltage" }
};
//----------------------------------------------------------------------

// Global timing variables
unsigned long lastClientUpdateTime = millis();  // Updated whenever a /update is received
unsigned long lastPrintTime = millis();         // Used for printing UI state (at least 3 times/sec)

// Global flag to track if admin is logged in
bool adminLoggedIn = false;

//----------------------------------------------------------------------
// Complete HTML page served by the ESP32 as a raw literal string.
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Modern DC RPS</title>
  <style>
    /* General Styles */
    body {
      font-family: Arial, sans-serif;
      background: #f0f4f7;
      margin: 0;
      padding: 20px;
      text-align: center;
    }
    .container {
      max-width: 900px;
      margin: auto;
      background: #fff;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.1);
      display: none; /* Hidden until login and connectivity verified */
    }
    h1 {
      color: #333;
      margin-bottom: 20px;
    }
    /* Login Overlay */
    #loginOverlay {
      position: fixed;
      top: 0; left: 0;
      width: 100%;
      height: 100%;
      background: rgba(0,0,0,0.75);
      display: flex;
      justify-content: center;
      align-items: center;
      z-index: 1000;
    }
    .login-box {
      background: #fff;
      padding: 20px;
      border-radius: 8px;
      width: 300px;
      box-shadow: 0 2px 8px rgba(0,0,0,0.2);
    }
    .login-box h2 {
      margin-top: 0;
      text-align: center;
    }
    .login-box input {
      width: 100%;
      padding: 10px;
      margin: 5px 0;
      box-sizing: border-box;
    }
    .login-box button {
      width: 100%;
      padding: 10px;
      background: #66bb6a;
      color: #fff;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
    .login-box button:hover {
      background: #57a05a;
    }
    .login-box p {
      color: red;
      display: none;
    }
    /* Error Overlay */
    #errorOverlay {
      position: fixed;
      top: 0; left: 0;
      width: 100%;
      height: 100%;
      background: rgba(255, 0, 0, 0.8);
      color: white;
      font-size: 2em;
      display: none;
      justify-content: center;
      align-items: center;
      z-index: 1500;
    }
    /* Power Source Styles */
    .power-source {
      margin: 20px 0;
      padding: 15px;
      border: 1px solid #ddd;
      border-radius: 8px;
      background: #fafafa;
      position: relative;
    }
    .source-header {
      display: flex;
      align-items: center;
      justify-content: center;
      gap: 10px;
      margin-bottom: 10px;
      background-color: #e8f5e9;
      padding: 5px;
      border-radius: 4px;
    }
    /* Special styling for Power Source 1 header */
    #source1 .source-header {
      background-color: #ffcc80;
      border: 2px solid #ff9800;
      border-radius: 8px;
      padding: 10px;
    }
    .source-header h2 {
      margin: 0;
    }
    .source-header label {
      font-weight: normal;
      margin-bottom: 0;
    }
    .readings {
      font-size: 18px;
      margin: 10px 0;
    }
    .control-group {
      margin: 10px 15px;
      text-align: left;
      display: inline-block;
      vertical-align: top;
    }
    .control-group h3 {
      margin: 10px 0 5px;
      font-size: 16px;
    }
    label {
      display: block;
      margin: 5px 0;
      font-weight: bold;
    }
    input[type="number"] {
      width: 80px;
      padding: 5px;
    }
    input[type="range"] {
      width: 200px;
    }
    .toggle-switch {
      position: relative;
      display: inline-block;
      width: 50px;
      height: 24px;
    }
    .toggle-switch input {
      display: none;
    }
    .slider {
      position: absolute;
      cursor: pointer;
      top: 0; left: 0; right: 0; bottom: 0;
      background-color: #ccc;
      transition: 0.4s;
      border-radius: 24px;
    }
    .slider:before {
      position: absolute;
      content: "";
      height: 18px;
      width: 18px;
      left: 3px;
      bottom: 3px;
      background-color: white;
      transition: 0.4s;
      border-radius: 50%;
    }
    input:checked + .slider {
      background-color: #66bb6a;
    }
    input:checked + .slider:before {
      transform: translateX(26px);
    }
    .mode-select {
      width: 180px;
      padding: 5px;
    }
    .label-span {
      margin-left: 8px;
      font-weight: normal;
    }
  </style>
</head>
<body>
  <!-- Login Overlay -->
  <div id="loginOverlay">
    <div class="login-box">
      <h2>Login</h2>
      <input type="text" id="username" placeholder="Username">
      <input type="password" id="password" placeholder="Password">
      <button onclick="doLogin()">Login</button>
      <p id="loginError">Invalid credentials</p>
    </div>
  </div>

  <!-- Error Overlay (shown when connectivity is lost) -->
  <div id="errorOverlay">
    Open lab Error page
  </div>

  <!-- Main Interface Container -->
  <div class="container" id="mainContainer">
    <h1>Modern DC RPS</h1>

    <!-- Power Source 1 -->
    <div class="power-source" id="source1">
      <div class="source-header">
        <h2>Power Source 1</h2>
        <label class="toggle-switch">
          <!-- 0 = Voltmeter, 1 = RPS -->
          <input type="checkbox" id="sourceType1" onchange="updateSourceType(1)">
          <span class="slider"></span>
        </label>
        <label id="sourceTypeLabel1" class="label-span">Voltmeter</label>
      </div>

      <div class="readings">
        Voltage: <span id="voltage1">0.00</span> V<br>
        Current: <span id="current1">0.00</span> A
      </div>

      <div class="control-group" id="controlGroup1">
        <h3>Voltage Setpoint</h3>
        <label>Adjust Voltage (Slider):</label>
        <input type="range" min="0" max="48" value="24" id="voltageDial1" onchange="updateVoltageDial(1)">
        <span id="voltageDialValue1">24</span><br>
        <label>Voltage (Numeric):</label>
        <input type="number" min="0" max="48" value="24" id="voltageNum1" onchange="updateVoltageNum(1)">
      </div>

      <div class="control-group" id="controlGroup1b">
        <h3>Current Setpoint</h3>
        <label>Adjust Current (Slider):</label>
        <input type="range" min="0" max="5" value="2.5" id="currentDial1" onchange="updateCurrentDial(1)">
        <span id="currentDialValue1">2.5</span><br>
        <label>Current (Numeric):</label>
        <input type="number" min="0" max="5" value="2.5" id="currentNum1" onchange="updateCurrentNum(1)">
      </div>
      
      <div class="control-group" id="controlGroup1c">
        <label>Grounding:</label>
        <label class="toggle-switch">
          <input type="checkbox" id="groundToggle1" onchange="updateGround(1)">
          <span class="slider"></span>
        </label>
        <span id="groundLabel1">Commonly Grounded</span>
      </div>
      <div class="control-group" id="controlGroup1d">
        <label for="modeSelect1">Mode:</label>
        <select id="modeSelect1" class="mode-select" onchange="updateMode(1)">
          <option value="voltage">Voltage Control</option>
          <option value="current">Current Control</option>
          <option value="power">Power Control</option>
        </select>
      </div>
    </div>

    <!-- Power Source 2 -->
    <div class="power-source" id="source2">
      <div class="source-header">
        <h2>Power Source 2</h2>
        <label class="toggle-switch">
          <input type="checkbox" id="sourceType2" onchange="updateSourceType(2)">
          <span class="slider"></span>
        </label>
        <label id="sourceTypeLabel2" class="label-span">Voltmeter</label>
      </div>

      <div class="readings">
        Voltage: <span id="voltage2">0.00</span> V<br>
        Current: <span id="current2">0.00</span> A
      </div>

      <div class="control-group" id="controlGroup2">
        <h3>Voltage Setpoint</h3>
        <label>Adjust Voltage (Slider):</label>
        <input type="range" min="0" max="48" value="24" id="voltageDial2" onchange="updateVoltageDial(2)">
        <span id="voltageDialValue2">24</span><br>
        <label>Voltage (Numeric):</label>
        <input type="number" min="0" max="48" value="24" id="voltageNum2" onchange="updateVoltageNum(2)">
      </div>

      <div class="control-group" id="controlGroup2b">
        <h3>Current Setpoint</h3>
        <label>Adjust Current (Slider):</label>
        <input type="range" min="0" max="5" value="2.5" id="currentDial2" onchange="updateCurrentDial(2)">
        <span id="currentDialValue2">2.5</span><br>
        <label>Current (Numeric):</label>
        <input type="number" min="0" max="5" value="2.5" id="currentNum2" onchange="updateCurrentNum(2)">
      </div>
      
      <div class="control-group" id="controlGroup2c">
        <label>Grounding:</label>
        <label class="toggle-switch">
          <input type="checkbox" id="groundToggle2" onchange="updateGround(2)">
          <span class="slider"></span>
        </label>
        <span id="groundLabel2">Commonly Grounded</span>
      </div>
      <div class="control-group" id="controlGroup2d">
        <label for="modeSelect2">Mode:</label>
        <select id="modeSelect2" class="mode-select" onchange="updateMode(2)">
          <option value="voltage">Voltage Control</option>
          <option value="current">Current Control</option>
          <option value="power">Power Control</option>
        </select>
      </div>
    </div>

    <!-- Power Source 3 -->
    <div class="power-source" id="source3">
      <div class="source-header">
        <h2>Power Source 3</h2>
        <label class="toggle-switch">
          <input type="checkbox" id="sourceType3" onchange="updateSourceType(3)">
          <span class="slider"></span>
        </label>
        <label id="sourceTypeLabel3" class="label-span">Voltmeter</label>
      </div>

      <div class="readings">
        Voltage: <span id="voltage3">0.00</span> V<br>
        Current: <span id="current3">0.00</span> A
      </div>

      <div class="control-group" id="controlGroup3">
        <h3>Voltage Setpoint</h3>
        <label>Adjust Voltage (Slider):</label>
        <input type="range" min="0" max="48" value="24" id="voltageDial3" onchange="updateVoltageDial(3)">
        <span id="voltageDialValue3">24</span><br>
        <label>Voltage (Numeric):</label>
        <input type="number" min="0" max="48" value="24" id="voltageNum3" onchange="updateVoltageNum(3)">
      </div>

      <div class="control-group" id="controlGroup3b">
        <h3>Current Setpoint</h3>
        <label>Adjust Current (Slider):</label>
        <input type="range" min="0" max="5" value="2.5" id="currentDial3" onchange="updateCurrentDial(3)">
        <span id="currentDialValue3">2.5</span><br>
        <label>Current (Numeric):</label>
        <input type="number" min="0" max="5" value="2.5" id="currentNum3" onchange="updateCurrentNum(3)">
      </div>
      
      <div class="control-group" id="controlGroup3c">
        <label>Grounding:</label>
        <label class="toggle-switch">
          <input type="checkbox" id="groundToggle3" onchange="updateGround(3)">
          <span class="slider"></span>
        </label>
        <span id="groundLabel3">Commonly Grounded</span>
      </div>
      <div class="control-group" id="controlGroup3d">
        <label for="modeSelect3">Mode:</label>
        <select id="modeSelect3" class="mode-select" onchange="updateMode(3)">
          <option value="voltage">Voltage Control</option>
          <option value="current">Current Control</option>
          <option value="power">Power Control</option>
        </select>
      </div>
    </div>
  </div>

  <script>
    // Global UI state for each source. Default: Voltmeter mode (0)
    let sources = {
      1: { sourceType: 0, ground: 0, mode: "voltage", voltageSet: 24, currentSet: 2.5 },
      2: { sourceType: 0, ground: 0, mode: "voltage", voltageSet: 24, currentSet: 2.5 },
      3: { sourceType: 0, ground: 0, mode: "voltage", voltageSet: 24, currentSet: 2.5 }
    };

    // Global variable to track the user's role: "user" or "admin"
    let userRole = "";
    let lastSuccessful = Date.now();

    // Login function supporting both regular and admin users.
    function doLogin() {
      let user = document.getElementById("username").value.trim();
      let pass = document.getElementById("password").value.trim();
      if ((user === "open lab" && pass === "openlab") || (user === "admin" && pass === "admin")) {
        if (user === "admin") {
          userRole = "admin";
          let xhr = new XMLHttpRequest();
          xhr.open("GET", "/setUserRole?role=admin", true);
          xhr.send();
          disableAllControls(); // Admin: view-only mode.
        } else {
          userRole = "user";
          let xhr = new XMLHttpRequest();
          xhr.open("GET", "/setUserRole?role=user", true);
          xhr.send();
        }
        document.getElementById("loginOverlay").style.display = "none";
        initializeControls();
      } else {
        document.getElementById("loginError").style.display = "block";
      }
    }

    // Disable control elements for all sources (view only mode)
    function disableAllControls() {
      for (let i = 1; i <= 3; i++){
        disableControls(i);
      }
    }
    
    // Initialize controls for sources.
    function initializeControls() {
      for(let i = 1; i <= 3; i++){
        if(sources[i].sourceType === 0){
          disableControls(i);
        } else {
          enableControls(i);
        }
      }
    }

    // Enable controls for a given source.
    function enableControls(source) {
      if(userRole === "admin") return; // Admin remains view-only.
      document.getElementById("voltageDial" + source).disabled = false;
      document.getElementById("voltageNum" + source).disabled = false;
      document.getElementById("currentDial" + source).disabled = false;
      document.getElementById("currentNum" + source).disabled = false;
    }
    
    // Disable controls for a given source and force setpoints to 0.
    function disableControls(source) {
      document.getElementById("voltageDial" + source).disabled = true;
      document.getElementById("voltageNum" + source).disabled = true;
      document.getElementById("currentDial" + source).disabled = true;
      document.getElementById("currentNum" + source).disabled = true;
      document.getElementById("voltageDial" + source).value = 0;
      document.getElementById("voltageDialValue" + source).innerHTML = "0";
      document.getElementById("voltageNum" + source).value = 0;
      document.getElementById("currentDial" + source).value = 0;
      document.getElementById("currentDialValue" + source).innerHTML = "0";
      document.getElementById("currentNum" + source).value = 0;
      sources[source].voltageSet = 0;
      sources[source].currentSet = 0;
    }

    // Toggle source type: RPS (1) or Voltmeter (0).
    function updateSourceType(source) {
      let toggle = document.getElementById("sourceType" + source);
      let label = document.getElementById("sourceTypeLabel" + source);
      if(toggle.checked) {
        sources[source].sourceType = 1;
        label.textContent = "RPS Mode";
        enableControls(source);
      } else {
        sources[source].sourceType = 0;
        label.textContent = "Voltmeter";
        disableControls(source);
      }
      sendUpdate(source, "sourceType", sources[source].sourceType);
    }

    // Voltage setpoint update with clamping.
    function updateVoltageDial(source) {
      let dial = document.getElementById("voltageDial" + source);
      let numInput = document.getElementById("voltageNum" + source);
      let dialValue = document.getElementById("voltageDialValue" + source);
      let value = parseFloat(dial.value);
      dialValue.innerHTML = value;
      numInput.value = value;
      sources[source].voltageSet = value;
      sendUpdate(source, "voltage", value);
    }
    function updateVoltageNum(source) {
      let numInput = document.getElementById("voltageNum" + source);
      let dial = document.getElementById("voltageDial" + source);
      let dialValue = document.getElementById("voltageDialValue" + source);
      let value = parseFloat(numInput.value);
      if(value > 48) { value = 48; numInput.value = value; }
      if(value < 0) { value = 0; numInput.value = value; }
      dialValue.innerHTML = value;
      dial.value = value;
      sources[source].voltageSet = value;
      sendUpdate(source, "voltage", value);
    }

    // Current setpoint update with clamping.
    function updateCurrentDial(source) {
      let dial = document.getElementById("currentDial" + source);
      let numInput = document.getElementById("currentNum" + source);
      let dialValue = document.getElementById("currentDialValue" + source);
      let value = parseFloat(dial.value);
      dialValue.innerHTML = value;
      numInput.value = value;
      sources[source].currentSet = value;
      sendUpdate(source, "current", value);
    }
    function updateCurrentNum(source) {
      let numInput = document.getElementById("currentNum" + source);
      let dial = document.getElementById("currentDial" + source);
      let dialValue = document.getElementById("currentDialValue" + source);
      let value = parseFloat(numInput.value);
      if(value > 5) { value = 5; numInput.value = value; }
      if(value < 0) { value = 0; numInput.value = value; }
      dialValue.innerHTML = value;
      dial.value = value;
      sources[source].currentSet = value;
      sendUpdate(source, "current", value);
    }

    // Ground toggle update.
    function updateGround(source) {
      let groundToggle = document.getElementById("groundToggle" + source);
      let groundLabel = document.getElementById("groundLabel" + source);
      sources[source].ground = groundToggle.checked ? 1 : 0;
      groundLabel.textContent = groundToggle.checked ? "Commonly Grounded" : "Not Commonly Grounded";
      sendUpdate(source, "ground", sources[source].ground);
    }

    // Mode select update.
    function updateMode(source) {
      let modeSelect = document.getElementById("modeSelect" + source);
      sources[source].mode = modeSelect.value;
      sendUpdate(source, "mode", modeSelect.value);
    }

    // Send update command to server via AJAX.
    function sendUpdate(source, param, value) {
      // If admin is logged in, do not send any updates.
      if(userRole === "admin") return;
      let xhr = new XMLHttpRequest();
      xhr.open("GET", "/update?source=" + source + "&param=" + param + "&value=" + value, true);
      xhr.send();
    }

    // Connectivity and data fetching.
    function fetchData() {
      let xhr = new XMLHttpRequest();
      xhr.timeout = 2000;
      xhr.onreadystatechange = function() {
        if (this.readyState === 4) {
          if (this.status === 200) {
            lastSuccessful = Date.now();
            try {
              let data = JSON.parse(this.responseText);
              // Update display only for sources in RPS mode.
              for(let i = 1; i <= 3; i++){
                if(sources[i].sourceType === 1) {
                  document.getElementById("voltage" + i).innerHTML = data["voltage" + i];
                  document.getElementById("current" + i).innerHTML = data["current" + i];
                } else {
                  document.getElementById("voltage" + i).innerHTML = "0.00";
                  document.getElementById("current" + i).innerHTML = "0.00";
                }
              }
            } catch(e) {
              console.error("Error parsing JSON:", e);
            }
          }
        }
      };
      xhr.onerror = function() { console.error("Error fetching data"); };
      xhr.ontimeout = function() { console.error("Data fetch timed out"); };
      xhr.open("GET", "/data", true);
      xhr.send();
    }

    // Check connectivity: if no successful fetch within 2 seconds, reset state and return to login.
    function checkConnectivity() {
      if (Date.now() - lastSuccessful > 2000) {
        for (let i = 1; i <= 3; i++){
          document.getElementById("voltageDial" + i).value = 0;
          document.getElementById("voltageDialValue" + i).innerHTML = "0";
          document.getElementById("voltageNum" + i).value = 0;
          document.getElementById("currentDial" + i).value = 0;
          document.getElementById("currentDialValue" + i).innerHTML = "0";
          document.getElementById("currentNum" + i).value = 0;
          document.getElementById("sourceType" + i).checked = false;
          document.getElementById("sourceTypeLabel" + i).textContent = "Voltmeter";
        }
        document.getElementById("mainContainer").style.display = "none";
        document.getElementById("loginOverlay").style.display = "flex";
      } else {
        if(document.getElementById("loginOverlay").style.display === "none")
          document.getElementById("mainContainer").style.display = "block";
        document.getElementById("errorOverlay").style.display = "none";
      }
    }

    setInterval(fetchData, 300);
    setInterval(checkConnectivity, 500);
  </script>
</body>
</html>
)rawliteral";

//----------------------------------------------------------------------
// Server Handlers

// Serve the main HTML page.
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// Serve sensor data as JSON.
void handleData() {
  String json = "{";
  json += "\"voltage1\":\"" + sensorData[0] + "\",";
  json += "\"current1\":\"" + sensorData[1] + "\",";
  json += "\"voltage2\":\"" + sensorData[2] + "\",";
  json += "\"current2\":\"" + sensorData[3] + "\",";
  json += "\"voltage3\":\"" + sensorData[4] + "\",";
  json += "\"current3\":\"" + sensorData[5] + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// Handle UI update requests from the client.
void handleUpdate() {
  if (server.hasArg("source") && server.hasArg("param") && server.hasArg("value")) {
    // If admin is logged in, ignore any update (view-only mode)
    if (adminLoggedIn) {
      server.send(200, "text/plain", "View Only Mode");
      return;
    }
    String sourceStr = server.arg("source");
    String param = server.arg("param");
    String value = server.arg("value");
    int source = sourceStr.toInt();

    if (param == "voltage") {
      uiState[source].voltageSet = value.toFloat();
    } else if (param == "current") {
      uiState[source].currentSet = value.toFloat();
    } else if (param == "sourceType") {
      uiState[source].sourceType = value.toInt();
    } else if (param == "ground") {
      uiState[source].ground = value.toInt();
    } else if (param == "mode") {
      uiState[source].mode = value;
    }
    lastClientUpdateTime = millis();

    String command = "SRC," + sourceStr + "," + param + "," + value;
    Serial.print("Sending command to Nano: ");
    Serial.println(command);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// Handler to set user role (admin or user)
void handleSetUserRole() {
  if (server.hasArg("role")) {
    String role = server.arg("role");
    if (role == "admin") {
      adminLoggedIn = true;
      Serial.println("Admin logged in.");
    } else {
      adminLoggedIn = false;
      Serial.println("Non-admin logged in.");
    }
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Bad Request");
  }
}

// Process sensor data received via Serial from a Nano.
void parseSerialData(String dataLine) {
  int lastComma = -1;
  String values[6];
  for (int i = 0; i < 6; i++) {
    int commaIndex = dataLine.indexOf(',', lastComma + 1);
    if (commaIndex == -1 && i < 5) {
      return;
    }
    if (commaIndex == -1) {
      values[i] = dataLine.substring(lastComma + 1);
    } else {
      values[i] = dataLine.substring(lastComma + 1, commaIndex);
      lastComma = commaIndex;
    }
  }
  for (int i = 0; i < 6; i++) {
    sensorData[i] = values[i];
  }
}

// Print the current UI state to the Serial Monitor.
void printUIState() {
  for (int i = 1; i <= 3; i++) {
    Serial.print("Source ");
    Serial.print(i);
    Serial.print(" | Mode: ");
    Serial.print(uiState[i].mode);
    Serial.print(" | Type: ");
    Serial.print(uiState[i].sourceType == 0 ? "Voltmeter" : "RPS");
    Serial.print(" | Voltage Set: ");
    Serial.print(uiState[i].voltageSet);
    Serial.print(" V | Current Set: ");
    Serial.print(uiState[i].currentSet);
    Serial.print(" A | Ground: ");
    Serial.println(uiState[i].ground == 0 ? "Commonly Grounded" : "Not Grounded");
  }
}

// Process Serial input from the IDE for updating voltage/current values.
void processSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() < 3) return;
    char type = input.charAt(0);
    int source = input.charAt(1) - '0';  // assuming source 1-3
    int eqPos = input.indexOf('=');
    if (eqPos == -1) return;
    String valStr = input.substring(eqPos + 1);
    float val = valStr.toFloat();
    if (type == 'V' || type == 'v') {
      if (val < 0) val = 0;
      if (val > 48) val = 48;
      uiState[source].voltageSet = val;
      Serial.print("Updated Source ");
      Serial.print(source);
      Serial.print(" Voltage via IDE: ");
      Serial.println(val);
    } else if (type == 'C' || type == 'c') {
      if (val < 0) val = 0;
      if (val > 5) val = 5;
      uiState[source].currentSet = val;
      Serial.print("Updated Source ");
      Serial.print(source);
      Serial.print(" Current via IDE: ");
      Serial.println(val);
    }
  }
}

//----------------------------------------------------------------------
// Setup function
void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("XIAO ESP32S3 starting...");

  WiFi.softAP(ssid, password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(myIP);

  dnsServer.start(53, "*", myIP);
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/update", handleUpdate);
  server.on("/setUserRole", handleSetUserRole);
  server.begin();
  Serial.println("Web server started.");
}

//----------------------------------------------------------------------
// Main loop
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Process incoming sensor data via Serial.
  if (Serial.available()) {
    String dataLine = Serial.readStringUntil('\n');
    dataLine.trim();
    if (dataLine.length() > 0) {
      Serial.print("Received sensor data: ");
      Serial.println(dataLine);
      parseSerialData(dataLine);
    }
  }

  // Process Serial input from the IDE.
  processSerialInput();

  // Print UI state at least 3 times per second.
  if (millis() - lastPrintTime >= 333) {
    printUIState();
    lastPrintTime = millis();
  }

  // If no client update is received for over 2 seconds, force initial conditions.
  if (millis() - lastClientUpdateTime > 2000) {
    for (int i = 1; i <= 3; i++) {
      if (uiState[i].voltageSet != 0 || uiState[i].currentSet != 0 || uiState[i].sourceType != 0) {
        uiState[i].voltageSet = 0;
        uiState[i].currentSet = 0;
        uiState[i].sourceType = 0;  // Force Voltmeter mode
        Serial.print("No client update for >2 sec: Forcing Source ");
        Serial.print(i);
        Serial.println(" to initial state (0, Voltmeter).");
      }
    }
  }
}
