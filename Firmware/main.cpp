/*
 * =====================================================================
 * ROBÔ LARI — SEGUIDOR DE LINHA COMPETIÇÃO  v5.4 (OTIMIZADO)
 * MCU:     NodeMCU ESP32-WROOM-32
 * Driver:  TB6612FNG
 * Sensor:  QTR-8A (analógico, 8 sensores)
 * Motor:   N20 6V 1000rpm
 * Bat.:    2x 18650 Li-ion série → 7.4V nominal
 * =====================================================================
 */

#include <Arduino.h>
#include <QTRSensors.h>

// Descomente a linha abaixo se quiser ajustar via Monitor Serial
// #define MODO_DEBUG

// ====================================================================
// 1. PINOS
// ====================================================================
#define NUM_SENSORS 8

// Atenção: Pinos 34, 35, 36 e 39 no ESP32 são apenas entrada (ADC1) e ideais para o QTR
const uint8_t QTR_PINS[NUM_SENSORS] = {32, 33, 34, 35, 36, 39, 25, 26};
#define PIN_LEDON 13

#define PIN_AIN1 19
#define PIN_AIN2 21
#define PIN_PWMA 18
#define PIN_BIN1 22
#define PIN_BIN2 27
#define PIN_PWMB 23
#define PIN_STBY 14

// ====================================================================
// 2. PWM (Configuração Nativa ESP32)
// ====================================================================
const int PWM_FREQ    = 5000;
const int PWM_RES     = 8;
const int CH_ESQUERDO = 0;
const int CH_DIREITO  = 1;

// ====================================================================
// 3. VELOCIDADES
// ====================================================================
const int PWM_MAX         = 255;   // Aumentado um pouco para dar mais torque
const int VELOCIDADE_BASE = 210;   // Reduzido levemente para dar tempo de corrigir na curva
const int VEL_MIN_CURVA   = -130;  // Permite contra-rotação firme para travar a traseira na curva

int leftTrim  = 0;
int rightTrim = 0;

// ====================================================================
// 4. PID OTIMIZADO PARA EVITAR RUIDO DO ADC DO ESP32
// ====================================================================
float Kp = 0.075f;   // Atualizado para a pista 2026.1 (curvas fechadas)
float Ki = 0.0000f;  // Zerado (Ki em seguidor de linha gera atraso de resposta nas curvas)
float Kd = 0.50f;    // Atualizado para antecipar entrada de hairpin

float ultimoErro = 0.0f;
float integral   = 0.0f;
float erroDerivativoFiltrado = 0.0f; // Filtro contra ruído do ADC

const float INTEGRAL_MAX  = 50.0f;
const int   POSICAO_ALVO  = 3500;
const int   LINHA_PERDIDA_LIMIAR = 250; 

// ====================================================================
// 5. LIMIARES DE ERRO
// ====================================================================
const int ERRO_PEQUENO = 400;
const int ERRO_MEDIO   = 1200;
const int ERRO_GRANDE  = 2500;

// ====================================================================
// 6. RECUPERAÇÃO DE LINHA
// ====================================================================
float         ultimaDirecao     = 0.0f;
bool          linhaPerdida      = false;
unsigned long tempoSemLinha     = 0;
const unsigned long MAX_TEMPO_RECUPERACAO = 400;

// ====================================================================
// 7. OBJETOS
// ====================================================================
QTRSensors qtr;
uint16_t   valoresSensores[NUM_SENSORS];

// PROTÓTIPOS
void controlarMotores(int esquerdo, int direito);
void pararMotores();
bool linhaDetectada();
void recuperarLinha();
void calibrar();
void handleSerialCommands();

// ====================================================================
// 8. SETUP
// ====================================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n========================================");
  Serial.println("   ROBÔ LARI — SEGUIDOR DE LINHA v5.4");
  Serial.println("========================================");

  // Pinos dos motores
  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_STBY, OUTPUT);

  pararMotores();
  digitalWrite(PIN_STBY, LOW); // Mantém ponte H desligada no início

  // PWM setup
  ledcSetup(CH_ESQUERDO, PWM_FREQ, PWM_RES);
  ledcSetup(CH_DIREITO,  PWM_FREQ, PWM_RES);
  ledcAttachPin(PIN_PWMA, CH_ESQUERDO);
  ledcAttachPin(PIN_PWMB, CH_DIREITO);
  ledcWrite(CH_ESQUERDO, 0);
  ledcWrite(CH_DIREITO,  0);

  // Configuração do QTR-8A
  qtr.setTypeAnalog();
  qtr.setSensorPins(QTR_PINS, NUM_SENSORS);
  qtr.setEmitterPin(PIN_LEDON);
    qtr.setSamplesPerSensor(8); 

  // Calibração
  calibrar();

  // Inicializa variáveis do PID
  ultimoErro = 0.0f;
  integral   = 0.0f;
  erroDerivativoFiltrado = 0.0f;

  // Libera a ponte H
  digitalWrite(PIN_STBY, HIGH);

  Serial.println("\n>>> Pronto para o combate! Ativando motores...");
  delay(500); 
}

// ====================================================================
// 9. CALIBRAÇÃO OTIMIZADA (Evita o travamento inicial)
// ====================================================================
void calibrar() {
  Serial.println("\n>>> PREPARANDO CALIBRAÇÃO...");
  Serial.println(">>> ATENÇÃO: Assim que o LED do robô piscar, mova-o lateralmente sobre a linha!");
  
  // Pisca o LED indicador para avisar o início imediato
  pinMode(LED_BUILTIN, OUTPUT);
  for(int i=0; i<6; i++){
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    delay(200);
  }

  Serial.println(">>> CALIBRANDO AGORA... MOVA O ROBÔ!");

  // Reduzido para 250 leituras rápidas para evitar estouro de leitura estática do ADC
   for (uint16_t i = 0; i < 400; i++)  {
    qtr.calibrate();
    delay(8);
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.println(">>> Calibração concluída com sucesso!");
}

// ====================================================================
// 10. LOOP PRINCIPAL
// ====================================================================
void loop() {
  uint16_t posicao = qtr.readLineBlack(valoresSensores);

  // Validação de linha perdida
  if (!linhaDetectada()) {
    if (!linhaPerdida) {
      linhaPerdida  = true;
      tempoSemLinha = millis();
    }
    recuperarLinha();
    return;
  }
  linhaPerdida = false;

  // Detecção de cruzamento (vários sensores ativos = pista cruzando)
  int sensoresAtivos = 0;
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    if (valoresSensores[i] > 500) sensoresAtivos++;
  }
  bool cruzamento = (sensoresAtivos >= 6);

  // Cálculo do Erro (-3500 a +3500)
  float erro = (float)posicao - POSICAO_ALVO;

  // PID — Termo Proporcional
  float P = erro;

  // PID — Termo Integral com trava estrita
  integral += erro;
  integral  = constrain(integral, -INTEGRAL_MAX, INTEGRAL_MAX);
  float I   = integral;

  // PID — Termo Derivativo com Filtro Passa-Baixas (Atenua o ruído do ADC do ESP32)
  float D_atual = erro - ultimoErro;
  erroDerivativoFiltrado = (0.6f * erroDerivativoFiltrado) + (0.4f * D_atual); 
  ultimoErro = erro;

  float sinalControle = (Kp * P) + (Ki * I) + (Kd * erroDerivativoFiltrado);

  // Em cruzamento: ignora correção e segue reto
  if (cruzamento) {
    sinalControle = 0;
  }

  ultimaDirecao = sinalControle;

  // Lógica de Velocidade Dinâmica Agressiva
  int velBase;
  float absErro = abs(erro);
  
  if (absErro < ERRO_PEQUENO) {
    velBase = VELOCIDADE_BASE + 30;           // Reta pura: Dispara o robô
  } else if (absErro < ERRO_MEDIO) {
    velBase = VELOCIDADE_BASE;                // Desvio leve: Mantém a velocidade
  } else if (absErro < ERRO_GRANDE) {
    velBase = (int)(VELOCIDADE_BASE * 0.70f); // Curva média: Reduz 30% da velocidade base
  } else {
    velBase = (int)(VELOCIDADE_BASE * 0.45f); // Curva fechada: Reduz 55% para dar torque de giro
  }

  // Aplicação do sinal nos motores
  int velEsq = velBase + (int)sinalControle + leftTrim;
  int velDir = velBase - (int)sinalControle + rightTrim;

  // Anti-saturação preservando o diferencial (curva fechada mantém autoridade)
  int excessoCima = max(velEsq, velDir) - PWM_MAX;
  if (excessoCima > 0) { velEsq -= excessoCima; velDir -= excessoCima; }
  int excessoBaixo = VEL_MIN_CURVA - min(velEsq, velDir);
  if (excessoBaixo > 0) { velEsq += excessoBaixo; velDir += excessoBaixo; }

  // Clamp final de segurança
  velEsq = constrain(velEsq, VEL_MIN_CURVA, PWM_MAX);
  velDir = constrain(velDir, VEL_MIN_CURVA, PWM_MAX);


  controlarMotores(velEsq, velDir);

#ifdef MODO_DEBUG
  static unsigned long ultimoDebug = 0;
  if (millis() - ultimoDebug > 100) {
    ultimoDebug = millis();
    Serial.print("Pos:"); Serial.print(posicao);
    Serial.print(" Err:"); Serial.print((int)erro);
    Serial.print(" SC:"); Serial.print(sinalControle, 1);
    Serial.print(" L:"); Serial.print(velEsq);
    Serial.print(" R:"); Serial.println(velDir);
  }
  handleSerialCommands();
#endif
}

// ====================================================================
// 11. DETECÇÃO DE LINHA
// ====================================================================
bool linhaDetectada() {
  for (uint8_t i = 0; i < NUM_SENSORS; i++) {
    if (valoresSensores[i] > LINHA_PERDIDA_LIMIAR) return true;
  }
  return false;
}

// ====================================================================
// 12. RECUPERAÇÃO DE LINHA EM CASO DE ESCAPE
// ====================================================================
void recuperarLinha() {
  if (millis() - tempoSemLinha > MAX_TEMPO_RECUPERACAO) {
    pararMotores();
    return;
  }

  int velGiro = 140; // Aumentado para o N20 vencer a inércia rapidamente
  if (ultimaDirecao > 0) {
    controlarMotores(velGiro, -velGiro); // Gira no próprio eixo para a direita
  } else {
    controlarMotores(-velGiro, velGiro); // Gira no próprio eixo para a esquerda
  }
}

// ====================================================================
// 13. CONTROLE DOS MOTORES (Correção de Direção / Pontes H)
// ====================================================================
void controlarMotores(int esquerdo, int direito) {
  // Motor Esquerdo (Canal A)
  if (esquerdo >= 0) {
    digitalWrite(PIN_AIN1, LOW);
    digitalWrite(PIN_AIN2, HIGH);
    ledcWrite(CH_ESQUERDO, (uint32_t)esquerdo);
  } else {
    digitalWrite(PIN_AIN1, HIGH);
    digitalWrite(PIN_AIN2, LOW);
    ledcWrite(CH_ESQUERDO, (uint32_t)abs(esquerdo));
  }
  
  // Motor Direito (Canal B)
  if (direito >= 0) {
    digitalWrite(PIN_BIN1, HIGH);
    digitalWrite(PIN_BIN2, LOW);
    ledcWrite(CH_DIREITO, (uint32_t)direito);
  } else {
    digitalWrite(PIN_BIN1, LOW);
    digitalWrite(PIN_BIN2, HIGH);
    ledcWrite(CH_DIREITO, (uint32_t)abs(direito));
  }
}

void pararMotores() {
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);
  ledcWrite(CH_ESQUERDO, 0);
  ledcWrite(CH_DIREITO,  0);
}

// ====================================================================
// 14. AJUSTE PID POR SERIAL
// ====================================================================
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if      (cmd.startsWith("Kp")) { Kp = cmd.substring(2).toFloat(); }
  else if (cmd.startsWith("Ki")) { Ki = cmd.substring(2).toFloat(); }
  else if (cmd.startsWith("Kd")) { Kd = cmd.substring(2).toFloat(); }
}