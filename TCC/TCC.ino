

// includes de bibliotecas utilizadas
#include <stdlib.h>
#include <dummy.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>


//variáveis que ajudam a contar o tempo
unsigned long tempoAnterior = 0;
int piscarAtivo = 0;


// Laboratório correspondente ao ESP
#define lab 9


//Pinos do ESP
#define RFID_SDA 5
#define RFID_RST 4
#define led_fechadura 26 // led da direita
#define led_modoAula 13 // led do meio
#define led_internet 25 // led da esquerda
#define adcBat 34
#define RelePin 14
#define buzzer 32
#define reedPin 12
#define fechadura 27


// Variáveis de controle para o estado da porta
unsigned long ultimaMudanca = 0;
volatile int estadoPortaAtual = LOW;
volatile bool flagEstadoPorta = false;


// Variáveis/Parâmetros utilizados para o monitor de carga e descarga da bateria
#define VminCarga 1.80
#define VmaxCarga 2.45
const int Nleituras = 50;
const int ADC_Max = 4095;
const float ADC_Ref = 3.3;
const float divisor = 10000.0/(10000.0 + 47000.0);
unsigned long tempoInicioCarga = 0;
unsigned long tempoEmCarga = 0;
unsigned long ultimaCarga = 0;
enum State {INATIVO, ATIVO, CARREGANDO, CONCLUIDO_CARGA, COMECAR_CARGA};
State state = CONCLUIDO_CARGA;


// Constantes auxiliares de ciclos 
const unsigned long cicloDeCarga = 10 * 60 * 1000;
const unsigned long tempoEspera = 20000;


// Variáveis auxiliares para a lógica utilizada
bool modoAula = 0;
bool modoAulaAtual = 0;
unsigned long ultimoPeriodo = 0;
const unsigned long intervalo = 2000;
int proxModoAula = 0;


// URLs para as requisições HTTP utilizadas para acessar APIs
String solicitacaoCadastro = "http://10.233.40.182/Fechadura_Eletronica/APIs/solicitacoes.php";
String atualizaDB = "http://10.233.40.182/Fechadura_Eletronica/APIs/atualizaDB.php";
String leitorCracha = "http://10.233.40.182/Fechadura_Eletronica/APIs/leiaCartao.php";
String atualizaModoAula = "http://10.233.40.182/Fechadura_Eletronica/APIs/atualizaModoAula.php";
String enviaHistorico = "http://10.233.40.182/Fechadura_Eletronica/APIs/atualizaHistorico.php";
String consultaListaUsuarios = "http://10.233.40.182/Fechadura_Eletronica/APIs/atualizaBackup.php";
String strAtualizaEstadoPorta = "http://10.233.40.182/Fechadura_Eletronica/APIs/atualizaEstadoPorta.php";


const char* localBackup = "/usuarios.json"; // Caminho do arquivo na memória


// Inicialização de variáveis de bibliotecas
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
//HardwareSerial RFIDserial(1);
MFRC522 rfid(RFID_SDA, RFID_RST);


// Inicialização das variáveis MUTEX, responsáveis por ""blindar"" as variáveis que são acessadas em mais de uma TASK ao mesmo tempo
SemaphoreHandle_t mutexState;
SemaphoreHandle_t mutexModoAula;


// Display LCD 16x2
String mensagem = "Controle de Acesso - Curso de Eletronica ";
int habilitaFrasePrincipal = 1;
LiquidCrystal_I2C lcd(0x27,16,2); //Cria o objeto lcd passando como parâmetros o endereço, o nº de colunas e o nº de linhas


// Função chamada pela a interrupção 
void IRAM_ATTR toggleEstadoPorta(){
  unsigned long agora = millis();
  if ((agora - ultimaMudanca) > 100){
    estadoPortaAtual = digitalRead(reedPin); // Recebe o estado da porta
    flagEstadoPorta = true; // Seta a tag, sinalizando a mudança de estado da porta
    ultimaMudanca = agora;
  }
}

void setup() {

  Serial.begin(115200); //Inicizalização da serial

  // Inicialização do sistema de arquivos e acesso à memória 
  if (!LittleFS.begin(true)) { 
    Serial.println("ERRO: Falha ao inicializar o LittleFS!");
    while(1);
  }

  // Criando o mutex e atribuindo à variável
  mutexState = xSemaphoreCreateMutex();
  mutexModoAula = xSemaphoreCreateMutex();

  // Verificação se as variáveis de mutex não estão nulas
  if (mutexState == NULL || mutexModoAula == NULL) {
      Serial.println("ERRO: Falha ao criar os mutex!");
      // Fica travado aqui ou reinicia, pois o sistema não pode continuar
      while(1); 
  }

  WiFiManager wm; 

  lcd.begin(16, 2);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Controle de Acesso - Curso de Eletronica");

  // Inicialização das bibliotecas 
  SPI.begin();
  rfid.PCD_Init();

  // Inicialização de pinos
  pinMode(RelePin, OUTPUT);
  digitalWrite(RelePin, 0);
  pinMode(fechadura, OUTPUT);
  pinMode(led_internet, OUTPUT);
  pinMode(led_modoAula, OUTPUT);
  pinMode(buzzer, OUTPUT);
  pinMode(led_fechadura, OUTPUT);
  digitalWrite(led_fechadura, LOW);
  pinMode(reedPin, INPUT_PULLUP);

  // Inicialização dos pinos ADC
  analogReadResolution(12);
  analogSetPinAttenuation(adcBat, ADC_11db);

  while (!Serial) {
    delay(1);
  }

  // Abre o portal de configuração do ESP
  bool res = wm.autoConnect("ESP32_Config"); // Cria a rede ESP32_Config para que seja inserida a senha da internet

  if (!res){ // Se não conectar na internet
    Serial.println("Falha ao conectar");
    digitalWrite(led_internet, LOW);
  } else { // Após conectar a primeira vez, ele salva a senha
    Serial.println("Conectado com sucesso!");
    Serial.println("IP local: ");
    Serial.println(WiFi.localIP());
    digitalWrite(led_internet, HIGH);
  }

  // Criação das tasks utilizadas no projeto
  xTaskCreatePinnedToCore(TaskHTTP, "TaskHTTP", 4096, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(TaskNovoRegistro, "TaskNovoRegistro", 4096, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(TaskAtualizaDBLocal, "TaskAtualizaDBLocal", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskControleCarga, "TaskControleCarga", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskModoAula, "TaskModoAula", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskFrasePrincipal, "TaskFrasePrincipal", 2048, NULL, 1, NULL, 1);
  attachInterrupt(digitalPinToInterrupt(reedPin), toggleEstadoPorta, CHANGE);

  // Inicialização do sensor de distância
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }
}

void loop() {
  vTaskDelay(1000/portTICK_PERIOD_MS);
}


        //////////////////////////////////////////////////////
        ///  TASK QUE EXIBE A FRASE PRINCIPAL NO DISPLAY   ///
        //////////////////////////////////////////////////////


void TaskFrasePrincipal(void *pv){
  while (1){

    int posFinal = 0;
    int espacoSobrando = 0;
    for (int c = 0; c <= mensagem.length() - 1; c++) {
      if (habilitaFrasePrincipal){
        lcd.clear();
        lcd.setCursor(0,0);
        posFinal = c + 16;
        if (posFinal > mensagem.length() - 1) {
          espacoSobrando = posFinal - mensagem.length();
          lcd.print(mensagem.substring(c, mensagem.length() - 1));
          lcd.setCursor(mensagem.length() - c, 0);
          lcd.print(mensagem.substring(0, espacoSobrando));
          vTaskDelay(400/portTICK_PERIOD_MS);
        } else {
          lcd.print(mensagem.substring(c, posFinal));
          vTaskDelay(400/portTICK_PERIOD_MS);
        }
      }
    }
  }
}


        ////////////////////////////////////////////
        ///  TASK QUE MONITORA NOVOS REGISTROS   ///
        ////////////////////////////////////////////


void TaskNovoRegistro(void *pv){
  while(1){
    if (WiFi.status() == WL_CONNECTED){
      newRegistration(); // Monitora se existe algum registro novo no banco de dados
    }

    vTaskDelay(100/portTICK_PERIOD_MS);
  }
}


        ////////////////////////////////////
        ///  TASK COM REQUISIÇÕES HTTP   ///
        ////////////////////////////////////


void TaskHTTP(void *pv){
  while(1){
    if (WiFi.status() == WL_CONNECTED){
      atualizarModoAula(); // Atualiza o modo aula presente no esp, de acordo com o valor de modo aula do banco de dados, deixando os dois lugares sempre com o mesmo valor
      FuncAtualizaEstadoPorta(); // Seta o estado da porta no banco de dados a partir do sensor lido pelo esp 
    }

    leiaCracha(); // Lê o crachá que está sendo aproximado
    vTaskDelay(200/portTICK_PERIOD_MS);
  }
}


        ///////////////////////////////////////////////
        ///  TASK QUE MONITORA A CARGA DA BATERIA   ///
        ///////////////////////////////////////////////


void TaskControleCarga(void *pv){
  while(1){
    if (mutexState && xSemaphoreTake(mutexState, portMAX_DELAY)){ 
      switch (state){
        case COMECAR_CARGA: // Se state for COMECAR_CARGA ele executa a rotina para começar a carga da bateria 
        { 
          Serial.println("Iniciando carga, relé ativado!");
          ReleOn(); // Ativa o Relé, chaveando o transistor 
          tempoInicioCarga = millis(); // Pega o tempo de início da carga
          state = CARREGANDO; 
          xSemaphoreGive(mutexState);

          habilitaFrasePrincipal = 0;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Comecando carga!");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          habilitaFrasePrincipal = 1;
          
          break;
        }
        
        case CARREGANDO:
        {
          unsigned long agora = millis();
          unsigned long tempoDeCiclo = agora - tempoInicioCarga;

          //Serial.printf("Carregando...\n");

          if (tempoDeCiclo >= cicloDeCarga){ // Verifica se já se passou um ciclo de carga para verificar a tensão da bateria
            Serial.println("Desligando relé para a medição da carga.");
            ReleOff(); // Desliga o relé para medir a tensão real da bateria
            vTaskDelay(tempoEspera / portTICK_PERIOD_MS); // Espera a bateria se estabilizar minimamente
            float VdivAtual = readVBat(); // Lê a tensão da bateria 
            if (VdivAtual < 0) VdivAtual = 0;
            Serial.printf("Medição realizada! Tensão atual da bateria: %.2f V\n", VdivAtual);
            
            habilitaFrasePrincipal = 0;
            lcd.clear();
            lcd.setCursor(5, 0);
            lcd.print("Tensao");
            lcd.setCursor(0, 1);
            lcd.print(VdivAtual);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            habilitaFrasePrincipal = 1;

            if (VdivAtual >= VmaxCarga){ // Se a tensão que foi lida for maior que a tensão máxima definida anteriormente, conclui o carregamento
              Serial.printf("Fim do ciclo de carga, bateria carregada.\n");
              state = CONCLUIDO_CARGA;
            } else { // Se não for maior, continua a carga, chaveando novamente o transistor
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

        case CONCLUIDO_CARGA: // Monitora a tensão da bateria constantemente até que a tensão seja menor do que a mínima, definida anteriormente 
        {
          float V = readVBat();
          if (V <= VminCarga){
            Serial.printf("Tensão muito baixa: %.2f V\nComeçando Carregamento...\n", V);
            state = COMECAR_CARGA;

            habilitaFrasePrincipal = 0;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Tensao baixa");
            lcd.setCursor(0, 1);
            lcd.print(V);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            habilitaFrasePrincipal = 1;
            
          } else {
            Serial.printf("Tensão ok: %.2f V\n", V);
            /*
            habilitaFrasePrincipal = 0;
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Tensao ok");
            lcd.setCursor(0, 1);
            lcd.print(V);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            habilitaFrasePrincipal = 1;*/
          }
          xSemaphoreGive(mutexState);
          vTaskDelay(500 / portTICK_PERIOD_MS);
          break;
        }
      }
    }
  }
}


        ////////////////////////////////////////////////////////////////
        ///  TASK QUE MONITORA O MODO AULA E O SENSOR DE DISTÂNCIA   ///
        ////////////////////////////////////////////////////////////////


void TaskModoAula(void *pv){ // Responsável por liberar a porta a partir do sensor de proximidade ou manter a porta trancada
  while (1){
  
  if(mutexModoAula && xSemaphoreTake(mutexModoAula, portMAX_DELAY)){
    if(modoAula) leDistancia();
    else desmagnetizaPorta();
    xSemaphoreGive(mutexModoAula);
  }
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}


        ////////////////////////////////////////////
        ///  TASK QUE ATUALIZA O BACKUP DO ESP   ///
        ////////////////////////////////////////////


void TaskAtualizaDBLocal(void *pv){ // Monitora constantemente o banco para verificar se a tabela de lá está igual a tabela que está na memória do esp
  while (1){  
    atualizaBackupUsuarios(); // A cada período pega os usuários e crachás do banco e traz para o backup no esp

    vTaskDelay(30 * 1000 / portTICK_PERIOD_MS);
  }
}


        ///////////////////////////////////////
        ///  Funções que acessam a memória  ///
        ///////////////////////////////////////

void atualizaBackupUsuarios(){

  HTTPClient httpBackup;

  Serial.println("Verificando atualizações no banco de dados de usuários...");

  httpBackup.begin(consultaListaUsuarios); // Envia uma requisição para uma API que pega as colunas de crachás e usuários do banco e envia como json
  int httpResponse = httpBackup.GET(); // Recebe o status da requisição

  if (httpResponse == 200){ // Se a requisição retornar o status 200, significa que a requisição deu certo
    String payloadBanco = httpBackup.getString(); // Pega o JSON do banco que foi enviado pela API
    String payloadLocal = readJson(); // Pega o JSON da memória do ESP

    if (payloadBanco != "[]"){
      if (payloadBanco != payloadLocal){ // Se forem diferentes, exclui o que está na memória atualmente e salva o do banco
        Serial.println("Payload banco: ");
        Serial.println(payloadBanco);
        Serial.println("Payload Local: ");
        Serial.println(payloadLocal);
        Serial.println("Diferença entre as listas de usuarios. Atualizando...");
        SalvaJson(payloadBanco);
      } else { // Se forem iguais, continua a rotina
        Serial.println("O banco de dados local já está atualizado.");
        Serial.println("Payload banco: ");
        Serial.println(payloadBanco);
        Serial.println("Payload Local: ");
        Serial.println(payloadLocal);
      }
    }
  } else Serial.printf("Falha ao buscar lista de usuários. Código de erro: %d\n", httpResponse);
  httpBackup.end();
}

String readJson(){
  if(!LittleFS.exists(localBackup)){ // Verifica se o arquivo já existe 
    Serial.println("Arquivo não encontrado na memória");
    return "";
  }

  File file = LittleFS.open(localBackup, FILE_READ); // Tenta abrir o arquivo para a leitura
  if (!file){
    Serial.println("Erro ao abrir arquivo para leitura.");
    return "";
  }

  String conteudo = file.readString(); // Lê o arquivo e salva
  file.close();

  return conteudo; // Retorna o conteúdo do arquivo
}

void SalvaJson(const String& jsonString){
  File file = LittleFS.open(localBackup, FILE_WRITE); // Tenta abrir o arquivo para a escrita
  if (!file) {
    Serial.println("Erro ao abrir arquivo para escrita.");
    return; // Se não conseguir abrir, retorna e não executa o resto da função
  }

  if (file.print(jsonString)) Serial.println("Banco de dados local atualizado com sucesso."); // Atualiza a memória do ESP 
  else Serial.println("Falha ao escrever no arquivo.");

  file.close();
}

bool leiaCrachaBackup(String tag){ 
  Serial.println("Lendo crachás da memória...");

  String jsonContent = readJson(); // Lê o arquivo da memória que contém os crachás

  // Rotina para deserializar o JSON
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, jsonContent);

  if (error){ // Se houver erro ao tentar deserializar, retorna e não executa a função
    Serial.print("Falha ao analisar JSON local: ");
    return false;
  }

  JsonArray usuarios = doc.as<JsonArray>();
  for (JsonObject usuario : usuarios){ // Percorre o vetor de JSON ( array = {{}, {}, {}, ...}), acessando as informações de cada usuário separadamente
    String cracha = usuario["cracha"]; // Pega o crachá do usuário atual
    if (cracha == tag) { // Verifica se corresponde ao crachá que foi lido
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
  for (int c = 0; c < Nleituras; c++){ // Faz a leitura ADC várias vezes e soma
    int V = analogRead(adcBat);
    soma += V;
    vTaskDelay(2 / portTICK_PERIOD_MS);
  }
  float mediaV = (soma / (float)Nleituras); // Faz a média das leituras
  float Vdiv = ((mediaV/ADC_Max) * ADC_Ref); // Acha o valor em tensão e retorna

  return Vdiv;
}

void ReleOn(){ // Ativa o relé, chaveando o transistor
  digitalWrite(RelePin, 1);
  ultimaCarga = millis();
}

void ReleOff(){ // Desativa o relé, desligando o sinal da base do transistor
  digitalWrite(RelePin, 0);
  tempoInicioCarga = 0;
  ultimaCarga = millis();
}

// Lê e transforma o crachá aproximado em Hexadecimal
String getUIDString(MFRC522::Uid &uid) {
  String uidStr = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

void leDistancia(){ // Lê a distância a partir do sensor VL53L0X
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  int medida = measure.RangeMilliMeter/10;

  if (medida < 20){ // Se alguém passar a mão na frente, magnetiza e libera a porta 
    Serial.print("\n\nPorta aberta\n\n");
    habilitaModoAula("SemCracha");
  }

  vTaskDelay(100 / portTICK_PERIOD_MS);
}

void desabilitaModoAula(){
  digitalWrite(led_modoAula, LOW); 
  digitalWrite(fechadura, LOW);
  bip(2);
}

void magnetizaPorta(){
  digitalWrite(led_fechadura, HIGH);
  digitalWrite(fechadura, HIGH);
  habilitaFrasePrincipal = 0;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Acesso liberado!");
  bip(1);
  habilitaFrasePrincipal = 1;
}

void desmagnetizaPorta(){
  digitalWrite(led_fechadura, LOW);
  digitalWrite(fechadura, LOW);
}

void bip(int repeticoes) { // Responsável por fazer os bips no buzzer
  unsigned long tempoInicio = millis();

  for (int cont = 0; cont < repeticoes; cont++) {
    unsigned long t0 = millis();

    // Bip de 100 ms
    while ((millis() - t0) < 100) {;
      digitalWrite(buzzer, HIGH);
    }

    // Pausa de 100 ms entre bipes
    while ((millis() - t0) < 200) {
      digitalWrite(buzzer, LOW);
    }
  }

  // Espera o tempo restante até completar 2000 ms no total
  while ((millis() - tempoInicio) < 2000) {
    digitalWrite(buzzer, LOW);
  }
}




        //////////////////////
        ///  Funções HTTP  ///
        //////////////////////


void newRegistration(){

  HTTPClient httpNovoRegistro;
  HTTPClient httpPostNovoRegistro;

  httpNovoRegistro.begin(solicitacaoCadastro); // Faz a requisição http GET para a api responsável por verificar se há algum cadastro pendente
  int httpResponse = httpNovoRegistro.GET(); 
  //Serial.println(httpResponse);
  
  if (httpResponse == 200){ // Se a resposta do GET for igual a 200, significa que ocorreu tudo certo com a requisição
    
    String payload = httpNovoRegistro.getString(); // Recebe o JSON que foi enviado pela API
    StaticJsonDocument<512> doc; // decodifica o JSON a partir da variável doc 
    DeserializationError error = deserializeJson(doc, payload);
    String status_cadastro = doc["status_cadastro"];

    if (!error) {
      if (status_cadastro == "pendente"){ // Se existir alguma solicitação pendente
        Serial.println("Nova solicitação encontrada.");
        
        const char* id = doc["id"];
        int id_solicitante = atoi(id); // id do usuário que fez o cadastro
        String usuario = doc["Username"];
        unsigned long tempoInicial = millis();

        habilitaFrasePrincipal = 0;
        while (rfid.PICC_IsNewCardPresent() == 0 || rfid.PICC_ReadCardSerial() == 0) { // Verifica se algum crachá está sendo aproximado 
          vTaskDelay(50 / portTICK_PERIOD_MS);  

          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Esperando cracha");

          if (usuario.length() < 13){
            lcd.setCursor(0, 1);
            lcd.print("de ");
            lcd.setCursor(4, 1);
            lcd.print(usuario);
          } 

          if (millis() - tempoInicial > 10000) { // Espera 10s a cada loop para não deixar a Task travada
            //Serial.println("Tempo limite para leitura de cartão excedido.");
            cont++;
            return; // Sai da função se ninguém aproximar o cartão
          }
        }

        habilitaFrasePrincipal = 1;
        String tag = getUIDString(rfid.uid); // Lê a tag
        bip(1);
        //Serial.println(tag);
        //Serial.println("Cartão lido");

        httpPostNovoRegistro.begin(atualizaDB); // Faz a requisição para outra API para atualizar o banco de dados com as informações do usuário e da TAG

        httpPostNovoRegistro.addHeader("Content-Type", "application/x-www-form-urlencoded"); // aponta qual vai ser o método de envio das informações por meio do corpo da requisição

        // Construção da URL de resposta
        String postData = "id_solicitacao=";
        postData += String(id_solicitante);
        postData += "&cracha=";
        postData += tag;
        postData += "&flag=0";

        int post = httpPostNovoRegistro.POST(postData); // Pegando a resposta da requisição POST
        String payloadPost = httpPostNovoRegistro.getString();
        //Serial.println(post);

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, payloadPost); // Deserializa o outro payload

        if (!error){
          bool ok = doc["ok"];
          if (ok){
            Serial.println("CRACHÁ CADASTRADO COM SUCESSO!");
          } else {
            Serial.println("ERRO AO CADASTRAR CRACHÁ, TENTE NOVAMENTE REALIZAR O CADASTRO!");
          }
        }

        rfid.PICC_HaltA(); 
      }
    } 
  } else Serial.println("Erro na requisição");
  httpPostNovoRegistro.end();
  httpNovoRegistro.end();
}


void FuncAtualizaEstadoPorta(){
  
  HTTPClient httpEstadoPorta;
  
  if (flagEstadoPorta){ // Verifica se a flag foi setada
  
    httpEstadoPorta.begin(strAtualizaEstadoPorta); // Chama a API que atualiza o estado da porta para que o valor presente no esp e no banco seja igual
    httpEstadoPorta.addHeader("Content-Type", "application/x-www-form-urlencoded"); // Envia as informações pelo corpo da requisição

    String corpoRequisicao = "lab=";
    corpoRequisicao += String(lab);
    corpoRequisicao += "&estadoPorta=";
    corpoRequisicao += estadoPortaAtual;

    int httpResponse = httpEstadoPorta.POST(corpoRequisicao);  // Envia o corpo da requisição
    
    // Prints para debug
    /*if (httpResponse == 200){

      Serial.println("\n\nRequisição deu certo.\n");
      String payloadPost = httpEstadoPorta.getString();

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, payloadPost);

      bool ok = doc["ok"];
      if (ok){
        Serial.println("Requisição deu certo.");
      } else Serial.println("Requisição deu errado.");
    } else Serial.println("Erro na requisição do estado porta");*/
    
    httpEstadoPorta.end();

    flagEstadoPorta = 0; // Reseta a flag
  }
}


void atualizarModoAula() {
  
  HTTPClient httpModoAula;

  if (mutexModoAula && xSemaphoreTake(mutexModoAula, portMAX_DELAY)){

    // Faz a requisição à uma API que compara o estado de modo aula do banco e o atual do ESP para manter atualiado o valor de modo aula
    httpModoAula.begin(atualizaModoAula + "?lab=" + String(lab) + "&modoAula=" + modoAula);
    xSemaphoreGive(mutexModoAula);
    int httpResponse = httpModoAula.GET();

    if (httpResponse == 200){
      String payload = httpModoAula.getString();

      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, payload); // Deserializa o JSON enviado pela API

      if (!error){
        bool ok = doc["ok"]; 

        if (ok){ // Se o valor de ok for true, retornado pela API, continua a lógica

          bool diferente = doc["diferente"];

          if (diferente){ // Se diferente for true (Se foi mudado o modo aula pelo site), significa que é preciso atualizar o valor local de modo aula

            if(mutexModoAula && xSemaphoreTake(mutexModoAula, portMAX_DELAY)){
              modoAulaAtual = doc["modoAula"];
              modoAula = modoAulaAtual; // Atualiza o valor de modo aula local
              xSemaphoreGive(mutexModoAula);
            }

            habilitaFrasePrincipal = 0;
            if (modoAulaAtual){ // Habilita o modo aula
              Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");
              modoAula = modoAulaAtual;

              habilitaModoAula("SemCracha");

            } else if (modoAulaAtual == 0){ // Desabilita o modo aula 
              Serial.println("MODO aula DESATIVADO!");
              modoAula = modoAulaAtual;

              lcd.clear();
              lcd.setCursor(3, 0);
              lcd.print("Fechadura");
              lcd.setCursor(3, 1);
              lcd.print("trancada!");
              desabilitaModoAula();
              lcd.clear();
              lcd.setCursor(4, 0);
              lcd.print("Modo aula");
              lcd.setCursor(3, 1);
              lcd.print("desativado");
              vTaskDelay(2000 / portTICK_PERIOD_MS);
              habilitaFrasePrincipal = 1;
            }
          } 
        }
      }  
    } else Serial.println("Teste ok AtualizaModoAula"); 
    
    httpModoAula.end();
  } 
}


void leiaCracha () {

  HTTPClient httpLeiaCracha;

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return; // Espera algum crachá ser aproximado
  String tag = getUIDString(rfid.uid); // Ao ser aproximado, já lê a tag

  if (WiFi.status() == WL_CONNECTED){ // Se a internet estiver conectada, envia uma requisição para a verificação do crachá

    httpLeiaCracha.begin(leitorCracha + "?cracha=" + tag + "&lab=" + String(lab)); // Envia uma requisição para a API que irá verificar se o crachá existe no banco e irá inverter o estado de modo aula
    int httpResponse = httpLeiaCracha.GET();

    if (httpResponse == 200){ // Se a requisição tiver sucesso
      String payload = httpLeiaCracha.getString(); // Pega o JSON enviado pela API

      // Deserializa ele
      StaticJsonDocument<512> doc;
      DeserializationError error = deserializeJson(doc, payload);

      if (!error){ // Se não der erro
        bool autorizado = doc["autorizado"]; // Pega o valor retornado pela API de autorizado
        if (mutexModoAula && xSemaphoreTake(mutexModoAula, portMAX_DELAY)){
          modoAula = doc["modoAula"]; // Pega o valor de modo aula
          xSemaphoreGive(mutexModoAula);
        }
        
        habilitaFrasePrincipal = 0;
        if (autorizado && modoAula == 0){ // Se for autorizado e o novo modo aula for desligado
          Serial.println("MODO AULA DESATIVADO!");
          
          lcd.clear();
          lcd.setCursor(3, 0);
          lcd.print("Fechadura");
          lcd.setCursor(3, 1);
          lcd.print("trancada!");
          desabilitaModoAula();
          lcd.clear();
          lcd.setCursor(4, 0);
          lcd.print("Modo aula");
          lcd.setCursor(3, 1);
          lcd.print("desativado");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          habilitaFrasePrincipal = 1;
        } else if (autorizado && modoAula){ // Se for autorizado e o novo modo aula for ligado
          Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");

          habilitaModoAula(tag); // Habilita o modo aula

        } else if (!autorizado){ // Se não for autorizado, rejeita o pedido 
          Serial.println("ACESSO NEGADO!");

          lcd.clear();
          lcd.setCursor(1, 0);
          lcd.print("Acesso negado!");
          vTaskDelay(1000 / portTICK_PERIOD_MS);
          habilitaFrasePrincipal = 1;
        }
      } 
    }

    httpLeiaCracha.end();
    rfid.PICC_HaltA();  

  } else { // Se a internet não estiver conectada, busca a memória local para verificar permissões de crachás
    if (leiaCrachaBackup(tag)){ // Se o crachá estiver na memória, permite o acesso, se não, não
      Serial.println("Acesso liberado.");
    } else Serial.println("Acesso negado");
  }
}


void habilitaModoAula(String crachaLido) { // Função que habilita o modo aula e atualiza o histórico

  HTTPClient httpHabilita;

  if (crachaLido != "SemCracha") {
    httpHabilita.begin(enviaHistorico); // Chama a API
    httpHabilita.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "cracha=" + crachaLido + "&lab=" + String(lab);
    int httpResponse = httpHabilita.POST(postData); // Envia os dados necessários para criar uma nova linha na tabela de histórico
    Serial.println("Registrado novo evento!");
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("Modo aula");
    lcd.setCursor(5, 1);
    lcd.print("ativado");
  }

  httpHabilita.end();

  digitalWrite(led_modoAula, HIGH);
  magnetizaPorta();
  habilitaFrasePrincipal = 1;
  desmagnetizaPorta();
}





