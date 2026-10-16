// Stillpoint Trigger Box — programa do ESP32
//
// Dispara câmeras pelo cabo da entrada de controle remoto (padrão Canon RS-60E3/E3:
// ponta = disparo, anel = foco, corpo = comum). Cada canal é um optoacoplador PC817 que
// fecha ponta+anel contra o corpo (= apertar o botão até o fundo).
//
// Canais:
//   1 e 2       -> ligados direto ao ESP32 (GPIO 25 e 26), para o protótipo
//   3 em diante -> placas MCP23017 (16 canais cada) no barramento I2C, endereços
//                  0x20, 0x21, ... 0x27 (até 8 placas = 128 canais)
//
// Fala com o computador pela porta USB serial, 115200, uma ordem por linha:
//   PING                 -> OK STILLPOINT-TRIGGER 1 CH=<canais> BOARDS=<placas>
//   PULSE <ms>           -> duração do "apertar" (padrão 100 ms)
//   FIRE <canais>        -> dispara agora. Ex.: FIRE 1,2,5-8   ou   FIRE ALL
//   PLAN <ch>:<ms>,...   -> agenda. Ex.: PLAN 1-6:0,7-12:1000   (ms = atraso após GO)
//   GO                   -> executa o que foi agendado; responde DONE no fim
//   CLEAR                -> apaga o agendado
//   STOP                 -> interrompe o que está rodando e solta todos os canais
//   TEST <ch>            -> dispara um canal (para descobrir qual câmera é qual)
// Respostas: OK ..., ERR <motivo>, DONE.
//
// Bibliotecas (Arduino IDE > Library Manager): "Adafruit MCP23017 Arduino Library"
// (traz a Adafruit BusIO). Placa: "ESP32 Dev Module" (pacote "esp32" da Espressif).

#include <Wire.h>
#include <Adafruit_MCP23X17.h>

static const int kDirectPins[] = { 25, 26 };          // canais 1 e 2
static const int kDirectCount = sizeof(kDirectPins) / sizeof(kDirectPins[0]);
static const int kMaxBoards = 8;
static const int kMaxChannels = kDirectCount + kMaxBoards * 16;
static const int kMaxEvents = 512;

Adafruit_MCP23X17 boards[kMaxBoards];
bool boardFound[kMaxBoards];
int boardCount = 0;
int channelCount = kDirectCount;
unsigned long pulseMs = 100;

struct Event { int channel; unsigned long atMs; bool on; bool done; };
Event plan[kMaxEvents];
int planCount = 0;
bool running = false;
unsigned long startMs = 0;
unsigned long offAt[kMaxChannels + 1];  // quando soltar cada canal (0 = solto)

void setChannel(int ch, bool on)
{
  if (ch < 1 || ch > channelCount) return;
  if (ch <= kDirectCount) {
    digitalWrite(kDirectPins[ch - 1], on ? HIGH : LOW);
    return;
  }
  const int idx = ch - kDirectCount - 1;
  const int board = idx / 16, pin = idx % 16;
  if (board < kMaxBoards && boardFound[board])
    boards[board].digitalWrite(pin, on ? HIGH : LOW);
}

void releaseAll()
{
  for (int ch = 1; ch <= channelCount; ++ch) { setChannel(ch, false); offAt[ch] = 0; }
}

// "1,2,5-8" ou "ALL" -> chama f(canal) para cada canal
template <typename F> bool forChannels(const String& list, F f)
{
  if (list.equalsIgnoreCase("ALL")) { for (int ch = 1; ch <= channelCount; ++ch) f(ch); return true; }
  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String part = list.substring(start, comma); part.trim();
    int dash = part.indexOf('-');
    int a = part.toInt(), b = a;
    if (dash > 0) b = part.substring(dash + 1).toInt();
    if (a < 1 || b < a || b > channelCount) return false;
    for (int ch = a; ch <= b; ++ch) f(ch);
    start = comma + 1;
  }
  return true;
}

void fireNow(const String& list)
{
  const unsigned long now = millis();
  bool ok = forChannels(list, [&](int ch) { setChannel(ch, true); offAt[ch] = now + pulseMs; });
  Serial.println(ok ? "OK" : "ERR channel");
}

bool addPlan(const String& spec)
{
  // "1-6:0,7-12:1000"
  int start = 0;
  while (start < (int)spec.length()) {
    int comma = spec.indexOf(',', start);
    if (comma < 0) comma = spec.length();
    String part = spec.substring(start, comma); part.trim();
    int colon = part.indexOf(':');
    if (colon < 0) return false;
    String chans = part.substring(0, colon);
    unsigned long at = part.substring(colon + 1).toInt();
    bool ok = forChannels(chans, [&](int ch) {
      if (planCount < kMaxEvents) plan[planCount++] = { ch, at, true, false };
    });
    if (!ok) return false;
    start = comma + 1;
  }
  return true;
}

void handle(String line)
{
  line.trim();
  if (line.length() == 0) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  cmd.toUpperCase(); arg.trim();

  if (cmd == "PING") {
    Serial.print("OK STILLPOINT-TRIGGER 1 CH="); Serial.print(channelCount);
    Serial.print(" BOARDS="); Serial.println(boardCount);
  } else if (cmd == "PULSE") {
    long v = arg.toInt();
    if (v < 20 || v > 2000) { Serial.println("ERR pulse 20-2000"); return; }
    pulseMs = v; Serial.println("OK");
  } else if (cmd == "FIRE") {
    if (running) { Serial.println("ERR running"); return; }
    fireNow(arg);
  } else if (cmd == "TEST") {
    if (running) { Serial.println("ERR running"); return; }
    fireNow(arg);
  } else if (cmd == "PLAN") {
    if (running) { Serial.println("ERR running"); return; }
    if (!addPlan(arg)) { Serial.println("ERR plan"); return; }
    Serial.print("OK "); Serial.println(planCount);
  } else if (cmd == "CLEAR") {
    planCount = 0; Serial.println("OK");
  } else if (cmd == "GO") {
    if (planCount == 0) { Serial.println("ERR empty"); return; }
    for (int i = 0; i < planCount; ++i) plan[i].done = false;
    startMs = millis(); running = true; Serial.println("OK");
  } else if (cmd == "STOP") {
    running = false; planCount = 0; releaseAll(); Serial.println("OK");
  } else {
    Serial.println("ERR unknown");
  }
}

void setup()
{
  Serial.begin(115200);
  for (int i = 0; i < kDirectCount; ++i) { pinMode(kDirectPins[i], OUTPUT); digitalWrite(kDirectPins[i], LOW); }
  Wire.begin();  // SDA = GPIO 21, SCL = GPIO 22
  for (int b = 0; b < kMaxBoards; ++b) {
    boardFound[b] = boards[b].begin_I2C(0x20 + b, &Wire);
    if (boardFound[b]) {
      for (int p = 0; p < 16; ++p) { boards[b].pinMode(p, OUTPUT); boards[b].digitalWrite(p, LOW); }
      boardCount = b + 1;  // placas contíguas a partir de 0x20
    }
  }
  channelCount = kDirectCount + boardCount * 16;
  for (int ch = 0; ch <= kMaxChannels; ++ch) offAt[ch] = 0;
  Serial.print("READY CH="); Serial.println(channelCount);
}

String input;

void loop()
{
  // Ordens do computador
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') { handle(input); input = ""; }
    else if (input.length() < 2000) input += c;
  }

  const unsigned long now = millis();

  // Agenda em andamento: liga cada canal na hora certa
  if (running) {
    bool pending = false;
    for (int i = 0; i < planCount; ++i) {
      if (plan[i].done) continue;
      if (now - startMs >= plan[i].atMs) {
        setChannel(plan[i].channel, true);
        offAt[plan[i].channel] = now + pulseMs;
        plan[i].done = true;
      } else {
        pending = true;
      }
    }
    if (!pending) { running = false; planCount = 0; Serial.println("DONE"); }
  }

  // Solta os canais cujo pulso terminou
  for (int ch = 1; ch <= channelCount; ++ch)
    if (offAt[ch] && now >= offAt[ch]) { setChannel(ch, false); offAt[ch] = 0; }
}
