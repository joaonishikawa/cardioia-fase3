/*
 * CardioIA - Fase 3: monitor cardíaco vestível (simulado no Wokwi)
 * Integrantes: Gustavo Trindade Soares (RM567848)
 *              João Pedro Nishikawa Alves (RM562376)
 *
 * Fluxo: captura (DHT22 + botão de batimento) -> buffer local -> envio FIFO
 *        por MQTT e Serial -> remoção da amostra enviada.
 *
 * O buffer usa RAM porque, nos simuladores, o SPIFFS é volátil (ver enunciado).
 * Toda leitura também aparece no Monitor Serial, a alternativa de resiliência
 * aceita pela atividade.
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// Pinos
const int PIN_DHT = 15;
const int PIN_BATIMENTO = 4;
const int PIN_CONEXAO = 5;
const int PIN_LED_ALERTA = 2;

// Rede. Broker público: use apenas dados fictícios, pois qualquer pessoa pode ler o tópico.
const char *WIFI_SSID = "Wokwi-GUEST";
const char *WIFI_SENHA = "";
const char *MQTT_BROKER = "broker.hivemq.com";
const int MQTT_PORTA = 1883;
const char *MQTT_TOPICO = "cardioia/fase3-7f3a/esp32-01/vitais";

// Temporização
const unsigned long LEITURA_MS = 5000;        // uma amostra a cada 5 s
const unsigned long ENVIO_MS = 50;            // até 20 envios/s, para esvaziar o buffer sem travar o loop
const unsigned long RECONEXAO_MS = 5000;
const unsigned long DEBOUNCE_MS = 200;        // limita a leitura a 300 bpm
const unsigned long SEM_BATIMENTO_MS = 6000;  // sem toque por 6 s: bpm = 0 ("sem leitura")

// Limites clínicos definidos pelo grupo
const float TEMP_MAX = 38.0;
const int BPM_MAX = 120;
const int BPM_MIN = 50;

// Estratégia de armazenamento limitado: 4.000 amostras x 5 s = cerca de 5 h 33 min offline,
// em 64 KB de RAM (5.000 não cabem na memória estática do ESP32).
// Cheio, descarta a mais antiga, pois a leitura recente vale mais na clínica.
const int CAPACIDADE = 4000;

struct Amostra {
  uint32_t instanteMs;
  uint32_t seq;
  int16_t tempDecimos;  // décimos de grau: 365 = 36,5 °C; economiza RAM em relação a float
  int16_t umidDecimos;
  uint16_t bpm;
};

Amostra buffer[CAPACIDADE];
int inicio = 0;
int total = 0;
uint32_t proximaSeq = 0;
uint32_t descartadas = 0;

// Batimentos: a interrupção não perde toques enquanto o loop tenta reconectar
const int N_BATIDAS = 6;
volatile unsigned long batidas[N_BATIDAS];
volatile int batidasContadas = 0;

DHT dht(PIN_DHT, DHT22);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long ultimaLeitura = 0;
unsigned long ultimoEnvio = 0;
unsigned long ultimaReconexao = 0;
bool sincronizando = false;

// Conectividade simulada pedida no enunciado: a chave deslizante define esta variável
bool wifiConectado = false;

void IRAM_ATTR aoBater() {
  unsigned long agora = millis();
  int ultima = (batidasContadas + N_BATIDAS - 1) % N_BATIDAS;
  if (batidasContadas > 0 && agora - batidas[ultima] < DEBOUNCE_MS) return;
  batidas[batidasContadas % N_BATIDAS] = agora;
  // Mantém o índice circular sem perder a informação de que já houve N batidas
  batidasContadas = batidasContadas >= 2 * N_BATIDAS - 1 ? N_BATIDAS : batidasContadas + 1;
}

// Média dos intervalos entre os últimos toques. Uma pausa longa encerra a janela,
// para que toques antigos não distorçam o resultado.
int calcularBpm() {
  noInterrupts();
  int contadas = batidasContadas;
  unsigned long copia[N_BATIDAS];
  for (int i = 0; i < N_BATIDAS; i++) copia[i] = batidas[i];
  interrupts();

  int n = min(contadas, N_BATIDAS);
  if (n < 2) return 0;
  int indiceUltima = (contadas + N_BATIDAS - 1) % N_BATIDAS;
  unsigned long ultima = copia[indiceUltima];
  if (millis() - ultima > SEM_BATIMENTO_MS) return 0;

  unsigned long primeira = ultima;
  int intervalos = 0;
  for (int k = 1; k < n; k++) {
    unsigned long anterior = copia[(indiceUltima + N_BATIDAS - k) % N_BATIDAS];
    if (primeira - anterior > SEM_BATIMENTO_MS) break;
    primeira = anterior;
    intervalos++;
  }
  return intervalos == 0 ? 0 : 60000UL * intervalos / (ultima - primeira);
}

void guardar(const Amostra &a) {
  if (total == CAPACIDADE) {
    inicio = (inicio + 1) % CAPACIDADE;
    total--;
    descartadas++;
  }
  buffer[(inicio + total) % CAPACIDADE] = a;
  total++;
}

void removerMaisAntiga() {
  inicio = (inicio + 1) % CAPACIDADE;
  total--;
}

void lerSensores() {
  float temp = dht.readTemperature();
  float umid = dht.readHumidity();
  if (isnan(temp) || isnan(umid)) {
    Serial.println("[ERRO] Falha na leitura do DHT22; amostra ignorada");
    return;
  }

  Amostra a = {millis(), proximaSeq++, (int16_t)lround(temp * 10), (int16_t)lround(umid * 10),
               (uint16_t)calcularBpm()};
  guardar(a);

  // Alerta na borda: o LED funciona mesmo sem internet
  bool alerta = temp > TEMP_MAX || a.bpm > BPM_MAX || (a.bpm > 0 && a.bpm < BPM_MIN);
  digitalWrite(PIN_LED_ALERTA, alerta);

  Serial.printf("[LEITURA] seq=%lu temp=%.1fC umid=%.1f%% bpm=%u alerta=%s pendentes=%d descartadas=%lu\n",
                (unsigned long)a.seq, temp, umid, a.bpm, alerta ? "SIM" : "nao", total,
                (unsigned long)descartadas);
}

// Mantém Wi-Fi e MQTT conectados conforme a variável de conectividade simulada
void atualizarConexao() {
  bool estado = digitalRead(PIN_CONEXAO) == LOW;
  if (estado != wifiConectado) {
    wifiConectado = estado;
    Serial.println(wifiConectado ? "[REDE] Online: sincronização liberada"
                                 : "[REDE] Offline: guardando amostras localmente");
    if (!wifiConectado) mqtt.disconnect();
  }
  if (!wifiConectado || WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  unsigned long agora = millis();
  if (agora - ultimaReconexao < RECONEXAO_MS) return;
  ultimaReconexao = agora;

  String clientId = "cardioia-esp32-" + String(esp_random(), HEX);
  if (mqtt.connect(clientId.c_str())) {
    Serial.println("[MQTT] Conectado ao broker");
  } else {
    Serial.printf("[MQTT] Falha na conexão, estado=%d\n", mqtt.state());
  }
}

// Só remove a amostra depois que o publish é aceito; se falhar, ela continua no buffer
void enviarMaisAntiga() {
  const Amostra &a = buffer[inicio];
  char json[128];
  snprintf(json, sizeof(json),
           "{\"dispositivo\":\"esp32-01\",\"seq\":%lu,\"idade_s\":%lu,\"temp\":%.1f,\"umid\":%.1f,"
           "\"bpm\":%u,\"pendentes\":%d}",
           (unsigned long)a.seq, (unsigned long)((millis() - a.instanteMs) / 1000),
           a.tempDecimos / 10.0, a.umidDecimos / 10.0, a.bpm, total - 1);

  if (!mqtt.publish(MQTT_TOPICO, json)) {
    Serial.println("[MQTT] Publicação recusada; amostra mantida no buffer");
    return;
  }
  Serial.print("[NUVEM] ");
  Serial.println(json);
  removerMaisAntiga();
}

void sincronizar() {
  if (total == 0) {
    if (sincronizando) Serial.println("[SYNC] Buffer vazio: sincronização concluída");
    sincronizando = false;
    return;
  }
  if (!wifiConectado || !mqtt.connected() || millis() - ultimoEnvio < ENVIO_MS) return;
  if (!sincronizando && total > 1) {
    Serial.printf("[SYNC] Enviando %d amostras pendentes\n", total);
    sincronizando = true;
  }
  ultimoEnvio = millis();
  enviarMaisAntiga();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BATIMENTO, INPUT_PULLUP);
  pinMode(PIN_CONEXAO, INPUT_PULLUP);
  pinMode(PIN_LED_ALERTA, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_BATIMENTO), aoBater, FALLING);

  dht.begin();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);  // não bloqueia: a coleta começa mesmo sem rede
  mqtt.setServer(MQTT_BROKER, MQTT_PORTA);
  mqtt.setSocketTimeout(2);

  Serial.printf("[INICIO] CardioIA pronto. Capacidade: %d amostras\n", CAPACIDADE);
}

void loop() {
  atualizarConexao();
  mqtt.loop();

  if (millis() - ultimaLeitura >= LEITURA_MS) {
    ultimaLeitura = millis();
    lerSensores();
  }
  sincronizar();
  delay(2);
}
