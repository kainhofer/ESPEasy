#ifdef USES_P197
//#######################################################################################################
//############################## Plugin 197: RFLink MQTT Bridge #########################################
//#######################################################################################################

// This plugin's structure is based on the ser2net plugin (P020), the MQTT handling
// is based on the MQTT import plugin (P037) and the RFLink message parsing
// is based on Phil Wilson's rflink-to-mqtt bridge (https://github.com/Phileep/rflink-to-mqtt.git)

#define PLUGIN_197
#define PLUGIN_ID_197         197
#define PLUGIN_NAME_197       "RFLink MQTT bridge"
#define PLUGIN_VALUENAME1_197 "rflinkMQTT"

#define PLUGIN_RECV_MQTT 197		// This is a 'private' function used only by this bridge module
#define P197_BUFFER_SIZE 128


// Arduino JSON library
// Copyright Benoit Blanchon 2014-2016
// MIT License
// https://github.com/bblanchon/ArduinoJson
#include <ArduinoJson.h>


WiFiClient espclient_197;
PubSubClient *MQTTclient_197 = NULL;		// Create a new pubsub instance


typedef struct RFLink_MQTT_config{
  char subscribe[128];
  char publish[128];
  bool sendRaw;
  bool sendJSON;
  bool includeSwitch;
} RFLink_MQTT_config;

boolean Plugin_197(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;
  String tmpClientName = F("%sysname%-RFLink-MQTT");
  String ClientName = parseTemplate(tmpClientName, 35);

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_197;
        Device[deviceCount].Type = DEVICE_TYPE_DUMMY;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;     // This means it has a single pin
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = false;
        Device[deviceCount].ValueCount = 0;
        Device[deviceCount].SendDataOption = false;
        Device[deviceCount].TimerOption = false;
        // Device[deviceCount].Custom = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_197);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        RFLink_MQTT_config cfg;
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&cfg, sizeof(cfg));

        addFormHeader(F("Serial Settings"));
      	addFormNumericBox(F("Baud Rate"), F("plugin_197_baud"), ExtraTaskSettings.TaskDevicePluginConfigLong[1]);
      	addFormNumericBox(F("Data bits"), F("plugin_197_data"), ExtraTaskSettings.TaskDevicePluginConfigLong[2]);

        byte choice = ExtraTaskSettings.TaskDevicePluginConfigLong[3];
        String options[3];
        options[0] = F("No parity");
        options[1] = F("Even");
        options[2] = F("Odd");
        int optionValues[3];
        optionValues[0] = 0;
        optionValues[1] = 2;
        optionValues[2] = 3;
        addFormSelector(F("Parity"), F("plugin_197_parity"), 3, options, optionValues, choice);

      	addFormNumericBox(F("Stop bits"), F("plugin_197_stop"), ExtraTaskSettings.TaskDevicePluginConfigLong[4]);


        addFormHeader(F("MQTT Settings"));
        addFormTextBox(F("Publish topic"), F("plugin_197_publish"), cfg.publish, sizeof(cfg.publish) - 1);
        addFormTextBox(F("Subscribe topic"), F("plugin_197_subscribe"), cfg.subscribe, sizeof(cfg.subscribe) - 1);
        addFormSeparator(2);

        addFormCheckBox(F("Send raw message"), F("plugin_197_sendraw"), cfg.sendRaw);
        addFormCheckBox(F("Send JSON message"), F("plugin_197_sendjson"), cfg.sendJSON);
        addFormCheckBox(F("Include Switch in publish path"), F("plugin_197_includeswitch"), cfg.includeSwitch);

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
        RFLink_MQTT_config cfg;
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&cfg, sizeof(cfg));

        // Serial Settings
        ExtraTaskSettings.TaskDevicePluginConfigLong[1] = getFormItemInt(F("plugin_197_baud"));
        ExtraTaskSettings.TaskDevicePluginConfigLong[2] = getFormItemInt(F("plugin_197_data"));
        ExtraTaskSettings.TaskDevicePluginConfigLong[3] = getFormItemInt(F("plugin_197_parity"));
        ExtraTaskSettings.TaskDevicePluginConfigLong[4] = getFormItemInt(F("plugin_197_stop"));

        // MQTT Settings
        strncpy(cfg.publish, WebServer.arg("plugin_197_publish").c_str(), sizeof(cfg.publish));
        strncpy(cfg.subscribe, WebServer.arg("plugin_197_subscribe").c_str(), sizeof(cfg.subscribe));
        cfg.sendRaw = isFormItemChecked(F("plugin_197_sendraw"));
        cfg.sendJSON = isFormItemChecked(F("plugin_197_sendjson"));
        cfg.includeSwitch = isFormItemChecked(F("plugin_197_includeswitch"));

        SaveCustomTaskSettings(event->TaskIndex, (byte*)&cfg, sizeof(cfg));

        success = true;
        break;
      }

    case PLUGIN_INIT:
      {
        success = Plugin_197_setup_serial(event->TaskIndex);
        break;
      }

    case PLUGIN_TEN_PER_SECOND:
      {
        if (MQTTclient_197 && !MQTTclient_197->loop()) {		// Listen out for callbacks
          Plugin_197_update_connect_status();
        }
        success = true;
        break;
      }

    case PLUGIN_ONCE_A_SECOND:
      {
        // Make the initial connection to MQTT, if not connected yet
        // During the plugin initialization, access to MQTT is not possible for some reason
        // (crashes with exception 28), so we need to defer MQTT connection until here
        if (!MQTTclient_197) {
          MQTTclient_197 = new PubSubClient(espclient_197);
          MQTTclient_197->disconnect();
          success = MQTTConnect_197(ClientName) && MQTTSubscribe_197();
        }

        //  Here we check that the MQTT client is alive.
        if (MQTTclient_197 && (!MQTTclient_197->connected() || MQTTclient_should_reconnect)) {
          if (MQTTclient_should_reconnect) {
            addLog(LOG_LEVEL_ERROR, F("IMPT : MQTT 037 Intentional reconnect"));
          }
          MQTTclient_197->disconnect();
          Plugin_197_update_connect_status();
          delay(250);

          Plugin_197_setup_serial(event->TaskIndex);
          if (! MQTTConnect_197(ClientName) || !MQTTSubscribe_197()) {
            success = false;
            break;
          }
        }

        success = true;
        break;
      }

    case PLUGIN_READ:
      {
        // This routine does not output any data and so we do not need to respond to regular read requests
        success = false;
        break;
      }

    case PLUGIN_RECV_MQTT:
      {
        // This is a private option only used by the MQTT 197 callback function

        //      Get the payload and check it out
        LoadTaskSettings(event->TaskIndex);
        RFLink_MQTT_config cfg;
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&cfg, sizeof(cfg));

        String Topic = event->String1;
        String Payload = event->String2;
        String subscribe = cfg.subscribe;

        subscribe.trim();
        if (subscribe.length() > 0) { // empty subscription => nothing to do
          // Now check if the incoming topic matches one of our subscriptions
          parseSystemVariables(subscribe, false);
          if (subscribe == Topic) {
            success = Plugin_197_handle_from_mqtt(event->TaskIndex, Payload);
          }
        }
        break;
      }

    case PLUGIN_SERIAL_IN:
      {
        addLog(LOG_LEVEL_ERROR, F("Serial received"));
        uint8_t serial_buf[P197_BUFFER_SIZE];
        size_t bytes_read = 0;
        while (Serial.available()) {
          if (bytes_read < P197_BUFFER_SIZE) {
            serial_buf[bytes_read] = Serial.read();
            bytes_read++;
          }
          else
            Serial.read();  // when the buffer is full, just read remaining input, but do not store...
        }
        if (bytes_read == P197_BUFFER_SIZE) {
          // full buffer, drop the last position to stuff with string end marker
          bytes_read--;
          // and log buffer full situation
          addLog(LOG_LEVEL_ERROR, F("RFLinkMqtt: serial buffer full!"));
        }
        serial_buf[bytes_read] = 0;
        String message = (char*)serial_buf;

String dbg = F("Serial message: ");
addLog(LOG_LEVEL_ERROR, dbg + message);
        success = Plugin_197_handle_from_rflink(event->TaskIndex, message);
        break;
      }

    case PLUGIN_WRITE:
      {
        String command = parseString(string, 1);
        if (command == F("rflinksend")) {
          success = Plugin_197_handle_from_mqtt(event->TaskIndex, string.substring(11));
        }
        if (command == F("rflinkreceive")) {
          success = Plugin_197_handle_from_rflink(event->TaskIndex, string.substring(13));
        }
        break;
      }

  }
  return success;
}

boolean Plugin_197_setup_serial(int task) {
String dbg = F("Serial setup: ");
  // Set up Serial (copied from the ser2net plugin P020)
  LoadTaskSettings(task);
  if (ExtraTaskSettings.TaskDevicePluginConfigLong[1] != 0) {
    Serial.end();
    #if defined(ESP8266)
      byte serialconfig = 0x10;
    #endif
    #if defined(ESP32)
      uint32_t serialconfig = 0x8000010;
    #endif
    serialconfig += ExtraTaskSettings.TaskDevicePluginConfigLong[3];
    serialconfig += (ExtraTaskSettings.TaskDevicePluginConfigLong[2] - 5) << 2;
    if (ExtraTaskSettings.TaskDevicePluginConfigLong[4] == 2)
      serialconfig += 0x20;
    #if defined(ESP8266)
      Serial.begin(ExtraTaskSettings.TaskDevicePluginConfigLong[1], (SerialConfig)serialconfig);
    #endif
    #if defined(ESP32)
      Serial.begin(ExtraTaskSettings.TaskDevicePluginConfigLong[1], serialconfig);
    #endif
addLog(LOG_LEVEL_ERROR, dbg + serialconfig);
  }
addLog(LOG_LEVEL_ERROR, F("197 End of serial setup"));
  return true;
}


boolean Plugin_197_handle_from_mqtt(int task, String message) {
  String TaskName = getTaskDeviceName(task);
  // Log the event
  String log = F("RFmqtt : [");
  log += TaskName;
  log += F("] received for RFLink: ");
  log += message;
  addLog(LOG_LEVEL_INFO, log);

  // Submit to the RFLink
  if (message.length() > 0) {
    Plugin_197_send_rflink(message);
  }

  if (Settings.UseRules) {
    String RuleEvent = F("");
    RuleEvent += TaskName;
    RuleEvent += F("=");
    RuleEvent += message;
    rulesProcessing(RuleEvent);
  }
  return true;
}


boolean Plugin_197_handle_from_rflink(int task, String message) {
  RFLink_MQTT_config cfg;
  LoadTaskSettings(task);
  LoadCustomTaskSettings(task, (byte*)&cfg, sizeof(cfg));

  Plugin_197_send_mqtt(message, &cfg);

  String log = F("RFmqtt: S>: ");
  addLog(LOG_LEVEL_DEBUG, log + message);

  // We can also use the rules engine for local control!
  if (Settings.UseRules && message.length() > 0) {
    int NewLinePos = message.indexOf(F("\r\n"));
    if (NewLinePos > 0)
      message = message.substring(0, NewLinePos);

    if (message.startsWith("20;")) {
      message = message.substring(6);
    }
    String eventString = "!RFlink#" + message; // strip 20;xx; from incoming message
    rulesProcessing(eventString);
  }
  return message.length() > 0;
}

boolean Plugin_197_send_rflink(String message) {
  Serial.println(message);
  return true;
}


boolean Plugin_197_send_mqtt(String message, struct RFLink_MQTT_config *cfg) {
  if (!cfg) return false;
  if (message.length() == 0 || !MQTTclient_197->connected())
    return false;
  boolean result = true;

  String publish = cfg->publish;
  String topic = publish;
  if (!publish.endsWith("/")) publish += F("/");
  if (cfg->sendRaw && MQTTclient_197) {
    topic = publish + F("raw");
    result = result && MQTTclient_197->publish(topic.c_str(), message.c_str());
  }
  if (cfg->sendJSON && MQTTclient_197) {
    result = result && Plugin_197_send_mqttjson(publish, message, cfg);
  }

  return result;
}

// Large Parts of the following RFLink -> JSON conversion function are taken from
// Phil Wilson's rflink-to-mqtt bridge (https://github.com/Phileep/rflink-to-mqtt.git)
boolean Plugin_197_send_mqttjson(String topic, String message, struct RFLink_MQTT_config *cfg) {
  DynamicJsonBuffer  jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  char msg[message.length() + 1];
  message.toCharArray(msg, message.length() + 1);

  char * strtokIndx;    // this is used by strtok() as an index
  strtokIndx = strtok(msg, "\n");  // Consider only the first line received (i.e. replace \n by \0)
  strtokIndx = strtok(msg, ";");   // get the first part - the string

  // float tmpfloat = 0.0; // temporary float used in tests
  // int tmpint = 0;       // temporary int used in tests
/*
  // Message needs to start with "20":
  if (strcmp(strtokIndx, "20") == 0 ) { // 20 means a message recieved from RFLINK - this is what we are interested in breaking up for use
    // IGNORE the next token (running packet counter)
    strtokIndx = strtok(NULL, ";");
    strcpy(RFData, strtokIndx);     // copy remainder of block to RFData
    // Next token is the family
    if (strtokIndx != NULL) {
      strtokIndx = strtok(NULL, ";");
    }
    if (strtokIndx != NULL) {
      strcpy(RFName, strtokIndx); // copy name to RFName
      root["Family"] = RFName;
    }
    // Ignore certain messages (i.e. confirmations of 10;... commands, debug output
    if (strcmp(RFName, "OK")==0 || strcmp(RFName, "RFDEBUG=")==0 || strcmp(RFName, "DEBUG")==0 || strcmp(RFName, "Slave")==0 ||strcmp(RFName, "PONG")==0 ) {
      return;
    }
    RFID[0] = '\0';

    strtokIndx = strtok(NULL, ";");
    // Read each command pair
    while (strtokIndx != 0) {
      // Split the command in two values
      char* separator = strchr(strtokIndx, '=');
      if (separator != 0) {
        // Actually split the string in 2: replace '=' with 0
        *separator = 0;
        String NamePart = strtokIndx;
        ++separator;

        if (NamePart == "ID") {
          root[NamePart] = separator;
          strcpy(RFID, separator);

        } else if (NamePart == "TEMP") { // test if it is TEMP, which is HEX
          int tmpval = hextoint(separator);
          float neg = 1.0;
          if (tmpval & 0x8000) {
            neg = -1.0;
            tmpval = tmpval & 0x7FFF;
          }
          float tmpfloat = float(tmpval)*0.1; // divide by 10 - using multiply as it is faster than divide
//            float tmpfloat = hextofloat(separator)*0.1;
//            if (tmpfloat < TempMax) { //test if we are inside the maximum test point - if not, assume spurious data
            root.set<float>(NamePart, tmpfloat*neg); // passed spurious test - add data to root
//          }
        } /// end of TEMP block
        else if (NamePart == "HUM") { // test if it is HUM, which is int
          if (strcmp(RFName,"DKW2012") == 0 ) { // digitech weather station - assume it is a hex humidity, not straight int
            tmpint = hextoint(separator);
          } else {
            tmpint = atoi(separator); // end of setting tmpint to the value we want to use & test
          }
          if (tmpint > 0 and tmpint < HumMax) { //test if we are inside the maximum test point - if not, assume spurious data
            root.set<int>(NamePart, tmpint);  // passed the test - add the data to rot, otherwise it will not be added as spurious
          }
        }  // end of HUM block
        else if (NamePart == "RAIN" || NamePart == "WINSP" || NamePart == "WINGS") { // Handle all HEX data fields:
          root.set<float>(NamePart, hextofloat(separator)*0.10 );
        }
        else {// check if an int, add as int, else add as text
          char *ptr;
          long val = strtol(separator, &ptr, 10);
          if (*ptr != '\0') { // not an integer, further characters following
            root[NamePart] = separator; // do normal string add
          } else {
            root.set<long>(NamePart, val); // do int add
          }
        }
      }

      // Find the next command in input string
      strtokIndx = strtok(NULL, ";");
    }

  } else { // not a 20 code- something else
    rflinkDebugln("Received serial command that is not a 20 code ");
    strcpy(RFData, strtokIndx); // copy all of it to RFData
    strcpy(RFName, "unknown");
    strcpy(RFID, "");
  }

  //  client.publish("RF/" + RFName + "-" + RFID , root );


  String MQTTTopic = mqtt_prefix;
  MQTTTopic += String(RFName);
  MQTTTopic += "/" ;
  MQTTTopic += String(RFID);
  MQTTTopic += '\0';
  size_t lenM = MQTTTopic.length(); // returns length of the json
  size_t sizeM = lenM + 1;

  char MQTTTopicConst[lenM];
  MQTTTopic.toCharArray(MQTTTopicConst,sizeM) ;

  // place the json data into variable 'json' for publishing over MQTT
  size_t len = root.measureLength(); // returns length of the json
  size_t size = len+1;
  char json[size];
  root.printTo(json,size);

  rflinkDebug(MQTTTopicConst);
  rflinkDebug("   ");
  rflinkDebugln(json);
  client.publish(MQTTTopicConst, json);
  // TODO: return
*/
  return false;
}




boolean MQTTSubscribe_197() {
  // Subscribe to the topics requested by ALL calls to this plugin.
  // We do this because if the connection to the broker is lost, we want to resubscribe for all instances.
  RFLink_MQTT_config cfg;

  //	Loop over all tasks looking for a 197 instance
  for (byte y = 0; y < TASKS_MAX; y++) {
    if (Settings.TaskDeviceNumber[y] == PLUGIN_ID_197) {
      LoadCustomTaskSettings(y, (byte*)&cfg, sizeof(cfg));
      String subscribeTo = cfg.subscribe;
      if (subscribeTo.length() > 0) {
        parseSystemVariables(subscribeTo, false);
        if (MQTTclient_197->subscribe(subscribeTo.c_str())) {
          String log = F("RFmqtt: [");
          LoadTaskSettings(y);
          log += getTaskDeviceName(y);
          log += F("] subscribed to ");
          log += subscribeTo;
          addLog(LOG_LEVEL_INFO, log);
        } else {
          String log = F("RFmqtt: Error subscribing to ");
          log += subscribeTo;
          addLog(LOG_LEVEL_ERROR, log);
          return false;
        }
      }
    }
  }
  return true;
}

//
// handle MQTT messages
//
void mqttcallback_197(char* c_topic, byte* b_payload, unsigned int length)
{
  // Incomng MQTT messages from the mqtt broker => to be passed on to the RFLink via serial
  String topic = c_topic;

  char cpayload[256];
  strncpy(cpayload, (char*)b_payload, length);
  cpayload[length] = 0;
  String payload = cpayload;		// convert byte to char string
  payload.trim();

  byte DeviceIndex = getDeviceIndex(PLUGIN_ID_197);   // This is the device index of 197 modules -there should be one!

  // We generate a temp event structure to pass to the plugins

  struct EventStruct TempEvent;

  TempEvent.String1 = topic;                            // This is the topic of the message
  TempEvent.String2 = payload;                          // This is the payload

  //  Here we loop over all tasks and call each 197 plugin with function PLUGIN_IMPORT

  for (byte y = 0; y < TASKS_MAX; y++) {
    if (Settings.TaskDeviceNumber[y] == PLUGIN_ID_197) {
      // if we have found a 197 device, let it handle the communication to the RFLink device!
      TempEvent.TaskIndex = y;
      LoadTaskSettings(TempEvent.TaskIndex);
      TempEvent.BaseVarIndex = y * VARS_PER_TASK;           // This is the index in Uservar where values for this task are stored
      schedule_plugin_task_event_timer(DeviceIndex, PLUGIN_RECV_MQTT, &TempEvent);
    }
  }
}

//
// Make a new client connection to the mqtt broker.
// For some reason this seems to failduring the call in INIT- however it succeeds later during recovery
// It would be nice to understand this....

boolean MQTTConnect_197(String clientid)
{
  boolean result = false;
  // @ToDo TD-er: Plugin allows for more than one MQTT controller, but we're now using only the first enabled one.
  int enabledMqttController = firstEnabledMQTTController();
  if (enabledMqttController < 0) {
    // No enabled MQTT controller
    return false;
  }
  // Do nothing if already connected
  if (MQTTclient_197->connected()) return true;

  // define stuff for the client - this could also be done in the intial declaration of MQTTclient_197
  if (!WiFiConnected(100)) {
    Plugin_197_update_connect_status();
    return false; // Not connected, so no use in wasting time to connect to a host.
  }
  ControllerSettingsStruct ControllerSettings;
  LoadControllerSettings(enabledMqttController, ControllerSettings);
  if (ControllerSettings.UseDNS) {
    MQTTclient_197->setServer(ControllerSettings.getHost().c_str(), ControllerSettings.Port);
  } else {
    MQTTclient_197->setServer(ControllerSettings.getIP(), ControllerSettings.Port);
  }
  MQTTclient_197->setCallback(mqttcallback_197);

  //  Try three times for a connection
  for (byte x = 1; x < 4; x++) {
    String log = "";

    if ((SecuritySettings.ControllerUser[enabledMqttController][0] != 0) && (SecuritySettings.ControllerPassword[enabledMqttController][0] != 0))
      result = MQTTclient_197->connect(clientid.c_str(), SecuritySettings.ControllerUser[enabledMqttController], SecuritySettings.ControllerPassword[enabledMqttController]);
    else
      result = MQTTclient_197->connect(clientid.c_str());


    if (result) {
      log = F("RFmqtt : Connected to MQTT broker with Client ID=");
      log += clientid;
      addLog(LOG_LEVEL_INFO, log);

      break; // end loop if succesfull
    }
    else
    {
      log = F("RFmqtt : Failed to connect to MQTT broker - attempt ");
      log += x;
      addLog(LOG_LEVEL_ERROR, log);
    }

    delay(500);
  }
  Plugin_197_update_connect_status();
  return MQTTclient_197->connected();
}

void Plugin_197_update_connect_status() {
  static bool MQTTclient_197_connected = false;
  bool connected = false;
  if (MQTTclient_197 != NULL) {
    connected = MQTTclient_197->connected();
  }
  if (MQTTclient_197_connected != connected) {
    MQTTclient_197_connected = !MQTTclient_197_connected;
    if (Settings.UseRules) {
      String event = connected ? F("RFLinkMQTT#Connected") : F("RFLinkMQTT#Disconnected");
      rulesProcessing(event);
    }
    if (!connected) {
      // workaround see: https://github.com/esp8266/Arduino/issues/4497#issuecomment-373023864
      espclient_197 = WiFiClient();
      addLog(LOG_LEVEL_ERROR, F("RFmqtt: MQTT 197 Connection lost"));
    }
  }
}


//============
/*
float hextofloat(char* hexchars) {return float(strtol(hexchars,NULL,16));}
int hextoint(char* hexchars) {return strtol(hexchars,NULL,16);}

void parseData(char *msg) {      // split the data into its parts
  DynamicJsonBuffer  jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  float tmpfloat = 0.0; // temporary float used in tests
  int tmpint = 0;       // temporary int used in tests
  char * strtokIndx;    // this is used by strtok() as an index
  strtokIndx = strtok(msg,"\n");  // Consider only the first line received (i.e. replace \n by \0)
  strtokIndx = strtok(msg,";");   // get the first part - the string

  // Message needs to start with "20":
  if (strcmp(strtokIndx, "20") == 0 ) { // 20 means a message recieved from RFLINK - this is what we are interested in breaking up for use
    // IGNORE the next token (running packet counter)
    strtokIndx = strtok(NULL, ";");
    strcpy(RFData, strtokIndx);     // copy remainder of block to RFData
    // Next token is the family
    if (strtokIndx != NULL) {
      strtokIndx = strtok(NULL, ";");
    }
    if (strtokIndx != NULL) {
      strcpy(RFName, strtokIndx); // copy name to RFName
      root["Family"] = RFName;
    }
    // Ignore certain messages (i.e. confirmations of 10;... commands, debug output
    if (strcmp(RFName, "OK")==0 || strcmp(RFName, "RFDEBUG=")==0 || strcmp(RFName, "DEBUG")==0 || strcmp(RFName, "Slave")==0 ||strcmp(RFName, "PONG")==0 ) {
      return;
    }
    RFID[0] = '\0';

    strtokIndx = strtok(NULL, ";");
    // Read each command pair
    while (strtokIndx != 0) {
      // Split the command in two values
      char* separator = strchr(strtokIndx, '=');
      if (separator != 0) {
        // Actually split the string in 2: replace '=' with 0
        *separator = 0;
        String NamePart = strtokIndx;
        ++separator;

        if (NamePart == "ID") {
          root[NamePart] = separator;
          strcpy(RFID, separator);

        } else if (NamePart == "TEMP") { // test if it is TEMP, which is HEX
          int tmpval = hextoint(separator);
          float neg = 1.0;
          if (tmpval & 0x8000) {
            neg = -1.0;
            tmpval = tmpval & 0x7FFF;
          }
          float tmpfloat = float(tmpval)*0.1; // divide by 10 - using multiply as it is faster than divide
//          float tmpfloat = hextofloat(separator)*0.1;
//          if (tmpfloat < TempMax) { //test if we are inside the maximum test point - if not, assume spurious data
            root.set<float>(NamePart, tmpfloat*neg); // passed spurious test - add data to root
//          }
        } /// end of TEMP block
        else if (NamePart == "HUM") { // test if it is HUM, which is int
          if (strcmp(RFName,"DKW2012") == 0 ) { // digitech weather station - assume it is a hex humidity, not straight int
            tmpint = hextoint(separator);
          } else {
            tmpint = atoi(separator); // end of setting tmpint to the value we want to use & test
          }
          if (tmpint > 0 and tmpint < HumMax) { //test if we are inside the maximum test point - if not, assume spurious data
            root.set<int>(NamePart, tmpint);  // passed the test - add the data to rot, otherwise it will not be added as spurious
          }
        }  // end of HUM block
        else if (NamePart == "RAIN" || NamePart == "WINSP" || NamePart == "WINGS") { // Handle all HEX data fields:
          root.set<float>(NamePart, hextofloat(separator)*0.10 );
        }
        else {// check if an int, add as int, else add as text
          char *ptr;
          long val = strtol(separator, &ptr, 10);
          if (*ptr != '\0') { // not an integer, further characters following
            root[NamePart] = separator; // do normal string add
          } else {
            root.set<long>(NamePart, val); // do int add
          }
        }
      }

      // Find the next command in input string
      strtokIndx = strtok(NULL, ";");
    }

  } else { // not a 20 code- something else
    rflinkDebugln("Received serial command that is not a 20 code ");
    strcpy(RFData, strtokIndx); // copy all of it to RFData
    strcpy(RFName, "unknown");
    strcpy(RFID, "");
  }

//  client.publish("RF/" + RFName + "-" + RFID , root );


  String MQTTTopic = mqtt_prefix;
  MQTTTopic += String(RFName);
  MQTTTopic += "/" ;
  MQTTTopic += String(RFID);
  MQTTTopic += '\0';
  size_t lenM = MQTTTopic.length(); // returns length of the json
  size_t sizeM = lenM + 1;

  char MQTTTopicConst[lenM];
  MQTTTopic.toCharArray(MQTTTopicConst,sizeM) ;

  // place the json data into variable 'json' for publishing over MQTT
  size_t len = root.measureLength(); // returns length of the json
  size_t size = len+1;
  char json[size];
  root.printTo(json,size);

  rflinkDebug(MQTTTopicConst);
  rflinkDebug("   ");
  rflinkDebugln(json);
  client.publish(MQTTTopicConst, json);

}
*/
//============


#endif // USES_P197
