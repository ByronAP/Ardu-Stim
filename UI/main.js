const { app, BrowserWindow, ipcMain } = require('electron')
const {spawn} = require('child_process');
const {execFile} = require('child_process');
const mdns = require('mdns-js');
const net = require('net');

let noble;
try {
  noble = require('@abandonware/noble');
  console.log('Noble imported successfully');
} catch (error) {
  console.error('Error importing Noble:', error.message);
}

let bleDevicesMap = new Map(); // To store full peripheral objects

// Keep a global reference of the window object, if you don't, the window will
// be closed automatically when the JavaScript object is garbage collected.
let win

var avrdudeErr = "";
var avrdudeIsRunning = false;

// Global variables for wireless connections
let mdnsBrowser = null;
let bleDevices = [];
let mdnsDevices = [];
let currentWirelessConnection = null;
let wifiSocket = null;

function createWindow () {
  // Create the browser window.
  windowWidth = 1024;
  windowHeight = 700;
  if(process.platform == "win32") 
  {
    windowWidth = 1098;
    windowHeight = 820;
  }

  win = new BrowserWindow({
    width: windowWidth,
    height: windowHeight, 
    backgroundColor: '#312450', 
    webPreferences: {
      contextIsolation: false,
      nodeIntegration: true,
      backgroundThrottling: false,
    },
  });

  // auto hide menu bar (Win, Linux)
  win.setMenuBarVisibility(true); // false
  win.setAutoHideMenuBar(false); // true

  // remove completely when app is packaged (Win, Linux)
  if (app.isPackaged) {
    win.removeMenu();
  }

  // and load the index.html of the app.
  win.loadFile('index.html')

  // Open the DevTools.
  //win.webContents.openDevTools()

  // Emitted when the window is closed.
  win.on('closed', () => {
    // Dereference the window object, usually you would store windows
    // in an array if your app supports multi windows, this is the time
    // when you should delete the corresponding element.
    win = null
  })

  // Open links in external browser
  win.webContents.setWindowOpenHandler(({ url }) => {
    if (url.startsWith('https:')) {
      require('electron').shell.openExternal(url);
    }
    return { action: 'deny' };
  });
  
  // Initialize wireless discovery when UI is ready
  win.webContents.on('did-finish-load', () => {
    // Initialize wireless discovery
    initMdnsBrowser();
    initBLEScanner();
  });
}

// Initialize mDNS browser
function initMdnsBrowser() {
  try {
    mdnsBrowser = mdns.createBrowser(mdns.tcp('telnet'));
    
    mdnsBrowser.on('ready', function() {
      console.log('mDNS browser ready');
      mdnsBrowser.discover();
    });
    
    mdnsBrowser.on('update', function(data) {
      // Filter for ardustim devices
      if (data.host && data.host.includes('ardustim')) {
        console.log('mDNS device found:', data);
        
        const deviceInfo = {
          name: data.host,
          address: data.addresses[0],
          port: data.port || 23, // Default to telnet port
          type: 'wifi'
        };
        
        // Check if device is already in our list
        const existingIndex = mdnsDevices.findIndex(dev => dev.address === deviceInfo.address);
        if (existingIndex === -1) {
          mdnsDevices.push(deviceInfo);
        } else {
          mdnsDevices[existingIndex] = deviceInfo;
        }
        
        // Send updated device list to renderer
        if (win) {
          win.webContents.send('update-wireless-devices', { wifi: mdnsDevices, ble: bleDevices });
        }
      }
    });
  } catch (error) {
    console.error('Error initializing mDNS browser:', error);
    if (win) {
      win.webContents.send('wireless-error', 'Failed to initialize mDNS: ' + error.message);
    }
  }
}

// Initialize BLE scanner
function initBLEScanner() {
  try {
    if (!noble) {
      console.log('BLE not available - Noble module not loaded');
      return;
    }
    
    noble.on('stateChange', function(state) {
      console.log('BLE state:', state);
      if (state === 'poweredOn') {
        // Start scanning for BLE devices
        noble.startScanning(['6E400001-B5A3-F393-E0A9-E50E24DCCA9E'], false);
      } else {
        noble.stopScanning();
        if (win) {
          win.webContents.send('ble-state-change', state);
		  
        }
      }
    });
    
    noble.on('discover', function(peripheral) {
  console.log('BLE device found:', peripheral.advertisement.localName || peripheral.id);
  
  // If it has the Nordic UART Service or has ArduStim in the name
  const isArduStim = peripheral.advertisement.localName && 
                    peripheral.advertisement.localName.includes('ArduStim');
  const hasNordicService = peripheral.advertisement.serviceUuids && 
                          peripheral.advertisement.serviceUuids.some(uuid => 
                            uuid.toLowerCase().includes('6e400001'));
  
  if (isArduStim || hasNordicService) {
    // Create a serializable version without the peripheral object
    const deviceInfo = {
      id: peripheral.id,
      name: peripheral.advertisement.localName || 'ArduStim BLE',
      rssi: peripheral.rssi,
      type: 'ble'
      // Don't include peripheral object here
    };
    
    // Store the peripheral object in our map
    bleDevicesMap.set(peripheral.id, peripheral);
    
    // Update the devices list
    const existingIndex = bleDevices.findIndex(dev => dev.id === deviceInfo.id);
    if (existingIndex === -1) {
      bleDevices.push(deviceInfo);
    } else {
      bleDevices[existingIndex] = deviceInfo;
    }
    
    // Send only the serializable device info
    if (win) {
      try {
        win.webContents.send('update-wireless-devices', { 
          wifi: mdnsDevices, 
          ble: bleDevices
        });
      } catch (error) {
        console.error('Error sending from webFrameMain: ', error);
      }
    }
  }
});
  } catch (error) {
    console.error('Error initializing BLE scanner:', error);
    if (win) {
      win.webContents.send('wireless-error', 'Failed to initialize BLE: ' + error.message);
    }
  }
}

app.allowRendererProcessReuse = false;

// Register handler before app.on/createWindow as this is used during window creation
ipcMain.handle('getAppVersion', async (e) => {
  return app.getVersion();
});

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.on('ready', createWindow)

// Quit when all windows are closed.
app.on('window-all-closed', () => {
  // On macOS it is common for applications and their menu bar
  // to stay active until the user quits explicitly with Cmd + Q
  if (process.platform !== 'darwin') {
    app.quit()
  }
})

app.on('activate', () => {
  // On macOS it's common to re-create a window in the app when the
  // dock icon is clicked and there are no other windows open.
  if (win === null) {
    createWindow()
  }
})

// Handle IPC messages from renderer process
ipcMain.on('start-wireless-discovery', (event, type) => {
  if (type === 'wifi' || !type) {
    // Start or restart WiFi discovery
    if (mdnsBrowser) {
      mdnsBrowser.stop();
      mdnsBrowser.discover();
    } else {
      initMdnsBrowser();
    }
    event.sender.send('wifi-discovery-started');
  }
  
  if (type === 'ble' || !type) {
    // Start or restart BLE discovery
    if (noble) {
      if (noble.state === 'poweredOn') {
        noble.stopScanning();
        noble.startScanning(['6E400001-B5A3-F393-E0A9-E50E24DCCA9E'], false);
        event.sender.send('ble-discovery-started');
      } else {
        event.sender.send('ble-state-change', noble.state);
      }
    } else {
      initBLEScanner();
    }
  }
});

ipcMain.on('connect-wifi', (event, details) => {
  try {
    wifiSocket = new net.Socket();
    
    wifiSocket.connect(details.port, details.address, function() {
      console.log('Connected to WiFi device:', details.address);
      currentWirelessConnection = {
        type: 'wifi',
        details: details,
        socket: wifiSocket
      };
      event.sender.send('wireless-connected', { success: true, type: 'wifi', details: details });
    });
    
    wifiSocket.on('data', function(data) {
      // Forward data to renderer process
      event.sender.send('wireless-data', data.toString());
    });
    
    wifiSocket.on('close', function() {
      console.log('WiFi connection closed');
      currentWirelessConnection = null;
      event.sender.send('wireless-disconnected', 'wifi');
    });
    
    wifiSocket.on('error', function(error) {
      console.error('WiFi connection error:', error);
      event.sender.send('wireless-error', error.message);
    });
  } catch (error) {
    console.error('Error connecting to WiFi device:', error);
    event.sender.send('wireless-connected', { success: false, error: error.message });
  }
});

ipcMain.on('connect-ble', async (event, deviceId) => {
  try {
    // Get the full peripheral object from our map
    const peripheral = bleDevicesMap.get(deviceId);
    
    if (!peripheral) {
      throw new Error('Device not found');
    }
    
    // Rest of your connection code using this peripheral object
    await new Promise((resolve, reject) => {
      peripheral.connect(error => {
        if (error) {
          reject(error);
        } else {
          resolve();
        }
      });
    });
    
    // Continue with rest of connection code...
    
    // For any updates back to the renderer, send only serializable data
    event.sender.send('wireless-connected', { 
      success: true, 
      type: 'ble', 
      details: {
        id: peripheral.id,
        name: peripheral.advertisement.localName || 'ArduStim BLE',
        rssi: peripheral.rssi,
        type: 'ble'
      }
    });
    
  } catch (error) {
    console.error('Error connecting to BLE device:', error);
    event.sender.send('wireless-connected', { success: false, error: error.message });
  }
});

ipcMain.on('disconnect-wireless', event => {
  if (currentWirelessConnection) {
    if (currentWirelessConnection.type === 'wifi' && wifiSocket) {
      wifiSocket.destroy();
      wifiSocket = null;
    } else if (currentWirelessConnection.type === 'ble' && currentWirelessConnection.peripheral) {
      // Unsubscribe from notifications first
      if (currentWirelessConnection.txCharacteristic) {
        currentWirelessConnection.txCharacteristic.unsubscribe();
      }
      currentWirelessConnection.peripheral.disconnect();
    }
    
    const type = currentWirelessConnection.type;
    currentWirelessConnection = null;
    event.sender.send('wireless-disconnected', type);
  }
});

ipcMain.on('send-wireless-data', (event, data) => {
  if (!currentWirelessConnection) {
    event.sender.send('wireless-error', 'Not connected');
    return;
  }
  
  try {
    if (currentWirelessConnection.type === 'wifi' && wifiSocket) {
      wifiSocket.write(data);
    } else if (currentWirelessConnection.type === 'ble' && currentWirelessConnection.rxCharacteristic) {
      // BLE may need to chunk data if it's too large
      const chunkSize = 20; // BLE MTU is typically small
      if (typeof data === 'string') {
        data = Buffer.from(data);
      }
      
      for (let i = 0; i < data.length; i += chunkSize) {
        const chunk = data.slice(i, Math.min(i + chunkSize, data.length));
        currentWirelessConnection.rxCharacteristic.write(chunk, false);
      }
    }
  } catch (error) {
    console.error('Error sending wireless data:', error);
    event.sender.send('wireless-error', error.message);
  }
});

ipcMain.on('uploadFW', (e, args) => {

  if(avrdudeIsRunning == true) { return; }
  avrdudeIsRunning = true; //Indicate that an avrdude process has started
  var platform;

  var burnStarted = false;
  var burnPercent = 0;

  //All Windows builds use the 32-bit binary
  if(process.platform == "win32") 
  { 
    platform = "avrdude-windows"; 
  }
  //All Mac builds use the 64-bit binary
  else if(process.platform == "darwin") 
  { 
    platform = "avrdude-darwin-x86_64";
  }
  else if(process.platform == "linux") 
  { 
    if(process.arch == "x32") { platform = "avrdude-linux_i686"; }
    else if(process.arch == "x64") { platform = "avrdude-linux_x86_64"; }
    else if(process.arch == "arm") { platform = "avrdude-armhf"; }
    else if(process.arch == "arm64") { platform = "avrdude-aarch64"; }
  }

  var executableName = __dirname + "/bin/" + platform + "/avrdude";
  executableName = executableName.replace('app.asar',''); //This is important for allowing the binary to be found once the app is packaed into an asar
  var configName = executableName + ".conf";
  if(process.platform == "win32") { executableName = executableName + '.exe'; } //This must come after the configName line above

  //var firmwareFile = __dirname + "/firmwares/m328p.hex";
  var firmwareFile = __dirname + "/firmwares/nano.hex";
  firmwareFile = firmwareFile.replace('app.asar',''); //This is important for allowing the binary to be found once the app is packaed into an asar

  var hexFile = 'flash:w:' + firmwareFile + ':i';

  //var execArgs = ['-v', '-pm328p', '-C', configName, '-carduino', '-b 57600', '-P', args.port, '-D', '-U', hexFile];
  var execArgs = ['-v', '-patmega328p', '-C', configName, '-carduino', '-b 115200', '-D', '-P', args.port, '-D', '-U', hexFile];
  var execArgs_old = ['-v', '-patmega328p', '-C', configName, '-carduino', '-b 57600', '-D', '-P', args.port, '-D', '-U', hexFile];

  console.log(executableName);
  //const child = spawn(executableName, execArgs);
  const child = execFile(executableName, execArgs);
  var child_oldbootloader;

  function logAvrdudeStdout(data) { console.log(`avrdude stdout:\n${data}`); }
  child.stdout.on('data', (data) => { logAvrdudeStdout(data); });
  

  function logAvrdudeStderr(data) 
  { 
    console.log(`avrdude stderr: ${data}`); 

    //Check if avrdude has started the actual burn yet, and if so, track the '#' characters that it prints. Each '#' represents 1% of the total burn process (50 for write and 50 for read)
    if (burnStarted == true)
    {
      if(data=="#") { burnPercent += 1; }
      e.sender.send( "upload percent", burnPercent );
    }
    else
    {
      //This is a hack, but basically watch the output from avrdude for the term 'Writing | ', everything after that is the #s indicating 1% of burn. 
      if(data.substr(data.length - 10) == "Writing | ")
      {
        burnStarted = true;
      }
    }
  }

  function onChildExit(code, attemptNum)
  {
    avrdudeIsRunning = false;
    if (code !== 0) 
    {
      if(attemptNum !== 0)
      {
        console.log(`avrdude process exited with code ${code}`);
        e.sender.send( "upload error", avrdudeErr );
        avrdudeErr = "";
      }
      else
      {
        console.log(`1st attempt with avrdude process exited with code ${code}. Will retry for old bootloader`);
      }
    }
    else
    {
      e.sender.send( "upload completed", code );
    }
  }

  child.stderr.on('data', (data) => 
  {
    avrdudeErr = avrdudeErr + data;

    //This is a hack to try and detect old nano bootloader vs new. There does not seem to be a 'nice' way to do this that I can find. 
    if(data.includes("resp=0x00") || data.includes("resp=0x01"))
    {
      avrdudeRetry = true;
      child.kill();
      console.log("Burn failed. Retrying for old bootloader.");
      child_oldbootloader = execFile(executableName, execArgs_old);
      e.sender.send( "old bootloader")
      child_oldbootloader.stdout.on('data', (data) => { logAvrdudeStdout(data); });
      child_oldbootloader.stderr.on('data', (data) => { logAvrdudeStderr(data); });
      child_oldbootloader.on('close', (code) => { onChildExit(code, 1); });
      //avrdudeRetry = false;
    }
    logAvrdudeStderr(data);
  });

  child.on('error', (err) => {
    console.log('Failed to start subprocess.');
    console.log(err);
    avrdudeIsRunning = false;
  });

  child.on('close', (code) => { onChildExit(code, 0); });
});