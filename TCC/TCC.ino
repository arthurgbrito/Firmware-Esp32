


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
#include <LiquidCrystal_I2C.h>

//variáveis que ajudam a contar o tempo
unsigned long tempoAnterior = 0;
int piscarAtivo = 0;

// Laboratório correspondente ao ESP
#define lab 9

//Pinos do ESP
#define RDM_RX 5 
#define RDM_TX 4
#define LED_PIN 26
#define Buzzer 32
#define reedPin 14
#define led_internet 25
#define adcBat 34
#define RelePin 14

// Variáveis de controle para o estado da porta
unsigned long ultimaMudanca = 0;
volatile int estadoPortaAtual = LOW;
volatile bool flagEstadoPorta = false;

// Variáveis/Parâmetros utilizados para o monitor de carga e descarga da bateria
#define VminCarga 2.26
#define VmaxCarga 2.50
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
const unsigned long cicloDeCarga = 5 * 60 * 1000;
const unsigned long tempoEspera = 20000;

// Variáveis auxiliares para a lógica utilizada
bool modoAula = 0;
bool modoAulaAtual = 0;
unsigned long ultimoPeriodo = 0;
const unsigned long intervalo = 2000;
int proxModoAula = 0;

// URLs para as requisições HTTP utilizadas para acessar APIs
String solicitacaoCadastro = "http://172.20.2.142/Fechadura_Eletronica/APIs/solicitacoes.php";
String atualizaDB = "http://172.20.2.142/Fechadura_Eletronica/APIs/atualizaDB.php";
String leitorCracha = "http://172.20.2.142/Fechadura_Eletronica/APIs/leiaCartao.php";
String atualizaModoAula = "http://172.20.2.142/Fechadura_Eletronica/APIs/atualizaModoAula.php";
String enviaHistorico = "http://172.20.2.142/Fechadura_Eletronica/APIs/atualizaHistorico.php";
String consultaListaUsuarios = "http://172.20.2.142/Fechadura_Eletronica/APIs/atualizaBackup.php";
String atualizaEstadoPorta = "http://172.20.2.142/Fechadura_Eletronica/APIs/atualizaEstadoPorta.php";

const char* localBackup = "/usuarios.json"; // Caminho do arquivo na memória

// Inicialização de variáveis de bibliotecas
Adafruit_VL53L0X lox = Adafruit_VL53L0X();
//HardwareSerial RFIDserial(1);
Rdm6300 rdm;

// Inicialização das variáveis MUTEX, responsáveis por ""blindar"" as variáveis que são acessadas em mais de uma TASK ao mesmo tempo
SemaphoreHandle_t mutexState;
SemaphoreHandle_t mutexModoAula;



LiquidCrystal_I2C lcd(0x27,16,2); //Cria o objeto lcd passando como parâmetros o endereço, o nº de colunas e o nº de linhas


// Função chamada pela a interrupção 
void IRAM_ATTR toggleEstadoPorta(){
  unsigned long agora = millis();
  if ((agora - ultimaMudanca) > 100){
    estadoPortaAtual = digitalRead(reedPin);
    flagEstadoPorta = true;
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

  lcd.init();                      // initialize the lcd 
  // Print a message to the LCD.
  lcd.backlight();
  lcd.setCursor(3,0);
  lcd.print("Controle de");

  // Inicialização das bibliotecas 

  //RFIDserial.begin(9600, SERIAL_8N1, RDM_RX, RDM_TX);
  //rdm.begin(&RFIDserial);

  // Inicialização de pinos
  pinMode(RelePin, OUTPUT);
  digitalWrite(RelePin, 0);
  pinMode(27, OUTPUT);
  pinMode(led_internet, OUTPUT);
  pinMode(19, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(32, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
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
    ESP.restart();
    digitalWrite(led_internet, LOW);
  } else { // Após conectar a primeira vez, ele salva a senha
    Serial.println("Conectado com sucesso!");
    Serial.println("IP local: ");
    Serial.println(WiFi.localIP());
    digitalWrite(led_internet, HIGH);
  }

  // Criação das tasks utilizadas no projeto
  xTaskCreatePinnedToCore(TaskHTTP, "TaskHTTP", 4096, NULL, 1, NULL, 0); 
  xTaskCreatePinnedToCore(TaskControleCarga, "TaskControleCarga", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskModoAula, "TaskModoAula", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskAtualizaDBLocal, "TaskAtualizaDBLocal", 8192, NULL, 1, NULL, 0);
  attachInterrupt(digitalPinToInterrupt(reedPin), toggleEstadoPorta, CHANGE);

  // Inicialização do sensor de distância
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }
}

void loop() {
  verificaEstadoPorta();
}


        ///////////////////////////////////
        ///  TASK DE REQUISIÇÕES HTTP   ///
        //////////////////////////////////


void TaskHTTP(void *pv){
  while(1){
    if (WiFi.status() == WL_CONNECTED){
      newRegistration(); // Monitora se existe algum registro novo no banco de dados
      atualizarModoAula(); // Atualiza o modo aula presente no esp, de acordo com o valor de modo aula do banco de dados, deixando os dois lugares sempre com o mesmo valor
    }

    leiaCracha(); // Lê o crachá que está sendo aproximado
    vTaskDelay(300/portTICK_PERIOD_MS);
  }
}


        ///////////////////////////////////
        ///  TASK DE CARGA DA BATERIA   ///
        ///////////////////////////////////


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
          vTaskDelay(500 / portTICK_PERIOD_MS);
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
            float VdivAtual = readVBat() - 0.49; // Lê a tensão da bateria e desconta a tensão em cima do diodo
            if (VdivAtual < 0) VdivAtual = 0;
            Serial.printf("Medição realizada! Tensão atual da bateria: %.2f V\n", VdivAtual);

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


void TaskModoAula(void *pv){ // Responsável por liberar a porta a partir do sensor de proximidade ou manter a porta trancada
  while (1){
  
  if(mutexModoAula && xSemaphoreTake(mutexModoAula, portMAX_DELAY)){
    if(modoAula) mededistancia();
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

  httpBackup.begin(consultaListaUsuarios); // Pega as colunas de crachás e usuários do banco e envia como json
  int httpResponse = httpBackup.GET(); // Recebe o status da requisição

  if (httpResponse == 200){ // Se a requisição retornar o status 200, significa que a requisição deu certo
    String payloadBanco = httpBackup.getString(); // Pega o JSON do banco que foi enviado pela API
    String payloadLocal = readJson(); // Pega o JSON da memória do ESP

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

void mededistancia(){ // Lê a distância a partir do sensor VL53L0X
  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);
  int medida = measure.RangeMilliMeter/10;

  if (medida < 10){ // Se alguém passar a mão na frente, magnetiza e libera a porta 
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

  bip(1);
}

void desmagnetizaPorta(){
  digitalWrite(19, LOW);  // LED verde
  digitalWrite(18, HIGH); // LED vermelho
  digitalWrite(27, LOW);
  digitalWrite(LED_PIN, HIGH);
  digitalWrite(32, LOW); // buzzer
}

void bip(int repeticoes) { // Responsaável por fazer os bips no buzzer
  unsigned long tempoInicio = millis();

  for (int cont = 0; cont < repeticoes; cont++) {
    unsigned long t0 = millis();

    // Bip de 100 ms
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




// MEXER NA FUNÇÃO ABAIXO



void newRegistration(){

  HTTPClient httpNovoRegistro;
  HTTPClient httpPostNovoRegistro;

  httpNovoRegistro.begin(solicitacaoCadastro); // Faz a requisição http GET para a api responsável por verificar se há algum cadastro pendente
  int httpResponse = httpNovoRegistro.GET(); 
  //Serial.println(httpResponse);
  
  if (httpResponse == 200){ // Se a resposta do GET for igual a 200, significa que ocorreu tudo certo com a requisição
    
    String payload = httpNovoRegistro.getString(); // Recebe o JSON que foi enviado pela API

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

          httpPostNovoRegistro.begin(atualizaDB); // Faz a requisição para outra API para atualizar o banco de dados com as informações do usuário e da TAG

          httpPostNovoRegistro.addHeader("Content-Type", "application/x-www-form-urlencoded"); // aponta qual vai ser o método de envio das informações por meio da URL

          // Construção da URL de resposta
          String postData = "id_solicitacao=";
          postData += String(id_solicitante);
          postData += "&cracha=";
          postData += tagNova;

          int post = httpPostNovoRegistro.POST(postData); // Pegando a resposta da requisição POST
          String payloadPost = httpPostNovoRegistro.getString();
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
  httpNovoRegistro.end();
}


void verificaEstadoPorta(){
  
  HTTPClient httpEstadoPorta;
  
  if (WiFi.status() == WL_CONNECTED){
    if (flagEstadoPorta){
    
      httpEstadoPorta.begin(atualizaEstadoPorta); // Chama a API que atualiza o estado da porta para que o valor presente no esp e no banco seja igual
      httpEstadoPorta.addHeader("Content-Type", "application/x-www-form-urlencoded");

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

      flagEstadoPorta = 0;
    }
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
            if (modoAulaAtual){ // Habilita o modo aula
              habilitaModoAula("aguarde");
              Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");
              modoAula = modoAulaAtual;
            } else if (modoAulaAtual == 0){ // Desabilita o modo aula 
              desabilitaModoAula();
              Serial.println("MODO aula DESATIVADO!");
              modoAula = modoAulaAtual;
            }
          } 
        }
      }  
    } else Serial.println("Teste ok AtualizaModoAula"); xSemaphoreGive(mutexModoAula);
    
    httpModoAula.end();
  } 
}





// MEXER NA FUNÇÃO ABAIXO





void leiaCracha () {

  HTTPClient httpLeiaCracha;
  uint32_t tag = rdm.get_new_tag_id();

  if (WiFi.status() == WL_CONNECTED){ // Se a internet estiver conectada, busca o banco de dados para a verificação
    if (tag = rdm.get_new_tag_id()) {

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

          if (autorizado && modoAula == 0){ // Se for autorizado e o novo modo aula for desligado
            desabilitaModoAula(); // Desabilita o modo aula 
            Serial.println("MODO AULA DESATIVADO!");
          } else if (autorizado && modoAula){ // Se for autorizado e o novo modo aula for ligado
            habilitaModoAula(String(tag)); // Habilita o modo aula
            Serial.println("ACESSO LIBERADO! MODO AULA ATIVADO!");
          } else if (!autorizado){ // Se não for autorizado, rejeita o pedido 
            Serial.println("ACESSO NEGADO!");
          }
        } 
      }
      httpLeiaCracha.end();
    } else Serial.println("Teste ok LeiaCracha");
  } else { // Se a internet não estiver conectada, busca a memória local para verificar permissões de crachás
    if (leiaCrachaBackup(String(tag))){ // Se o crachá estiver na memória, permite o acesso, se não, não
      Serial.println("Acesso liberado.");
    } else Serial.println("Acesso negado");
  }
}

void habilitaModoAula(String crachaLido) { // Função que habilita o modo aula e atualiza o histórico

  HTTPClient httpHabilita;

  if (crachaLido != "aguarde") {
    httpHabilita.begin(enviaHistorico);
    httpHabilita.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "cracha=" + crachaLido + "&lab=" + String(lab) + "&acao=on";
    int httpResponse = httpHabilita.POST(postData); // Envia os dados necessários para criar uma nova linha na tabela de histórico
    Serial.println("Registrado novo evento!");

    // Prints de debug
    /*
    String payloadPost = httpHabilita.getString();

    
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

  httpHabilita.end();

  digitalWrite(LED_PIN, HIGH); // LED branco
  magnetizaPorta();
  desmagnetizaPorta();
}