
// includes de bibliotecas utilizadas
#include <stdlib.h>
#include <dummy.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <rdm6300.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Arduino.h>
//#include <LiquidCrystal_I2C.h>

//variáveis que ajudam a contar o tempo
unsigned long tempoAnterior = 0;
int piscarAtivo = 0;

// Laboratório correspondente ao ESP
#define lab13 9

//Pinos do ESP
#define RDM_RX 5 
#define RDM_TX 4
#define LED_PIN 19
#define Buzzer 32
#define botao 35

// Variáveis/Parâmetros utilizados para monitor carga e descarga da bateria
#define adcBat 34
#define RelePin 25
#define VminCarga 1.83
#define VmaxCarga 2.53
int carregando = 0;
const int Nleituras = 10;
const int ADC_Max = 4095;
const float ADC_Ref = 3.3;
const float divisor = 10000/(10000 + 47000);
unsigned long tempoInicioCarga = 0;
unsigned long tempoEmCarga = 0;
unsigned long ultimaCarga = 0;
enum State {INATIVO, ATIVO, CARREGANDO, CONCLUIDO_CARGA, COMECAR_CARGA};
State state = ATIVO;

//Variáveis de monitoramento da Fonte


// Constantes de ciclo
const unsigned long ciclo = 5UL * 60UL * 1000UL;
const unsigned long tempo_espera = 1000UL;

// Variáveis auxiliares para a lógica utilizada
bool modoAula = 0;
bool modoAulaAtual = 0;
unsigned long ultimoPeriodo = 0;
const unsigned long intervalo = 2000;
int proxModoAula = 0;

// URLs para as requisições utilizadas
String solicitacaoCadastro = "http://192.168.1.8/Fechadura_Eletronica/APIs/solicitacoes.php";
String atualizaDB = "http://192.168.1.8/Fechadura_Eletronica/APIs/atualizaDB.php";
String leitorCracha = "http://192.168.1.8/Fechadura_Eletronica/APIs/leiaCartao.php";
String strModoAula = "http://192.168.1.8/Fechadura_Eletronica/APIs/atualizaModoAula.php";
String enviaHistorico = "http://192.168.1.8/Fechadura_Eletronica/APIs/atualizaHistorico.php";

// Inicialização de variáveis de bibliotecas
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
HardwareSerial RFIDserial(1);
Rdm6300 rdm;
HTTPClient http;
HTTPClient httpPost;

/*
#define col  16 //Define o número de colunas do display utilizado
#define lin   2 //Define o número de linhas do display utilizado
#define ende  0x3F //Define o endereço do display

LiquidCrystal_I2C lcd(ende,16,2); //Cria o objeto lcd passando como parâmetros o endereço, o nº de colunas e o nº de linhas
*/
void setup() {

  WiFiManager wm;
/*
  lcd.init(); //Inicializa a comunicação com o display já conectado
  lcd.clear(); //Limpa a tela do display
  lcd.backlight(); //Aciona a luz de fundo do display
*/

  // Inicialização da Serial e bibliotecas 
  Serial.begin(115200);
  RFIDserial.begin(9600, SERIAL_8N1, RDM_RX, RDM_TX);
  rdm.begin(&RFIDserial);

  // Inicialização de pinos
  pinMode(RelePin, OUTPUT);
  digitalWrite(RelePin, LOW);
  pinMode(27, OUTPUT);
  pinMode(19, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(32, OUTPUT);
  //pinMode(botao, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Inicialização dos pinos ADC
  analogReadResolution(12);
  analogSetPinAttenuation(adcBat, ADC_11db);

  while (! Serial) {
    delay(1);
  }

  // Abre o portal de configuração do ESP
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

/*  lcd.setCursor(0, 0); //Coloca o cursor do display na coluna 1 e linha 1
  lcd.print("Grupo Robotica"); //Exibe a mensagem na primeira linha do display
  lcd.setCursor(0, 1); //Coloca o cursor do display na coluna 1 e linha 2
  lcd.print("Hello Word");  //Exibe a mensagem na segunda linha do display
  */

  switch (state){
    case COMECAR_CARGA:
    { 
      Serial.println("Iniciando carga, relé ativado!");
      ReleOn();
      tempoInicioCarga = millis();
      state = CARREGANDO;
      delay(500);
      break;
    }
    
    case CARREGANDO:
    {
      carregando = 1;
      unsigned long agora = millis();
      unsigned long tempoDeCiclo = agora - tempoInicioCarga;

      if (tempoDeCiclo >= ciclo){
        Serial.println("Desligando relé para a medição da carga.");
        ReleOff();
        delay(tempo_espera);
        float VdivAtual = readVBat();
        Serial.printf("Medição realizada! Tensão atual da bateria: %.2f V", VdivAtual/divisor);

        if (VdivAtual >= VmaxCarga){
          Serial.printf("Fim do ciclo de carga, bateria carregada.");
          state = CONCLUIDO_CARGA;
          carregando = 0;
        } else {
          ReleOn();
          tempoInicioCarga = millis();
          state = CARREGANDO;
        }
      }
      
      delay(200);
      break;
    }

    case CONCLUIDO_CARGA:
    {
      float V = readVBat();
      if (V <= VminCarga){
        Serial.printf("Tensão muito baixa: %.2f V\nComeçando Carregamento...", V);
        state = COMECAR_CARGA;
      } else Serial.printf("Tensão ok: %.2f V", V);
      
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED){

    if (millis() - ultimoPeriodo >= intervalo){ // A cada 2 segundos ele faz a requisição http para verificar se há alguém novo se cadastrando
      ultimoPeriodo = millis();
      newRegistration();
    }
    leiaCracha();
    atualizarModoAula();
  }

  if (modoAula){
    mededistancia();
  } else {
    desmagnetizaPorta();
  }
}


float readVBat(){

  long soma = 0;
  for (int c = 0; c < Nleituras; c++){
    int V = analogRead(adcBat);
    soma += V;
    delay(2);
  }
  float mediaV = soma / (float)Nleituras;
  float Vdiv = (mediaV/ADC_Max) * ADC_Ref;

  return Vdiv;
}


void ReleOn(){
  digitalWrite(RelePin, HIGH);
  ultimaCarga = millis();
}


void ReleOff(){
  digitalWrite(RelePin, LOW);

  unsigned long agora = millis();
  if (tempoInicioCarga != 0 && agora > tempoInicioCarga){
    tempoEmCarga += (agora - tempoInicioCarga);
  }

  tempoInicioCarga = 0;
  ultimaCarga = millis();
}

void newRegistration(){

  http.begin(solicitacaoCadastro); // Faz a requisição http GET para a api responsável por verificar se há algum cadastro pendente
  int httpResponse = http.GET();
  //Serial.println(httpResponse);
  
  if (httpResponse == 200){ // Se a resposta do GET for igual a 200, significa que ocorreu tudo certo com a requisição
    
    String payload = http.getString(); // Recebe o JSON que foi enviado pela API

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
          bip(1);
          Serial.println("Cartão lido");

          httpPost.begin(atualizaDB); // Faz a requisição para outra API para atualizar o banco de dados com as informações do usuário e da TAG

          httpPost.addHeader("Content-Type", "application/x-www-form-urlencoded"); // aponta qual vai ser o método de envio das informações por meio da URL

          // Construção da URL de resposta
          String postData = "id_solicitacao=";
          postData += String(id_solicitante);
          postData += "&cracha=";
          postData += tagNova;

          int post = httpPost.POST(postData); // Pegando a resposta da requisição POST
          String payloadPost = httpPost.getString();
          //Serial.println(post);

          StaticJsonDocument<512> doc;
          DeserializationError error = deserializeJson(doc, payloadPost);

          if (!error){
            bool ok = doc["ok"];
            if (ok){
              Serial.println("CRACHÁ CADASTRADO COM SUCESSO!");
            } else {
              Serial.println("ERRO AO CADASTRAR CRACHÁ, TENTE NOVAMENTE REALIZAR O CADASTRO!");
            }
          }
      }
    } else {
      Serial.println("Nenhuma solicitação encontrada.");
    }
  } else Serial.println("Erro na requisição");
  http.end();
}

void atualizarModoAula() {

  // Faz a requisição à uma API que compara o estado de modo aula do banco e o atual do ESP
  http.begin(strModoAula + "?lab=" + String(  lab13) + "&modoAula=" + modoAula);
  int httpResponse = http.GET();

  if (httpResponse == 200){
    String payload = http.getString();

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error){
      bool ok = doc["ok"];

      if (ok){

        bool diferente = doc["diferente"];

        if (diferente){
          modoAulaAtual = doc["modoAula"];

          if (modoAulaAtual){
            habilitaModoAula("aguarde");
            Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");
            modoAula = modoAulaAtual;
          } else if (modoAulaAtual == 0){
            desabilitaModoAula();
            Serial.println("MODO aula DESATIVADO!");
            modoAula = modoAulaAtual;
          }
        } 
      }
    }  
  }
  http.end();
}

void leiaCracha () {
  if (uint32_t tag = rdm.get_new_tag_id()) {

    http.begin(leitorCracha + "?cracha=" + tag + "&lab=" + String(lab13));
    int httpResponse = http.GET();

    if (httpResponse == 200){
      String payload = http.getString();

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error){
        bool autorizado = doc["autorizado"];
        modoAula = doc["modoAula"];

        if (autorizado && modoAula == 0){
          desabilitaModoAula();
          Serial.println("MODO AULA DESATIVADO!");
        } else if (autorizado && modoAula){
          habilitaModoAula(String(tag));
          Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");
        } else if (!autorizado){
          Serial.println("ACESSO NEGADO!");
        }
      } 
    }
    http.end();
  }
}

void mededistancia(){
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  int medida = measure.RangeMilliMeter/10;

  if (medida < 10){
    Serial.print("\n\nPorta aberta\n\n");
    habilitaModoAula("aguarde");
  }

  delay(100);
}

void habilitaModoAula(String crachaLido) {

  if (crachaLido != "aguarde") {
    httpPost.begin(enviaHistorico);
    httpPost.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "cracha=" + crachaLido + "&lab=" + String(lab13) + "&acao=on";
    int httpResponse = httpPost.POST(postData);
    Serial.println("Registrado novo evento!");
    httpPost.end();

    /*
    String payloadPost = httpPost.getString();

    
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payloadPost);

    int ok = doc["ok"];
    String erro = doc["erro"];

    if (ok){
      Serial.println("php inseriu no banco");
    } else {
      Serial.println("nao bombo");
      Serial.println("erro: " + erro);
    }*/
  }


  digitalWrite(13, HIGH); // LED branco
  magnetizaPorta();
  desmagnetizaPorta();
    
}

void desabilitaModoAula(){
  digitalWrite(13, LOW); // LED branco
  digitalWrite(19, LOW);  // LED verde
  digitalWrite(18, HIGH); // LED vermelho
  digitalWrite(27, LOW);
  bip(2);
  digitalWrite(32, LOW); // buzzer
}

void magnetizaPorta(){
  digitalWrite(19, HIGH); // LED verde
  digitalWrite(18, LOW);  // LED vermelho
  digitalWrite(27, HIGH); // Fechadura
  //digitalWrite(33, LOW);

  bip(1);
}

void desmagnetizaPorta(){
  digitalWrite(19, LOW);  // LED verde
  digitalWrite(18, HIGH); // LED vermelho
  digitalWrite(27, LOW);
  digitalWrite(32, LOW); // buzzer
}

void bip(int repeticoes) {
  unsigned long tempoInicio = millis();

  for (int cont = 0; cont < repeticoes; cont++) {
    unsigned long t0 = millis();

    // Bipe de 100 ms
    while ((millis() - t0) < 100) {
      digitalWrite(32, HIGH);
    }

    // Pausa de 100 ms entre bipes
    while ((millis() - t0) < 200) {
      digitalWrite(32, LOW);
    }
  }

  // Espera o tempo restante até completar 2000 ms no total
  while ((millis() - tempoInicio) < 2000) {
    digitalWrite(32, LOW);
  }
}

