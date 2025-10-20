
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
#include <LittleFS.h>
#include <FS.h>
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
#define RelePin 26
#define VminCarga 2.26
#define VmaxCarga 2.67
const int Nleituras = 50;
const int ADC_Max = 4095;
const float ADC_Ref = 3.3;
const float divisor = 10000.0/(10000.0 + 47000.0);
unsigned long tempoInicioCarga = 0;
unsigned long tempoEmCarga = 0;
unsigned long ultimaCarga = 0;
enum State {INATIVO, ATIVO, CARREGANDO, CONCLUIDO_CARGA, COMECAR_CARGA};
State state = CONCLUIDO_CARGA;

// Constantes de ciclo
const unsigned long ciclo = 1 * 60 * 1000;
const unsigned long tempo_espera = 3000;

// Variáveis auxiliares para a lógica utilizada
bool modoAula = 0;
bool modoAulaAtual = 0;
unsigned long ultimoPeriodo = 0;
const unsigned long intervalo = 2000;
int proxModoAula = 0;

// URLs para as requisições utilizadas
String solicitacaoCadastro = "http://10.205.149.196/Fechadura_Eletronica/APIs/solicitacoes.php";
String atualizaDB = "http://10.205.149.196/Fechadura_Eletronica/APIs/atualizaDB.php";
String leitorCracha = "http://10.205.149.196/Fechadura_Eletronica/APIs/leiaCartao.php";
String strModoAula = "http://10.205.149.196/Fechadura_Eletronica/APIs/atualizaModoAula.php";
String enviaHistorico = "http://10.205.149.196/Fechadura_Eletronica/APIs/atualizaHistorico.php";
String consultaListaUsuarios = "http://10.205.149.196/Fechadura_Eletronica/APIs/atualizaBackup.php";

const char* localBackup = "/usuarios.json"; // Caminho do arquivo na memória
int falhasConexao = 0;

// Inicialização de variáveis de bibliotecas
//Adafruit_VL53L0X lox = Adafruit_VL53L0X();
//HardwareSerial RFIDserial(1);
Rdm6300 rdm;
HTTPClient http;
HTTPClient httpPost;

SemaphoreHandle_t mutexState;
SemaphoreHandle_t mutexModoAula;

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
  //RFIDserial.begin(9600, SERIAL_8N1, RDM_RX, RDM_TX);
  //rdm.begin(&RFIDserial);

  if (!LittleFS.begin(true)){
    Serial.println("Ocorreu um erro ao montar o LittleFS. O dispositivo será reiniciado.");
    ESP.restart();
  } 
  Serial.println("Sistema de arquivos inicializado.");

  mutexState = xSemaphoreCreateMutex();
  mutexModoAula = xSemaphoreCreateMutex();
  Serial.println("Mutex criados!");
      

  // Inicialização de pinos
  pinMode(RelePin, OUTPUT);
  digitalWrite(RelePin, 0);
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

  while (!Serial) {
    delay(1);
  }

  // Abre o portal de configuração do ESP
  bool res = wm.autoConnect("ESP32_Config"); // Cria a rede ESP32_Config para que seja inserida a senha da internet
  Serial.println("autoConnect retornou.");

  //WiFi.begin("Alunos", "liberato");
  //while (WiFi.status() != WL_CONNECTED) {
  //  Serial.print(".");
  //  delay(500);
  //}
  //Serial.println("Conectado!");

  if (!res){ // Se não for possível conectar
    Serial.println("Falha ao conectar. Por segurança, ");
    ESP.restart();
  } else { // Após conectar a primeira vez, ele salva a senha
    Serial.println("Conectado com sucesso!");
    Serial.println("IP local: ");
    Serial.println(WiFi.localIP());

    atualizaBackupUsuarios();
  }

  
  /*if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }*/


  xTaskCreatePinnedToCore(TaskHTTP, "TaskHTTP", 4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskControleCarga, "TaskControleCarga", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskModoAula, "TaskModoAula", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskAtualizaDBLocal, "TaskAtualizaDBLocal", 4096, NULL, 1, NULL, 1);

  Serial.println("Tasks criadas");

}

void loop() {
  vTaskDelay(1000 / portTICK_PERIOD_MS); // opcional, evita uso de CPU desnecessário
}


        ///////////////////////////////////
        ///  TASK DE REQUISIÇÕES HTTP   ///
        //////////////////////////////////


void TaskHTTP(void *pv){
  while(1){
    if (WiFi.status() == WL_CONNECTED){
      newRegistration();
      atualizarModoAula();
    }
    
    leiaCracha();

    vTaskDelay(500/portTICK_PERIOD_MS);
  }
}


        ////////////////////////////////////////////
        ///  TASK QUE ATUALIZA O BACKUP DO ESP   ///
        ////////////////////////////////////////////


void TaskAtualizaDBLocal(void *pv){
  while (1){
    atualizaBackupUsuarios();

    vTaskDelay(1 * 60 * 1000 / portTICK_PERIOD_MS);
  }
}


        ///////////////////////////////////
        ///  TASK DE CARGA DA BATERIA   ///
        ///////////////////////////////////


void TaskControleCarga(void *pv){
  while(1){
    if (xSemaphoreTake(mutexState, portMAX_DELAY)){
      switch (state){
        case COMECAR_CARGA:
        { 
          Serial.println("Iniciando carga, relé ativado!");
          ReleOn();
          tempoInicioCarga = millis();
          state = CARREGANDO;
          xSemaphoreGive(mutexState);
          vTaskDelay(500 / portTICK_PERIOD_MS);
          break;
        }
        
        case CARREGANDO:
        {
          unsigned long agora = millis();
          unsigned long tempoDeCiclo = agora - tempoInicioCarga;

          Serial.printf("Carregando...\n");

          if (tempoDeCiclo >= ciclo){
            Serial.println("Desligando relé para a medição da carga.");
            ReleOff();
            vTaskDelay(tempo_espera / portTICK_PERIOD_MS);
            float VdivAtual = readVBat() - 0.49;
            if (VdivAtual < 0) VdivAtual = 0;
            Serial.printf("Medição realizada! Tensão atual da bateria: %.2f V\n", VdivAtual);

            if (VdivAtual >= VmaxCarga){
              Serial.printf("Fim do ciclo de carga, bateria carregada.\n");
              state = CONCLUIDO_CARGA;
            } else {
              ReleOn();
              Serial.printf("RELE ATIVADO\n");
              tempoInicioCarga = millis();
              state = CARREGANDO;
            }
          }
          
          xSemaphoreGive(mutexState);
          vTaskDelay(200 / portTICK_PERIOD_MS);
          break;
        }

        case CONCLUIDO_CARGA:
        {
          float V = readVBat();
          if (V <= VminCarga){
            Serial.printf("Tensão muito baixa: %.2f V\nComeçando Carregamento...\n", V);
            state = COMECAR_CARGA;
          } else Serial.printf("Tensão ok: %.2f V\n", V);
          
          xSemaphoreGive(mutexState);
          vTaskDelay(500 / portTICK_PERIOD_MS);
          break;
        }
      }
    }
  }
}


        /////////////////////////
        ///  TASK MODO AULA   ///
        /////////////////////////


void TaskModoAula(void *pv){
  while (1){
  
  if(xSemaphoreTake(mutexModoAula, portMAX_DELAY)){
    if(modoAula) mededistancia();
    else desmagnetizaPorta();
    xSemaphoreGive(mutexModoAula);
  }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}


        /////////////////////////////////////
        ///  Funções que acessam a memória  ///
        /////////////////////////////////////

String readJson(){
if(!LittleFS.exists(localBackup)){
  Serial.println("Arquivo não encontrado na memória");
  return "";
}

File file = LittleFS.open(localBackup, FILE_READ);
if (!file){
  Serial.println("Erro ao abrir arquivo para leitura.");
  return "";
}

String conteudo = file.readString();
file.close();

return conteudo;
}

void AddJson(const String& jsonString){
File file = LittleFS.open(localBackup, FILE_WRITE);
if (!file) {
  Serial.println("Erro ao abrir arquivo para escrita.");
  return;
}

if (file.print(jsonString)) Serial.println("Banco de dados local atualizado com sucesso.");
else Serial.println("Falha ao escrever no arquivo.");

file.close();
}

void atualizaBackupUsuarios(){
Serial.println("Verificando atualizações no banco de dados de usuários...");

http.begin(consultaListaUsuarios);
int httpResponse = http.GET();

if (httpResponse == 200){
  String payloadBanco = http.getString();
  String payloadLocal = readJson();

  if (payloadBanco != payloadLocal){
    Serial.println("Diferença entre as listas de usuarios. Atualizando...");
    AddJson(payloadBanco);
  } else Serial.println("O banco de dados local já está atualizado.");
} else Serial.printf("Falha ao buscar lista de usuários.\n");

http.end();
}

bool leiaCrachaBackup(String tag){
Serial.println("Lendo crachás da memória...");

String jsonContent = readJson();

StaticJsonDocument<2048> doc;
DeserializationError error = deserializeJson(doc, jsonContent);

if (error){
  Serial.print("Falha ao analisar JSON local: ");
  return false;
}

JsonArray usuarios = doc.as<JsonArray>();
for (JsonObject usuario : usuarios){
  String cracha = usuario["cracha"];
  if (cracha == tag) {
    Serial.printf("Acesso liberado (offline).\n");
    return true;
  }
}

Serial.printf("Acesso negado (offline).\n");
return false;

}


        ////////////////////////////
        ///  Funções auxiliares  ///
        ////////////////////////////


float readVBat(){

  long soma = 0;
  for (int c = 0; c < Nleituras; c++){
    int V = analogRead(adcBat);
    soma += V;
  vTaskDelay(2 / portTICK_PERIOD_MS);
  }
  float mediaV = (soma / (float)Nleituras);
  float Vdiv = ((mediaV/ADC_Max) * ADC_Ref);

  return Vdiv;
}

void ReleOn(){
  digitalWrite(RelePin, 1);
  ultimaCarga = millis();
}

void ReleOff(){
  digitalWrite(RelePin, 0);

  unsigned long agora = millis();
  if (tempoInicioCarga != 0 && agora > tempoInicioCarga){
    tempoEmCarga += (agora - tempoInicioCarga);
  }

  tempoInicioCarga = 0;
  ultimaCarga = millis();
}

void mededistancia(){
  VL53L0X_RangingMeasurementData_t measure;
  //lox.rangingTest(&measure, false);
  int medida = measure.RangeMilliMeter/10;

  if (medida < 10){
    Serial.print("\n\nPorta aberta\n\n");
    habilitaModoAula("aguarde");
  }

  vTaskDelay(100 / portTICK_PERIOD_MS);
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

        //////////////////////
        ///  Funções HTTP  ///
        //////////////////////

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
              atualizaBackupUsuarios();
            } else Serial.println("ERRO AO CADASTRAR CRACHÁ, TENTE NOVAMENTE REALIZAR O CADASTRO!");
          }
      }
    } else {
      Serial.println("Nenhuma solicitação encontrada.");
    }
  } else Serial.println("Erro na requisição");
  http.end();
}

void atualizarModoAula() {

  bool modoAulaLocal;

  if (xSemaphoreTake(mutexModoAula, portMAX_DELAY)){

    modoAulaLocal = modoAula;
    xSemaphoreGive(mutexModoAula);
  } 

  // Faz a requisição à uma API que compara o estado de modo aula do banco e o atual do ESP
  http.begin(strModoAula + "?lab=" + String(  lab13) + "&modoAula=" + modoAulaLocal);
  
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

          xSemaphoreTake(mutexModoAula, portMAX_DELAY);
          modoAulaAtual = doc["modoAula"];
          modoAula = modoAulaAtual;
          xSemaphoreGive(mutexModoAula);

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
  } else Serial.println("Teste ok AtualizaModoAula"); xSemaphoreGive(mutexModoAula);
  
  http.end();
}

void leiaCracha () {

  uint32_t tag = 0;

  if (tag = rdm.get_new_tag_id()) {
    if (WiFi.status() == WL_CONNECTED){

      http.begin(leitorCracha + "?cracha=" + tag + "&lab=" + String(lab13));
      int httpResponse = http.GET();

      if (httpResponse == 200){
        String payload = http.getString();

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (!error){
          bool autorizado = doc["autorizado"];
          xSemaphoreTake(mutexModoAula, portMAX_DELAY);
          modoAula = doc["modoAula"];
          xSemaphoreGive(mutexModoAula);

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
    } else Serial.println("Teste ok LeiaCracha");
  } else {
    Serial.println("Sem conexão WIFI. Conectando com o backup.");
    if (leiaCrachaBackup(String(tag))){
      magnetizaPorta();
    } else bip(3);
  }
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
