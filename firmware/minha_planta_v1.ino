/*
  Minha Planta - v1
  Placa: ESP32-C3 SuperMini   (Ferramentas -> USB CDC On Boot -> Enabled)

  Wi-Fi (sem rede e senha no codigo):
    - Sem Wi-Fi salvo, ou se nao conseguir entrar no Wi-Fi salvo ao ligar, abre a rede "MINHA_PLANTA" (sem senha)
      e tenta de novo a rede salva a cada 2 minutos.
    - Conecte nela: a pagina de configuracao abre sozinha (ou http://192.168.4.1).
    - Escolha a rede da casa, digite a senha. Conectou: o site fica em http://minhaplanta.local
    - Se o Wi-Fi cair com ela ja ligada, o ESP32 tenta reconectar sozinho.
    - Com ela ja ligada, segurar o botao BOOT por 5 s abre "MINHA_PLANTA".
    - Para trocar de rede: no site, icone (i) -> botao "Trocar Wi-Fi".

  Site:
    - Topo: rede e sinal do Wi-Fi, botao de tema claro/escuro (salvo em cada aparelho), "Online" e "Calibrar".
    - Plantas: Kalanchoe + ate 10 plantas criadas por voce ("+ Criar planta"), salvas no ESP32.
    - Sol de hoje: cada planta tem o seu; so a planta selecionada soma.
    - Calibrar: passo a passo do sensor de solo (ar e agua) e da temperatura (termometro).
      O ajuste de -5,5 C da temperatura (calor do ESP32 na caixa) e aplicado automaticamente.
    - Chave em cada card: desligada = nao monitorado (card apagado, sem alerta no site).
      Solo, temperatura e luz: o LED tambem ignora o sensor. Umidade do ar: so a tela
      (ela vem do mesmo sensor da temperatura; o LED segue a chave da temperatura).

  LED azul:
    aceso fixo .............. rede MINHA_PLANTA aberta (configuracao do Wi-Fi)
    pisca 1 vez por segundo . tudo ok
    2 piscadas a cada 3 s ... problema no sensor de solo        (so se ele estiver monitorado)
    3 piscadas a cada 3 s ... problema no sensor de temperatura (so se ele estiver monitorado)
    4 piscadas a cada 3 s ... problema no sensor de luz         (so se ele estiver monitorado)
    pisca bem rapido ........ conectando no Wi-Fi

  Ligacoes (placa base v3):
    Sensor de luz (BH1750) ........: VCC 3V3, GND GND, SCL GPIO5, SDA GPIO6
    Sensor de temperatura (AHT20) .: VIN GPIO21, GND GPIO20, SCL GPIO10, SDA GPIO7
    Sensor de solo ................: sinal GPIO4, 3V3, GND
  Nao precisa instalar biblioteca (tudo ja vem com o ESP32)
*/
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <time.h>

// Wi-Fi: rede e senha ficam salvas no ESP32 (configuradas pela pagina), nunca no codigo
const char* AP_NOME = "MINHA_PLANTA";      // rede propria da sua planta (sem senha)
const char* NOME_SITE = "minhaplanta";     // http://minhaplanta.local
String wifiNome = "", wifiSenha = "";
bool modoReserva = false;            // true = rede MINHA_PLANTA aberta (configuracao)
DNSServer dns;

#if !ARDUINO_USB_CDC_ON_BOOT
#error "Na placa v3 o sensor de temperatura usa o GPIO20 e o GPIO21. Ative: Ferramentas -> USB CDC On Boot -> Enabled"
#endif

// Placa v3: cada sensor tem os seus pinos
const int LUZ_SDA   = 6;          // sensor de luz (BH1750) - VCC no 3V3, GND no GND
const int LUZ_SCL   = 5;
const int TEMP_SDA  = 7;          // sensor de temperatura (AHT20)
const int TEMP_SCL  = 10;
const int TEMP_VCC  = 21;         // alimenta o sensor de temperatura (saida em 3,3 V)
const int TEMP_GND  = 20;         // terra do sensor de temperatura (saida em 0 V)
const int PINO_LED  = 8;          // LED azul da placa (acende com LOW)
const float AJUSTE_PADRAO = -5.5;  // correcao PADRAO da temperatura: -3,0 (sem caixa) mais -2,5 (calor do ESP32 na caixa)
float ajusteTemp = AJUSTE_PADRAO;  // correcao em uso (aplicada sempre; pode ser recalibrada na pagina)
const int PINO_BOOT = 9;          // botao BOOT da placa (apertado = LOW)
const int PINO_SOLO = 4;          // saida AOUT do sensor de solo (ADC1)
const int SOLO_SECO    = 2766;    // PADRAO: mV com o sensor no ar (sua medida)
const int SOLO_MOLHADO = 1350;    // PADRAO: mV com o sensor na agua (sua medida)
int soloSeco = SOLO_SECO, soloMolhado = SOLO_MOLHADO;   // em uso (podem ser recalibrados na pagina)
// sensor de solo com problema: leitura bem abaixo da agua (sensor solto: deu 969-1113 mV no teste) ou bem acima do ar
const int SOLO_FOLGA_AGUA = 200, SOLO_FOLGA_AR = 300;


const uint8_t END_AHT20 = 0x38;
uint8_t END_BH1750 = 0x23;        // 0x23 com ADDR solto; 0x5C se ADDR estiver em 3,3 V

bool temAHT = false, temBH = false;  // sensor encontrado e iniciado

// ultimas leituras (a pagina pede a cada 2 s)
bool  okA = false, okB = false;     // okA = temperatura (AHT20) leu certo; okB = luz (BH1750) leu certo
bool  okS = true;                    // sensor de solo sem problema (o problema "trava": ver lerSensores)
bool  soloFalha = false;             // problema no solo detectado e ainda nao resolvido
int   soloBoas = 0;                  // leituras normais seguidas desde o problema (10 = ~20 s para liberar)
// sensores monitorados (chave em cada card do site; salvo no ESP32)
bool  monSolo = true, monTemp = true, monLuz = true, monUmid = true;   // monUmid: so a tela (o LED usa monTemp)
float vTemp = 0, vUmid = 0, vLux = 0;
float vTempSensor = 0;               // temperatura lida no sensor, antes da correcao
int   vSoloMV = 0, vSoloPct = 0;

// luz acumulada no dia (DLI, em mol/m2/dia)
// lux -> PPFD da luz do sol: x 0,0185 (Apogee Instruments)
float dliHoje = 0, dliOntem = -1;     // -1 = ontem nao foi medido inteiro
int   diaAtual = -1, anoAtual = -1;   // dia de hoje (dia do ano e ano)
int   horaInicio = 24;                // hora em que comecou a medir hoje (antes das 6h = dia inteiro; 24 = dia incompleto)
String plantaLuz = "";                // planta dona do sol que esta sendo somado agora (so a selecionada soma)
unsigned long msUltimaLuz = 0, msUltimoSalvo = 0;

// temperatura dos ultimos 30 minutos (uma leitura por minuto)
float tempMin[30];
int   tempQtd = 0, tempPos = 0;
unsigned long msUltimaTemp = 0;

// guardarTemp: guarda uma temperatura por minuto (ultimos 30 min) para o aviso de "quente/frio demais"
void guardarTemp() {
  if (!okA) return;
  if (msUltimaTemp != 0 && millis() - msUltimaTemp < 60000UL) return;
  msUltimaTemp = millis();
  tempMin[tempPos] = vTemp;
  tempPos = (tempPos + 1) % 30;
  if (tempQtd < 30) tempQtd++;
}

WebServer server(80);
Preferences prefs;
String planta = "";             // tipo de planta escolhido na pagina (fica salvo)

// ---------- I2C: um par de pinos para cada sensor ----------
// O ESP32-C3 tem um so controlador I2C; ele troca de pinos antes de falar com cada sensor.
int busSDA = -1, busSCL = -1;
// usarBarramento: liga o I2C nos pinos do sensor que vai ser lido
void usarBarramento(int sda, int scl) {
  if (sda == busSDA && scl == busSCL) return;
  if (busSDA >= 0) Wire.end();
  Wire.begin(sda, scl);
  Wire.setTimeOut(50);
  busSDA = sda; busSCL = scl;
}
// usarLuz / usarTemp: atalhos para ligar o I2C nos pinos do sensor de luz ou de temperatura
void usarLuz()  { usarBarramento(LUZ_SDA, LUZ_SCL); }
void usarTemp() { usarBarramento(TEMP_SDA, TEMP_SCL); }

// liga (ou religa) a alimentacao do sensor de temperatura
void ligarTemp() {
  pinMode(TEMP_GND, OUTPUT); digitalWrite(TEMP_GND, LOW);
  pinMode(TEMP_VCC, OUTPUT); digitalWrite(TEMP_VCC, HIGH);
  delay(50);                                   // AHT20 pede ate 20 ms depois de ligar
}

// ---------- AHT20 ----------
// ahtIniciar: verifica se o AHT20 responde e, se precisar, manda o comando de iniciar
bool ahtIniciar() {
  usarTemp();
  delay(40);                                   // AHT20 pede 40 ms depois de ligar
  Wire.beginTransmission(END_AHT20);
  if (Wire.endTransmission() != 0) return false;
  Wire.requestFrom(END_AHT20, (uint8_t)1);     // le o status
  uint8_t st = Wire.available() ? Wire.read() : 0;
  if ((st & 0x08) == 0) {                      // nao calibrado: manda o comando de iniciar
    Wire.beginTransmission(END_AHT20);
    Wire.write(0xBE); Wire.write(0x08); Wire.write(0x00);
    Wire.endTransmission();
    delay(10);
  }
  return true;
}

// ahtLer: pede uma medida ao AHT20 e devolve temperatura (C) e umidade do ar (%), sem correcao
bool ahtLer(float &temp, float &umid) {
  usarTemp();
  Wire.beginTransmission(END_AHT20);
  Wire.write(0xAC); Wire.write(0x33); Wire.write(0x00);   // pede uma medida
  if (Wire.endTransmission() != 0) return false;
  delay(80);                                              // tempo de medida
  uint8_t d[6];
  for (int tent = 0; tent < 5; tent++) {
    if (Wire.requestFrom(END_AHT20, (uint8_t)6) != 6) return false;
    for (int i = 0; i < 6; i++) d[i] = Wire.read();
    if ((d[0] & 0x80) == 0) break;                        // pronto
    delay(20);
  }
  if (d[0] & 0x80) return false;
  uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
  uint32_t t  = ((uint32_t)(d[3] & 0x0F) << 16) | ((uint32_t)d[4] << 8) | d[5];
  umid = rh * 100.0f / 1048576.0f;
  temp = t * 200.0f / 1048576.0f - 50.0f;
  return true;
}

// ---------- BH1750 ----------
// bhIniciar: liga o BH1750 em modo continuo de alta resolucao
bool bhIniciar() {
  usarLuz();
  Wire.beginTransmission(END_BH1750);
  Wire.write(0x01);                            // liga
  if (Wire.endTransmission() != 0) return false;
  Wire.beginTransmission(END_BH1750);
  Wire.write(0x10);                            // modo continuo, alta resolucao (1 lux)
  Wire.endTransmission();
  delay(180);                                  // primeira medida
  return true;
}

// bhLer: le a luz em lux
bool bhLer(float &lux) {
  usarLuz();
  if (Wire.requestFrom(END_BH1750, (uint8_t)2) != 2) return false;
  uint16_t v = ((uint16_t)Wire.read() << 8) | Wire.read();
  lux = v / 1.2f;
  return true;
}

// ---------- teste das linhas SDA e SCL ----------
// Com o I2C parado, as duas linhas tem que ficar em 1 (os resistores dos modulos puxam para 3,3 V).
// Se alguma ficar em 0, tem algo segurando a linha no GND: fio trocado, solda encostando ou modulo com defeito.
// testarLinhas: mostra no Monitor Serial se SDA e SCL estao em 1 (ok) ou presas em 0
void testarLinhas(const char* nome, int sda, int scl) {
  if (busSDA == sda && busSCL == scl) { Wire.end(); busSDA = busSCL = -1; }
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
  delay(5);
  int vs = digitalRead(sda), vc = digitalRead(scl);
  Serial.printf("Linhas %s: SDA(GPIO%d) = %d   SCL(GPIO%d) = %d   ", nome, sda, vs, scl, vc);
  if (vs && vc) Serial.println("-> OK");
  else Serial.println("-> PROBLEMA: linha presa em 0 (curto com GND, fio trocado ou sensor sem energia)");
}

// ---------- destrava o I2C quando um sensor segura a linha ----------
// reiniciarI2C: solta um sensor que travou a linha SDA (9 pulsos de clock)
void reiniciarI2C(int sda, int scl) {
  if (busSDA >= 0) Wire.end();
  busSDA = busSCL = -1;
  pinMode(sda, INPUT);
  pinMode(scl, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; i++) {  // 9 pulsos de clock soltam um sensor travado
    digitalWrite(scl, LOW);  delayMicroseconds(5);
    digitalWrite(scl, HIGH); delayMicroseconds(5);
  }
  pinMode(scl, INPUT);
}

// ---------- sensor de solo ----------
// Mede 16 vezes e tira a media, em milivolts.
// Quanto MAIS seco, MAIOR a tensao; quanto mais molhado, MENOR.
int soloLerMV() {
  long soma = 0;
  for (int i = 0; i < 16; i++) { soma += analogReadMilliVolts(PINO_SOLO); delay(2); }
  return soma / 16;
}

// converte mV em % de umidade do solo (0 = seco, 100 = na agua)
int soloPorcento(int mv) {
  long p = (long)(soloSeco - mv) * 100 / (soloSeco - soloMolhado);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

// ---------- procura dispositivos no I2C ----------
// procurarI2C: lista no Monitor Serial os enderecos I2C encontrados (ajuda a achar fio solto)
void procurarI2C(const char* nome, int sda, int scl) {
  usarBarramento(sda, scl);
  Serial.printf("Procurando sensor de %s (SDA GPIO%d, SCL GPIO%d)...\n", nome, sda, scl);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  achei 0x%02X", a);
      if (a == END_AHT20)  Serial.print("  (AHT20)");
      if (a == 0x23 || a == 0x5C) { Serial.print("  (BH1750)"); END_BH1750 = a; }
      Serial.println();
      n++;
    }
  }
  if (n == 0) Serial.println("  nenhum! Confira o encaixe do sensor no soquete.");
}


// ---------- PAGINA: o site da planta (HTML, CSS e JavaScript num texto so) ----------
const char PAGINA[] PROGMEM =
"\n"
"<!doctype html>\n"
"<html lang=\"pt-BR\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>Minha Planta</title>\n"
"<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><path d='M32 34C32 26 33 20 35 14' stroke='%234c7d3a' stroke-width='4' fill='none' stroke-linecap='round'/><path d='M34 22C25 11 13 12 7 16C14 27 27 28 34 22Z' fill='%234c7d3a'/><path d='M35 16C41 5 52 3 58 6C55 16 44 21 35 16Z' fill='%236fa257'/><rect x='10' y='32' width='44' height='8' rx='3' fill='%23587565'/><path d='M13 40H51L46 61H18Z' fill='%236f8f7a'/><circle cx='26' cy='47' r='2.6' fill='%23fbfdf8'/><circle cx='38' cy='47' r='2.6' fill='%23fbfdf8'/><path d='M27 53Q32 57 37 53' stroke='%23fbfdf8' stroke-width='2.4' fill='none' stroke-linecap='round'/></svg>\">\n"
"<style>\n"
":root{--bg:#e9efe5;--ink:#1d2a1f;--soft:#5d6d5f;--line:#cfd9ca;--pot:#6f8f7a;--potd:#587565;--soil:#6b4f3a;--water:#3f8fb5;--leaf:#4c7d3a;--sun:#d9a321;--warm:#c0583b;--cold:#4f7fc9;--bad:#c8423b;--panel:#f4f7f1;--face:#fbfdf8}\n"
"@media (prefers-color-scheme:dark){:root:not([data-tema=claro]){--bg:#131c15;--ink:#e4ece1;--soft:#98a897;--line:#2a372c;--pot:#5c7a67;--potd:#4a6454;--soil:#4a3728;--water:#4aa3cc;--leaf:#6fa257;--sun:#e6b53c;--warm:#e0785a;--cold:#7aa3e6;--bad:#e8665e;--panel:#1a251c;--face:#eef4ea}}\n"
":root[data-tema=escuro]{--bg:#131c15;--ink:#e4ece1;--soft:#98a897;--line:#2a372c;--pot:#5c7a67;--potd:#4a6454;--soil:#4a3728;--water:#4aa3cc;--leaf:#6fa257;--sun:#e6b53c;--warm:#e0785a;--cold:#7aa3e6;--bad:#e8665e;--panel:#1a251c;--face:#eef4ea}\n"
"*{box-sizing:border-box;margin:0}\n"
"body{background:var(--bg);color:var(--ink);font-family:ui-rounded,\"SF Pro Rounded\",\"Segoe UI Variable Display\",\"Segoe UI\",system-ui,sans-serif;min-height:100vh;min-height:100dvh;display:flex;justify-content:center;align-items:flex-start;padding:12px 16px}\n"
"main{width:100%;max-width:880px}\n"
"header{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-bottom:8px}\n"
".escolha{position:relative;flex:1;max-width:360px}\n"
".escolha label{display:block;font-size:.8rem;color:var(--soft);margin-bottom:3px}\n"
".ddBtn{flex:1;min-width:0;width:100%;text-align:left;font:inherit;font-size:1.05rem;font-weight:600;color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:8px 40px 8px 14px;cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}\n"
".linha{display:flex;align-items:center;gap:8px;position:relative}\n"
".dd::after{content:\"\";position:absolute;right:16px;top:50%;margin-top:-6px;width:8px;height:8px;border-right:2px solid var(--soft);border-bottom:2px solid var(--soft);transform:rotate(45deg);pointer-events:none}\n"
".ddBtn:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".dd{position:relative;flex:1;min-width:0}\n"
".ddPainel{position:absolute;left:0;right:0;top:calc(100% + 6px);z-index:30;background:var(--panel);border:1px solid var(--line);border-radius:14px;box-shadow:0 12px 32px rgba(20,40,25,.22);overflow:hidden}\n"
".ddPainel[hidden]{display:none}\n"
".ddLista{list-style:none;margin:0;padding:6px 0;max-height:min(50vh,320px);overflow-y:auto}\n"
".ddLista li[role=option]{display:flex;align-items:center;gap:10px;padding:10px 14px;cursor:pointer;font-weight:600}\n"
".ddLista li[role=option]::before{content:\"\";width:16px;flex:none;text-align:center;color:var(--leaf)}\n"
".ddLista li[aria-selected=true]::before{content:\"✓\"}\n"
".ddLista li[role=option]:hover,.ddLista li.foco{background:var(--bg)}\n"
".ddLista .grupo{padding:10px 14px 4px 40px;font-size:.75rem;color:var(--soft);text-transform:uppercase;letter-spacing:.04em}\n"
".ddCriar{display:block;width:100%;font:inherit;font-weight:700;font-size:1rem;color:var(--leaf);background:transparent;border:none;border-top:1px solid var(--line);padding:12px 14px;text-align:left;cursor:pointer}\n"
".ddCriar:hover:not(:disabled){background:var(--bg)}\n"
".ddCriar:disabled{color:var(--soft);cursor:default}\n"
".ddCriar:focus-visible{outline:3px solid var(--leaf);outline-offset:-3px}\n"
".ddLista:focus{outline:none}\n"
".ddPainel.inline{position:static;margin-top:6px;box-shadow:none}\n"
".ddForm::after{top:21px}\n"
".ddForm .ddBtn{font-size:1rem;background:var(--bg);border-radius:10px;padding:9px 40px 9px 12px}\n"
".ddLista .tx{display:flex;flex-direction:column;min-width:0}\n"
".ddLista .tx .s{font-weight:400;font-size:.8rem;color:var(--soft);margin-top:1px}\n"
".mbotoes .sec{background:var(--bg);color:var(--leaf);border:1px solid var(--line)}\n"
".mbotoes .sec[hidden]{display:none}\n"
".ladoD{display:flex;flex-direction:column;align-items:flex-end;gap:5px;padding-bottom:2px}\n"
".ladoD .live{padding-bottom:0}\n"
"@media (min-width:720px){.ladoD{flex-direction:row;align-items:center;gap:12px;padding-bottom:10px}}\n"
"dialog#cal{width:min(460px,100%)}\n"
".tela[hidden]{display:none}\n"
".calOp{display:flex;align-items:center;gap:14px;width:100%;text-align:left;font:inherit;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:14px;padding:14px;margin:10px 0;cursor:pointer}\n"
".calOp:hover{border-color:var(--leaf)}\n"
".calOp:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".calOp .ic{flex:none;width:40px;height:40px;border-radius:12px;display:grid;place-items:center;background:var(--panel)}\n"
".calOp .ic svg{width:24px;height:24px}\n"
".calOp b{display:block;font-size:1.05rem}\n"
".calOp small{display:block;color:var(--soft);font-size:.85rem;margin-top:1px}\n"
".calOp .seta{margin-left:auto;width:9px;height:9px;border-right:2px solid var(--soft);border-top:2px solid var(--soft);transform:rotate(45deg);flex:none}\n"
".passo{font-size:.8rem;font-weight:700;color:var(--leaf);text-transform:uppercase;letter-spacing:.05em;margin:12px 0 4px}\n"
".tela h3{font-size:1.15rem;margin-bottom:6px}\n"
".tela p{line-height:1.45}\n"
".aviso2{display:flex;gap:8px;align-items:flex-start;margin-top:10px;padding:10px 12px;border-radius:12px;background:color-mix(in srgb,var(--warm) 14%,transparent);font-size:.92rem}\n"
".leitura{margin-top:12px;font-size:.9rem;color:var(--soft)}\n"
".leitura b{color:var(--ink)}\n"
".feito{display:flex;gap:10px;align-items:center;margin-top:8px;font-weight:600}\n"
".feito i{flex:none;width:22px;height:22px;border-radius:50%;background:var(--leaf);color:#fff;display:grid;place-items:center;font-style:normal;font-size:.8rem}\n"
".tela .resumo{margin-top:10px;padding:10px 12px;border:1px solid var(--line);border-radius:12px;background:var(--bg);font-size:.95rem}\n"
"#cal input{font:inherit;font-size:1.1rem;padding:9px 10px;border:1px solid var(--line);border-radius:10px;background:var(--bg);color:var(--ink);width:5.5em}\n"
"#cal input:focus-visible{outline:3px solid var(--leaf);outline-offset:1px}\n"
".calIn{display:flex;align-items:center;gap:8px;margin-top:12px;font-size:1.05rem}\n"
".fimOk{text-align:center;padding:18px 0 6px}\n"
".fimOk .bola{width:56px;height:56px;border-radius:50%;background:var(--leaf);color:#fff;display:grid;place-items:center;margin:0 auto 10px;font-size:1.6rem}\n"
".calMsg{min-height:1.2em;font-size:.9rem;font-weight:600;margin-top:10px;color:var(--bad)}\n"
"#cEsq{white-space:nowrap}\n"
"#cEsq[hidden]{display:none}\n"
".mbotoes button:disabled{opacity:.5;cursor:default}\n"
".live{display:flex;align-items:center;gap:8px;font-size:.9rem;color:var(--soft);white-space:nowrap;padding-bottom:10px}\n"
".dot{width:9px;height:9px;border-radius:50%;background:var(--leaf)}\n"
".topo{display:flex;align-items:center;justify-content:space-between;gap:8px;margin-bottom:3px;min-height:22px}\n"
".topo label{font-size:.8rem;color:var(--soft);white-space:nowrap}\n"
".wifi{display:flex;align-items:center;gap:5px;font-size:.75rem;color:var(--soft);white-space:nowrap;min-width:0;margin-left:auto}\n"
".wifi .nr{overflow:hidden;text-overflow:ellipsis;min-width:0}\n"
".sinal{display:inline-flex;align-items:flex-end;gap:1.5px;height:11px;flex:none}\n"
".sinal i{display:block;width:3px;background:var(--line);border-radius:1px}\n"
".sinal i.on{background:var(--ink)}\n"
".trocar{flex:none;font:inherit;font-size:.72rem;font-weight:600;color:var(--leaf);background:transparent;border:1px solid var(--line);border-radius:999px;padding:2px 9px;cursor:pointer}\n"
".trocar:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".off #wifi{display:none}\n"
".off .dot{background:var(--warm)}\n"
"h1{font-size:1.5rem;font-weight:700;letter-spacing:-.02em;line-height:1.1}\n"
".titulo{display:flex;align-items:baseline;gap:10px;flex-wrap:wrap}\n"
".cientifico{color:var(--soft);font-style:italic}\n"
".vazio{padding:40px 0;color:var(--soft);font-size:1.1rem}\n"
".painel{display:none}\n"
".com-planta .painel{display:block}\n"
".com-planta .vazio{display:none}\n"
".grid{display:grid;grid-template-columns:1fr;gap:10px;align-items:center;margin-top:4px}\n"
".potbox{display:flex;flex-direction:column;align-items:center;min-width:0}\n"
"svg.pot{height:clamp(100px,31vh,320px);height:clamp(100px,31dvh,320px);width:auto;display:block}\n"
"#agua{transition:transform 1.2s cubic-bezier(.3,.7,.2,1)}\n"
".rosto *{transition:opacity .4s}\n"
".big{font-size:clamp(2.6rem,7vh,4.2rem);font-weight:700;line-height:1;letter-spacing:-.04em;font-variant-numeric:tabular-nums;margin-top:4px}\n"
".big small{font-size:.4em;font-weight:600;margin-left:2px}\n"
".cap{color:var(--soft);font-size:.9rem;margin-top:2px;text-align:center}\n"
".msg{margin-top:8px;height:2.4rem;max-width:100%;display:flex;align-items:center;padding:0 18px;border-radius:999px;border:2px solid currentColor;color:var(--leaf);font-weight:600;font-size:1rem;white-space:nowrap;overflow:hidden}\n"
".msg span{overflow:hidden;text-overflow:ellipsis}\n"
".potbox .msg{display:none}\n"
".msg.ruim{color:var(--bad)}\n"
".msg.frio,.msg.agua{color:var(--bad)}\n"
".leituras{display:grid;grid-template-columns:1fr;gap:8px}\n"
".item{display:grid;grid-template-columns:28px 1fr auto;column-gap:12px;row-gap:3px;align-items:center;background:var(--panel);border:1px solid var(--line);border-left:6px solid var(--line);border-radius:14px;padding:8px 14px;min-width:0}\n"
".item.ok{border-left-color:var(--leaf)}\n"
".item.ruim{border-left-color:var(--bad)}\n"
".item>svg{grid-row:1/3;width:26px;height:26px;display:block}\n"
".txt{grid-column:2;grid-row:1;min-width:0}\n"
".l{font-size:.95rem;line-height:1.2}\n"
".veredito{font-size:.95rem;font-weight:700;line-height:1.2;color:var(--soft)}\n"
".item.ok .veredito{color:var(--leaf)}\n"
".item.ruim .veredito{color:var(--bad)}\n"
".v{grid-column:3;grid-row:1/3;font-size:1.6rem;font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:-.02em;line-height:1.05;white-space:nowrap;text-align:right}\n"
".v small{font-size:.85rem;font-weight:600;color:var(--soft);margin-left:2px}\n"
".ideal{grid-column:2;grid-row:2;font-size:.8rem;color:var(--soft);line-height:1.3}\n"
".barra{grid-column:2/4;grid-row:3;position:relative;height:8px;border-radius:99px;background:var(--line);margin-top:3px}\n"
".barra .enche{position:absolute;left:0;top:0;bottom:0;border-radius:99px;background:var(--sun);transition:width .8s}\n"
".item.ok .barra .enche{background:var(--leaf)}\n"
".item.ruim .barra .enche{background:var(--bad)}\n"
".aviso{margin-top:18px;padding:12px 14px;border-radius:14px;border:1px solid var(--warm);font-size:.95rem;display:none}\n"
".off .aviso{display:block}\n"
"\n"
".info{flex:none;width:42px;height:42px;border-radius:50%;border:1px solid var(--line);background:var(--panel);color:var(--leaf);cursor:pointer;display:grid;place-items:center;padding:0}\n"
".info svg{width:24px;height:24px}\n"
".info[hidden]{display:none}\n"
".info:hover{border-color:var(--leaf)}\n"
".info:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
"dialog{margin:auto;border:none;padding:0;background:var(--panel);color:var(--ink);width:min(560px,100%);max-height:86vh;max-height:86dvh;overflow:hidden;border-radius:24px;box-shadow:0 20px 60px rgba(20,40,25,.35)}\n"
"body:has(dialog[open]){overflow:hidden}\n"
"dialog::backdrop{background:rgba(18,32,22,.55)}\n"
"dialog[open]{display:flex;flex-direction:column;animation:sobe .35s cubic-bezier(.2,.8,.2,1)}\n"
"@keyframes sobe{from{transform:translateY(24px);opacity:0}to{transform:none;opacity:1}}\n"
".mtopo{flex:none;background:var(--panel);padding:20px 22px 12px;border-bottom:1px solid var(--line);display:flex;gap:14px;align-items:flex-start}\n"
".mtopo svg{flex:none;width:44px;height:44px}\n"
".mtopo h2{font-size:1.4rem;line-height:1.15}\n"
".mtopo p{color:var(--soft);font-size:.92rem;margin-top:3px;line-height:1.4}\n"
".mtopo .mcient{font-style:italic;margin-top:0}\n"
".fechar{margin-left:auto;flex:none;width:36px;height:36px;border-radius:50%;border:1px solid var(--line);background:var(--bg);color:var(--ink);font-size:1.3rem;cursor:pointer;line-height:1}\n"
".fechar:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".mcorpo{flex:1;min-height:0;overflow-y:auto;padding:6px 22px 18px}\n"
".cuidado{display:grid;grid-template-columns:34px 1fr;gap:12px;padding:12px 0;border-bottom:1px solid var(--line)}\n"
".cuidado:last-of-type{border-bottom:none}\n"
".cuidado .ic{width:34px;height:34px;border-radius:10px;display:grid;place-items:center;background:var(--bg)}\n"
".cuidado .ic svg{width:20px;height:20px}\n"
".cuidado h3{font-size:1rem;margin-bottom:2px}\n"
".cuidado p{font-size:.93rem;line-height:1.45;color:var(--ink)}\n"
".cuidado.alerta .ic{background:color-mix(in srgb,var(--warm) 18%,transparent)}\n"
".fontes{font-size:.8rem;color:var(--soft);margin-top:10px;line-height:1.4}\n"
"@media (max-width:719px){\n"
" dialog{margin:auto 0 0;width:100%;max-width:100%;border-radius:24px 24px 0 0;max-height:88dvh}\n"
" @keyframes sobe{from{transform:translateY(100%)}to{transform:none}}\n"
"}\n"
"@media (prefers-reduced-motion:reduce){dialog[open]{animation:none}}\n"
"dialog#troca{width:min(460px,100%)}\n"
".passos{margin:4px 0 0;padding-left:22px;display:flex;flex-direction:column;gap:10px;font-size:.95rem;line-height:1.45}\n"
".passos b{white-space:nowrap}\n"
".mbotoes{display:flex;gap:10px;padding:14px 22px 20px;border-top:1px solid var(--line)}\n"
".mbotoes button{flex:1;font:inherit;font-weight:700;font-size:1rem;padding:11px;border-radius:12px;cursor:pointer}\n"
".mbotoes .nao{background:transparent;color:var(--ink);border:1px solid var(--line)}\n"
".mbotoes .sim{background:var(--leaf);color:#fff;border:none}\n"
".mbotoes button:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".esperando{display:none;align-items:center;gap:12px;margin-bottom:10px;padding:12px 14px;border-radius:14px;border:1px solid var(--leaf);background:var(--panel);font-size:.92rem;line-height:1.4}\n"
".trocando .esperando{display:flex}\n"
".trocando #wifi,.trocando .trocar{display:none}\n"
".trocando .dot{background:var(--soft)}\n"
".trocando .aviso{display:none}\n"
".roda{flex:none;width:22px;height:22px;border-radius:50%;border:3px solid var(--line);border-top-color:var(--leaf);animation:gira 1s linear infinite}\n"
"@keyframes gira{to{transform:rotate(360deg)}}\n"
"@media (prefers-reduced-motion:reduce){.roda{animation:none}}\n"
"@media (max-width:719px){\n"
" .potbox{display:flex;justify-content:center}\n"
" svg.pot{height:clamp(100px,24vh,200px);height:clamp(100px,24dvh,200px)}\n"
"}\n"
"@media (max-width:350px),(max-height:600px) and (max-width:719px){\n"
" body{padding:8px 12px}\n"
" .topo label{display:none}\n"
" .item{padding:5px 10px;row-gap:1px}\n"
" svg.pot{height:clamp(64px,11vh,100px);height:clamp(64px,11dvh,100px)}\n"
" .grid{gap:6px}\n"
" .leituras{gap:6px}\n"
" .v{font-size:1.3rem}\n"
" .l,.veredito{font-size:.88rem}\n"
" .ideal{font-size:.74rem}\n"
" .msg{height:2.1rem;font-size:.9rem;margin-top:4px}\n"
" .big{font-size:2.4rem}\n"
" .cap{font-size:.82rem}\n"
"}\n"
"@media (min-width:720px){\n"
" body{padding:20px 32px}\n"
" h1{font-size:1.9rem}\n"
" svg.pot{height:clamp(160px,56vh,420px);height:clamp(160px,56dvh,420px)}\n"
" .grid{min-height:calc(100dvh - 130px)}\n"
" .grid{grid-template-columns:1.05fr 1fr;gap:44px;margin-top:12px}\n"
" .leituras{gap:12px}\n"
" .v{font-size:2rem}\n"
" .item{padding:12px 20px}\n"
"}\n"
"@media (prefers-reduced-motion:reduce){#agua,.rosto *{transition:none}}\n"
"dialog#nova{width:min(480px,100%)}\n"
".campo{margin:12px 0}\n"
".campo>label,.campo>.rot{display:block;font-weight:700;font-size:.95rem;margin-bottom:5px}\n"
".campo .dica{font-size:.8rem;color:var(--soft);margin-top:4px;line-height:1.35}\n"
".par{display:flex;align-items:center;gap:8px;flex-wrap:wrap}\n"
".par[hidden],.dica[hidden]{display:none}\n"
".par input{width:6em}\n"
"#nova input,#nova select{font:inherit;font-size:1rem;padding:8px 10px;border:1px solid var(--line);border-radius:10px;background:var(--bg);color:var(--ink);min-width:0}\n"
"#nova input[type=text],#nova select{width:100%}\n"
"#nova .par input{width:6em}\n"
"#nova input:focus-visible,#nova select:focus-visible{outline:3px solid var(--leaf);outline-offset:1px}\n"
"#arPers{margin-top:8px}\n"
".erroForm{color:var(--bad);font-weight:600;font-size:.9rem;min-height:1.2em;margin-top:6px}\n"
".mbotoes .apagar{background:transparent;color:var(--bad);border:1px solid var(--line)}\n"
".mbotoes .apagar[hidden]{display:none}\n"
"/* chave \"monitorar\" em cada card (a esquerda do icone) */\n"
".item{grid-template-columns:30px 28px 1fr auto}\n"
".item>svg{grid-column:2}\n"
".txt{grid-column:3}\n"
".v{grid-column:4}\n"
".ideal{grid-column:3}\n"
".barra{grid-column:3/5}\n"
".mon,.monVazio{grid-column:1;grid-row:1/3;align-self:center}\n"
".mon{display:inline-flex;align-items:center;cursor:pointer;position:relative}\n"
".mon input{position:absolute;opacity:0;width:1px;height:1px}\n"
".mon .sw{width:30px;height:18px;border-radius:99px;background:var(--line);position:relative;transition:background .2s;flex:none}\n"
".mon .sw::after{content:\"\";position:absolute;top:2px;left:2px;width:14px;height:14px;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.25);transition:left .2s}\n"
".mon input:checked+.sw{background:var(--leaf)}\n"
".mon input:checked+.sw::after{left:14px}\n"
".mon input:focus-visible+.sw{outline:3px solid var(--leaf);outline-offset:2px}\n"
".item.desl{border-left-color:var(--line)}\n"
".item.desl .v,.item.desl .ideal,.item.desl>svg,.item.desl .barra{opacity:.4}\n"
".item.desl .veredito{color:var(--soft)}\n"
".tema{flex:none;width:28px;height:28px;border-radius:50%;border:1px solid var(--line);background:transparent;color:var(--soft);cursor:pointer;display:grid;place-items:center;padding:0}\n"
".tema svg{width:16px;height:16px}\n"
".tema:hover{color:var(--ink);border-color:var(--leaf)}\n"
".tema:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".tema .lua{display:none}\n"
":root[data-tema=escuro] .tema .sol{display:none}\n"
":root[data-tema=escuro] .tema .lua{display:block}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<main id=\"app\">\n"
" <div class=\"topo\">\n"
"  <label for=\"planta\">Tipo de planta</label>\n"
"  <div class=\"wifi\"><span id=\"wifi\" class=\"wifi\"></span><button class=\"tema\" id=\"tema\" type=\"button\" title=\"Modo claro / escuro\" aria-label=\"Alternar modo claro e escuro\"><svg class=\"sol\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"4\"/><path d=\"M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4\"/></svg><svg class=\"lua\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linejoin=\"round\"><path d=\"M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5Z\"/></svg></button></div>\n"
" </div>\n"
" <header>\n"
"  <div class=\"escolha\">\n"
"   <div class=\"linha\">\n"
"    <div class=\"dd\" id=\"dd\">\n"
"     <button type=\"button\" class=\"ddBtn\" id=\"planta\" aria-haspopup=\"listbox\" aria-expanded=\"false\" aria-controls=\"ddLista\">Escolha a planta</button>\n"
"     <div class=\"ddPainel\" id=\"ddPainel\" hidden>\n"
"      <ul class=\"ddLista\" id=\"ddLista\" role=\"listbox\" aria-label=\"Plantas\" tabindex=\"-1\"></ul>\n"
"      <button type=\"button\" class=\"ddCriar\" id=\"ddCriar\">+ Criar planta</button>\n"
"     </div>\n"
"    </div>\n"
"    <button class=\"info\" id=\"abreInfo\" type=\"button\" aria-label=\"Como cuidar desta planta\" title=\"Como cuidar\">\n"
"     <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"10\"/><path d=\"M12 11v6\"/><circle cx=\"12\" cy=\"7.5\" r=\".6\" fill=\"currentColor\"/></svg>\n"
"    </button>\n"
"   </div>\n"
"  </div>\n"
"  <div class=\"ladoD\">\n"
"   <div class=\"live\"><span class=\"dot\"></span><span id=\"estado\">Conectando…</span></div>\n"
"   <button class=\"trocar\" id=\"abreCal\" type=\"button\" title=\"Calibrar os sensores\">Calibrar</button>\n"
"  </div>\n"
" </header>\n"
" <div class=\"esperando\" role=\"status\"><span class=\"roda\" aria-hidden=\"true\"></span><span>Aguardando você conectar o celular na rede <b>MINHA_PLANTA</b>. Os dados abaixo são os últimos lidos e não estão sendo atualizados.</span></div>\n"
"\n"
" <p class=\"vazio\">Escolha o tipo de planta, ou crie a sua em \"+ Criar planta\".</p>\n"
"\n"
" <div class=\"painel\">\n"
"  <div class=\"grid\">\n"
"   <section class=\"potbox\" aria-label=\"Umidade da terra\">\n"
"    <svg class=\"pot\" viewBox=\"0 0 240 250\" role=\"img\" aria-labelledby=\"msgtxt\">\n"
"     <defs><clipPath id=\"dentro\"><path d=\"M52 112 H188 L172 232 Q171 240 162 240 H78 Q69 240 68 232 Z\"/></clipPath></defs>\n"
"     <path d=\"M120 100 C120 80 121 62 124 44\" stroke=\"var(--leaf)\" stroke-width=\"5\" fill=\"none\" stroke-linecap=\"round\"/>\n"
"     <path d=\"M123 60 C104 40 82 42 70 50 C84 70 108 72 123 60 Z\" fill=\"var(--leaf)\"/>\n"
"     <path d=\"M125 46 C138 22 160 16 176 20 C170 42 146 54 125 46 Z\" fill=\"var(--leaf)\" opacity=\".85\"/>\n"
"     <path d=\"M52 112 H188 L172 232 Q171 240 162 240 H78 Q69 240 68 232 Z\" fill=\"var(--soil)\"/>\n"
"     <g clip-path=\"url(#dentro)\"><rect id=\"agua\" x=\"40\" y=\"112\" width=\"160\" height=\"140\" fill=\"var(--water)\" opacity=\".85\" style=\"transform:translateY(128px)\"/></g>\n"
"     <path d=\"M52 112 H188 L172 232 Q171 240 162 240 H78 Q69 240 68 232 Z\" fill=\"none\" stroke=\"var(--pot)\" stroke-width=\"6\" stroke-linejoin=\"round\"/>\n"
"     <rect x=\"40\" y=\"96\" width=\"160\" height=\"22\" rx=\"8\" fill=\"var(--pot)\"/>\n"
"     <rect x=\"40\" y=\"112\" width=\"160\" height=\"6\" fill=\"var(--potd)\"/>\n"
"     <!-- carinha -->\n"
"     <g class=\"rosto\" fill=\"none\" stroke=\"var(--face)\" stroke-width=\"5\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n"
"      <g id=\"olhosAbertos\" fill=\"var(--face)\" stroke=\"none\"><circle cx=\"100\" cy=\"160\" r=\"7\"/><circle cx=\"140\" cy=\"160\" r=\"7\"/></g>\n"
"      <g id=\"olhosFechados\" opacity=\"0\"><path d=\"M92 162 Q100 154 108 162\"/><path d=\"M132 162 Q140 154 148 162\"/></g>\n"
"      <g id=\"olhosCansados\" opacity=\"0\"><path d=\"M92 158 H108\"/><path d=\"M132 158 H148\"/><path d=\"M94 163 Q100 167 106 163\"/><path d=\"M134 163 Q140 167 146 163\"/></g>\n"
"      <path id=\"boca\" d=\"M104 190 Q120 204 136 190\"/>\n"
"      <g id=\"suor\" opacity=\"0\" fill=\"var(--face)\" stroke=\"none\"><path d=\"M162 134 C156 144 156 150 162 152 C168 150 168 144 162 134 Z\"/></g>\n"
"      <g id=\"gotas\" opacity=\"0\" fill=\"var(--face)\" stroke=\"none\"><path d=\"M88 172 C85 178 85 182 88 183 C91 182 91 178 88 172 Z\"/><path d=\"M152 172 C149 178 149 182 152 183 C155 182 155 178 152 172 Z\"/></g>\n"
"      <g id=\"tremor\" opacity=\"0\" stroke-width=\"3\"><path d=\"M72 150 L66 156 L72 162\"/><path d=\"M168 150 L174 156 L168 162\"/></g>\n"
"      <g id=\"bochecha\" opacity=\"0\" fill=\"var(--warm)\" stroke=\"none\"><circle cx=\"86\" cy=\"178\" r=\"6\"/><circle cx=\"154\" cy=\"178\" r=\"6\"/></g>\n"
"      <g id=\"zz\" opacity=\"0\" stroke-width=\"3\"><path d=\"M160 132 H170 L160 142 H170\"/><path d=\"M174 120 H181 L174 127 H181\"/></g>\n"
"     </g>\n"
"    </svg>\n"
"    <div class=\"msg\" id=\"msg\"><span id=\"msgtxt\">Lendo os sensores…</span></div>\n"
"   </section>\n"
"\n"
"   <section class=\"leituras\" aria-label=\"Leituras\">\n"
"    <div class=\"item\" id=\"cSolo\"><label class=\"mon\" title=\"Monitorar umidade da terra\"><input type=\"checkbox\" role=\"switch\" checked id=\"mSolo\" aria-label=\"Monitorar umidade da terra\"><span class=\"sw\"></span></label>\n"
"     <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--soil)\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M4 10h16l-2 10H6Z\"/><path d=\"M12 3c2 2 2 4 0 6c-2-2-2-4 0-6Z\" stroke=\"var(--water)\"/></svg>\n"
"     <div class=\"txt\"><div class=\"l\">Umidade da terra</div><div class=\"veredito\" id=\"vSolo\">--</div></div>\n"
"     <div class=\"v\"><span id=\"solo\">--</span><small>%</small></div>\n"
"     <div class=\"ideal\" id=\"iSolo\"></div>\n"
"    </div>\n"
"    <div class=\"item\" id=\"cTemp\"><label class=\"mon\" title=\"Monitorar temperatura e umidade do ar\"><input type=\"checkbox\" role=\"switch\" checked id=\"mTemp\" aria-label=\"Monitorar temperatura e umidade do ar\"><span class=\"sw\"></span></label>\n"
"     <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--warm)\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M10 14.5V5a2 2 0 1 1 4 0v9.5a4 4 0 1 1-4 0Z\"/><path d=\"M12 9v7\"/></svg>\n"
"     <div class=\"txt\"><div class=\"l\">Temperatura</div><div class=\"veredito\" id=\"vTemp\">--</div></div>\n"
"     <div class=\"v\"><span id=\"temp\">--</span><small>°C</small></div>\n"
"     <div class=\"ideal\" id=\"iTemp\"></div>\n"
"    </div>\n"
"    <div class=\"item\" id=\"cLuz\"><label class=\"mon\" title=\"Monitorar luz\"><input type=\"checkbox\" role=\"switch\" checked id=\"mLuz\" aria-label=\"Monitorar luz\"><span class=\"sw\"></span></label>\n"
"     <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--sun)\" stroke-width=\"2\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"4\"/><path d=\"M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4\"/></svg>\n"
"     <div class=\"txt\"><div class=\"l\">Sol de hoje</div><div class=\"veredito\" id=\"vLuz\">--</div></div>\n"
"     <div class=\"v\"><span id=\"luzpct\">--</span><small>%</small></div>\n"
"     <div class=\"ideal\">Ideal: 100% do sol do dia</div>\n"
"     <div class=\"barra\" aria-hidden=\"true\"><div class=\"enche\" id=\"eLuz\" style=\"width:0\"></div></div>\n"
"    </div>\n"
"    <div class=\"item\" id=\"cUmid\"><label class=\"mon\" title=\"Monitorar umidade do ar\"><input type=\"checkbox\" role=\"switch\" checked id=\"mUmid\" aria-label=\"Monitorar umidade do ar\"><span class=\"sw\"></span></label>\n"
"     <svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--water)\" stroke-width=\"2\" stroke-linejoin=\"round\"><path d=\"M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11Z\"/></svg>\n"
"     <div class=\"txt\"><div class=\"l\">Umidade do ar</div><div class=\"veredito\" id=\"vUmid\">--</div></div>\n"
"     <div class=\"v\"><span id=\"umid\">--</span><small>%</small></div>\n"
"     <div class=\"ideal\" id=\"iUmid\"></div>\n"
"    </div>\n"
"   </section>\n"
"  </div>\n"
" </div>\n"
" <dialog id=\"modal\" aria-labelledby=\"mNome\">\n"
"  <div class=\"mtopo\">\n"
"   <svg viewBox=\"0 0 48 48\" aria-hidden=\"true\"><path d=\"M24 30 C24 22 25 16 27 10\" stroke=\"var(--leaf)\" stroke-width=\"3\" fill=\"none\" stroke-linecap=\"round\"/><path d=\"M26 17 C18 9 10 10 6 13 C12 21 21 22 26 17 Z\" fill=\"var(--leaf)\"/><path d=\"M27 12 C32 3 40 1 45 3 C43 11 34 15 27 12 Z\" fill=\"var(--leaf)\" opacity=\".8\"/><path d=\"M10 30 H38 L35 45 H13 Z\" fill=\"var(--pot)\"/></svg>\n"
"   <div><h2 id=\"mNome\">--</h2><p class=\"mcient\" id=\"mCient\"></p><p id=\"mSub\"></p></div>\n"
"   <button class=\"fechar\" id=\"fechaInfo\" type=\"button\" aria-label=\"Fechar\">×</button>\n"
"  </div>\n"
"  <div class=\"mcorpo\" id=\"mCorpo\"></div>\n"
"  <div class=\"mbotoes\"><button class=\"sec\" id=\"mEditar\" type=\"button\">Editar planta</button><button class=\"sec\" id=\"mWifi\" type=\"button\">Trocar Wi-Fi</button></div>\n"
" </dialog>\n"
" <dialog id=\"troca\" aria-labelledby=\"tNome\">\n"
"  <div class=\"mtopo\">\n"
"   <svg viewBox=\"0 0 48 48\" aria-hidden=\"true\" fill=\"none\" stroke=\"var(--leaf)\" stroke-width=\"3.5\" stroke-linecap=\"round\"><path d=\"M6 19a26 26 0 0 1 36 0\"/><path d=\"M12 26a17 17 0 0 1 24 0\"/><path d=\"M18 33a8.5 8.5 0 0 1 12 0\"/><circle cx=\"24\" cy=\"39\" r=\"2.5\" fill=\"var(--leaf)\" stroke=\"none\"/></svg>\n"
"   <div><h2 id=\"tNome\">Trocar Wi-Fi</h2><p>A sua planta vai sair da rede atual para você escolher outra.</p></div>\n"
"   <button class=\"fechar\" id=\"fechaTroca\" type=\"button\" aria-label=\"Fechar\">×</button>\n"
"  </div>\n"
"  <div class=\"mcorpo\">\n"
"   <ol class=\"passos\">\n"
"    <li>Toque em <b>Continuar</b>. A sua planta cria a rede <b>MINHA_PLANTA</b>.</li>\n"
"    <li>Abra o <b>Wi-Fi do celular</b> e conecte na rede <b>MINHA_PLANTA</b> (não tem senha).</li>\n"
"    <li>A página de configuração abre sozinha. Se não abrir, acesse <b>192.168.4.1</b>.</li>\n"
"    <li>Escolha a nova rede e digite a senha.</li>\n"
"   </ol>\n"
"  </div>\n"
"  <div class=\"mbotoes\"><button class=\"nao\" id=\"trocaNao\" type=\"button\">Cancelar</button><button class=\"sim\" id=\"trocaSim\" type=\"button\">Continuar</button></div>\n"
" </dialog>\n"
" <dialog id=\"cal\" aria-labelledby=\"cTit\">\n"
"  <div class=\"mtopo\">\n"
"   <svg viewBox=\"0 0 48 48\" aria-hidden=\"true\" fill=\"none\" stroke=\"var(--leaf)\" stroke-width=\"3.5\" stroke-linecap=\"round\"><path d=\"M10 14h28M10 24h28M10 34h28\"/><circle cx=\"18\" cy=\"14\" r=\"4\" fill=\"var(--panel)\"/><circle cx=\"31\" cy=\"24\" r=\"4\" fill=\"var(--panel)\"/><circle cx=\"22\" cy=\"34\" r=\"4\" fill=\"var(--panel)\"/></svg>\n"
"   <div><h2 id=\"cTit\">Calibrar sensores</h2><p id=\"cSub\">Escolha o sensor.</p></div>\n"
"   <button class=\"fechar\" id=\"fechaCal\" type=\"button\" aria-label=\"Fechar\">×</button>\n"
"  </div>\n"
"  <div class=\"mcorpo\">\n"
"   <div class=\"tela\" data-t=\"escolha\">\n"
"    <button class=\"calOp\" type=\"button\" data-ir=\"solo1\"><span class=\"ic\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--soil)\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M4 10h16l-2 10H6Z\"/><path d=\"M12 3c2 2 2 4 0 6c-2-2-2-4 0-6Z\" stroke=\"var(--water)\"/></svg></span><span><b>Sensor de solo</b><small>Umidade da terra</small></span><span class=\"seta\"></span></button>\n"
"    <button class=\"calOp\" type=\"button\" data-ir=\"temp1\"><span class=\"ic\"><svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--warm)\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M10 14.5V5a2 2 0 1 1 4 0v9.5a4 4 0 1 1-4 0Z\"/><path d=\"M12 9v7\"/></svg></span><span><b>Temperatura</b><small>Com um termômetro</small></span><span class=\"seta\"></span></button>\n"
"   </div>\n"
"   <div class=\"tela\" data-t=\"solo1\" hidden>\n"
"    <p class=\"passo\">Passo 1 de 2</p>\n"
"    <h3>Sensor no ar</h3>\n"
"    <p>Tire o sensor da terra, limpe e seque bem. Deixe-o no ar e toque em <b>Medir</b>.</p>\n"
"    <p class=\"leitura\">Leitura agora: <b class=\"cMv\">--</b> mV</p>\n"
"   </div>\n"
"   <div class=\"tela\" data-t=\"solo2\" hidden>\n"
"    <p class=\"passo\">Passo 2 de 2</p>\n"
"    <p class=\"feito\"><i>✓</i><span>Ar medido: <b id=\"cArV\">--</b> mV</span></p>\n"
"    <h3>Sensor na água</h3>\n"
"    <p>Coloque o sensor num copo com água <b>até a linha branca</b> e toque em <b>Medir</b>.</p>\n"
"    <p class=\"aviso2\"><span aria-hidden=\"true\">⚠️</span><span>Não molhe a parte de cima, onde ficam os componentes.</span></p>\n"
"    <p class=\"leitura\">Leitura agora: <b class=\"cMv\">--</b> mV</p>\n"
"   </div>\n"
"   <div class=\"tela\" data-t=\"solo3\" hidden>\n"
"    <h3>Tudo medido</h3>\n"
"    <p class=\"resumo\">No ar (0 %): <b id=\"cArV2\">--</b> mV<br>Na água (100 %): <b id=\"cAguaV\">--</b> mV</p>\n"
"    <p class=\"leitura\">Toque em <b>Salvar</b> para usar essa calibração.</p>\n"
"   </div>\n"
"   <div class=\"tela\" data-t=\"temp1\" hidden>\n"
"    <h3>Temperatura real</h3>\n"
"    <p>Coloque um termômetro ao lado da planta e espere uns 5 minutos. Depois digite o valor dele.</p>\n"
"    <label class=\"calIn\">Termômetro: <input type=\"text\" id=\"cReal\" inputmode=\"decimal\" autocomplete=\"off\" placeholder=\"ex.: 24,5\"> °C</label>\n"
"    <p class=\"leitura\">O sensor mostra agora: <b id=\"cTAgora\">--</b> °C</p>\n"
"   </div>\n"
"   <div class=\"tela\" data-t=\"fim\" hidden>\n"
"    <div class=\"fimOk\"><div class=\"bola\" aria-hidden=\"true\">✓</div><h3 id=\"cFimTxt\">Calibração salva</h3><p class=\"leitura\">Vale para todas as plantas.</p></div>\n"
"   </div>\n"
"   <p class=\"calMsg\" id=\"cMsg\" role=\"alert\"></p>\n"
"  </div>\n"
"  <div class=\"mbotoes\"><button class=\"apagar\" id=\"cEsq\" type=\"button\">Voltar ao padrão</button><button class=\"sim\" id=\"cDir\" type=\"button\">Fechar</button></div>\n"
" </dialog>\n"
" <dialog id=\"nova\" aria-labelledby=\"nTit\">\n"
"  <div class=\"mtopo\">\n"
"   <svg viewBox=\"0 0 48 48\" aria-hidden=\"true\"><path d=\"M24 30 C24 22 25 16 27 10\" stroke=\"var(--leaf)\" stroke-width=\"3\" fill=\"none\" stroke-linecap=\"round\"/><path d=\"M26 17 C18 9 10 10 6 13 C12 21 21 22 26 17 Z\" fill=\"var(--leaf)\"/><path d=\"M27 12 C32 3 40 1 45 3 C43 11 34 15 27 12 Z\" fill=\"var(--leaf)\" opacity=\".8\"/><path d=\"M10 30 H38 L35 45 H13 Z\" fill=\"var(--pot)\"/></svg>\n"
"   <div><h2 id=\"nTit\">Criar planta</h2><p>Defina as faixas ideais.</p></div>\n"
"   <button class=\"fechar\" id=\"fechaNova\" type=\"button\" aria-label=\"Fechar\">×</button>\n"
"  </div>\n"
"  <form class=\"mcorpo\" id=\"formNova\" novalidate>\n"
"   <div class=\"campo\"><label for=\"fNome\">Nome da planta</label><input type=\"text\" id=\"fNome\" maxlength=\"24\" placeholder=\"Ex.: Minha orquídea\" autocomplete=\"off\"></div>\n"
"   <div class=\"campo\"><span class=\"rot\">Umidade da terra ideal</span><div class=\"par\"><input type=\"number\" id=\"fS0\" min=\"0\" max=\"100\" inputmode=\"numeric\" aria-label=\"Umidade da terra mínima\"> a <input type=\"number\" id=\"fS1\" min=\"0\" max=\"100\" inputmode=\"numeric\" aria-label=\"Umidade da terra máxima\"> %</div></div>\n"
"   <div class=\"campo\"><span class=\"rot\">Temperatura ideal</span><div class=\"par\"><input type=\"text\" id=\"fT0\" inputmode=\"decimal\" autocomplete=\"off\" aria-label=\"Temperatura mínima\"> a <input type=\"text\" id=\"fT1\" inputmode=\"decimal\" autocomplete=\"off\" aria-label=\"Temperatura máxima\"> °C</div></div>\n"
"   <div class=\"campo\"><span class=\"rot\" id=\"rLuz\">Luz por dia</span><div class=\"dd ddForm\" id=\"fLuz\"></div><div class=\"dica\" id=\"dLuz\"></div></div>\n"
"   <div class=\"campo\"><span class=\"rot\" id=\"rAr\">Umidade do ar</span><div class=\"dd ddForm\" id=\"fAr\"></div>\n"
"    <div class=\"par\" id=\"arPers\" hidden><input type=\"number\" id=\"fU0\" min=\"0\" max=\"100\" inputmode=\"numeric\" aria-label=\"Umidade do ar mínima\" placeholder=\"mín\"> a <input type=\"number\" id=\"fU1\" min=\"0\" max=\"100\" inputmode=\"numeric\" aria-label=\"Umidade do ar máxima\" placeholder=\"máx\"> %</div>\n"
"    <div class=\"dica\" id=\"dAr\" hidden>Deixe um vazio para usar só o mínimo ou só o máximo.</div></div>\n"
"   <p class=\"erroForm\" id=\"erroNova\" role=\"alert\"></p>\n"
"  </form>\n"
"  <div class=\"mbotoes\"><button class=\"apagar\" id=\"apagaNova\" type=\"button\" hidden>Apagar</button><button class=\"nao\" id=\"cancelaNova\" type=\"button\">Cancelar</button><button class=\"sim\" id=\"salvaNova\" type=\"button\">Salvar</button></div>\n"
" </dialog>\n"
" <p class=\"aviso\">Sem sinal da sua planta. Confira se ela está ligada e se o aparelho está no mesmo Wi-Fi da casa.</p>\n"
"</main>\n"
"<script>\n"
"/* Cuidados por planta. Cada valor e uma FAIXA ok (min a max), nunca um valor exato.\n"
"   Kalanchoe - fontes:\n"
"   temp: NC State Extension - trazer para dentro antes de 45 F (7 C); estufa: maxima de dia 80 F (27 C)\n"
"   luz:  Greenhouse Grower - luz diaria (DLI) de 8 a 9 mol/m2/dia melhora a qualidade -> minimo 8 por dia\n"
"         (a sua planta soma a luz do dia inteiro; lux -> PPFD da luz do sol: x 0,0185, Apogee Instruments)\n"
"   umid: as fontes nao dao faixa (so \"ar seco da casa serve, nao precisa borrifar\") -> so mostra\n"
"   solo: regar quando os 2,5 cm de cima secarem -> faixa PROVISORIA, falta calibrar com a sua terra */\n"
"const PLANTAS={\n"
" kalanchoe:{nome:'Kalanchoe',cient:'Kalanchoe blossfeldiana',\n"
"  solo:[25,60], temp:[7,27], umid:null, dliMin:8,\n"
"  sub:'Flor-da-fortuna. Suculenta de Madagascar.',\n"
"  cuidados:[\n"
"   ['sol','Luz','Muito sol: 4 h de sol direto por dia. No calor forte, sombra à tarde. Meta: 8 de luz por dia (DLI).'],\n"
"   ['termo','Temperatura','Ideal 16 °C à noite e 21 °C de dia. Aceita 7 a 27 °C. Alerta após 30 min fora da faixa.'],\n"
"   ['gota','Rega','Regue quando os 2,5 cm de cima secarem. Na dúvida, regue menos. Faixa no site: 25 a 60 %.'],\n"
"   ['ar','Umidade do ar','Ar normal da casa. Não precisa borrifar.'],\n"
"   ['terra','Terra','Terra que escoa bem a água.'],\n"
"   ['flor','Para florir de novo','6 semanas com 14 h seguidas de escuro por dia.'],\n"
"   ['pata','Cuidado com pets','Tóxica para cães e gatos: pode dar vômito e diarreia.',true]\n"
"  ],\n"
"  fontes:'Fontes: NC State, NY Botanical Garden, Clemson, Iowa State, Greenhouse Grower e ASPCA.'}\n"
"};\n"
"const ICONES={\n"
" sol:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--sun)\" stroke-width=\"2\" stroke-linecap=\"round\"><circle cx=\"12\" cy=\"12\" r=\"4\"/><path d=\"M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4\"/></svg>',\n"
" termo:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--warm)\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M10 14.5V5a2 2 0 1 1 4 0v9.5a4 4 0 1 1-4 0Z\"/><path d=\"M12 9v7\"/></svg>',\n"
" gota:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--water)\" stroke-width=\"2\" stroke-linejoin=\"round\"><path d=\"M12 3s6 6.5 6 11a6 6 0 0 1-12 0c0-4.5 6-11 6-11Z\"/></svg>',\n"
" ar:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--water)\" stroke-width=\"2\" stroke-linecap=\"round\"><path d=\"M3 8h11a3 3 0 1 0-3-3M3 12h15a3 3 0 1 1-3 3M3 16h8\"/></svg>',\n"
" terra:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--soil)\" stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M4 10h16l-2 10H6Z\"/><path d=\"M8 14h.01M12 16h.01M15 13h.01\"/></svg>',\n"
" flor:'<svg viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"var(--warm)\" stroke-width=\"2\"><circle cx=\"12\" cy=\"9\" r=\"2.5\"/><path d=\"M12 6.5a3 3 0 1 1 2.5 2.5M9.5 9A3 3 0 1 1 12 6.5M14.5 9a3 3 0 1 1-2.5 2.5M12 11.5A3 3 0 1 1 9.5 9\"/><path d=\"M12 12v9\" stroke=\"var(--leaf)\"/></svg>',\n"
" pata:'<svg viewBox=\"0 0 24 24\" fill=\"var(--warm)\"><circle cx=\"7\" cy=\"9\" r=\"2\"/><circle cx=\"17\" cy=\"9\" r=\"2\"/><circle cx=\"10\" cy=\"5.5\" r=\"2\"/><circle cx=\"14\" cy=\"5.5\" r=\"2\"/><path d=\"M12 11c-3 0-6 4-6 6.5 0 2 2 2.5 3 2 1.5-.7 4.5-.7 6 0 1 .5 3 0 3-2 0-2.5-3-6.5-6-6.5Z\"/></svg>'\n"
"};\n"
"const $=id=>document.getElementById(id);\n"
"const nf=(v,c)=>Number(v).toLocaleString('pt-BR',{minimumFractionDigits:c,maximumFractionDigits:c});\n"
"const sel=$('planta');\n"
"let atual='';\n"
"/* ---------- plantas criadas por voce (ate 10, salvas no ESP32) ----------\n"
"   Luz (DLI minimo por dia, mol/m2/dia) - Iowa State University Extension (plantas de interior)\n"
"   e Purdue Extension HO-238 (exemplos de plantas em cada nivel).\n"
"   Umidade do ar - Univ. de Minnesota (cactos/suculentas 10-30%), Penn State/ASHRAE (casa 30-60%),\n"
"   Oklahoma State (50-60%) e Univ. de Arkansas (samambaias e violetas preferem ar mais umido). */\n"
"const MAXP=10;\n"
"const LUZES=[\n"
" [3,'Pouca luz','samambaia, orquídea phalaenopsis, lírio-da-paz, violeta','3 a 6'],\n"
" [6,'Luz média','kalanchoe, begônia, cíclame','6 a 12'],\n"
" [12,'Muita luz','hibisco, gérbera, gerânio, minirrosa','12 a 18'],\n"
" [18,'Luz muito forte','tomate, pimenta, ervas','mais de 18']\n"
"];\n"
"const ARES={\n"
" nao:['Não monitorar',null,'só mostra o valor'],\n"
" seco:['Ar seco',[10,null],'cactos e suculentas · a partir de 10 %'],\n"
" normal:['Ar normal da casa',[30,60],'30 a 60 %'],\n"
" umido:['Ar úmido',[50,null],'samambaias e violetas · a partir de 50 %'],\n"
" pers:['Personalizado',undefined,'você escolhe mínimo e máximo']\n"
"};\n"
"const num=x=>Number.isInteger(x)?String(x):nf(x,1);\n"
"function txtFaixa(f,u){\n"
" if(!f)return '';\n"
" if(f[0]!=null&&f[1]!=null)return num(f[0])+'\\u00a0a\\u00a0'+num(f[1])+'\\u00a0'+u;\n"
" if(f[0]!=null)return 'a partir de '+num(f[0])+'\\u00a0'+u;\n"
" return 'até '+num(f[1])+'\\u00a0'+u;\n"
"}\n"
"function nivelLuz(l){return LUZES.find(x=>x[0]===l)||[l,'Luz','',String(l)];}\n"
"function plantaCriada(id,v){\n"
" const a=v.split('|');if(a.length<8)return null;\n"
" const n=a.slice(1).map(Number);\n"
" const u=(n[5]<0&&n[6]<0)?null:[n[5]<0?null:n[5],n[6]<0?null:n[6]];\n"
" const L=nivelLuz(n[4]);\n"
" return {nome:a[0],cient:'Criada por você',criada:true,\n"
"  solo:[n[0],n[1]],temp:[n[2],n[3]],umid:u,dliMin:n[4],\n"
"  sub:'Faixas escolhidas por você.',\n"
"  cuidados:[\n"
"   ['sol','Luz',L[1]+'. Meta: '+n[4]+' de luz por dia (DLI).'],\n"
"   ['termo','Temperatura',txtFaixa([n[2],n[3]],'°C')+'. Alerta após 30 min fora da faixa.'],\n"
"   ['gota','Umidade da terra',txtFaixa([n[0],n[1]],'%')+'.'],\n"
"   ['ar','Umidade do ar',u?txtFaixa(u,'%')+'.':'Não monitorada.']\n"
"  ],\n"
"  fontes:'Níveis de luz: Iowa State e Purdue Extension.'};\n"
"}\n"
"/* ---------- lista de plantas (dropdown) ---------- */\n"
"const lista=$('ddLista'),painel=$('ddPainel');\n"
"function itemPlanta(k){const li=document.createElement('li');li.setAttribute('role','option');li.id='op_'+k;li.dataset.k=k;li.textContent=PLANTAS[k].nome;li.setAttribute('aria-selected',k===atual?'true':'false');return li;}\n"
"function montaSelect(){\n"
" lista.innerHTML='';\n"
" const cri=[];\n"
" for(const k in PLANTAS){if(PLANTAS[k].criada)cri.push(k);else lista.appendChild(itemPlanta(k));}\n"
" if(cri.length){const g=document.createElement('li');g.className='grupo';g.setAttribute('role','presentation');g.textContent='Criadas por você';lista.appendChild(g);cri.forEach(k=>lista.appendChild(itemPlanta(k)));}\n"
" const cheio=cri.length>=MAXP;\n"
" $('ddCriar').disabled=cheio;$('ddCriar').textContent=cheio?'Limite de '+MAXP+' plantas criadas':'+ Criar planta';\n"
" sel.textContent=atual&&PLANTAS[atual]?PLANTAS[atual].nome:'Escolha a planta';\n"
"}\n"
"let foco=-1;\n"
"function opcoes(){return [...lista.querySelectorAll('li[role=option]')];}\n"
"function marcaFoco(i){const o=opcoes();o.forEach(x=>x.classList.remove('foco'));foco=i;if(o[i]){o[i].classList.add('foco');o[i].scrollIntoView({block:'nearest'});lista.setAttribute('aria-activedescendant',o[i].id);}}\n"
"function abreLista(){montaSelect();painel.hidden=false;sel.setAttribute('aria-expanded','true');const o=opcoes();marcaFoco(Math.max(0,o.findIndex(x=>x.dataset.k===atual)));lista.focus();}\n"
"function fechaLista(volta){painel.hidden=true;sel.setAttribute('aria-expanded','false');if(volta)sel.focus();}\n"
"sel.addEventListener('click',()=>{painel.hidden?abreLista():fechaLista(false);});\n"
"sel.addEventListener('keydown',e=>{if(e.key==='ArrowDown'||e.key==='ArrowUp'){e.preventDefault();abreLista();}});\n"
"lista.addEventListener('click',e=>{const li=e.target.closest('li[role=option]');if(!li)return;fechaLista(true);escolhe(li.dataset.k);});\n"
"lista.addEventListener('keydown',e=>{\n"
" const o=opcoes();\n"
" if(e.key==='ArrowDown'){e.preventDefault();marcaFoco(Math.min(o.length-1,foco+1));}\n"
" else if(e.key==='ArrowUp'){e.preventDefault();marcaFoco(Math.max(0,foco-1));}\n"
" else if(e.key==='Enter'||e.key===' '){e.preventDefault();if(o[foco]){fechaLista(true);escolhe(o[foco].dataset.k);}}\n"
" else if(e.key==='Escape'){e.preventDefault();fechaLista(true);}\n"
" else if(e.key==='Tab'){fechaLista(false);}\n"
"});\n"
"$('ddCriar').addEventListener('click',()=>{fechaLista(false);abreNova('');});\n"
"document.addEventListener('click',e=>{if(!painel.hidden&&!$('dd').contains(e.target))fechaLista(false);});\n"
"async function carregaPlantas(){\n"
" try{\n"
"  const r=await fetch('/plantas',{cache:'no-store'});const l=await r.json();\n"
"  for(const k in PLANTAS)if(PLANTAS[k].criada)delete PLANTAS[k];\n"
"  l.forEach(x=>{const p=plantaCriada(x.id,x.v);if(p)PLANTAS[x.id]=p;});\n"
" }catch(e){}\n"
" montaSelect();\n"
"}\n"
"function aplicaPlanta(k){\n"
" atual=PLANTAS[k]?k:'';sel.textContent=atual?PLANTAS[atual].nome:'Escolha a planta';\n"
" $('app').classList.toggle('com-planta',!!atual);\n"
" $('abreInfo').hidden=!atual;\n"
" if(!atual)return;\n"
" const p=PLANTAS[atual];\n"
" $('iSolo').textContent='Ideal: '+txtFaixa(p.solo,'%');\n"
" $('iTemp').textContent='Ideal: '+txtFaixa(p.temp,'°C');\n"
"}\n"
"function abreInfo(){\n"
" const p=PLANTAS[atual];if(!p)return;\n"
" $('mNome').textContent=p.nome;$('mCient').textContent=p.cient;$('mSub').textContent=p.sub;\n"
" $('mCorpo').innerHTML=p.cuidados.map(c=>'<div class=\"cuidado'+(c[3]?' alerta':'')+'\"><div class=\"ic\">'+ICONES[c[0]]+'</div><div><h3>'+c[1]+'</h3><p>'+c[2]+'</p></div></div>').join('')+'<p class=\"fontes\">'+p.fontes+'</p>';\n"
" $('mEditar').hidden=!p.criada;\n"
" $('modal').showModal();\n"
"}\n"
"$('mEditar').onclick=()=>{$('modal').close();abreNova(atual);};\n"
"$('mWifi').onclick=()=>{$('modal').close();abreTroca();};\n"
"$('abreInfo').addEventListener('click',abreInfo);\n"
"$('fechaInfo').addEventListener('click',()=>$('modal').close());\n"
"$('modal').addEventListener('click',e=>{if(e.target===$('modal'))$('modal').close();});\n"
"function escolhe(k){aplicaPlanta(k);fetch('/planta?id='+encodeURIComponent(atual)).catch(()=>{});}\n"
"\n"
"/* ---------- janela \"Criar planta\" ---------- */\n"
"let editando='',apagaConfirma=false;\n"
"/* escolha bonita (mesmo visual da lista de plantas), usada no lugar do <select> */\n"
"function escolha(id,rot,itens){\n"
" const el=$(id);let val=String(itens[0][0]),f=-1;\n"
" el.innerHTML='<button type=\"button\" class=\"ddBtn\" aria-haspopup=\"listbox\" aria-expanded=\"false\" aria-labelledby=\"'+rot+' '+id+'_b\" id=\"'+id+'_b\"></button><div class=\"ddPainel inline\" hidden><ul class=\"ddLista\" role=\"listbox\" tabindex=\"-1\" aria-labelledby=\"'+rot+'\"></ul></div>';\n"
" const b=el.querySelector('button'),pn=el.querySelector('.ddPainel'),ul=el.querySelector('ul');\n"
" itens.forEach(([v,t,sub],i)=>{const li=document.createElement('li');li.setAttribute('role','option');li.id=id+'_'+i;li.dataset.v=String(v);\n"
"  li.innerHTML='<span class=\"tx\"><span class=\"t\"></span><span class=\"s\"></span></span>';li.querySelector('.t').textContent=t;li.querySelector('.s').textContent=sub||'';ul.appendChild(li);});\n"
" const lis=[...ul.children];\n"
" const pinta=()=>{lis.forEach(li=>li.setAttribute('aria-selected',li.dataset.v===val?'true':'false'));const it=itens.find(x=>String(x[0])===val);b.textContent=it?it[1]:'';};\n"
" const foca=i=>{lis.forEach(x=>x.classList.remove('foco'));f=i;if(lis[i]){lis[i].classList.add('foco');ul.setAttribute('aria-activedescendant',lis[i].id);lis[i].scrollIntoView({block:'nearest'});}};\n"
" const abre=()=>{pn.hidden=false;b.setAttribute('aria-expanded','true');foca(Math.max(0,lis.findIndex(x=>x.dataset.v===val)));ul.focus();};\n"
" const fecha=v=>{pn.hidden=true;b.setAttribute('aria-expanded','false');if(v)b.focus();};\n"
" const pega=v=>{const mudou=v!==val;val=v;pinta();fecha(true);if(mudou&&el.onchange)el.onchange();};\n"
" b.onclick=()=>pn.hidden?abre():fecha(false);\n"
" ul.onclick=e=>{const li=e.target.closest('li');if(li)pega(li.dataset.v);};\n"
" ul.onkeydown=e=>{\n"
"  if(e.key==='ArrowDown'){e.preventDefault();foca(Math.min(lis.length-1,f+1));}\n"
"  else if(e.key==='ArrowUp'){e.preventDefault();foca(Math.max(0,f-1));}\n"
"  else if(e.key==='Enter'||e.key===' '){e.preventDefault();if(lis[f])pega(lis[f].dataset.v);}\n"
"  else if(e.key==='Escape'){e.preventDefault();e.stopPropagation();fecha(true);}\n"
"  else if(e.key==='Tab')fecha(false);\n"
" };\n"
" Object.defineProperty(el,'value',{get:()=>val,set:v=>{val=String(v);pinta();fecha(false);}});\n"
" pinta();\n"
"}\n"
"escolha('fLuz','rLuz',LUZES.map(x=>[x[0],x[1],x[2]]));\n"
"escolha('fAr','rAr',Object.keys(ARES).map(k=>[k,ARES[k][0],ARES[k][2]]));\n"
"function dicaLuz(){const L=nivelLuz(Number($('fLuz').value));$('dLuz').textContent='Meta: '+L[0]+' de luz por dia (DLI).';}\n"
"$('fLuz').onchange=dicaLuz;\n"
"$('fAr').onchange=()=>{\n"
" const k=$('fAr').value,f=ARES[k][1];\n"
" $('arPers').hidden=$('dAr').hidden=(k!=='pers');\n"
" if(f!==undefined){$('fU0').value=f&&f[0]!=null?f[0]:'';$('fU1').value=f&&f[1]!=null?f[1]:'';}\n"
"};\n"
"function abreNova(id){\n"
" editando=id;apagaConfirma=false;\n"
" const p=id?PLANTAS[id]:null;\n"
" $('nTit').textContent=p?'Editar planta':'Criar planta';\n"
" $('fNome').value=p?p.nome:'';\n"
" $('fS0').value=p?p.solo[0]:25;$('fS1').value=p?p.solo[1]:60;\n"
" $('fT0').value=p?p.temp[0]:15;$('fT1').value=p?p.temp[1]:28;\n"
" $('fLuz').value=p?p.dliMin:6;dicaLuz();\n"
" let ar='nao';\n"
" if(p&&p.umid){ar='pers';for(const k in ARES){const f=ARES[k][1];if(f&&f[0]===p.umid[0]&&f[1]===p.umid[1])ar=k;}}\n"
" $('fAr').value=ar;$('arPers').hidden=$('dAr').hidden=(ar!=='pers');\n"
" $('fU0').value=p&&p.umid&&p.umid[0]!=null?p.umid[0]:'';$('fU1').value=p&&p.umid&&p.umid[1]!=null?p.umid[1]:'';\n"
" $('apagaNova').hidden=!p;$('apagaNova').textContent='Apagar';\n"
" $('erroNova').textContent='';\n"
" $('nova').showModal();\n"
"}\n"
"$('fechaNova').onclick=$('cancelaNova').onclick=()=>$('nova').close();\n"
"$('formNova').onsubmit=e=>{e.preventDefault();salvaNova();};\n"
"function lerNum(id){const t=$(id).value.trim().replace(',','.');return t===''?null:Number(t);}\n"
"async function salvaNova(){\n"
" const er=m=>{$('erroNova').textContent=m;};\n"
" const nome=$('fNome').value.trim().replace(/[|\"\\\\<>]/g,' ');\n"
" const s0=lerNum('fS0'),s1=lerNum('fS1'),t0=lerNum('fT0'),t1=lerNum('fT1');\n"
" let u0=null,u1=null;\n"
" const ar=$('fAr').value;\n"
" if(ar==='pers'){u0=lerNum('fU0');u1=lerNum('fU1');}else if(ARES[ar][1]){[u0,u1]=ARES[ar][1];}\n"
" if(!nome)return er('Escreva o nome.');\n"
" if(s0===null||s1===null||isNaN(s0)||isNaN(s1)||s0<0||s1>100||s0>=s1)return er('Terra: mínimo menor que o máximo, de 0 a 100 %.');\n"
" if(t0===null||t1===null||isNaN(t0)||isNaN(t1)||t0<-10||t1>50||t0>=t1)return er('Temperatura: mínimo menor que o máximo, de -10 a 50 °C.');\n"
" if(ar==='pers'){\n"
"  if(u0===null&&u1===null)return er('Ar: preencha o mínimo ou o máximo.');\n"
"  if((u0!==null&&(isNaN(u0)||u0<0||u0>100))||(u1!==null&&(isNaN(u1)||u1<0||u1>100)))return er('Ar: use de 0 a 100 %.');\n"
"  if(u0!==null&&u1!==null&&u0>=u1)return er('Ar: mínimo menor que o máximo.');\n"
" }\n"
" const q=new URLSearchParams({id:editando,n:nome,s0,s1,t0,t1,l:$('fLuz').value,u0:u0===null?-1:u0,u1:u1===null?-1:u1});\n"
" $('salvaNova').disabled=true;\n"
" try{\n"
"  const r=await fetch('/salvar-planta?'+q.toString(),{cache:'no-store'});\n"
"  const t=(await r.text()).trim();\n"
"  if(!r.ok)return er(r.status===409?'Limite de '+MAXP+' plantas. Apague uma.':'Não salvou. Confira os valores.');\n"
"  await carregaPlantas();\n"
"  $('nova').close();\n"
"  escolhe(t);\n"
" }catch(e){er('Sem conexão. Tente de novo.');}\n"
" finally{$('salvaNova').disabled=false;}\n"
"}\n"
"$('salvaNova').onclick=salvaNova;\n"
"$('apagaNova').onclick=async()=>{\n"
" if(!apagaConfirma){apagaConfirma=true;$('apagaNova').textContent='Apagar mesmo?';return;}\n"
" try{\n"
"  const r=await fetch('/apagar-planta?id='+encodeURIComponent(editando),{cache:'no-store'});\n"
"  if(!r.ok)throw 0;\n"
"  const era=editando;await carregaPlantas();$('nova').close();\n"
"  if(atual===era||!PLANTAS[atual])aplicaPlanta('');else aplicaPlanta(atual);\n"
" }catch(e){$('erroNova').textContent='Não apagou. Tente de novo.';}\n"
"};\n"
"\n"
"const T0=0,T1=45;   // escala da barra de temperatura (°C)\n"
"function pos(t){return Math.max(0,Math.min(100,(t-T0)/(T1-T0)*100));}\n"
"function faixa(v,f){if(!f||v===null||v===undefined)return 'nd';if(f[0]!=null&&v<f[0])return 'baixo';if(f[1]!=null&&v>f[1])return 'alto';return 'ok';}\n"
"const HUMOR={\n"
" feliz:{olhos:'olhosAbertos',boca:'M104 190 Q120 204 136 190',extra:[],cls:'',txt:'Tudo certo comigo'},\n"
" sede:{olhos:'olhosCansados',boca:'M108 198 Q120 186 132 198',extra:['bochecha'],cls:'ruim',txt:'Estou com sede, hora de regar'},\n"
" encharcada:{olhos:'olhosCansados',boca:'M104 196 Q112 190 120 196 Q128 202 136 196',extra:['gotas'],cls:'agua',txt:'Terra encharcada, espere secar'},\n"
" calor:{olhos:'olhosAbertos',boca:'M106 192 Q120 200 134 192 Q120 210 106 192',extra:['suor','bochecha'],cls:'ruim',txt:'Está quente demais para mim'},\n"
" frio:{olhos:'olhosFechados',boca:'M104 194 L110 190 L116 196 L122 190 L128 196 L134 190 L138 194',extra:['tremor'],cls:'frio',txt:'Está frio demais para mim'},\n"
" escuro:{olhos:'olhosFechados',boca:'M112 194 Q120 190 128 194',extra:['zz'],cls:'ruim',txt:'Ontem tive pouca luz'},\n"
" arseco:{olhos:'olhosAbertos',boca:'M106 194 H134',extra:[],cls:'ruim',txt:'O ar está seco demais'},\n"
" arumido:{olhos:'olhosAbertos',boca:'M106 194 H134',extra:['gotas'],cls:'agua',txt:'O ar está úmido demais'}\n"
"};\n"
"function mostraHumor(h){\n"
" const m=HUMOR[h];\n"
" ['olhosAbertos','olhosFechados','olhosCansados'].forEach(i=>$(i).style.opacity=(i===m.olhos?1:0));\n"
" $('boca').setAttribute('d',m.boca);\n"
" ['suor','gotas','tremor','bochecha','zz'].forEach(i=>$(i).style.opacity=m.extra.includes(i)?1:0);\n"
" $('msg').className='msg '+m.cls;\n"
" $('msgtxt').textContent=m.txt;\n"
"}\n"
"function marca(id,est){$(id).className='item '+(est==='ok'?'ok':(est==='baixo'||est==='alto'||est==='ruim')?'ruim':'');}\n"
"\n"
"/* ---------- calibracao dos sensores (passo a passo) ---------- */\n"
"let calTela='escolha',calTimer=null,calPadraoConf=false,calAr=null,calAgua=null,calInfo=null;\n"
"const TELAS={\n"
" escolha:{sub:'Escolha o sensor.',esq:'Voltar ao padrão',dir:'Fechar'},\n"
" solo1:{sub:'Sensor de solo',esq:'Voltar',dir:'Medir'},\n"
" solo2:{sub:'Sensor de solo',esq:'Voltar',dir:'Medir'},\n"
" solo3:{sub:'Sensor de solo',esq:'Refazer',dir:'Salvar'},\n"
" temp1:{sub:'Temperatura',esq:'Voltar',dir:'Salvar'},\n"
" fim:{sub:'',esq:'',dir:'Fechar'}\n"
"};\n"
"function calErro(t){$('cMsg').textContent=t||'';}\n"
"function vaiTela(t){\n"
" calTela=t;calErro('');calPadraoConf=false;\n"
" document.querySelectorAll('#cal .tela').forEach(e=>e.hidden=(e.dataset.t!==t));\n"
" const T=TELAS[t];$('cSub').textContent=T.sub;\n"
" $('cEsq').hidden=!T.esq;$('cEsq').textContent=T.esq;$('cDir').textContent=T.dir;\n"
" $('cEsq').className=(t==='escolha')?'apagar':'sec';\n"
" if(t==='escolha')$('cEsq').disabled=!!(calInfo&&calInfo.padrao);else $('cEsq').disabled=false;\n"
" if(t==='temp1'){$('cReal').value='';setTimeout(()=>$('cReal').focus(),50);}\n"
"}\n"
"async function calLer(){\n"
" try{\n"
"  const d=await (await fetch('/calib',{cache:'no-store'})).json();\n"
"  d.padrao=d.seco===d.pSeco&&d.molh===d.pMolh&&Math.abs(d.aj-d.pAj)<0.05;calInfo=d;\n"
"  document.querySelectorAll('#cal .cMv').forEach(e=>e.textContent=d.mv);\n"
"  $('cTAgora').textContent=d.tb===null?'--':nf(d.tb+d.aj,1);\n"
"  if(calTela==='escolha')$('cEsq').disabled=d.padrao;\n"
" }catch(e){}\n"
"}\n"
"function abreCal(){calAr=calAgua=null;vaiTela('escolha');calLer();clearInterval(calTimer);calTimer=setInterval(calLer,2000);$('cal').showModal();}\n"
"$('cal').addEventListener('close',()=>clearInterval(calTimer));\n"
"$('abreCal').onclick=abreCal;\n"
"$('fechaCal').onclick=()=>$('cal').close();\n"
"document.querySelectorAll('#cal .calOp').forEach(b=>b.onclick=()=>vaiTela(b.dataset.ir));\n"
"$('cReal').oninput=()=>calErro('');\n"
"async function pede(url){const r=await fetch(url,{cache:'no-store'});const t=(await r.text()).trim();if(!r.ok)throw new Error(t||'Não deu certo.');return t;}\n"
"async function medir(){const t=await pede('/calib-medir');const v=parseInt(t,10);if(isNaN(v))throw new Error('Leitura inválida.');return v;}\n"
"$('cEsq').onclick=async()=>{\n"
" if(calTela==='escolha'){\n"
"  if(!calPadraoConf){calPadraoConf=true;$('cEsq').textContent='Voltar mesmo?';return;}\n"
"  try{await pede('/calib-padrao');await calLer();$('cFimTxt').textContent='Voltou ao padrão';vaiTela('fim');}catch(e){calErro(e.message);}\n"
"  return;\n"
" }\n"
" if(calTela==='solo2'){vaiTela('solo1');return;}\n"
" if(calTela==='solo3'){calAr=calAgua=null;vaiTela('solo1');return;}\n"
" vaiTela('escolha');\n"
"};\n"
"$('cDir').onclick=async()=>{\n"
" const b=$('cDir');\n"
" if(calTela==='escolha'||calTela==='fim'){$('cal').close();return;}\n"
" b.disabled=true;\n"
" try{\n"
"  if(calTela==='solo1'){calAr=await medir();$('cArV').textContent=calAr;vaiTela('solo2');}\n"
"  else if(calTela==='solo2'){\n"
"   calAgua=await medir();\n"
"   if(calAr-calAgua<300){calErro('A leitura na água deveria ser bem menor que no ar. Confira se o sensor está na água e refaça.');return;}\n"
"   $('cArV2').textContent=calAr;$('cAguaV').textContent=calAgua;vaiTela('solo3');\n"
"  }\n"
"  else if(calTela==='solo3'){await pede('/calib-solo?ar='+calAr+'&agua='+calAgua);await calLer();$('cFimTxt').textContent='Sensor de solo calibrado';vaiTela('fim');}\n"
"  else if(calTela==='temp1'){\n"
"   const v=$('cReal').value.trim().replace(',','.');\n"
"   if(v===''||isNaN(Number(v))){calErro('Digite a temperatura do termômetro.');return;}\n"
"   await pede('/calib-temp?real='+encodeURIComponent(v));await calLer();$('cFimTxt').textContent='Temperatura calibrada';vaiTela('fim');\n"
"  }\n"
" }catch(e){calErro(e.message==='Failed to fetch'?'Sem conexão. Tente de novo.':e.message);}\n"
" finally{b.disabled=false;}\n"
"};\n"
"function abreTroca(){$('troca').showModal();}\n"
"$('fechaTroca').onclick=$('trocaNao').onclick=()=>$('troca').close();\n"
"let trocando=false,relogio=null;\n"
"$('trocaSim').onclick=()=>{\n"
" $('troca').close();\n"
" trocando=true;clearInterval(relogio);                 // para o monitoramento; os dados ficam na tela\n"
" $('app').classList.remove('off');$('app').classList.add('trocando');$('estado').textContent='Pausado';\n"
" fetch('/esquecer-wifi',{cache:'no-store'}).catch(()=>{});\n"
" // quando o celular entrar na MINHA_PLANTA, abre a pagina de configuracao\n"
" const t=setInterval(()=>{fetch('http:\\/\\/192.168.4.1/redes',{mode:'no-cors',cache:'no-store'}).then(()=>{clearInterval(t);location.href='http:\\/\\/192.168.4.1/';}).catch(()=>{});},2000);\n"
"};\n"
"/* ---------- chaves \"monitorar\" (solo, temperatura, luz) ----------\n"
"   Desligada: card apagado com \"Não monitorado\", sem alerta e o rosto ignora (salvo no ESP32).\n"
"   Solo, temperatura e luz: o LED também ignora. Umidade do ar: só a tela (o LED segue a temperatura). */\n"
"const MON={solo:true,temp:true,luz:true,umid:true};let monMexeu=0;\n"
"[['mSolo','solo'],['mTemp','temp'],['mLuz','luz'],['mUmid','umid']].forEach(([id,k])=>{\n"
" $(id).onchange=()=>{MON[k]=$(id).checked;monMexeu=Date.now();\n"
"  fetch('/monitorar?s='+k+'&v='+(MON[k]?1:0),{cache:'no-store'}).catch(()=>{});atualiza();};\n"
"});\n"
"function leMon(d){                       // so aceita o valor do ESP32 se ninguem mexeu na chave nos ultimos 3 s\n"
" if(typeof d.mon!=='string'||d.mon.length<4||Date.now()-monMexeu<3000)return;\n"
" MON.solo=d.mon[0]==='1';MON.temp=d.mon[1]==='1';MON.luz=d.mon[2]==='1';MON.umid=d.mon[3]==='1';\n"
" $('mSolo').checked=MON.solo;$('mTemp').checked=MON.temp;$('mLuz').checked=MON.luz;$('mUmid').checked=MON.umid;\n"
"}\n"
"function desliga(card,ver){$(card).className='item desl';$(ver).textContent='Não monitorado';}\n"
"let falhas=0;\n"
"async function atualiza(){\n"
" if(trocando)return;\n"
" try{\n"
"  const r=await fetch('/dados',{cache:'no-store'});const d=await r.json();\n"
"  if(trocando)return;\n"
"  falhas=0;$('app').classList.remove('off');$('estado').textContent='Online';\n"
"  if(d.rede){const n=d.sinal>-60?4:d.sinal>-70?3:d.sinal>-80?2:1;let b='';for(let i=1;i<=4;i++)b+='<i class=\"'+(i<=n?'on':'')+'\" style=\"height:'+(2+i*2.25)+'px\"></i>';\n"
"   $('wifi').innerHTML='<span class=\"sinal\" aria-hidden=\"true\">'+b+'</span><span class=\"nr\"></span><span>· '+(n>=3?'Bom':n==2?'Normal':'Ruim')+'</span>';\n"
"   $('wifi').querySelector('.nr').textContent=d.rede;$('wifi').title='Sinal: '+d.sinal+' dBm';}\n"
"  if(!atual&&d.planta)aplicaPlanta(d.planta);\n"
"  leMon(d);\n"
"  $('temp').textContent=d.t===null?'--':nf(d.t,1);\n"
"  $('umid').textContent=d.ur===null?'--':nf(d.ur,0);\n"
"  const solo=(d.solo===null||d.solo<0)?null:d.solo;   // null = sensor de solo com problema\n"
"  $('solo').textContent=solo===null?'--':nf(solo,0);\n"
"  $('agua').style.transform='translateY('+(128-1.28*(solo||0))+'px)';\n"
"  if(!atual)return;\n"
"  const p=PLANTAS[atual];\n"
"  const eSolo=faixa(solo,p.solo),eTemp=faixa(d.t,p.temp),eUmid=faixa(d.ur,p.umid);\n"
"  // luz: avalia o dia de ONTEM inteiro (luz acumulada), nao o instante\n"
"  let eLuz='nd';\n"
"  if(d.dliOntem>=0)eLuz=d.dliOntem<p.dliMin?'baixo':'ok';\n"
"  // terra\n"
"  marca('cSolo',eSolo);\n"
"  $('vSolo').textContent={ok:'Ideal',alto:'Molhada demais',baixo:'Seca, precisa de água',nd:'Sem leitura'}[eSolo];\n"
"  // temperatura\n"
"  // temperatura: so fica vermelha se ficou fora da faixa nos ultimos 30 minutos seguidos\n"
"  let tRuim=false;\n"
"  if(eTemp==='alto'&&d.tmin30!==null&&d.tmin30>p.temp[1])tRuim=true;\n"
"  if(eTemp==='baixo'&&d.tmax30!==null&&d.tmax30<p.temp[0])tRuim=true;\n"
"  if(eTemp==='ok'){marca('cTemp','ok');$('vTemp').textContent='Ideal';}\n"
"  else if(eTemp==='nd'){marca('cTemp','');$('vTemp').textContent='Sem leitura';}\n"
"  else if(tRuim){marca('cTemp','ruim');$('vTemp').textContent=eTemp==='alto'?'Quente demais':'Frio demais';}\n"
"  else{marca('cTemp','');$('vTemp').textContent=eTemp==='alto'?'Quente agora':'Frio agora';}\n"
"  // sol de hoje: quanto da meta do dia ja recebeu\n"
"  if(d.dli>=0){\n"
"   const pct=Math.round(d.dli/p.dliMin*100);\n"
"   $('luzpct').textContent=Math.min(pct,999);$('eLuz').style.width=Math.min(100,pct)+'%';\n"
"   if(pct>=100){marca('cLuz','ok');$('vLuz').textContent='Meta de hoje batida';}\n"
"   else if(d.hora>=16){marca('cLuz','ruim');$('vLuz').textContent='Pouco sol hoje';}\n"
"   else{marca('cLuz','');$('vLuz').textContent='Faltam '+(100-pct)+'%';}\n"
"  }else{$('luzpct').textContent='--';marca('cLuz','');$('vLuz').textContent='Acertando o relógio…';}\n"
"  // umidade do ar\n"
"  if(p.umid){marca('cUmid',eUmid);$('vUmid').textContent={ok:'Ideal',alto:'Úmido demais',baixo:'Seco demais',nd:'Sem leitura'}[eUmid];$('iUmid').textContent='Ideal: '+txtFaixa(p.umid,'%');}\n"
"  else{marca('cUmid',d.ur===null?'':'ok');$('vUmid').textContent=d.ur===null?'Sem leitura':'Sem problema';$('iUmid').textContent='Ideal: ar normal da casa';}\n"
"  // sensores nao monitorados: card apagado e fora do rosto da planta\n"
"  if(!MON.solo)desliga('cSolo','vSolo');\n"
"  if(!MON.temp)desliga('cTemp','vTemp');\n"
"  if(!MON.umid)desliga('cUmid','vUmid');\n"
"  if(!MON.luz)desliga('cLuz','vLuz');\n"
"  const probs=[];              // em ordem de prioridade\n"
"  if(MON.solo&&eSolo==='baixo')probs.push('sede');\n"
"  if(MON.solo&&eSolo==='alto')probs.push('encharcada');\n"
"  if(MON.temp&&tRuim&&eTemp==='alto')probs.push('calor');\n"
"  if(MON.temp&&tRuim&&eTemp==='baixo')probs.push('frio');\n"
"  if(MON.luz&&eLuz==='baixo')probs.push('escuro');\n"
"  if(MON.umid&&eUmid==='baixo')probs.push('arseco');\n"
"  if(MON.umid&&eUmid==='alto')probs.push('arumido');\n"
"  mostraHumor(probs[0]||'feliz');\n"
" }catch(e){if(trocando)return;if(++falhas>=3){$('app').classList.add('off');$('estado').textContent='Sem sinal';}}\n"
"}\n"
"aplicaPlanta('');carregaPlantas().finally(()=>{atualiza();relogio=setInterval(atualiza,2000);});\n"
"</script>\n"
"<script>\n"
"/* modo claro/escuro: botao no topo; a escolha fica salva neste aparelho (navegador) */\n"
"(function(){\n"
" const r=document.documentElement,b=document.getElementById('tema');\n"
" let t=null;try{t=localStorage.getItem('tema');}catch(e){}\n"
" if(t!=='claro'&&t!=='escuro')t=matchMedia('(prefers-color-scheme: dark)').matches?'escuro':'claro';\n"
" r.dataset.tema=t;\n"
" b.onclick=()=>{t=r.dataset.tema==='escuro'?'claro':'escuro';r.dataset.tema=t;try{localStorage.setItem('tema',t);}catch(e){}};\n"
"})();\n"
"</script>\n"
"</body>\n"
"</html>\n";

// ---------- PAGINA_WIFI: pagina de escolher a rede, aberta na MINHA_PLANTA ----------
const char PAGINA_WIFI[] PROGMEM =
"\n"
"<!doctype html>\n"
"<html lang=\"pt-BR\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>Configurar Wi-Fi</title>\n"
"<link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><path d='M32 34C32 26 33 20 35 14' stroke='%234c7d3a' stroke-width='4' fill='none' stroke-linecap='round'/><path d='M34 22C25 11 13 12 7 16C14 27 27 28 34 22Z' fill='%234c7d3a'/><path d='M35 16C41 5 52 3 58 6C55 16 44 21 35 16Z' fill='%236fa257'/><rect x='10' y='32' width='44' height='8' rx='3' fill='%23587565'/><path d='M13 40H51L46 61H18Z' fill='%236f8f7a'/><circle cx='26' cy='47' r='2.6' fill='%23fbfdf8'/><circle cx='38' cy='47' r='2.6' fill='%23fbfdf8'/><path d='M27 53Q32 57 37 53' stroke='%23fbfdf8' stroke-width='2.4' fill='none' stroke-linecap='round'/></svg>\">\n"
"<style>\n"
":root{--bg:#e9efe5;--ink:#1d2a1f;--soft:#5d6d5f;--line:#cfd9ca;--leaf:#4c7d3a;--bad:#c8423b;--panel:#f4f7f1}\n"
"@media (prefers-color-scheme:dark){:root{--bg:#131c15;--ink:#e4ece1;--soft:#98a897;--line:#2a372c;--leaf:#6fa257;--bad:#e8665e;--panel:#1a251c}}\n"
"*{box-sizing:border-box;margin:0}\n"
"body{background:var(--bg);color:var(--ink);font-family:ui-rounded,\"SF Pro Rounded\",\"Segoe UI\",system-ui,sans-serif;padding:20px 16px;display:flex;justify-content:center}\n"
"main{width:100%;max-width:440px}\n"
"h1{font-size:1.45rem;letter-spacing:-.02em}\n"
".sub{color:var(--soft);margin:4px 0 16px;line-height:1.4}\n"
".lista{display:flex;flex-direction:column;gap:8px}\n"
".rede{display:flex;align-items:center;gap:12px;width:100%;text-align:left;font:inherit;color:var(--ink);background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:12px 14px;cursor:pointer}\n"
".rede:focus-visible,button:focus-visible,input:focus-visible{outline:3px solid var(--leaf);outline-offset:2px}\n"
".rede .n{flex:1;font-weight:600;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}\n"
".rede.sel{border-color:var(--leaf);border-width:2px}\n"
".sinal{display:flex;align-items:flex-end;gap:2px;height:16px}\n"
".sinal i{display:block;width:4px;background:var(--line);border-radius:1px}\n"
".sinal i.on{background:var(--leaf)}\n"
".cad{font-size:.8rem;color:var(--soft)}\n"
".form{margin-top:14px;background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:14px;display:none}\n"
".form.on{display:block}\n"
"label{display:block;font-size:.85rem;color:var(--soft);margin-bottom:4px}\n"
"input{width:100%;font:inherit;font-size:1.05rem;padding:11px 12px;border:1px solid var(--line);border-radius:12px;background:var(--bg);color:var(--ink)}\n"
".ver{display:flex;align-items:center;gap:6px;margin-top:8px;font-size:.9rem;color:var(--soft)}\n"
".ver input{width:auto}\n"
".btn{width:100%;margin-top:12px;font:inherit;font-weight:700;font-size:1.05rem;padding:12px;border:none;border-radius:12px;background:var(--leaf);color:#fff;cursor:pointer}\n"
".btn.sec{background:transparent;color:var(--leaf);border:1px solid var(--line)}\n"
".estado{display:none;text-align:center;padding:24px 8px}\n"
".estado.on{display:block}\n"
".estado h2{font-size:1.3rem;margin:12px 0 6px}\n"
".estado p{color:var(--soft);line-height:1.45}\n"
".estado a{color:var(--leaf);font-weight:700}\n"
".roda{width:42px;height:42px;border-radius:50%;border:4px solid var(--line);border-top-color:var(--leaf);margin:0 auto;animation:g 1s linear infinite}\n"
"@keyframes g{to{transform:rotate(360deg)}}\n"
"@media (prefers-reduced-motion:reduce){.roda{animation:none}}\n"
".erro{color:var(--bad);font-weight:600;margin-bottom:10px;display:none}\n"
".erro.on{display:block}\n"
".vazio{color:var(--soft);padding:10px 2px}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<main>\n"
" <section id=\"telaLista\">\n"
"  <h1>Wi-Fi da sua planta</h1>\n"
"  <p class=\"sub\">Escolha a rede da sua casa e digite a senha.</p>\n"
"  <p class=\"erro\" id=\"erro\"></p>\n"
"  <div class=\"lista\" id=\"lista\"><p class=\"vazio\">Procurando redes…</p></div>\n"
"  <div class=\"form\" id=\"form\">\n"
"   <label for=\"senha\">Senha de <b id=\"nomeRede\"></b></label>\n"
"   <input id=\"senha\" type=\"password\" autocomplete=\"current-password\">\n"
"   <label class=\"ver\"><input type=\"checkbox\" id=\"mostrar\"> mostrar senha</label>\n"
"   <button class=\"btn\" id=\"conectar\">Conectar</button>\n"
"  </div>\n"
"  <button class=\"btn sec\" id=\"atualizar\">Procurar redes de novo</button>\n"
" </section>\n"
" <section class=\"estado\" id=\"telaConectando\">\n"
"  <div class=\"roda\"></div>\n"
"  <h2>Conectando…</h2>\n"
"  <p>A sua planta está entrando em <b id=\"nomeRede2\"></b>. Isso leva até 1 minuto.</p>\n"
" </section>\n"
" <section class=\"estado\" id=\"telaOk\">\n"
"  <h2>Conectado!</h2>\n"
"  <p>Seu celular vai voltar para o Wi-Fi da casa e o site abre sozinho.<br><br>\n"
"  Se não abrir, toque em <a id=\"link1\" href=\"http://minhaplanta.local/\">minhaplanta.local</a><br>ou em <a id=\"link2\" href=\"#\"></a></p>\n"
" </section>\n"
"</main>\n"
"<script>\n"
"const $=id=>document.getElementById(id);\n"
"let escolhida='';\n"
"function barras(r){const n=r>-60?4:r>-70?3:r>-80?2:1;let h='';for(let i=1;i<=4;i++)h+='<i class=\"'+(i<=n?'on':'')+'\" style=\"height:'+(4*i)+'px\"></i>';return '<span class=\"sinal\" aria-label=\"sinal '+n+' de 4\">'+h+'</span>';}\n"
"async function procurar(){\n"
" $('lista').innerHTML='<p class=\"vazio\">Procurando redes…</p>';\n"
" try{const r=await fetch('/redes',{cache:'no-store'});const d=await r.json();\n"
"  if(!d.length){$('lista').innerHTML='<p class=\"vazio\">Nenhuma rede encontrada. Toque em \"Procurar redes de novo\".</p>';return;}\n"
"  $('lista').innerHTML='';\n"
"  d.forEach(x=>{const b=document.createElement('button');b.type='button';b.className='rede';\n"
"   b.innerHTML=barras(x.r)+'<span class=\"n\"></span>'+(x.s?'<span class=\"cad\">🔒</span>':'');\n"
"   b.querySelector('.n').textContent=x.n;\n"
"   b.onclick=()=>{document.querySelectorAll('.rede').forEach(e=>e.classList.remove('sel'));b.classList.add('sel');escolhida=x.n;$('nomeRede').textContent=x.n;$('form').classList.add('on');$('senha').value='';$('senha').focus();};\n"
"   $('lista').appendChild(b);});\n"
" }catch(e){$('lista').innerHTML='<p class=\"vazio\">Não consegui procurar. Toque em \"Procurar redes de novo\".</p>';}\n"
"}\n"
"$('atualizar').onclick=procurar;\n"
"$('mostrar').onchange=()=>{$('senha').type=$('mostrar').checked?'text':'password';};\n"
"$('conectar').onclick=async()=>{\n"
" if(!escolhida)return;\n"
" const body='ssid='+encodeURIComponent(escolhida)+'&pass='+encodeURIComponent($('senha').value);\n"
" try{await fetch('/salvar',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});}catch(e){}\n"
" $('nomeRede2').textContent=escolhida;mostra('telaConectando');acompanha();\n"
"};\n"
"function mostra(id){['telaLista','telaConectando','telaOk'].forEach(t=>{$(t).style.display=(t===id?'block':'none');});}\n"
"let ip='';\n"
"async function acompanha(){\n"
" try{const r=await fetch('/status',{cache:'no-store'});const d=await r.json();\n"
"  if(d.estado==='ok'){ip=d.ip;$('link2').textContent=ip;$('link2').href='http://'+ip+'/';mostra('telaOk');abrirQuandoVoltar();return;}\n"
"  if(d.estado==='erro'){mostra('telaLista');$('erro').textContent='Não conectou em '+escolhida+'. Confira a senha e tente de novo.';$('erro').classList.add('on');procurar();return;}\n"
" }catch(e){}\n"
" setTimeout(acompanha,1000);\n"
"}\n"
"// quando o celular voltar para o Wi-Fi da casa, abre o site sozinho\n"
"function abrirQuandoVoltar(){\n"
" const alvos=['http://minhaplanta.local/'];if(ip)alvos.push('http://'+ip+'/');\n"
" // a rede MINHA_PLANTA fecha 3 s depois do \"Conectado!\"; o celular volta para o Wi-Fi da casa e o site abre\n"
" setTimeout(()=>{let foi=false;const t=setInterval(()=>{alvos.forEach(u=>{fetch(u+'dados',{mode:'no-cors',cache:'no-store'}).then(()=>{if(foi)return;foi=true;clearInterval(t);location.href=u;}).catch(()=>{});});},1500);},4000);\n"
"}\n"
"mostra('telaLista');procurar();\n"
"</script>\n"
"</body>\n"
"</html>\n";

// enviarPagina: "/" -> site da planta, ou a pagina de configurar Wi-Fi quando a MINHA_PLANTA esta aberta
void enviarPagina() {
  if (modoReserva) server.send_P(200, "text/html; charset=utf-8", PAGINA_WIFI);
  else             server.send_P(200, "text/html; charset=utf-8", PAGINA);
}

// ---------- Wi-Fi: conectar, configurar, trocar ----------
// conectarWiFi: tenta entrar no Wi-Fi com 4 potencias (o SuperMini as vezes falha na maxima).
// Usado na configuracao pela pagina e nas novas tentativas a cada 2 min.
bool conectarWiFi(const String& nome, const String& senha, bool manterAP) {
  const wifi_power_t POT[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm, WIFI_POWER_15dBm};
  for (int p = 0; p < 4; p++) {
    WiFi.disconnect(false); delay(100);
    WiFi.mode(manterAP ? WIFI_AP_STA : WIFI_STA);
    WiFi.begin(nome.c_str(), senha.c_str());
    WiFi.setTxPower(POT[p]);
    Serial.printf("Conectando em %s (tentativa %d de 4)", nome.c_str(), p + 1);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
      if (manterAP) { dns.processNextRequest(); server.handleClient(); }
      digitalWrite(PINO_LED, (millis() / 150) % 2 ? LOW : HIGH);
      delay(50);
      if ((millis() - t0) % 1000 < 50) Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) return true;
  }
  return false;
}

// abrirModoConfig: abre a rede propria "MINHA_PLANTA" (sem senha) com a pagina de configuracao
void abrirModoConfig() {
  modoReserva = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(AP_NOME);                         // sem senha
  dns.start(53, "*", IPAddress(192,168,4,1));   // todo endereco cai na pagina de configuracao
  Serial.printf("Rede de configuracao aberta: %s (sem senha)  Pagina: http://192.168.4.1\n", AP_NOME);
}

// estado da configuracao pedida pela pagina
enum { CFG_PARADO, CFG_PEDIDO, CFG_OK, CFG_ERRO };
int cfgEstado = CFG_PARADO;
String cfgNome, cfgSenha;
unsigned long msCfgOk = 0, msUltimaTentativa = 0, msUltimoUsoPagina = 0;

// enviarRedes: "/redes" -> lista as redes Wi-Fi vistas (sem repetir nomes), para a pagina de configuracao
void enviarRedes() {
  msUltimoUsoPagina = millis();
  int n = WiFi.scanNetworks();
  String j = "[";
  int usados = 0;
  for (int i = 0; i < n && usados < 25; i++) {
    String nome = WiFi.SSID(i);
    if (nome.length() == 0) continue;
    bool repetida = false;                      // mostra cada nome uma vez (a de sinal mais forte vem primeiro)
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == nome) { repetida = true; break; }
    if (repetida) continue;
    nome.replace("\\", "\\\\"); nome.replace("\"", "\\\"");
    if (usados++) j += ",";
    j += "{\"n\":\"" + nome + "\",\"r\":" + String(WiFi.RSSI(i)) + ",\"s\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "0" : "1") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// salvarWiFi: "/salvar" -> recebe rede e senha da pagina; a conexao e feita em cuidarModoConfig
void salvarWiFi() {
  msUltimoUsoPagina = millis();
  cfgNome = server.arg("ssid"); cfgSenha = server.arg("pass");
  cfgEstado = CFG_PEDIDO;
  server.send(200, "text/plain", "ok");
}

// enviarStatus: "/status" -> diz a pagina se esta conectando, se conectou (com o IP) ou se deu erro
void enviarStatus() {
  msUltimoUsoPagina = millis();
  String e = cfgEstado == CFG_OK ? "ok" : cfgEstado == CFG_ERRO ? "erro" : "conectando";
  String j = "{\"estado\":\"" + e + "\",\"ip\":\"" + (cfgEstado == CFG_OK ? WiFi.localIP().toString() : String("")) + "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// esquecerWiFi: "/esquecer-wifi" (botao Trocar) -> apaga a rede salva e reinicia abrindo a MINHA_PLANTA
void esquecerWiFi() {
  server.send(200, "text/html; charset=utf-8",
    "<meta name=viewport content='width=device-width,initial-scale=1'><body style='font-family:system-ui;padding:24px'>"
    "<h2>Pronto</h2><p>A sua planta vai abrir a rede <b>MINHA_PLANTA</b>. Conecte nela para escolher o novo Wi-Fi.</p>");
  prefs.remove("wNome"); prefs.remove("wSenha");
  delay(800);
  ESP.restart();
}

// cuidarModoConfig: roda no loop enquanto a MINHA_PLANTA esta aberta: conecta na rede pedida pela pagina,
// reinicia quando conecta e tenta de novo a rede salva a cada 2 min
void cuidarModoConfig() {
  dns.processNextRequest();
  if (cfgEstado == CFG_PEDIDO) {
    cfgEstado = CFG_PARADO;
    if (conectarWiFi(cfgNome, cfgSenha, true)) {
      prefs.putString("wNome", cfgNome); prefs.putString("wSenha", cfgSenha);
      Serial.printf("Conectado em %s! IP %s. Reiniciando em 3 s para usar so o Wi-Fi da casa.\n", cfgNome.c_str(), WiFi.localIP().toString().c_str());
      cfgEstado = CFG_OK; msCfgOk = millis();
    } else {
      Serial.println("Nao conectou. Confira a senha.");
      WiFi.disconnect(false);
      cfgEstado = CFG_ERRO;
    }
  }
  if (cfgEstado == CFG_OK && millis() - msCfgOk > 3000) {   // 3 s: tempo da pagina receber o "Conectado!"
    WiFi.softAPdisconnect(true);                              // fecha a MINHA_PLANTA: o celular volta logo para o Wi-Fi da casa
    delay(100);
    ESP.restart();
  }
  // Wi-Fi salvo existe mas caiu: tenta de novo a cada 2 minutos (se ninguem estiver configurando)
  if (wifiNome.length() && cfgEstado != CFG_OK && millis() - msUltimaTentativa > 120000UL && millis() - msUltimoUsoPagina > 120000UL) {
    msUltimaTentativa = millis();
    Serial.println("Tentando de novo o Wi-Fi salvo...");
    if (conectarWiFi(wifiNome, wifiSenha, true)) { Serial.println("Voltou! Reiniciando."); delay(300); ESP.restart(); }
    WiFi.disconnect(false);
  }
}

// naoEncontradoOuPortal: endereco desconhecido -> na configuracao manda para a pagina de Wi-Fi
// (faz a pagina abrir sozinha no celular); fora dela responde "nao encontrada"
void naoEncontradoOuPortal() {
  if (modoReserva) {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Pagina nao encontrada. Use http://minhaplanta.local");
  }
}

// horaAtual: hora do dia pela internet; -1 se ainda nao sabe
int horaAtual() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return -1;
  return t.tm_hour;
}

// ---------- sol do dia, separado por planta ----------
// Cada planta tem o seu proprio sol (chave "L"+id). So a planta SELECIONADA soma;
// as outras ficam pausadas. Quem sai da selecao no meio do dia fica com o dia "incompleto"
// (o total desse dia nao vale como "ontem").
struct SolSalvo { int dia; int ano; int ini; float hoje; float ontem; };

// chaveLuz: nome da chave na memoria onde fica o sol de cada planta ("L" + id)
String chaveLuz(const String& id) { String k = "L" + id; if (k.length() > 15) k = k.substring(0, 15); return k; }

// salvarLuz: guarda o sol da planta ativa na memoria permanente (nao perde ao tirar da tomada)
void salvarLuz() {
  if (plantaLuz.length() == 0) return;
  SolSalvo d = { diaAtual, anoAtual, horaInicio, dliHoje, dliOntem };
  prefs.putBytes(chaveLuz(plantaLuz).c_str(), &d, sizeof(d));
  msUltimoSalvo = millis();
}

// pausarLuz: pausa a planta que estava somando (o dia dela fica incompleto)
void pausarLuz() {
  if (plantaLuz.length() == 0) return;
  horaInicio = 24;
  salvarLuz();
  plantaLuz = "";
}

// ativarLuz: passa a somar o sol da planta selecionada, continuando de onde ela parou
void ativarLuz(struct tm& t) {
  pausarLuz();
  if (planta.length() == 0) return;
  SolSalvo d = { -1, -1, 24, 0, -1 };
  String k = chaveLuz(planta);
  if (prefs.isKey(k.c_str())) {
    prefs.getBytes(k.c_str(), &d, sizeof(d));
  } else if (prefs.isKey("lDia")) {           // versoes antigas: um sol so -> vai para a planta selecionada
    d.dia = prefs.getInt("lDia", -1); d.ano = prefs.getInt("lAno", -1); d.ini = prefs.getInt("lIni", 24);
    d.hoje = prefs.getFloat("lHoje", 0); d.ontem = prefs.getFloat("lOntem", -1);
    prefs.remove("lDia"); prefs.remove("lAno"); prefs.remove("lIni"); prefs.remove("lHoje"); prefs.remove("lOntem");
  }
  bool mesmoDia = (d.dia == t.tm_yday && d.ano == t.tm_year);
  bool diaAnterior = (d.ano == t.tm_year && d.dia == t.tm_yday - 1) ||
                     (d.ano == t.tm_year - 1 && t.tm_yday == 0 && d.dia >= 364);
  diaAtual = t.tm_yday; anoAtual = t.tm_year;
  if (mesmoDia) {                              // continua o dia de hoje
    dliHoje = d.hoje; dliOntem = d.ontem; horaInicio = d.ini;
  } else if (diaAnterior) {                    // parou ontem e voltou hoje
    dliOntem = (d.ini < 6) ? d.hoje : -1;      // ontem so vale se comecou antes das 6h e nao foi pausada
    dliHoje = 0; horaInicio = t.tm_hour;
  } else {                                     // parada ha mais de um dia (ou planta nova)
    dliOntem = -1; dliHoje = 0; horaInicio = t.tm_hour;
  }
  plantaLuz = planta;
  msUltimaLuz = millis();
  salvarLuz();
}

// somarLuz: soma a luz de cada leitura na planta selecionada; na virada do dia guarda o total de ontem
void somarLuz() {
  unsigned long agora = millis();
  float dt = (msUltimaLuz == 0) ? 0 : (agora - msUltimaLuz) / 1000.0f;
  msUltimaLuz = agora;
  struct tm t;
  if (!getLocalTime(&t, 0)) return;            // sem hora certa, nao soma
  if (planta.length() == 0) { pausarLuz(); return; }   // nenhuma planta escolhida: nao soma
  if (plantaLuz != planta) { ativarLuz(t); return; }   // trocou de planta (ou acabou de ligar)

  if (t.tm_yday != diaAtual) {                 // virou o dia com ela ligada
    dliOntem = (horaInicio < 6) ? dliHoje : -1;
    dliHoje = 0;
    diaAtual = t.tm_yday; anoAtual = t.tm_year;
    horaInicio = t.tm_hour;                    // meia-noite: o dia novo vale inteiro
    salvarLuz();
  }
  if (okB && dt > 0 && dt < 60) dliHoje += vLux * 0.0185f * dt / 1000000.0f;
  if (millis() - msUltimoSalvo >= 300000UL) salvarLuz();   // salva a cada 5 minutos
}

// salvarPlanta: "/planta?id=" -> guarda a planta escolhida no site e troca o sol para ela
void salvarPlanta() {
  String id = server.arg("id");
  if (id.length() > 20) id = id.substring(0, 20);
  planta = id;
  prefs.putString("planta", planta);
  struct tm t;
  if (getLocalTime(&t, 0)) { if (planta.length()) ativarLuz(t); else pausarLuz(); }   // troca o sol na hora
  server.send(200, "text/plain", "ok");
}

// enviarDados: "/dados" -> todas as leituras em JSON (o site pede a cada 2 s)
void enviarDados() {
  char j[480];
  char tmn[12], tmx[12];
  if (tempQtd >= 30) {
    float mn = tempMin[0], mx = tempMin[0];
    for (int i = 1; i < 30; i++) { if (tempMin[i] < mn) mn = tempMin[i]; if (tempMin[i] > mx) mx = tempMin[i]; }
    dtostrf(mn, 0, 1, tmn); dtostrf(mx, 0, 1, tmx);
  } else { strcpy(tmn, "null"); strcpy(tmx, "null"); }
  char dh[12], dO[12];
  if (horaAtual() >= 0 && plantaLuz.length() && plantaLuz == planta) dtostrf(dliHoje, 0, 2, dh); else strcpy(dh, "-1");
  if (plantaLuz.length() && plantaLuz == planta) dtostrf(dliOntem, 0, 2, dO); else strcpy(dO, "-1");
  char t[12], u[12], l[12];
  if (okA) { dtostrf(vTemp, 0, 1, t); dtostrf(vUmid, 0, 1, u); } else { strcpy(t, "null"); strcpy(u, "null"); }
  if (okB) dtostrf(vLux, 0, 0, l); else strcpy(l, "null");
  String rede = WiFi.SSID();
  rede.replace("\\", "\\\\"); rede.replace("\"", "\\\"");
  snprintf(j, sizeof(j), "{\"rede\":\"%s\",\"sinal\":%d,\"t\":%s,\"ur\":%s,\"lux\":%s,\"solo\":%d,\"mv\":%d,\"hora\":%d,\"dli\":%s,\"dliOntem\":%s,\"tmin30\":%s,\"tmax30\":%s,\"planta\":\"%s\",\"mon\":\"%d%d%d%d\"}",
           rede.c_str(), (int)WiFi.RSSI(), t, u, l, okS ? vSoloPct : -1, vSoloMV, horaAtual(), dh, dO, tmn, tmx, planta.c_str(), monSolo, monTemp, monLuz, monUmid);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// ---------- calibracao dos sensores (vale para todas as plantas) ----------
// carregarMonitor: le quais sensores estao monitorados (chave nos cards do site)
void carregarMonitor() {
  monSolo = prefs.getBool("mSolo", true);
  monTemp = prefs.getBool("mTemp", true);
  monLuz  = prefs.getBool("mLuz", true);
  monUmid = prefs.getBool("mUmid", true);
}

// salvarMonitor: "/monitorar?s=solo|temp|luz|umid&v=0|1" -> liga/desliga o monitoramento de um sensor
void salvarMonitor() {
  String q = server.arg("s");
  bool v = server.arg("v") == "1";
  if (q == "solo")      { monSolo = v; prefs.putBool("mSolo", v); }
  else if (q == "temp") { monTemp = v; prefs.putBool("mTemp", v); }
  else if (q == "luz")  { monLuz = v;  prefs.putBool("mLuz", v); }
  else if (q == "umid") { monUmid = v; prefs.putBool("mUmid", v); }
  else { server.send(400, "text/plain", "sensor invalido"); return; }
  server.send(200, "text/plain", "ok");
}

// carregarCalibracao: le a calibracao salva (ou usa o padrao)
void carregarCalibracao() {
  soloSeco    = prefs.getInt("cSeco", SOLO_SECO);
  soloMolhado = prefs.getInt("cMolh", SOLO_MOLHADO);
  ajusteTemp  = prefs.getFloat("cTemp", AJUSTE_PADRAO);
  if (soloSeco - soloMolhado < 300) { soloSeco = SOLO_SECO; soloMolhado = SOLO_MOLHADO; }   // protecao
}

// enviarCalibracao: "/calib" -> calibracao em uso, padrao e leituras atuais (para a janela Calibrar)
void enviarCalibracao() {
  char j[300];
  char tb[12], aj[12], ajp[12];
  if (okA) dtostrf(vTempSensor, 0, 1, tb); else strcpy(tb, "null");
  dtostrf(ajusteTemp, 0, 1, aj); dtostrf(AJUSTE_PADRAO, 0, 1, ajp);
  snprintf(j, sizeof(j), "{\"seco\":%d,\"molh\":%d,\"aj\":%s,\"mv\":%d,\"solo\":%d,\"tb\":%s,"
           "\"pSeco\":%d,\"pMolh\":%d,\"pAj\":%s}",
           soloSeco, soloMolhado, aj, vSoloMV, vSoloPct, tb, SOLO_SECO, SOLO_MOLHADO, ajp);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// medirSolo: "/calib-medir" -> mede o sensor de solo agora (passo a passo; nao salva)
void medirSolo() {
  int mv = soloLerMV();
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/plain", String(mv));
}

// calibrarSolo: "/calib-solo?ar=&agua=" -> salva os valores medidos no ar (0 %) e na agua (100 %)
void calibrarSolo() {
  int ar = server.arg("ar").toInt(), agua = server.arg("agua").toInt();
  if (ar <= 0 || agua <= 0 || ar > 3300 || agua > 3300) { server.send(400, "text/plain; charset=utf-8", "Valores inválidos."); return; }
  if (ar - agua < 300) {                       // na agua tem que dar bem menos mV que no ar
    server.send(400, "text/plain; charset=utf-8", "A leitura na água deveria ser bem menor que no ar. Refaça.");
    return;
  }
  soloSeco = ar; soloMolhado = agua;
  prefs.putInt("cSeco", soloSeco); prefs.putInt("cMolh", soloMolhado);
  vSoloPct = soloPorcento(vSoloMV);
  Serial.printf("Solo calibrado: ar %d mV, agua %d mV\n", soloSeco, soloMolhado);
  server.send(200, "text/plain", "ok");
}

// calibrarTemp: "/calib-temp?real=" -> calcula e salva o ajuste para a tela mostrar a temperatura do termometro
void calibrarTemp() {
  String r = server.arg("real"); r.trim(); r.replace(",", ".");
  float real = r.toFloat();
  if (!okA) { server.send(400, "text/plain; charset=utf-8", "O sensor de temperatura está sem leitura."); return; }
  if (r.length() == 0 || real < -10 || real > 50) { server.send(400, "text/plain; charset=utf-8", "Use um valor de -10 a 50 °C."); return; }
  float aj = real - vTempSensor;
  if (aj < -15 || aj > 15) { server.send(400, "text/plain; charset=utf-8", "Diferença grande demais. Confira o termômetro."); return; }
  ajusteTemp = aj;
  prefs.putFloat("cTemp", ajusteTemp);
  Serial.printf("Temperatura calibrada: ajuste %.1f C\n", ajusteTemp);
  server.send(200, "text/plain", "ok");
}

// calibracaoPadrao: "/calib-padrao" -> apaga a calibracao salva e volta ao padrao
void calibracaoPadrao() {
  const char* k[] = {"cSeco", "cMolh", "cTemp"};
  for (int i = 0; i < 3; i++) if (prefs.isKey(k[i])) prefs.remove(k[i]);
  carregarCalibracao();
  Serial.println("Calibracao voltou ao padrao.");
  server.send(200, "text/plain", "ok");
}

// ---------- plantas criadas pelo usuario (ate 10, salvas na memoria) ----------
// cada uma fica na chave "c0".."c9" no formato: nome|soloMin|soloMax|tempMin|tempMax|luz|arMin|arMax  (-1 = nao usa)
const int MAX_PLANTAS = 10;

// enviarPlantas: "/plantas" -> lista as plantas criadas pelo usuario
void enviarPlantas() {
  String j = "[";
  bool primeira = true;
  for (int i = 0; i < MAX_PLANTAS; i++) {
    String k = "c" + String(i);
    if (!prefs.isKey(k.c_str())) continue;
    String v = prefs.getString(k.c_str(), "");
    if (!v.length()) continue;
    if (!primeira) j += ",";
    primeira = false;
    j += "{\"id\":\"" + k + "\",\"v\":\"" + v + "\"}";   // o nome ja foi limpo ao salvar (sem aspas nem barras)
  }
  j += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// numeroArg: le um numero enviado pela pagina e confere se esta na faixa (ok = false se nao estiver)
String numeroArg(const char* nome, float minimo, float maximo, bool opcional, bool &ok) {
  String a = server.arg(nome);
  a.trim();
  if (opcional && (a.length() == 0 || a == "-1")) return "-1";
  float v = a.toFloat();
  if (a.length() == 0 || v < minimo || v > maximo) { ok = false; return ""; }
  return String(v, (v == (int)v) ? 0 : 1);
}

// salvarPlantaCriada: "/salvar-planta" -> cria ou edita uma planta (nome e faixas)
void salvarPlantaCriada() {
  String id = server.arg("id");
  String nome = server.arg("n");
  nome.trim();
  for (unsigned int i = 0; i < nome.length(); i++) {          // tira caracteres que quebrariam o formato
    char c = nome[i];
    if (c == '|' || c == '"' || c == '\\' || c == '<' || c == '>' || (c >= 0 && c < 32)) nome.setCharAt(i, ' ');
  }
  if (nome.length() > 60) nome = nome.substring(0, 60);   // a pagina limita a 24 letras
  bool ok = nome.length() > 0;
  String s0 = numeroArg("s0", 0, 100, false, ok), s1 = numeroArg("s1", 0, 100, false, ok);
  String t0 = numeroArg("t0", -10, 50, false, ok), t1 = numeroArg("t1", -10, 50, false, ok);
  String l  = numeroArg("l", 1, 40, false, ok);
  String u0 = numeroArg("u0", 0, 100, true, ok), u1 = numeroArg("u1", 0, 100, true, ok);
  if (!ok) { server.send(400, "text/plain", "dados invalidos"); return; }
  int slot = -1;
  if (id.length() == 2 && id[0] == 'c' && id[1] >= '0' && id[1] <= '9') slot = id[1] - '0';   // editar
  if (slot < 0) {                                                                          // nova: primeira vaga livre
    for (int i = 0; i < MAX_PLANTAS; i++) if (!prefs.isKey(("c" + String(i)).c_str())) { slot = i; break; }
  }
  if (slot < 0) { server.send(409, "text/plain", "limite de 10 plantas"); return; }
  String v = nome + "|" + s0 + "|" + s1 + "|" + t0 + "|" + t1 + "|" + l + "|" + u0 + "|" + u1;
  String k = "c" + String(slot);
  prefs.putString(k.c_str(), v);
  Serial.printf("Planta salva (%s): %s\n", k.c_str(), v.c_str());
  server.send(200, "text/plain", k);
}

// apagarPlantaCriada: "/apagar-planta?id=" -> apaga a planta e o sol dela
void apagarPlantaCriada() {
  String id = server.arg("id");
  if (!(id.length() == 2 && id[0] == 'c' && id[1] >= '0' && id[1] <= '9')) { server.send(400, "text/plain", "id invalido"); return; }
  if (plantaLuz == id) plantaLuz = "";                        // nao regrava o sol de uma planta apagada
  if (prefs.isKey(id.c_str())) prefs.remove(id.c_str());
  String kl = chaveLuz(id);
  if (prefs.isKey(kl.c_str())) prefs.remove(kl.c_str());      // apaga o sol dela tambem
  if (planta == id) { planta = ""; prefs.putString("planta", planta); }
  server.send(200, "text/plain", "ok");
}

// umidade relativa depende da temperatura: o ar dentro e fora da caixa tem a mesma
// quantidade de agua, mas o sensor esta mais quente. Recalcula para a temperatura real (formula de Magnus).
// pressaoSat: pressao de vapor de saturacao (hPa) na temperatura t (formula de Magnus)
float pressaoSat(float t) { return 6.112f * expf(17.62f * t / (243.12f + t)); }
// corrigeUmidade: umidade do ar recalculada para a temperatura real (o sensor esta mais quente na caixa)
float corrigeUmidade(float urSensor, float tSensor, float tReal) {
  float ur = urSensor * pressaoSat(tSensor) / pressaoSat(tReal);
  if (ur > 100.0f) ur = 100.0f;
  if (ur < 0.0f) ur = 0.0f;
  return ur;
}

// ---------- leitura dos sensores ----------
// lerSensores: le os 3 sensores (a cada 2 s), aplica as correcoes, soma o sol e mostra no Monitor Serial
void lerSensores() {
  if (!temAHT) temAHT = ahtIniciar();
  if (!temBH)  temBH  = bhIniciar();
  okA = temAHT && ahtLer(vTemp, vUmid);
  if (okA) {
    vTempSensor = vTemp;                // temperatura no sensor (aquecido pelo ESP32 dentro da caixa)
    vTemp += ajusteTemp;                // temperatura real do ambiente (correcao automatica)
    vUmid = corrigeUmidade(vUmid, vTempSensor, vTemp);
  }
  okB = temBH  && bhLer(vLux);
  if (!okA) {                           // temperatura falhou: destrava a linha e religa o sensor
    reiniciarI2C(TEMP_SDA, TEMP_SCL);
    digitalWrite(TEMP_VCC, LOW); delay(100);
    ligarTemp();
    temAHT = ahtIniciar();
  }
  if (!okB) {                           // luz falhou: destrava a linha
    reiniciarI2C(LUZ_SDA, LUZ_SCL);
    temBH = bhIniciar();
  }
  somarLuz();
  guardarTemp();
  vSoloMV  = soloLerMV();
  vSoloPct = soloPorcento(vSoloMV);
  // sensor de solo: leitura bem abaixo da agua ou bem acima do ar = solto ou com defeito.
  // O problema fica valendo ate 10 leituras normais seguidas (~20 s), para o LED e o site nao ficarem alternando.
  bool soloNormal = (vSoloMV >= soloMolhado - SOLO_FOLGA_AGUA && vSoloMV <= soloSeco + SOLO_FOLGA_AR);
  if (!soloNormal) { soloFalha = true; soloBoas = 0; }
  else if (soloFalha && ++soloBoas >= 10) soloFalha = false;
  okS = !soloFalha;
  Serial.printf("Temp: %.1f C  Ar: %.1f %%  Luz: %.0f lux  Solo: %d %% (%d mV%s)  Wi-Fi: %s",
                vTemp, vUmid, vLux, vSoloPct, vSoloMV, okS ? "" : " PROBLEMA", modoReserva ? "reserva" : (WiFi.status() == WL_CONNECTED ? "ok" : "sem conexao"));
  if (!modoReserva && WiFi.status() == WL_CONNECTED) Serial.printf("  Sinal: %d dBm", (int)WiFi.RSSI());
  Serial.println();
}

// ---------- LED ----------
// atualizarLED: mostra o estado no LED azul (ver a tabela no comeco do arquivo).
// Problema num sensor monitorado: o padrao dele repete direto ate resolver (nunca volta a "1 por segundo" no meio).
// Sensor nao monitorado (chave desligada no site): o LED ignora o problema dele.
void atualizarLED() {
  unsigned long ms = millis();
  bool aceso;
  int falhas[3], n = 0;
  if (!okS && monSolo) falhas[n++] = 2;        // 2 piscadas: solo
  if (!okA && monTemp) falhas[n++] = 3;        // 3 piscadas: temperatura
  if (!okB && monLuz)  falhas[n++] = 4;        // 4 piscadas: luz
  if (modoReserva) {
    aceso = true;                              // MINHA_PLANTA aberta
  } else if (n > 0) {
    int qtd = falhas[(ms / 3000) % n];         // mais de um com problema: um padrao de cada vez
    unsigned long f = ms % 3000;               // 150 ms aceso, 150 ms apagado, depois pausa ate 3 s
    aceso = (f < (unsigned long)qtd * 300) && ((f % 300) < 150);
  } else if (WiFi.status() == WL_CONNECTED) {
    aceso = (ms % 1000) < 500;                 // tudo ok: 1 vez por segundo
  } else {
    aceso = (ms % 200) < 100;                  // sem Wi-Fi: bem rapido
  }
  digitalWrite(PINO_LED, aceso ? LOW : HIGH);
}

// setup: liga os sensores, le o que esta salvo, conecta no Wi-Fi (ou abre a MINHA_PLANTA) e inicia o site
void setup() {
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BOOT, INPUT_PULLUP);
  digitalWrite(PINO_LED, HIGH);
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);          // nao trava esperando o Monitor Serial (funciona sem computador)
#endif
  delay(1500);
  Serial.println("\n=== Minha Planta - v1 ===");

  analogSetAttenuation(ADC_11db);
  ligarTemp();
  testarLinhas("luz", LUZ_SDA, LUZ_SCL);
  testarLinhas("temperatura", TEMP_SDA, TEMP_SCL);
  procurarI2C("luz", LUZ_SDA, LUZ_SCL);
  procurarI2C("temperatura", TEMP_SDA, TEMP_SCL);
  temAHT = ahtIniciar();
  temBH  = bhIniciar();

  prefs.begin("minhaplanta", false);
  planta = prefs.getString("planta", "");
  carregarCalibracao();
  carregarMonitor();

  // Wi-Fi: usa a rede salva; sem rede salva (ou se nao conectar), abre a rede MINHA_PLANTA
  WiFi.setHostname(NOME_SITE);
  wifiNome = prefs.getString("wNome", "");
  wifiSenha = prefs.getString("wSenha", "");
  bool conectado = false;
  if (wifiNome.length()) {
    // conecta na rede escolhida na pagina: 4 potencias, 12 s cada
    WiFi.mode(WIFI_STA);
    String rede = wifiNome;
    const wifi_power_t POTENCIAS[] = {WIFI_POWER_8_5dBm, WIFI_POWER_11dBm, WIFI_POWER_13dBm, WIFI_POWER_15dBm};
    for (int p = 0; p < 4 && WiFi.status() != WL_CONNECTED; p++) {
      WiFi.disconnect(true); delay(200);
      WiFi.mode(WIFI_STA);
      WiFi.begin(rede.c_str(), wifiSenha.c_str());
      WiFi.setTxPower(POTENCIAS[p]);
      Serial.printf("Conectando em %s (tentativa %d de 4)", rede.c_str(), p + 1);
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
        digitalWrite(PINO_LED, (millis() / 150) % 2 ? LOW : HIGH);   // pisca rapido enquanto conecta
        delay(150);
        Serial.print(".");
      }
      Serial.println();
    }
    conectado = (WiFi.status() == WL_CONNECTED);
    if (!conectado) Serial.printf("Nao conectou em %s. Codigo do Wi-Fi: %d\n", rede.c_str(), WiFi.status());
  } else {
    Serial.println("Nenhum Wi-Fi salvo ainda.");
  }
  if (conectado) {
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);                         // sem economia de energia no Wi-Fi: a pagina responde mais rapido
    configTzTime("<-03>3", "pool.ntp.org", "time.google.com");   // hora de Brasilia (para saber se e dia)
    Serial.printf("Conectado em %s! Pagina: http://minhaplanta.local  ou  http://%s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    abrirModoConfig();
    msUltimaTentativa = millis();
  }
  MDNS.begin(NOME_SITE);                          // http://minhaplanta.local
  MDNS.addService("http", "tcp", 80);

  server.on("/", enviarPagina);
  server.on("/dados", enviarDados);
  server.on("/planta", salvarPlanta);
  server.on("/plantas", enviarPlantas);
  server.on("/salvar-planta", salvarPlantaCriada);
  server.on("/apagar-planta", apagarPlantaCriada);
  server.on("/calib", enviarCalibracao);
  server.on("/monitorar", salvarMonitor);
  server.on("/calib-medir", medirSolo);
  server.on("/calib-solo", calibrarSolo);
  server.on("/calib-temp", calibrarTemp);
  server.on("/calib-padrao", calibracaoPadrao);
  server.on("/redes", enviarRedes);
  server.on("/salvar", HTTP_POST, salvarWiFi);
  server.on("/status", enviarStatus);
  server.on("/esquecer-wifi", esquecerWiFi);
  server.onNotFound(naoEncontradoOuPortal);
  server.begin();
  lerSensores();
}

unsigned long ultimaLeitura = 0;

// verBotaoBoot: botao BOOT segurado 5 s (com a placa ja ligada) abre a rede MINHA_PLANTA
unsigned long bootDesde = 0;
void verBotaoBoot() {
  if (digitalRead(PINO_BOOT) == LOW) {
    if (bootDesde == 0) bootDesde = millis();
    if (!modoReserva && millis() - bootDesde >= 5000) {
      Serial.println("Botao BOOT segurado 5 s: abrindo a rede MINHA_PLANTA.");
      abrirModoConfig();
      msUltimaTentativa = millis();
    }
  } else {
    bootDesde = 0;
  }
}

// loop: atende o site, confere o botao BOOT, le os sensores a cada 2 s e atualiza o LED
void loop() {
  server.handleClient();
  verBotaoBoot();
  if (modoReserva) cuidarModoConfig();
  if (millis() - ultimaLeitura >= 2000) {
    ultimaLeitura = millis();
    lerSensores();
  }
  atualizarLED();
  delay(2);
}
