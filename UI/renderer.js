const { SerialPort } = require('serialport')
const usb = require('usb').usb;
const { ReadlineParser } = require('@serialport/parser-readline')
const { ByteLengthParser } = require('@serialport/parser-byte-length')
const { InterByteTimeoutParser } = require('@serialport/parser-inter-byte-timeout')
const {ipcRenderer} = require("electron")
var port = null;

const CONFIG_SIZE = 88; // Updated for WiFi and Bluetooth settings
const FW_VERSION = 2;
var onConnectIntervalConfig;
var onConnectIntervalWheels;
var isConnected = false;
var currentRPM = 0;
var rpmRequestPending = false;
var initComplete = false;

// Global variables for wireless connections
var wirelessDevices = {
  wifi: [],
  ble: []
};
var isWirelessConnected = false;
var wirelessConnectionType = null;

// Buffer for partial wireless data
var wirelessBuffer = "";
var wirelessDataExpectedLength = 0;
var wirelessDataCallback = null;

function refreshSerialPorts() {
    SerialPort.list().then((ports) => {
        console.log('Serial ports found: ', ports);

        if (ports.length === 0) { document.getElementById('serialDetectError').textContent = 'No ports discovered'; }
        else { document.getElementById('serialDetectError').textContent = ''; }

        select = document.getElementById('portsSelect');

        // Clear the current options
        while (select.options.length > 0) {
            select.remove(0);
        }

        // Load the current serial values
        for(var i = 0; i < ports.length; i++) {
            var newOption = document.createElement('option');
            newOption.value = ports[i].path;
            newOption.innerHTML = ports[i].path;
            if(ports[i].vendorId == "2341") {
              // Arduino Mega device
              if(ports[i].productId == "0010" || ports[i].productId == "0042") {
                // Mega2560
                newOption.innerHTML = newOption.innerHTML + " (Arduino Mega)";
              }
            }
            else if(ports[i].vendorId == "16c0") {
              // Teensy
              if(ports[i].productId == "0483") {
                // Teensy - Unfortunately all Teensy devices use the same device ID :(
                newOption.innerHTML = newOption.innerHTML + " (Teensy)";
              }
            }
            else if(ports[i].vendorId == "1a86") {
              // Arduino Nano device
              if(ports[i].productId == "7523") {
                // Nano
                newOption.innerHTML = newOption.innerHTML + " (Arduino Nano)";
              }
            }

            select.add(newOption);
            console.log("Vendor: " + ports[i].vendorId);
            console.log("Product: " +ports[i].productId);
        var button = document.getElementById("btnConnect")
        if(ports.length > 0) {
            select.selectedIndex = 0;
            button.disabled = false;
        }
        else { button.disabled = true; }
      }
    })
}

function openSerialPort() {
    // If we have a wireless connection active, disconnect first
    if (isWirelessConnected) {
        disconnectWireless();
        // The function will be called again after disconnection
        return;
    }

    var e = document.getElementById('portsSelect');
    if (!e.options[e.selectedIndex]) {
        window.alert("Please select a serial port first.");
        return;
    }

    console.log("Opening serial port: ", e.options[e.selectedIndex].value);
    port = new SerialPort({
        path: e.options[e.selectedIndex].value,
        baudRate: 115200
    }, function (err) {
        if (err) {
          window.alert(`Error while opening serial port: ${err.message}`);
          throw err;
        }

        //Drop the modal dialog until connection is complete
        modalLoading.init(true);
        initComplete = false;
        isConnected = true;
    });

    // Update the patterns downdown list
    port.on('open', onSerialConnect);
    
    port.on('close', function() {
        console.log("Serial port closed");
        isConnected = false;
        document.getElementById("link_live").removeAttribute("href");
        document.getElementById("link_config").removeAttribute("href");
    });

    // If there was an error, update connected state
    port.on('error', function(err) {
        console.error("Serial port error:", err);
        isConnected = false;
    });
}

function onSerialConnect() {
  console.log("Serial port opened");

  onConnectIntervalConfig = setInterval(requestConfig, 2000);

  // Activate the links
  document.getElementById("link_live").href = "#live";
  document.getElementById("link_config").href = "#config";
}

function uploadFW() {
    // Set the status and spinner
    var spinner = document.getElementById('progressSpinner');
    var burnPercentText = document.getElementById('burnPercent');
    burnPercentText.innerHTML = "Preparing to burn firmware...";

    // Remove any old icons
    spinner.classList.remove('fa-pause');
    spinner.classList.remove('fa-check');
    spinner.classList.remove('fa-times');
    spinner.classList.add('fa-spinner');

    // Retrieve the select serial port
    var e = document.getElementById('portsSelect');
    uploadPort = e.options[e.selectedIndex].value;
    console.log("Uploading to port: " + uploadPort);

    // Begin the upload
    ipcRenderer.send("uploadFW", {
      port: uploadPort,
    });

    ipcRenderer.on("upload completed", (event, code) => {
        burnPercentText.innerHTML = "Upload to arduino completed successfully!";
        spinner.classList.remove('fa-spinner');
        spinner.classList.add('fa-check');
    });

    ipcRenderer.on("upload percent", (event, percent) => {
        burnPercentText.innerHTML = "Uploading (" + percent + "% Complete)";
    });

    ipcRenderer.on("old bootloader", (event) => {
      burnPercentText.innerHTML = "No new Nano found. Retrying for old bootloader.";
    });

    ipcRenderer.on("upload error", (event, code) => {
        burnPercentText.innerHTML = "Upload to arduino failed";
        // Make the terminal/error section visible
        spinner.classList.remove('fa-spinner');
        spinner.classList.add('fa-times');
    });
}

function saveData(showCheck) {
  if (isWirelessConnected) {
    sendWirelessCommand('s');
  } else if (port && port.isOpen) {
    port.write("s");
  } else {
    console.error('No active connection to save data');
    return;
  }
  
  // Show checkmark animation if requested
  if (showCheck) {
    var checkmark = document.getElementById("saveCheck");
    checkmark.style.animation = 'none';
    checkmark.offsetHeight; /* trigger reflow */
    checkmark.style.opacity = 1;
    checkmark.style.visibility = "visible";
    checkmark.style.animation = null;
  }
}

function requestConfig() {
  // Clear interval regardless of connection type
  if (onConnectIntervalConfig) {
    clearInterval(onConnectIntervalConfig);
  }
  
  if (isWirelessConnected) {
    console.log("Requesting config via wireless");
    wirelessDataExpectedLength = CONFIG_SIZE;
    wirelessDataCallback = receiveConfig;
    
    // Send the command
    sendWirelessCommand('C');
  } else if (port && port.isOpen) {
    console.log("Requesting config via serial");
    parser = port.pipe(new InterByteTimeoutParser({ maxBufferSize: CONFIG_SIZE, interval: 1500 }));
    parser.on('data', receiveConfig);
    port.write("C");
  } else {
    console.error('No active connection to request config');
  }
}

function receiveConfig(data) {
  console.log("Received config: " + data);
  console.log("Mode: " + data[2]);

  if(data.length == 0) {
    console.log("TIMEOUT: No config data received");
    alert("Timeout connecting to arduino. Try uploading firmware again.");
    modalLoading.remove();
    return;
  }
  
  if(data.length != CONFIG_SIZE) {
    console.log("Incorrect amount of config data received. Expected: " + CONFIG_SIZE + ", Got: " + data.length);
    return;
  }

  document.getElementById("patternSelect").value = data[1];
  document.getElementById("rpmSelect").value = data[2];
  document.getElementById("fixedRPM").value = data.readUInt16LE(3);
  document.getElementById("rpmSweepMin").value = data.readUInt16LE(5);
  document.getElementById("rpmSweepMax").value = data.readUInt16LE(7);
  document.getElementById("rpmSweepSpeed").value = data.readUInt16LE(9);
  document.getElementById("compressionEnable").checked = data[11] ? true : false;
  document.getElementById("compressionMode").value = data[12];
  document.getElementById("compressionRPM").value = data.readUInt16LE(13);
  document.getElementById("compressionOffset").value = data.readUInt16LE(15);
  document.getElementById("compressionDynamic").checked = data[17] ? true : false;
  
  document.getElementById("wifiEnable").checked = data[18] ? true : false;
  
  // Parse strings manually to handle null terminators correctly
  let ssid = '';
  for (let i = 0; i < 32; i++) {
    if (data[19 + i] === 0) break;
    ssid += String.fromCharCode(data[19 + i]);
  }
  document.getElementById("wifiSSID").value = ssid;
  
  let password = '';
  for (let i = 0; i < 32; i++) {
    if (data[51 + i] === 0) break;
    password += String.fromCharCode(data[51 + i]);
  }
  document.getElementById("wifiPassword").value = password;
  
  document.getElementById("bluetoothEnable").checked = data[83] ? true : false;
  
  // Parse PIN as uint32
  const pinValue = data.readUInt32LE(84);
  document.getElementById("bluetoothPin").value = pinValue > 0 ? pinValue.toString() : '';

  if (port && port.pipe) {
    port.unpipe();
  }

  if(data[0] == FW_VERSION) {
    setRPMMode();
    requestPatternList();

    // Enable or disabled the compression settings
    var compressionState = document.getElementById('compressionEnable').checked;
    document.getElementById('compressionDynamic').disabled = !compressionState;
    document.getElementById('compressionMode').disabled = !compressionState;
    document.getElementById('compressionRPM').disabled = !compressionState;
    document.getElementById('compressionOffset').disabled = !compressionState;

    // Enable or disable WiFi settings based on checkbox state
    var wifiEnabled = document.getElementById('wifiEnable').checked;
    document.getElementById('wifiSSID').disabled = !wifiEnabled;
    document.getElementById('wifiPassword').disabled = !wifiEnabled;
  }
  else {
    console.log("Firmware version mismatch. Expected: ", FW_VERSION, " Received: ", data[0]);
    alert("Firmware version mismatch. Please press the 'Upload Firmware' button to update the firmware.");
    // Drop the modal loading window
    modalLoading.remove();
    window.location.hash = '#connect';
    initComplete = false;
  }
}

function sendConfig() {
  // Create configuration buffer
  var configBuffer = Buffer.alloc(CONFIG_SIZE);
  
  // Command byte is already handled separately for wireless
  configBuffer[0] = 0x63; // 'c' character command
  configBuffer[1] = parseInt(document.getElementById('patternSelect').value);
  configBuffer[2] = parseInt(document.getElementById('rpmSelect').value);
  
  // Fixed values with correct endianness
  configBuffer.writeUInt16LE(parseInt(document.getElementById('fixedRPM').value), 3);
  configBuffer.writeUInt16LE(parseInt(document.getElementById('rpmSweepMin').value), 5);
  configBuffer.writeUInt16LE(parseInt(document.getElementById('rpmSweepMax').value), 7);
  configBuffer.writeUInt16LE(parseInt(document.getElementById('rpmSweepSpeed').value), 9);
  
  // Boolean and enum values
  configBuffer[11] = document.getElementById('compressionEnable').checked ? 1 : 0;
  configBuffer[12] = parseInt(document.getElementById('compressionMode').value);
  
  // More fixed values
  configBuffer.writeUInt16LE(parseInt(document.getElementById('compressionRPM').value), 13);
  configBuffer.writeUInt16LE(parseInt(document.getElementById('compressionOffset').value), 15);
  configBuffer[17] = document.getElementById('compressionDynamic').checked ? 1 : 0;
  
  // Wireless settings
  configBuffer[18] = document.getElementById('wifiEnable').checked ? 1 : 0;
  
  // For strings, write them explicitly character by character
  const ssid = document.getElementById('wifiSSID').value;
  for (let i = 0; i < 32; i++) {
    configBuffer[19 + i] = i < ssid.length ? ssid.charCodeAt(i) : 0;
  }
  
  const password = document.getElementById('wifiPassword').value;
  for (let i = 0; i < 32; i++) {
    configBuffer[51 + i] = i < password.length ? password.charCodeAt(i) : 0;
  }
  
  configBuffer[83] = document.getElementById('bluetoothEnable').checked ? 1 : 0;
  
  // Handle PIN as a numeric value
  const pinValue = parseInt(document.getElementById('bluetoothPin').value) || 0;
  configBuffer.writeUInt32LE(pinValue, 84);
  
  console.log("Sending config");
  
  // If wireless connection is active, use that instead of serial
  if (isWirelessConnected) {
    sendWirelessCommand('c', configBuffer.slice(1)); // Skip the command byte as it's sent separately
  } else if (port && port.isOpen) {
    port.write(configBuffer);
  } else {
    console.error('No active connection to send config');
  }
}

function requestPatternList() {
  // Clear interval regardless of connection type
  if (onConnectIntervalWheels) {
    clearInterval(onConnectIntervalWheels);
  }
  
  // Clear the existing list
  var select = document.getElementById('patternSelect');
  while(select.options.length > 0) {
    select.remove(0);
  }
  patternOptionCounter = 0;
  numPatterns = 0;
  
  if (isWirelessConnected) {
    console.log("Requesting pattern list via wireless");
    // Reset patternRow for proper pattern parsing
    patternRow = 0;
    
    // Request the number of wheels
    sendWirelessCommand('n');
    
    // Request the pattern list
    sendWirelessCommand('L');
  } else if (port && port.isOpen) {
    console.log("Requesting pattern list via serial");
    const parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' }));
    port.write("n");
    port.write("L");
    parser.on('data', refreshPatternList);
  } else {
    console.error('No active connection to request pattern list');
  }
}

function requestWirelessSupport() {
  if (isWirelessConnected) {
    sendWirelessCommand('w');
  } else if (port && port.isOpen) {
    port.write("w");
  }
}

var patternOptionCounter = 0;
var numPatterns = 0;
function refreshPatternList(data) {
  // If this is the first line received, the number is the total number of wheels avaialable
  if(numPatterns == 0) {
    numPatterns = parseInt(data);
    console.log(`Number of wheels: ${numPatterns}`);
    return;
  }

  console.log(`Adding option #${patternOptionCounter}:\t${data}`)
  var select = document.getElementById('patternSelect')
  var option = document.createElement("option");
  option.text = data;
  option.value = patternOptionCounter;

  // Add new item
  select.add(option);

  patternOptionCounter++;

  if(patternOptionCounter == numPatterns) {
    port.unpipe();

    // Request the currently selected pattern
    port.write("N"); // Send the command to issue the current pattern number
    const parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' })); // Attach the readline parser
    parser.on('data', refreshPatternNumber);
  }
}

function receiveWirelessSupport(data) {
  console.log("Received wireless support data: ", data);
  const support = data[0]; // Extract the support byte
  
  port.unpipe(); // Remove the parser to free the port

  console.log(`Received wireless support: ${support}`);

  // Enable/disable checkboxes based on support
  document.getElementById('wifiEnable').disabled = !(support & 1); // WiFi supported if bit 0 is 1
  document.getElementById('bluetoothEnable').disabled = !(support & 2); // Bluetooth supported if bit 1 is 1

  // Update field states based on checkbox states and support
  toggleWifi();
  toggleBluetooth();
}

function refreshPatternNumber(data) {
  var select = document.getElementById('patternSelect')
  var patternID = parseInt(data);
  port.unpipe();

  // Temporarily disable the onchange event while we set the initial value
  var changeFunction = select.onchange;
  select.onchange = null;
  select.value = patternID;
  select.onchange = changeFunction;

  console.log("Currently selected Pattern: " + patternID);
  updatePatternQueue();
}

var patternRow = 0;
var newPattern;
var patternDegrees;

var nextPatternID = null;
var currentPatternID = null;
function updatePatternQueue() {
  nextPatternID = document.getElementById('patternSelect').value;

  if (currentPatternID === null) {
    updatePattern();
  }
}

function updatePattern() {
  currentPatternID = nextPatternID;
  nextPatternID = null;
  
  if (isWirelessConnected) {
    console.log(`Sending 'S' command with pattern ${currentPatternID}`);
    
    // Send the pattern selection command
    sendWirelessCommand('S', String.fromCharCode(parseInt(currentPatternID)));
    
    // Save to EEPROM
    saveData(false);
    
    // Request the new pattern
    patternRow = 0; // Reset pattern row counter
    sendWirelessCommand('P');
  } else if (port && port.isOpen) {
    const parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' }));
    
    var buffer = Buffer.alloc(2);
    buffer[0] = 0x53; // Ascii 'S'
    buffer[1] = parseInt(currentPatternID);
    port.write(buffer);
    
    saveData(false);
    
    port.write("P");
    parser.on('data', refreshPattern);
  } else {
    console.error('No active connection to update pattern');
  }
}

function refreshPattern(data) {
  if(patternRow == 0) {
    // First line sent is the pattern itself
    console.log(`Received pattern: ${data}`);
    newPattern = data.split(",");
    patternRow++;
  }
  else {
    // 2nd line received is the number of degrees the pattern runs over (360 or 720 usually)
    console.log(`Pattern duration: ${data}`);
    patternDegrees = parseInt(data);
    redrawGears(newPattern, patternDegrees);

    patternRow = 0;
    port.unpipe();

    if(initComplete == false) {
      requestWirelessSupport(); // Request wireless support

      // Drop the modal loading window
      modalLoading.remove();
      // Move to the Live tab
      window.location.hash = '#live';
      initComplete = true;
    }

    if (nextPatternID !== null) {
      updatePattern();
    }
    else {
      currentPatternID = null;
    }
  }
}

function resetGears() {
  redrawGears(newPattern, patternDegrees);
}

function redrawGears(pattern, degrees)
{
  var teeth, depth, radius, width;
  //teeth =  toothPattern.length / 2;

  radius = 150;
  width = Number("100");

  var halfspeed = false;
  if(degrees == 720) { halfspeed = true; }
  //var halfspeed = false;

  var background = document.getElementById('canvas-background-colour');
  var style = document.getElementById('wheelDisplaySelect').value
  var crank = document.getElementById('crank');
  var cam = document.getElementById('cam');
  //style = 1;
  if(style == 0)
  {
    crank.width = 420;
    crank.height = 300;
    cam.width = 420;
    cam.height = 300;
    depth = 10;
    line = 1;
    background.style.backgroundColor = "#0071b8";
    draw_crank_gear(pattern, depth, radius, width, line, halfspeed);
    draw_cam_gear(pattern, depth, radius, width, line);
  }
  else
  {
    crank.width = 840;
    crank.height = 150;
    cam.width = 840;
    cam.height = 150;
    depth = 80;
    line = 3;
    background.style.backgroundColor = "#000000";
    draw_crank_scope(pattern, depth, radius, width, line, halfspeed);
    draw_cam_scope(pattern, depth, radius, width, line);
  }

  
}

/*
var timers = [];
function animateGauges() {
  document.gauges.forEach(function(gauge) {
      timers.push(setInterval(function() {
          gauge.value = Math.random() *
              (gauge.options.maxValue - gauge.options.minValue) / 4 +
              gauge.options.minValue / 4;
      }, gauge.animation.duration + 50));
  });
}
*/

var RPMInterval = 0;
function enableRPM()
{
  console.log("Enabling RPM reads");
  if(RPMInterval == 0)
  {
    RPMInterval = setInterval(updateRPM, 100);
    const parser = port.pipe(new Readline({ delimiter: '\r\n' }));
    parser.on('data', receiveRPM);
    rpmRequestPending = false;
  }
  
}

function setRPMMode() {
  var newMode = parseInt(document.getElementById('rpmSelect').value);

  // If the new mode is fixed RPM or linear sweep, then send the RPM set values for them
  if(newMode == 0) {
    // Update the text box enablement
    document.getElementById("rpmSweepMin").disabled = false;
    document.getElementById("rpmSweepMax").disabled = false;
    document.getElementById("rpmSweepSpeed").disabled = false;
    document.getElementById("fixedRPM").disabled = true;
  }
  else if (newMode == 1) {
    // Update the text box enablement
    document.getElementById("fixedRPM").disabled = false;
    document.getElementById("rpmSweepMin").disabled = true;
    document.getElementById("rpmSweepMax").disabled = true;
    document.getElementById("rpmSweepSpeed").disabled = true;
  }
  else if(newMode == 2) {
    // Pot mode

    // Update the text box enablement
    document.getElementById("rpmSweepMin").disabled = true;
    document.getElementById("rpmSweepMax").disabled = true;
    document.getElementById("rpmSweepSpeed").disabled = true;
    document.getElementById("fixedRPM").disabled = true;
  }

  if(initComplete) { sendConfig(); }
}

var RPMInterval = 0;
function enableRPM() {
  console.log("Enabling RPM reads");
  if(RPMInterval == 0) {
    RPMInterval = setInterval(updateRPM, 100);
    if (port && port.pipe) {
      const parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' }));
      parser.on('data', receiveRPM);
    }
    rpmRequestPending = false;
  }
}

function disableRPM() {
  console.log("Deactivating RPM reads");
  clearInterval(RPMInterval);
  RPMInterval = 0;
  if (port && port.pipe) {
    port.unpipe();
    port.read(); // Flush the port
  }
}

function receiveRPM(data) {
  currentRPM = parseInt(data);
  rpmRequestPending = false;
}

function toggleCompression() {
  var state = document.getElementById('compressionEnable').checked

  document.getElementById('compressionDynamic').disabled = !state
  document.getElementById('compressionMode').disabled = !state
  document.getElementById('compressionRPM').disabled = !state
  document.getElementById('compressionOffset').disabled = !state

  sendConfig();
}

function updateRPM() {
  if (rpmRequestPending) {
    return;
  }
  
  if (isWirelessConnected) {
    sendWirelessCommand('R');
    document.gauges[0].value = currentRPM;
    rpmRequestPending = true;
  } else if (port && port.isOpen) {
    port.write("R");
    document.gauges[0].value = currentRPM;
    rpmRequestPending = true;
  } else {
    console.error('No active connection to update RPM');
  }
}

async function checkForUpdates() {
    let current_version = await ipcRenderer.invoke("getAppVersion");
    document.getElementById('versionSpan').innerHTML = current_version;

    var url = "https://api.github.com/repos/speeduino/Ardu-Stim/releases/latest";

    fetch(url)
      .then(function (response) {
        if (response.ok) {
            return response.json();
        }
        return Promise.reject(response);
      })
      .then(function (json) {
        latest_version = json.tag_name.substring(0);

        var semver = require('semver');
        if(semver.gt(latest_version, current_version)) {
            // New version has been found
            document.getElementById('update_url').setAttribute("href", json.html_url);
            document.getElementById('update_text').style.display = "block";
        }
      })
      .catch(function (err) {
        console.log("Error checking for updates.", err);
      });
}

function liveShowHide(mutationsList, observer) {
  mutationsList.forEach(mutation => {
    if (mutation.attributeName === 'style') {
      if (mutation.target.style.display === 'none') {
        disableRPM();
      }
      else {
        enableRPM();
      }
    }
  })
}

function initCollapsibleSections() {
    const stimHeader = document.getElementById('stim-config-header');
    const stimContent = document.getElementById('stim-config-content');
    const wirelessHeader = document.getElementById('wireless-config-header');
    const wirelessContent = document.getElementById('wireless-config-content');
    
    if (!stimHeader || !stimContent || !wirelessHeader || !wirelessContent) {
        console.error('Collapsible elements not found in the DOM');
        return;
    }
    
    // Initialize: Stim config open by default, wireless closed
    stimHeader.classList.add('active');
    wirelessHeader.classList.remove('active');
    stimContent.style.display = 'block';
    wirelessContent.style.display = 'none';
    
    // Toggle Stim Configuration
    stimHeader.addEventListener('click', function() {
        // If already active, close it and open the other one
        if (stimHeader.classList.contains('active')) {
            closeSection(stimHeader, stimContent);
            openSection(wirelessHeader, wirelessContent);
        } else {
            // If not active, open this one and close the other
            openSection(stimHeader, stimContent);
            closeSection(wirelessHeader, wirelessContent);
        }
    });
    
    // Toggle Wireless Configuration
    wirelessHeader.addEventListener('click', function() {
        // If already active, close it and open the other one
        if (wirelessHeader.classList.contains('active')) {
            closeSection(wirelessHeader, wirelessContent);
            openSection(stimHeader, stimContent);
        } else {
            // If not active, open this one and close the other
            openSection(wirelessHeader, wirelessContent);
            closeSection(stimHeader, stimContent);
        }
    });

    // Update canvas layout when display style changes
    document.getElementById('wheelDisplaySelect').addEventListener('change', function() {
        updateCanvasLayout();
    });

    // Initial layout setup with a delay to ensure DOM is ready
    setTimeout(function() {
        updateCanvasLayout();
        if (typeof resetGears === 'function') {
            resetGears();
        }
    }, 200);
}

function openSection(header, content) {
    header.classList.add('active');
    content.style.display = 'block';
    header.querySelector('.toggle-icon').textContent = '▼';
    
    // Fix for canvas sizing issues - redraw gears after opening Stim section
    if (header.id === 'stim-config-header') {
        // Use setTimeout to allow the DOM to update first
        setTimeout(function() {
            updateCanvasLayout();
            if (typeof resetGears === 'function') {
                resetGears();
            }
        }, 100);
    }
}

function closeSection(header, content) {
    header.classList.remove('active');
    content.style.display = 'none';
    header.querySelector('.toggle-icon').textContent = '▲';
}

function updateCanvasLayout() {
    // Update the canvas container class based on display style
    const displayStyle = document.getElementById('wheelDisplaySelect').value;
    const screenContainer = document.getElementById('screen');
    
    if (screenContainer) {
        // Remove existing mode classes
        screenContainer.classList.remove('wheel-mode', 'scope-mode');
        
        // Add appropriate mode class
        if (displayStyle === '0') {
            screenContainer.classList.add('wheel-mode');
        } else {
            screenContainer.classList.add('scope-mode');
        }
    }
}

// Validate the Bluetooth PIN (allow only digits)
function validateBluetoothPin() {
    const pinInput = document.getElementById('bluetoothPin');
    // Remove any non-numeric characters
    pinInput.value = pinInput.value.replace(/\D/g, '');
    
    // Enforce maximum length of 6 digits
    if (pinInput.value.length > 6) {
        pinInput.value = pinInput.value.substring(0, 6);
    }

    // Ensure it doesn't exceed the maximum uint32_t value
    const maxValue = 4294967295; // Max value for uint32_t
    if (parseInt(pinInput.value) > maxValue) {
        pinInput.value = maxValue.toString();
    }
    
    // If valid, send config update
    sendConfig();
}

// Save wireless configuration
function saveWirelessConfig() {
    // First validate all fields, passing the button ID to enable strict validation
    if (validateWirelessConfig('btnSaveWireless')) {
        // Send the save command to perform EEPROM burn
        saveData(false);
        
        // Show the checkmark animation for the wireless section
        var checkmark = document.getElementById("saveWirelessCheck");
        checkmark.style.animation = 'none';
        checkmark.offsetHeight; /* trigger reflow */
        checkmark.style.visibility = "visible";
        checkmark.style.opacity = 1;
        
        // Restart the animation
        setTimeout(function() {
            checkmark.style.animation = null;
        }, 10);
    }
}

// Validate wireless configuration
function validateWirelessConfig(callerID) {
    // Get the ID of the element that triggered the validation
    // This helps us determine if we're toggling or explicitly saving
    // If WiFi is enabled, validate SSID and password
    if (document.getElementById('wifiEnable').checked) {
        const ssid = document.getElementById('wifiSSID').value;
        const password = document.getElementById('wifiPassword').value;
        
        // Only check for empty SSID when explicitly saving (not during toggle)
        // This allows switching between Bluetooth and WiFi without alerts
        if (ssid.trim() === '' && this.id === 'btnSaveWireless') {
            alert('WiFi SSID cannot be empty');
            return false;
        }
        
        // Enforce max length (also enforced by maxlength attribute)
        if (ssid.length > 32) {
            alert('WiFi SSID must be 32 characters or less');
            document.getElementById('wifiSSID').value = ssid.substring(0, 32);
            return false;
        }
        
        if (password.length > 32) {
            alert('WiFi password must be 32 characters or less');
            document.getElementById('wifiPassword').value = password.substring(0, 32);
            return false;
        }
    }
    
    // If Bluetooth is enabled, validate PIN
    if (document.getElementById('bluetoothEnable').checked) {
        const pin = document.getElementById('bluetoothPin').value;
        
        // Only allow digits and enforce max length
        if (!/^\d+$/.test(pin) && pin.trim() !== '') {
            alert('Bluetooth PIN must contain only digits');
            // The validateBluetoothPin function will already clean this up
            validateBluetoothPin();
            return false;
        }
        
        if (pin.length > 6) {
            alert('Bluetooth PIN must be 6 digits or less');
            document.getElementById('bluetoothPin').value = pin.substring(0, 6);
            return false;
        }
    }
    
    return true;
}

function toggleWifi() {
  var wifiCheckbox = document.getElementById('wifiEnable');
  var wifiEnabled = wifiCheckbox.checked && !wifiCheckbox.disabled;
  document.getElementById('wifiSSID').disabled = !wifiEnabled;
  document.getElementById('wifiPassword').disabled = !wifiEnabled;

  // If WiFi is being enabled, disable Bluetooth
  if (wifiEnabled) {
    document.getElementById('bluetoothEnable').checked = false;
    document.getElementById('bluetoothPin').disabled = true;
  }

  if (validateWirelessConfig('wifiEnable')) {
    sendConfig();
  }
}

function toggleBluetooth() {
  var bluetoothCheckbox = document.getElementById('bluetoothEnable');
  var bluetoothEnabled = bluetoothCheckbox.checked && !bluetoothCheckbox.disabled;
  document.getElementById('bluetoothPin').disabled = !bluetoothEnabled;

  // If Bluetooth is being enabled, disable WiFi
  if (bluetoothEnabled) {
    document.getElementById('wifiEnable').checked = false;
    document.getElementById('wifiSSID').disabled = true;
    document.getElementById('wifiPassword').disabled = true;
  }

  if (validateWirelessConfig('bluetoothEnable')) {
    sendConfig();
  }
}

// Wireless functionality

// Toggle between serial and wireless UI
function toggleConnectionUI() {
  const serialContainer = document.getElementById('serialUIContainer');
  const wirelessContainer = document.getElementById('wirelessUIContainer');
  const toggleButton = document.getElementById('btnSwitchToWireless');
  
  console.log("Toggle button clicked");
  
  // Ensure elements exist
  if (!serialContainer || !wirelessContainer) {
    console.error("UI containers not found! Check HTML structure.");
    alert("Error: UI elements not found. Please check the console for details.");
    return;
  }
  
  // Check if we need to disconnect from current connection first
  if (isWirelessConnected) {
    console.log("Disconnecting from wireless before switching");
    disconnectWireless();
    return; // Will continue after disconnection
  } else if (isConnected) {
    console.log("Disconnecting from serial before switching");
    disconnectSerial();
    return; // Will continue after disconnection
  }
  
  // Check which UI is currently visible
  const isWirelessVisible = window.getComputedStyle(wirelessContainer).display !== 'none';
  
  if (isWirelessVisible) {
    // Switch to Serial UI
    console.log("Switching to Serial UI");
    serialContainer.style.display = 'block';
    wirelessContainer.style.display = 'none';
    toggleButton.value = "Switch to Wireless";
    refreshSerialPorts();
  } else {
    // Switch to Wireless UI
    console.log("Switching to Wireless UI");
    serialContainer.style.display = 'none';
    wirelessContainer.style.display = 'block';
    toggleButton.value = "Switch to Serial";
    refreshWirelessDevices();
  }
}

// Refresh the list of wireless devices
function refreshWirelessDevices() {
	// Initialize if undefined
  if (!wirelessDevices) {
    wirelessDevices = { wifi: [], ble: [] };
  }
  const spinner = document.getElementById('wirelessProgressSpinner');
  const statusText = document.getElementById('wirelessStatus');
  const errorDiv = document.getElementById('wirelessError');
  
  // Get selected wireless type (wifi or ble)
  const radioButton = document.querySelector('input[name="wirelessType"]:checked');
  if (!radioButton) {
    console.error("No wireless type selected");
    return;
  }
  const selectedType = radioButton.value;
  
  // Clear error message
  if (errorDiv) {
    errorDiv.style.display = 'none';
  }
  
  // Show spinner
  if (spinner) {
    spinner.classList.remove('fa-check', 'fa-times');
    spinner.classList.add('fa-spinner');
  }
  
  if (statusText) {
    statusText.textContent = `Scanning for ${selectedType} devices...`;
  }
  
  // Clear existing devices of the selected type
  wirelessDevices[selectedType] = [];
  updateWirelessDevicesList();
  
  // Request discovery from main process
  ipcRenderer.send('start-wireless-discovery', selectedType);
  
  // Set a timeout to hide spinner after 10 seconds if no devices found
  setTimeout(() => {
    if (wirelessDevices[selectedType].length === 0 && spinner && statusText) {
      spinner.classList.remove('fa-spinner');
      statusText.textContent = `No ${selectedType} devices found`;
    }
  }, 10000);
}

// Connect to selected wireless device
function connectWirelessDevice() {
  const select = document.getElementById('wirelessSelect');
  const spinner = document.getElementById('wirelessProgressSpinner');
  const statusText = document.getElementById('wirelessStatus');
  const errorDiv = document.getElementById('wirelessError');
  
  // Clear error message
  if (errorDiv) {
    errorDiv.style.display = 'none';
  }
  
  if (!select || select.selectedIndex === -1) {
    if (errorDiv) {
      errorDiv.textContent = 'Please select a device';
      errorDiv.style.display = 'block';
    }
    return;
  }
  
  const deviceId = select.options[select.selectedIndex].value;
  const radioButton = document.querySelector('input[name="wirelessType"]:checked');
  if (!radioButton) {
    console.error("No wireless type selected");
    return;
  }
  const selectedType = radioButton.value;
  
  // Show spinner
  if (spinner) {
    spinner.classList.remove('fa-check', 'fa-times');
    spinner.classList.add('fa-spinner');
  }
  
  if (statusText) {
    statusText.textContent = 'Connecting...';
  }
  
  if (selectedType === 'wifi') {
    const device = wirelessDevices.wifi.find(d => d.address === deviceId);
    if (device) {
      ipcRenderer.send('connect-wifi', device);
    } else if (errorDiv && statusText) {
      errorDiv.textContent = 'Selected device not found';
      errorDiv.style.display = 'block';
      spinner.classList.remove('fa-spinner');
      statusText.textContent = 'Connection failed';
    }
  } else if (selectedType === 'ble') {
    ipcRenderer.send('connect-ble', deviceId);
  }
}

// Update the wireless devices dropdown
function updateWirelessDevicesList() {
  const select = document.getElementById('wirelessSelect');
  const spinner = document.getElementById('wirelessProgressSpinner');
  const statusText = document.getElementById('wirelessStatus');
  
  if (!select) {
    console.error("Wireless device select element not found");
    return;
  }
  
  // Get the selected wireless type
  const radioButton = document.querySelector('input[name="wirelessType"]:checked');
  if (!radioButton) {
    console.error("No wireless type selected");
    return;
  }
  const selectedType = radioButton.value;
  
  // Clear existing options
  while (select.options.length > 0) {
    select.remove(0);
  }
  
  // Add devices based on selected type
  if (selectedType === 'wifi') {
    wirelessDevices.wifi.forEach(device => {
      const option = document.createElement('option');
      option.value = device.address;
      option.textContent = `${device.name} (${device.address})`;
      select.add(option);
    });
  } else if (selectedType === 'ble') {
    wirelessDevices.ble.forEach(device => {
      const option = document.createElement('option');
      option.value = device.id;
      option.textContent = `${device.name} (Signal: ${device.rssi} dBm)`;
      select.add(option);
    });
  }
  
  // Update UI
  if (select.options.length > 0) {
    if (spinner) {
      spinner.classList.remove('fa-spinner');
    }
    if (statusText) {
      statusText.textContent = `Found ${select.options.length} ${selectedType} devices`;
    }
    
    const connectButton = document.getElementById('btnWirelessConnect');
    if (connectButton) {
      connectButton.disabled = false;
    }
  } else {
    // If scanning is done but no devices found
    if (statusText && !statusText.textContent.includes('Scanning')) {
      if (spinner) {
        spinner.classList.remove('fa-spinner');
      }
      statusText.textContent = `No ${selectedType} devices found`;
    }
    
    const connectButton = document.getElementById('btnWirelessConnect');
    if (connectButton) {
      connectButton.disabled = true;
    }
  }
}

// Setup wireless type listeners (WiFi/BLE radio buttons)
function setupWirelessTypeListeners() {
  const radioButtons = document.querySelectorAll('input[name="wirelessType"]');
  if (radioButtons.length === 0) {
    console.error("Wireless type radio buttons not found!");
    return;
  }
  
  radioButtons.forEach(radio => {
    radio.addEventListener('change', () => {
      // Clear the device list and update for the selected type
      updateWirelessDevicesList();
      refreshWirelessDevices();
    });
  });
}

// Disconnect from wireless connection
function disconnectWireless() {
  if (!isWirelessConnected) return;
  
  const spinner = document.getElementById('wirelessProgressSpinner');
  const statusText = document.getElementById('wirelessStatus');
  
  // Show disconnecting status
  if (spinner) {
    spinner.classList.remove('fa-check', 'fa-times');
    spinner.classList.add('fa-spinner');
  }
  
  if (statusText) {
    statusText.textContent = 'Disconnecting...';
  }
  
  // Send disconnect message to main process
  ipcRenderer.send('disconnect-wireless');
  
  // UI will be updated in the wireless-disconnected event handler
}

// Disconnect from serial connection
function disconnectSerial() {
  if (!isConnected || !port || !port.isOpen) return;
  
  console.log("Closing serial port connection");
  
  // Disable the RPM updates if active
  if (RPMInterval !== 0) {
    disableRPM();
  }
  
  // Close the port
  port.close(err => {
    if (err) {
      console.error('Error closing serial port:', err);
    }
    
    // Update connection state
    isConnected = false;
    
    // Disable UI navigation
    document.getElementById("link_live").removeAttribute("href");
    document.getElementById("link_config").removeAttribute("href");
    
    // Toggle to wireless UI
    const serialContainer = document.getElementById('serialUIContainer');
    const wirelessContainer = document.getElementById('wirelessUIContainer');
    const toggleButton = document.getElementById('btnSwitchToWireless');
    
    if (serialContainer && wirelessContainer && toggleButton) {
      serialContainer.style.display = 'none';
      wirelessContainer.style.display = 'block';
      toggleButton.value = "Switch to Serial";
      refreshWirelessDevices();
    }
  });
}

// Send a command over wireless connection
function sendWirelessCommand(command, additionalData = null) {
  if (!isWirelessConnected) {
    console.error('Not connected to any wireless device');
    return;
  }
  
  let dataToSend;
  
  if (additionalData) {
    if (typeof additionalData === 'string') {
      dataToSend = command + additionalData;
    } else if (Buffer.isBuffer(additionalData)) {
      const buffer = Buffer.alloc(additionalData.length + 1);
      buffer[0] = command.charCodeAt(0);
      additionalData.copy(buffer, 1);
      dataToSend = buffer;
    }
  } else {
    dataToSend = command;
  }
  
  ipcRenderer.send('send-wireless-data', dataToSend);
}

// Process data received from wireless connection
function processWirelessData(data) {
  // Append to buffer
  wirelessBuffer += data;
  
  // If we're expecting a specific data length (e.g., for config)
  if (wirelessDataExpectedLength > 0) {
    if (wirelessBuffer.length >= wirelessDataExpectedLength) {
      try {
        // Convert string to buffer for processing
        const buffer = Buffer.from(wirelessBuffer.substring(0, wirelessDataExpectedLength), 'binary');
        
        // Call the callback - important to use a temporary reference since
        // the callback might set up a new callback
        const tempCallback = wirelessDataCallback;
        
        // Reset parser state
        const tempLength = wirelessDataExpectedLength;
        wirelessDataExpectedLength = 0;
        wirelessDataCallback = null;
        wirelessBuffer = wirelessBuffer.substring(tempLength);
        
        // Call the callback
        if (tempCallback) {
          tempCallback(buffer);
        }
      } catch (error) {
        console.error('Error processing wireless data:', error);
        // Reset parser state on error
        wirelessDataExpectedLength = 0;
        wirelessDataCallback = null;
        wirelessBuffer = "";
      }
    }
  } else {
    // Process line-based data
    const lines = wirelessBuffer.split(/[\r\n]+/);
    
    // If the last line is incomplete, keep it in the buffer
    if (lines.length > 0 && !wirelessBuffer.endsWith('\n') && !wirelessBuffer.endsWith('\r')) {
      wirelessBuffer = lines.pop();
    } else {
      wirelessBuffer = "";
    }
    
    // Process each complete line
    lines.forEach(line => {
      if (line.trim().length > 0) {
        processWirelessLine(line);
      }
    });
  }
}

// Process incoming data line from wireless connection
function processWirelessLine(line) {
  console.log('Wireless data line:', line);
  
  // Process wheel pattern list
  if (numPatterns === 0 && !isNaN(parseInt(line))) {
    numPatterns = parseInt(line);
    console.log(`Number of wheel patterns: ${numPatterns}`);
    return;
  }
  
  if (numPatterns > 0 && patternOptionCounter < numPatterns) {
    console.log(`Adding wheel pattern option #${patternOptionCounter}: ${line}`);
    var select = document.getElementById('patternSelect');
    var option = document.createElement("option");
    option.text = line;
    option.value = patternOptionCounter;
    select.add(option);
    
    patternOptionCounter++;
    
    if (patternOptionCounter >= numPatterns) {
      console.log("All wheel patterns received");
      // Request the currently selected pattern
      sendWirelessCommand('N');
    }
    return;
  }
  
  // Process pattern number
  if (numPatterns > 0 && patternOptionCounter >= numPatterns) {
    var patternID = parseInt(line);
    var select = document.getElementById('patternSelect');
    
    // Temporarily disable the onchange event while we set the initial value
    var changeFunction = select.onchange;
    select.onchange = null;
    select.value = patternID;
    select.onchange = changeFunction;
    
    console.log("Currently selected Pattern: " + patternID);
    updatePatternQueue();
    
    // Reset counters
    patternOptionCounter = 0;
    numPatterns = 0;
    return;
  }
  
  // Process RPM updates
  if (rpmRequestPending && !isNaN(parseInt(line))) {
    currentRPM = parseInt(line);
    rpmRequestPending = false;
    return;
  }
  
  // Process wheel pattern data (for visualization)
  if (patternRow === 0 && line.includes(',')) {
    console.log(`Received pattern: ${line}`);
    newPattern = line.split(",");
    patternRow++;
    return;
  }
  
  if (patternRow === 1 && !isNaN(parseInt(line))) {
    console.log(`Pattern duration: ${line}`);
    patternDegrees = parseInt(line);
    redrawGears(newPattern, patternDegrees);
    
    patternRow = 0;
    
    if (initComplete === false) {
      // If this is initial setup
      requestWirelessSupport();
      
      // Drop the modal loading window
      if (typeof modalLoading !== 'undefined') {
        modalLoading.remove();
      }
      
      // Move to Live tab
      window.location.hash = '#live';
      initComplete = true;
    }
    
    if (nextPatternID !== null) {
      updatePattern();
    } else {
      currentPatternID = null;
    }
    return;
  }
}

// Setup IPC event listeners for wireless
function setupWirelessIPCListeners() {
  // Update wireless device lists
  ipcRenderer.on('update-wireless-devices', (event, devices) => {
    wirelessDevices = devices;
    updateWirelessDevicesList();
  });
  
  // Handle wireless connection status
  ipcRenderer.on('wireless-connected', (event, result) => {
    const spinner = document.getElementById('wirelessProgressSpinner');
    const statusText = document.getElementById('wirelessStatus');
    const errorDiv = document.getElementById('wirelessError');
    
    if (result.success) {
      if (spinner) {
        spinner.classList.remove('fa-spinner');
        spinner.classList.add('fa-check');
      }
      
      if (statusText) {
        statusText.textContent = `Connected to ${result.details.name}`;
      }
      
      isWirelessConnected = true;
      wirelessConnectionType = result.type;
      
      // Enable UI navigation
      document.getElementById("link_live").href = "#live";
      document.getElementById("link_config").href = "#config";
      
      // Drop the modal loading window
      if (typeof modalLoading !== 'undefined') {
        modalLoading.init(true);
      }
      
      // Request the configuration after connection
      wirelessDataExpectedLength = CONFIG_SIZE;
      wirelessDataCallback = receiveConfig;
      sendWirelessCommand('C');
    } else {
      if (spinner) {
        spinner.classList.remove('fa-spinner');
        spinner.classList.add('fa-times');
      }
      
      if (statusText) {
        statusText.textContent = `Connection failed`;
      }
      
      if (errorDiv) {
        errorDiv.textContent = result.error;
        errorDiv.style.display = 'block';
      }
      
      isWirelessConnected = false;
    }
  });
  
  // Handle wireless disconnection
  ipcRenderer.on('wireless-disconnected', () => {
    const statusText = document.getElementById('wirelessStatus');
    const spinner = document.getElementById('wirelessProgressSpinner');
    
    if (spinner) {
      spinner.classList.remove('fa-spinner', 'fa-check');
    }
    
    if (statusText) {
      statusText.textContent = 'Disconnected';
    }
    
    isWirelessConnected = false;
    
    // Disable UI navigation
    document.getElementById("link_live").removeAttribute("href");
    document.getElementById("link_config").removeAttribute("href");
    
    // Check if we need to toggle the UI
    const serialContainer = document.getElementById('serialUIContainer');
    const wirelessContainer = document.getElementById('wirelessUIContainer');
    const toggleButton = document.getElementById('btnSwitchToWireless');
    
    if (serialContainer && wirelessContainer && toggleButton) {
      // If this was triggered by toggling to serial, switch UI
      if (window.getComputedStyle(wirelessContainer).display !== 'none') {
        serialContainer.style.display = 'block';
        wirelessContainer.style.display = 'none';
        toggleButton.value = "Switch to Wireless";
        refreshSerialPorts();
      }
    }
  });
  
  // Handle wireless data
  ipcRenderer.on('wireless-data', (event, data) => {
    processWirelessData(data);
  });
  
  // Handle wireless errors
  ipcRenderer.on('wireless-error', (event, error) => {
    console.error('Wireless error:', error);
    
    const errorDiv = document.getElementById('wirelessError');
    if (errorDiv) {
      errorDiv.textContent = error;
      errorDiv.style.display = 'block';
    }
    
    const spinner = document.getElementById('wirelessProgressSpinner');
    if (spinner) {
      spinner.classList.remove('fa-spinner');
      spinner.classList.add('fa-times');
    }
    
    const statusText = document.getElementById('wirelessStatus');
    if (statusText) {
      statusText.textContent = 'Error';
    }
  });
  
  // BLE state change notifications
  ipcRenderer.on('ble-state-change', (event, state) => {
    if (state !== 'poweredOn') {
      const errorDiv = document.getElementById('wirelessError');
      if (errorDiv) {
        errorDiv.textContent = `Bluetooth is ${state}. Please enable Bluetooth on your computer.`;
        errorDiv.style.display = 'block';
      }
    }
  });
}

window.onload = function ()
{
    refreshSerialPorts();
    redrawGears(toothPatterns[0]);
    window.location.hash = '#connect';
    checkForUpdates();

    // Enable and disabled retrieval of RPM when viewing live panel
    const liveShowHideObserver = new MutationObserver(liveShowHide);
    liveShowHideObserver.observe(
      document.getElementById('live'),
      { attributes: true }
    );

    // Initialize wireless UI components
    const wirelessContainer = document.getElementById('wirelessUIContainer');
    if (wirelessContainer) {
      // Initially ensure wireless UI is hidden
      wirelessContainer.style.display = 'none';
      
      // Setup wireless type listeners (WiFi/BLE radio buttons)
      setupWirelessTypeListeners();
      
      // Setup IPC listeners for wireless events
      setupWirelessIPCListeners();
      
      console.log("Wireless UI initialized");
    } else {
      console.error("Wireless UI container not found in DOM!");
    }

    // Attach event listeners for WiFi and Bluetooth checkboxes
    document.getElementById('wifiEnable').addEventListener('change', toggleWifi);
    document.getElementById('bluetoothEnable').addEventListener('change', toggleBluetooth);

    // Add validation for input fields
    document.getElementById('bluetoothPin').addEventListener('input', validateBluetoothPin);
    document.getElementById('wifiSSID').addEventListener('input', function() {
        if (this.value.length > 32) {
            this.value = this.value.substring(0, 32);
        }
    });
    document.getElementById('wifiPassword').addEventListener('input', function() {
        if (this.value.length > 32) {
            this.value = this.value.substring(0, 32);
        }
    });

    // Initially disable WiFi and Bluetooth config fields
    toggleWifi();
    toggleBluetooth();

    // Initialize collapsible sections
    initCollapsibleSections();

    usb.on('attach', refreshSerialPorts);
    usb.on('detach', refreshSerialPorts);
};