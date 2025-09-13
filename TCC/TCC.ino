
#include <stdlib.h>
#include <dummy.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <rdm6300.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>


unsigned long tempoAnterior = 0;
int piscarAtivo = 0;

#define RDM_RX 5 
#define RDM_TX 4
#define LED_PIN 19
#define Buzzer 32
#define botao 35

int altera_estado = 0;
unsigned long ultimoPeriodo = 0;
const unsigned long intervalo = 3000;

String requisicaoUrl = "http://192.168.0.58/Fechadura_Eletronica/APIs/solicitacoes.php";
String atualizaDB = "http://192.168.0.58/Fechadura_Eletronica/APIs/atualizaDB.php";

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
HardwareSerial RFIDserial(1);
Rdm6300 rdm;
HTTPClient http;
HTTPClient httpPost;

void setup() {

  WiFiManager wm;

  Serial.begin(115200);
  RFIDserial.begin(9600, SERIAL_8N1, RDM_RX, RDM_TX);
  rdm.begin(&RFIDserial);
  pinMode(27, OUTPUT);
  pinMode(19, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(32, OUTPUT);
  //pinMode(botao, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  while (! Serial) {
    delay(1);
  }

  bool res = wm.autoConnect("ESP32_Config"); // Cria a rede ESP32_Config para que seja inserida a senha da internet

  if (!res){ // Se não for possível conectar
    Serial.println("Falha ao conectar");
    ESP.restart();
  } else { // Após conectar a primeira vez, ele salva a senha para sempre
    Serial.println("Conectado com sucesso!");
    Serial.println("IP local: ");
    Serial.println(WiFi.localIP());
  }

  
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }
}

void loop() {

  if (millis() - ultimoPeriodo >= intervalo){ // A cada 2 segundos ele faz a requisição http para verificar se há alguém se cadastrando
    ultimoPeriodo = millis();

    if (WiFi.status() == WL_CONNECTED){
     newRegistration(); 
    }
  }
  tagrfid();
  mededistancia();
}


void newRegistration(){

  http.begin(requisicaoUrl); // Faz a requisição http para a api responsável por verificar se há algum cadastro pendente
  int httpResponse = http.GET();
  Serial.println(httpResponse);

  if (httpResponse == 200){ // Se a resposta do GET for igual a 200, significa que ocorreu tudo certo com a requisição
    
    String payload = http.getString(); // Recebe o JSON que foi enviado pela API
    //Serial.println("Resposta JSON: " + payload);

    if (payload.indexOf("pendente") > 0){ // Se foi encontrado uma ou mais solicitações pendentes
      Serial.println("Nova solicitação encontrada.");
      
      StaticJsonDocument<512> doc; // decodifica o JSON a partir da variável doc 
      
      DeserializationError error = deserializeJson(doc, payload);
      if (!error) {
        const char* id = doc["id"];
        int id_solicitante = atoi(id); // id do usuário que fez o cadastro

        uint32_t tagNova = rdm.get_new_tag_id();

        while (( tagNova = rdm.get_new_tag_id()) == 0); // Tag a ser cadastrada
        digitalWrite(LED_PIN, HIGH);
        Serial.println("Cartão lido");

        httpPost.begin(atualizaDB); // Faz a requisição para outra API para atualizar o banco de dados com as informações do usuário e da TAG

        httpPost.addHeader("Content-Type", "application/x-www-form-urlencoded"); // aponta qual vai ser o método de envio das informações por meio da URL

        // Construção da URL de resposta
        String postData = "id_solicitacao=";
        postData += String(id_solicitante);
        postData += "&cracha=";
        postData += tagNova;

        int post = httpPost.POST(postData);
        String payloadPost = httpPost.getString();
        Serial.println(post);
        
        if (payloadPost.indexOf("ok") < 0){
          Serial.println("Erro ao atualizar valores.");
        } else {
          Serial.println("CRACHÁ CADASTRADO COM SUCESSO!");
        }
      }

    } else {
      Serial.println("Nenhuma solicitação encontrada.");
    }

  } else {
    Serial.println("Erro na requisição");
  }
  http.end();
}

void tagrfid() {
  if (uint32_t tag = rdm.get_new_tag_id()) {

    if (altera_estado == 0 && tag == 0x909B98 || altera_estado == 0 && tag == 0x80CB48) {
      Serial.print("ACESSO LIBERADO\n");
      digitalWrite(13, HIGH);
      abreporta();
      altera_estado = !altera_estado;
    }
    else if(altera_estado && tag == 0x909B98){
      digitalWrite(13, LOW);
      Serial.print("PORTA TRANCADA\n");
      altera_estado = 0;
    }
  }
}

void mededistancia(){
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  int medida = measure.RangeMilliMeter/10;

  /* if(medida == 819){
    Serial.print("Porta fechada\n");
  } */

  if (altera_estado == 1 && medida < 10){
  abreporta();
  }
  else{
  fechaporta();
  }

  delay(100);
}

void abreporta() {
  unsigned long tempoInicio = millis();

  digitalWrite(19, HIGH);  
  digitalWrite(18, LOW);  
  digitalWrite(27, HIGH); 
  digitalWrite(33, LOW);

  while(millis() - tempoInicio <= 200){
    digitalWrite(32, HIGH);
  }

  while (millis() - tempoInicio > 200 && millis() - tempoInicio < 2700) {
    digitalWrite(32, LOW);
  }
}

void fechaporta(){
  digitalWrite(19, LOW);
  digitalWrite(18, HIGH);
  digitalWrite(27, LOW);
  digitalWrite(32, LOW);
}

