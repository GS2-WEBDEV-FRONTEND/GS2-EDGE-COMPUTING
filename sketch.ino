
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Configuração 
const char* ssid = "Wokwi-GUEST";
const char* password = "";

// Coloque o endereço IP público da VM
const char* mqtt_server = "4.229.225.150";
const int mqtt_port = 1883;

// Topic que o IoT-Agent MQTT espera 
const String deviceId = "futurepath-gs";
const String pubTopic = "/json/" + deviceId;       
const String subTopic = "/commands/" + deviceId;    

// Hardware
#define BUTTON_PIN 32
#define LED_STUDY 19
#define LED_WORK  5
#define LED_PAUSE 18

// DS18B20
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// LDR
#define LDR_PIN 34

String modes[] = {"study", "work", "pause"};
int modeIndex = 0;

unsigned long lastPress = 0;
unsigned long lastSend = 0;

// MQTT / WIFI
WiFiClient espClient;
PubSubClient client(espClient);

// Para gerar clientId único
String makeClientId() {
  String id = "futurepath";
  id += String((uint32_t)ESP.getEfuseMac(), HEX);
  return id;
}

// Funções
void connectWiFi() {
  Serial.print("Conectando ao WiFi...");
  WiFi.begin(ssid, password);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
    if (millis() - started > 15000) { // tenta 15s antes de continuar 
      Serial.println();
      Serial.println("Ainda tentando conectar...");
      started = millis();
    }
  }
  Serial.println();
  Serial.print("WiFi conectado. IP: ");
  Serial.println(WiFi.localIP());
}

void handleCommand(const JsonDocument &cmd) {
  if (cmd.containsKey("led") && cmd.containsKey("value")) {
    String led = cmd["led"].as<String>();
    int val = cmd["value"].as<int>();
    if (led == "study") digitalWrite(LED_STUDY, val);
    if (led == "work")  digitalWrite(LED_WORK, val);
    if (led == "pause") digitalWrite(LED_PAUSE, val);
    Serial.printf("Comando LED: %s -> %d\n", led.c_str(), val);
  }
  if (cmd.containsKey("mode")) {
    String m = cmd["mode"].as<String>();
    for (int i=0;i<3;i++){
      if (m == modes[i]) {
        modeIndex = i;
        updateLEDs();
        Serial.printf("Modo alterado via comando para: %s\n", m.c_str());
      }
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Converte payload em string e tenta parsear JSON
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];

  Serial.print("Mensagem recebida em ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, message);
  if (err) {
    Serial.print("Erro parse JSON comando: ");
    Serial.println(err.c_str());
    return;
  }
  handleCommand(doc);
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);

  while (!client.connected()) {
    String clientId = makeClientId();
    Serial.print("Conectando MQTT...");
    if (client.connect(clientId.c_str())) {
      Serial.println("conectado");
      // Inscrever tópico de comando
      client.subscribe(subTopic.c_str());
      Serial.print("Inscrito em: ");
      Serial.println(subTopic);
    } else {
      Serial.print("falhou, rc=");
      Serial.print(client.state());
      Serial.println(" tentando novamente em 2s");
      delay(2000);
    }
  }
}

void updateLEDs() {
  digitalWrite(LED_STUDY, modeIndex == 0 ? HIGH : LOW);
  digitalWrite(LED_WORK,  modeIndex == 1 ? HIGH : LOW);
  digitalWrite(LED_PAUSE, modeIndex == 2 ? HIGH : LOW);
}

void sendContext(const char* evt) {
  StaticJsonDocument<300> doc;

  sensors.requestTemperatures();
  float temperature = sensors.getTempCByIndex(0);
  if (temperature == DEVICE_DISCONNECTED_C) temperature = NAN;

  int ldrValue = analogRead(LDR_PIN);
  float lux = (ldrValue / 4095.0) * 1000.0;

  doc["deviceId"] = deviceId;
  doc["event"] = evt;
  doc["mode"] = modes[modeIndex];
  if (!isnan(temperature)) doc["temperature"] = temperature;
  doc["lux"] = lux;
  doc["timestamp"] = millis();

  char output[300];
  size_t len = serializeJson(doc, output);
  bool ok = client.publish(pubTopic.c_str(), output, len);
  if (!ok) {
    Serial.println("Falha ao publicar MQTT");
  } else {
    Serial.print("Publicado em ");
    Serial.print(pubTopic);
    Serial.print(" -> ");
    Serial.println(output);
  }
}

// Setup/Loop
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(LED_STUDY, OUTPUT);
  pinMode(LED_WORK, OUTPUT);
  pinMode(LED_PAUSE, OUTPUT);
  updateLEDs();

  sensors.begin();

  connectWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  reconnectMQTT();

  lastSend = millis();
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  // Botão para trocar o modo 
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastPress > 400) {
    lastPress = millis();
    modeIndex = (modeIndex + 1) % 3;
    updateLEDs();
    sendContext("context_change");
  }

  // Envio a cada 5s
  if (millis() - lastSend >= 5000) {
    lastSend = millis();
    sendContext("periodic_update");
  }
}
