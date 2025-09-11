#include "Adafruit_VL53L0X.h"
#include <rdm6300.h>
#include <Wire.h>

unsigned long tempoAnterior = 0;
int piscarAtivo = 0;

#define RDM_RX 5 
#define RDM_TX 4
#define LED_PIN 19
#define Buzzer 32
#define botao 35
int altera_estado = 0;

Adafruit_VL53L0X lox = Adafruit_VL53L0X();
HardwareSerial RFIDserial(1);
Rdm6300 rdm;

void setup() {
  Serial.begin(115200);
  RFIDserial.begin(9600, SERIAL_8N1, RDM_RX, RDM_TX);
  rdm.begin(&RFIDserial);
  pinMode(27, OUTPUT);
  pinMode(19, OUTPUT);
  pinMode(18, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(33, OUTPUT);
  pinMode(32, OUTPUT);
  pinMode(botao, INPUT_PULLDOWN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  while (! Serial) {
    delay(1);
  }
  
  if (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    while(1);
  }
}

void loop() {
  tagrfid();
  mededistancia();
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

