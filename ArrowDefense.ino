// =============================================================
//  Arrow Defense  -  テープLED + ESP32 ゲーム
//
//  ・テープLEDの上端から色付きの「矢印」が落ちてくる
//  ・同じ色のボタンを押すと、下端からその色の「ミサイル」が発射される
//  ・ミサイルと矢印が同じ色 → 矢印を破壊
//  ・色が違う → 矢印は壊れず、ダメージ+1
//  ・矢印が下端（基地）に到達してもダメージ+1
//  ・ダメージ10回でゲームオーバー
//
//  おもしろ要素
//  ・コンボ   : 連続で破壊するとコンボ。10コンボごとにHPが1回復
//               コンボが続くと1体あたりの得点アップ、ミサイルも長くなる
//  ・レベル   : 10体破壊ごとにレベルアップ（速く・多くなる）
//  ・特殊な矢印
//      アーマー   (尾が白)       : 2回当てないと壊れない。1回目は押し戻される
//      カメレオン (Lv3〜)       : 落ちながら色が変わる。変わる直前に点滅
//      レインボー (虹色)         : どの色でも当たればOK。画面の矢印を全部破壊！
//  ・ボス戦   : Lv5ごとに出現。上半分が赤く点滅する警告のあと、
//               色の節がつながった長いボスが降りてくる。
//               一番下の節の色を順番に撃って全部壊せば撃破（ボーナス＋HP回復）
//               色を間違えるとボスが前進、ボスは子分の矢印も撃ってくる
//               基地に到達されると大ダメージ（3）でボスは跳ね返る
//  ・ハイスコア: 電源を切っても保存。更新するとお祝い演出
//  ・効果音   : 圧電スピーカー（パッシブブザー）をつなぐと鳴る（任意）
//
//  ・モード   : ノーマル / リズム / ポン（2人・CPU） / サイモン（記憶ゲーム）
//
//  対応ボード: ESP32-S3 SuperMini / ESP32-C3 SuperMini / Seeeduino XIAO (SAMD21) /
//              Seeed XIAO nRF52840・XIAO ESP32C3（充電回路つき） / ESP32（ピンは自動で切り替え）
//  必要ライブラリ: FastLED, TM1637 (Avishay Orpaz)
//                  XIAO(SAMD21) の場合は FlashStorage_SAMD も（ハイスコア保存用）
//                  XIAO nRF52840 は追加ライブラリ不要（ボードパッケージ「Seeed nRF52 Boards」に同梱）
// =============================================================
#include <FastLED.h>
#include <stdarg.h>

// ---------- ハードウェア設定（環境に合わせて変更） ----------
#if defined(ARDUINO_XIAO_ESP32C3)  // Seeed XIAO ESP32C3（充電回路つき）
// D0(GPIO2)・D8(GPIO8)・D9(GPIO9) は起動モードに関係するピンなので、ボタンなどはつながない
// （I2Sアンプへの出力としてなら使える）
#define USE_I2S 1                              // 音の出し方: 1 = I2S（PCM5102A か MAX98357A）、0 = パッシブブザー
#define I2S_DIN_PIN 20                         // D7 → DIN
#define I2S_BCLK_PIN 8                         // D8 → BCK（MAX98357A では BCLK）
#define I2S_LRC_PIN 9                          // D9 → LCK（MAX98357A では LRC）
#define LED_PIN 10                             // D10
#define BUZZER_PIN 20                          // D7（USE_I2S が 0 のとき）。使わないなら -1
#define SEG_CLK_PIN 2                          // D0（7セグモジュールのプルアップ抵抗で起動時に HIGH になるので、ここなら安全）
#define SEG_DIO_PIN 21                         // D6
#define POWER_PIN 7                            // D5 → MOSFETモジュールの TRIG（テープLEDとDACの電源を入り切り）。使わないなら -1
const uint8_t BUTTON_PINS[] = { 3, 4, 5, 6 };  // D1 赤・D2 緑・D3 青・D4 黄
#elif defined(CONFIG_IDF_TARGET_ESP32C3)  // ESP32-C3 SuperMini
// GPIO 2/8/9 は起動モードに関係するピン（8=青LED, 9=BOOTボタン）なので使わない
#define LED_PIN 10
#define BUZZER_PIN 7  // 使わないなら -1

#define SEG_CLK_PIN 5
#define SEG_DIO_PIN 6
const uint8_t BUTTON_PINS[] = { 0, 1, 3, 4 };  // 赤・緑・青・黄
#elif defined(CONFIG_IDF_TARGET_ESP32S3)  // ESP32-S3 SuperMini
// GPIO 0/3/45/46 は起動モードに関係、19/20 はUSB、48 は基板のRGB LED なので使わない
#define USE_I2S 0        // 音の出し方: 1 = I2Sアンプ(MAX98357A)＋スピーカー、0 = パッシブブザー
#define I2S_DIN_PIN 11   // → アンプの DIN
#define I2S_BCLK_PIN 12  // → アンプの BCLK
#define I2S_LRC_PIN 13   // → アンプの LRC
#define LED_PIN 10
#define BUZZER_PIN 7  // 使わないなら -1
#define SEG_CLK_PIN 5
#define SEG_DIO_PIN 6
const uint8_t BUTTON_PINS[] = { 1, 2, 4, 8 };  // 赤・緑・青・黄
#elif defined(ESP32)
#define LED_PIN 5      // テープLEDのDIN
#define BUZZER_PIN 32  // パッシブブザー。使わないなら -1
#define SEG_CLK_PIN 18
#define SEG_DIO_PIN 19
const uint8_t BUTTON_PINS[] = { 25, 26, 27, 33 };  // 赤・緑・青・黄
#else                  // Seeeduino XIAO (SAMD21) / Seeed XIAO nRF52840（ピンの並びは同じ）
#define LED_PIN 10     // D10
#define BUZZER_PIN 8   // D8。使わないなら -1
#define SEG_CLK_PIN 5  // D5
#define SEG_DIO_PIN 6  // D6
const uint8_t BUTTON_PINS[] = { 1, 2, 3, 4 };  // D1 赤・D2 緑・D3 青・D4 黄
#endif
#ifndef USE_I2S
#define USE_I2S 0  // I2Sアンプは XIAO ESP32C3 と ESP32-S3 SuperMini だけ対応
#endif
// 自動で電源を切る（XIAO ESP32C3 ＋ MOSFETモジュールのときだけ）
// ゲーム中以外で、この時間ボタンを押さないと、テープLEDとDACの電源を切って眠る。どれかのボタンの長押しで起きる
#ifndef POWER_PIN
#define POWER_PIN -1
#endif
#define SLEEP_AFTER_MIN 3  // 何分で眠るか
#define POWER_HOLD_MS 2000  // 赤と黄を同時にこの時間（ミリ秒）押し続けると、すぐに電源を切る
#define POWER_ON_HOLD_MS 1000  // 眠っているとき、どれかのボタンをこの時間（ミリ秒）押し続けると電源が入る
#define AUTO_SLEEP (POWER_PIN >= 0 && defined(CONFIG_IDF_TARGET_ESP32C3))
#if AUTO_SLEEP
#include <driver/gpio.h>
#include <esp_sleep.h>
#endif
#define VOLUME 100   // I2Sアンプの音量 0-255
#define NUM_LEDS 90  // LEDの個数
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 80       // 0-255
#define MAX_MILLIAMPS 800   // 電源に合わせて制限（803048P 電池 = 800、USB給電 = 400、外部5V 3A電源 = 1500）

// 7セグ表示器（TM1637 4桁モジュール）。使わないなら USE_7SEG を 0 に
#define USE_7SEG 1
#define SEG_BRIGHT 4  // 0-7
#if USE_7SEG
#include <TM1637Display.h>
#endif

// XIAO nRF52840：電池の充電電流。1 = 100mA（803048P など 200mAh 以上の電池向け）、0 = 50mA（小さいLiPo向け）
// ⚠ 容量が200mAh未満の小さい電池をつなぐときは必ず 0 に
#define FAST_CHARGE 1

// ハイスコア保存（ESP32 = Preferences、nRF52840 = 内蔵ファイルシステム、SAMD21 = FlashStorage）
#if defined(ESP32)
#include <Preferences.h>
#elif defined(NRF52840_XXAA)
#include <Adafruit_TinyUSB.h>  // Serial を使うのに必要
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#else
#include <FlashStorage_SAMD.h>
#endif

// 小さい方・大きい方（ボードによって min/max の定義が違いコンパイルエラーになるので、自前で用意）
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
#define MAX2(a, b) ((a) > (b) ? (a) : (b))

// Serial.printf が無いボード用
void logf(const char* fmt, ...) {
  char buf[128];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}

// ボタン（GND との間に接続。内部プルアップ使用）
const CRGB COLORS[] = {
  CRGB(255, 0, 0),    // 赤
  CRGB(0, 255, 0),    // 緑
  CRGB(0, 0, 255),    // 青
  CRGB(255, 160, 0),  // 黄
};
const char* COLOR_NAMES[] = { "RED", "GREEN", "BLUE", "YELLOW" };
#define NUM_COLORS 4

// ---------- ゲーム設定 ----------
// 速さや距離は「LED 60個のテープ」での値。LEN_SCALE を掛けて、テープの長さが変わっても同じ手ごたえにする
#define LEN_SCALE (NUM_LEDS / 60.0f)
#define MAX_DAMAGE 10
#define BASE_POS 0   // 下端のLED = 基地（残りHPを色で表示）
#define ARROW_LEN 3  // 矢印の長さ（先端＋尾）
#define MISSILE_LEN 2
#define MISSILE_SPEED (45.0f * LEN_SCALE)     // LED/秒
#define KILLS_PER_LEVEL 10      // 何体破壊でレベルアップか
#define ARROW_SPEED_START (5.0f * LEN_SCALE)  // LED/秒
#define ARROW_SPEED_STEP (1.5f * LEN_SCALE)   // レベルごとの加速
#define ARROW_SPEED_MAX (22.0f * LEN_SCALE)
#define SPAWN_MS_START 2200
#define SPAWN_MS_STEP 180  // レベルごとに出現間隔を短縮
#define SPAWN_MS_MIN 600
#define HEAL_COMBO 10         // このコンボごとにHP回復
#define CHAMELEON_MS 700      // カメレオンが色を変える間隔
#define ARMOR_KNOCKBACK (4.0f * LEN_SCALE)  // アーマーに当てたときの押し戻し量
// ボス戦
#define BOSS_EVERY_LEVELS 5   // このレベルごとにボス
#define BOSS_WARNING_MS 2500  // 警告演出の長さ
#define BOSS_BASE_LEN 6       // 1体目のボスの節の数
#define BOSS_LEN_STEP 2       // ボスごとに増える節の数
#define BOSS_MAX_LEN 16
#define BOSS_SPEED_START (1.5f * LEN_SCALE)  // LED/秒（ゆっくり迫ってくる）
#define BOSS_SPEED_STEP (0.5f * LEN_SCALE)
#define BOSS_SPEED_MAX (4.0f * LEN_SCALE)
#define BOSS_KNOCKBACK (1.0f * LEN_SCALE)  // 正しく当てたときの押し戻し量
#define BOSS_SHOT_MS 3000    // 子分の矢印を撃つ間隔
#define BOSS_SHOT_STEP 400
#define BOSS_SHOT_MIN 1200
#define BOSS_CRASH_DAMAGE 3  // 基地に到達されたときのダメージ
#define BOSS_BOUNCE 15.0f    // 到達後に跳ね返る距離
#define BOSS_BONUS 10        // 撃破ボーナス（×ボス番号）
#define BOSS_HEAL 3          // 撃破時のHP回復量

#define FIRE_COOLDOWN_MS 120
#define DEBOUNCE_MS 25

#define MAX_ARROWS 10
#define MAX_MISSILES 10
#define MAX_BLASTS 12
#define MAX_SWEEPS 3

CRGB leds[NUM_LEDS];

enum ArrowType : uint8_t { T_NORMAL,
                           T_ARMOR,
                           T_CHAMELEON,
                           T_RAINBOW };

struct Arrow {
  bool active;
  float pos;
  uint8_t color;
  uint8_t type;
  uint8_t hp;
  uint32_t nextChange;
};
struct Missile {
  bool active;
  float pos;
  uint8_t color;
};
struct Blast {
  bool active;
  int pos;
  CRGB color;
  uint32_t start;
};
struct Sweep {
  bool active;
  CRGB color;
  uint32_t start;
};  // 下から上へ走る光（演出用）

Arrow arrows[MAX_ARROWS];
Missile missiles[MAX_MISSILES];
Blast blasts[MAX_BLASTS];
Sweep sweeps[MAX_SWEEPS];

enum BossPhase { BOSS_NONE,
                 BOSS_WARNING,
                 BOSS_FIGHT };
struct Boss {
  float pos;                     // 一番下の節の位置
  uint8_t len;                   // 残りの節の数
  uint8_t colors[BOSS_MAX_LEN];  // colors[0] が一番下（次に撃つ色）
  uint32_t nextShot;
  uint32_t flashUntil;
};
BossPhase bossPhase = BOSS_NONE;
Boss boss;
int bossCount = 0;
uint32_t bossPhaseStart = 0;

enum State { TITLE,
             PLAYING,
             GAMEOVER };
State state = TITLE;

enum Mode { MODE_NORMAL,
            MODE_RHYTHM,
            MODE_PONG,
            MODE_SIMON };
const char* MODE_NAMES[] = { "NORMAL", "RHYTHM", "PONG", "SIMON" };
bool pongCpu = false;  // ポンで上側をCPUが操作するか
Mode mode = MODE_NORMAL;
// Arduino IDE が自動で作る宣言は Mode の定義より前に入ってしまうので、ここで自分で宣言しておく
void startGame(Mode m, uint32_t now);

int damage = 0;
int score = 0;
int kills = 0;
int combo = 0;
int maxCombo = 0;
int level = 1;                     // リズムモードでは何曲目か
int hiScores[4] = { 0, 0, 0, 0 };  // モードごとのハイスコア（ポンは未使用）

// ---------- ハイスコアの保存・読み込み ----------
#if defined(ESP32)
Preferences prefs;
const char* HI_KEYS[] = { "hi", "hiR", "hiP", "hiS" };
void loadHiScores() {
  prefs.begin("arrowdef", false);
  for (int i = 0; i < 4; i++) hiScores[i] = prefs.getInt(HI_KEYS[i], 0);
}
void saveHiScore(int m) {
  prefs.putInt(HI_KEYS[m], hiScores[m]);
}
#elif defined(NRF52840_XXAA)
// nRF52840 は内蔵フラッシュのファイルに保存（スケッチを書き込み直しても残る）
#define SAVE_FILE "/arrowdef.bin"
void loadHiScores() {
  InternalFS.begin();
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (!f.open(SAVE_FILE, Adafruit_LittleFS_Namespace::FILE_O_READ)) return;
  int32_t d[4];
  if (f.read(d, sizeof(d)) == sizeof(d))
    for (int i = 0; i < 4; i++) hiScores[i] = d[i];
  f.close();
}
void saveHiScore(int) {
  int32_t d[4] = { hiScores[0], hiScores[1], hiScores[2], hiScores[3] };
  InternalFS.remove(SAVE_FILE);
  Adafruit_LittleFS_Namespace::File f(InternalFS);
  if (!f.open(SAVE_FILE, Adafruit_LittleFS_Namespace::FILE_O_WRITE)) return;
  f.write((const uint8_t*)d, sizeof(d));
  f.close();
}
#else
// SAMD21 はフラッシュに保存（スケッチを書き込み直すと消える）
struct SaveData {
  uint32_t magic;
  int hi[4];
};
#define SAVE_MAGIC 0xA77D0001
void loadHiScores() {
  SaveData d;
  EEPROM.get(0, d);
  if (d.magic == SAVE_MAGIC)
    for (int i = 0; i < 4; i++) hiScores[i] = d.hi[i];
}
void saveHiScore(int) {
  SaveData d = { SAVE_MAGIC, { hiScores[0], hiScores[1], hiScores[2], hiScores[3] } };
  EEPROM.put(0, d);
  EEPROM.commit();
}
#endif
bool newRecord = false;
bool recordPlayed = false;
float arrowSpeed;
uint32_t spawnInterval;
uint32_t lastSpawn = 0;
uint32_t lastFire = 0;
uint32_t damageFlashUntil = 0;
uint32_t bombFlashUntil = 0;
uint32_t stateStart = 0;
uint32_t lastFrame = 0;

// ---------- 効果音（ノンブロッキング） ----------
// 音色: W_TONE = 鐘のような澄んだ音、W_NOISE = 「ザッ」という爆発っぽい音
//       （音色が変わるのは I2Sアンプのときだけ。ブザーではどちらも普通の「ピー」）
enum Wave : uint8_t { W_TONE,
                      W_NOISE };
struct Note {
  uint16_t freq;
  uint16_t ms;
  uint8_t wave;  // 省略すると W_TONE
};

const Note SND_START[] = { { 392, 100 }, { 523, 100 }, { 659, 100 }, { 784, 200 } };
const Note SND_FIRE[] = { { 1800, 15 } };
const Note SND_ARMOR[] = { { 2500, 30 }, { 1800, 40 } };
const Note SND_MISS[] = { { 300, 60, W_NOISE }, { 150, 140, W_NOISE } };
const Note SND_DAMAGE[] = { { 180, 80, W_NOISE }, { 110, 180, W_NOISE } };
const Note SND_HEAL[] = { { 880, 60 }, { 1175, 60 }, { 1568, 140 } };
const Note SND_LEVELUP[] = { { 523, 80 }, { 659, 80 }, { 784, 80 }, { 1047, 180 } };
const Note SND_BOMB[] = { { 200, 40, W_NOISE }, { 400, 40, W_NOISE }, { 800, 40, W_NOISE }, { 1600, 40 }, { 3200, 150 } };
const Note SND_GAMEOVER[] = { { 392, 200 }, { 330, 200 }, { 262, 200 }, { 196, 500 } };
const Note SND_RECORD[] = { { 523, 100 }, { 659, 100 }, { 784, 100 }, { 1047, 120 }, { 784, 100 }, { 1047, 400 } };
const Note SND_WARNING[] = { { 600, 250 }, { 900, 250 }, { 600, 250 }, { 900, 250 }, { 600, 250 }, { 900, 250 }, { 600, 250 }, { 900, 250 }, { 600, 250 }, { 900, 250 } };
const Note SND_BOSS_CRASH[] = { { 150, 120, W_NOISE }, { 100, 120, W_NOISE }, { 70, 300, W_NOISE } };
const Note SND_BOSS_CLEAR[] = { { 523, 90 }, { 659, 90 }, { 784, 90 }, { 1047, 90 }, { 1319, 90 }, { 1568, 90 }, { 2093, 400 } };
Note sndHit[1];  // コンボで音程が上がる

// ---------- 録音された効果音（sounds.h） ----------
// sounds フォルダに入れた音声ファイルを make_sounds.bat で sounds.h に変換しておくと、
// その場面ではスケッチが作る音の代わりに、その音声が鳴る（I2S のときだけ）
// ※ 並び順は tools/make_sounds.py の EVENTS と同じにすること
enum SampleId : uint8_t { SMP_SND_START,
                          SMP_SND_FIRE,
                          SMP_SND_HIT,
                          SMP_SND_ARMOR,
                          SMP_SND_MISS,
                          SMP_SND_DAMAGE,
                          SMP_SND_HEAL,
                          SMP_SND_LEVELUP,
                          SMP_SND_BOMB,
                          SMP_SND_GAMEOVER,
                          SMP_SND_RECORD,
                          SMP_SND_WARNING,
                          SMP_SND_BOSS_CRASH,
                          SMP_SND_BOSS_CLEAR,
                          SMP_SND_RMISS,
                          SMP_SND_PONG_POINT,
                          SMP_COUNT };
#if USE_I2S && __has_include("sounds.h")
#include "sounds.h"
#define HAS_SAMPLES 1
#else
#define HAS_SAMPLES 0
#endif

// 同時に鳴らせる音の数（I2Sアンプなら3つまで重ねられる。ブザーは1つ）
#if USE_I2S
#define NUM_TRACKS 3
#else
#define NUM_TRACKS 1
#endif
struct Track {
  const Note* seq;
  uint8_t len, idx;
  uint32_t noteEnd, started;
};
Track tracks[NUM_TRACKS];

#if USE_I2S
// ---------- I2Sアンプ（MAX98357A）または I2S DAC（PCM5102A）：スケッチの中で音の波形を作ってスピーカーから出す ----------
#if !(defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR >= 3)
#error "USE_I2S は ESP32（ボードパッケージ esp32 バージョン3以上）専用です。USE_I2S を 0 にしてください"
#endif
#include <ESP_I2S.h>
#define SAMPLE_RATE 44100  // PCM5102A でも確実に動く速さ
I2SClass i2s;
int16_t sineTab[256];  // サイン波1周ぶんの表

struct Voice {
  uint32_t phase, inc;  // 波のどこにいるか／1サンプルで進む量（＝音の高さ）
  int32_t level;        // 今の音量 0〜65535（鳴り始めてからだんだん小さくなる）
  int32_t samplesLeft;  // あと何サンプル鳴らすか
  uint8_t wave;
  bool attack;  // 鳴り始め（音量を急いで上げている最中）
  int16_t noise;
};
Voice voices[NUM_TRACKS];

// ゲーム側 → 音を作るタスク への「この音を鳴らして」という連絡
struct VoiceCmd {
  uint32_t inc;
  int32_t samples;
  uint8_t wave;
  bool pending;
};
VoiceCmd voiceCmds[NUM_TRACKS];
portMUX_TYPE synthMux = portMUX_INITIALIZER_UNLOCKED;

#if HAS_SAMPLES
// 録音された効果音の再生（3つまで重ねられる）
#if SOUNDS_COUNT != 16
#error "sounds.h が古いです。make_sounds.bat で作り直してください"
#endif
#define NUM_SVOICES 3
struct SVoice {
  const uint8_t* data;  // nullptr = 鳴っていない
  uint32_t len;
  uint32_t pos, step;  // 今の位置と進む速さ（1/4096 サンプル単位）
};
SVoice svoices[NUM_SVOICES];
struct SCmd {
  const uint8_t* data;
  uint32_t len, step;
  bool pending;
};
SCmd scmds[NUM_SVOICES];
volatile bool sbusy[NUM_SVOICES];
uint8_t snext = 0;
int16_t mulawTab[256];  // 圧縮された1バイトの値 → 元の音の大きさ

// ratePct: 再生の速さ（100 = そのまま。大きくすると音が高く・短くなる）
bool playSample(uint8_t id, uint16_t ratePct = 100) {
  if (id >= SOUNDS_COUNT || !SAMPLES[id].data) return false;  // その場面の音声ファイルがない
  int v = -1;
  for (int i = 0; i < NUM_SVOICES; i++) {
    if (!sbusy[i] && !scmds[i].pending) {
      v = i;
      break;
    }
  }
  if (v < 0) {  // 空きがなければ順番に止めて使う
    v = snext;
    snext = (snext + 1) % NUM_SVOICES;
  }
  portENTER_CRITICAL(&synthMux);
  scmds[v].data = SAMPLES[id].data;
  scmds[v].len = SAMPLES[id].len;
  scmds[v].step = (uint32_t)((uint64_t)4096 * SOUNDS_RATE * ratePct / ((uint32_t)SAMPLE_RATE * 100));
  scmds[v].pending = true;
  portEXIT_CRITICAL(&synthMux);
  return true;
}
#endif

void synthNote(uint8_t t, uint16_t freq, uint16_t ms, uint8_t wave) {
  portENTER_CRITICAL(&synthMux);
  voiceCmds[t].inc = (uint32_t)(freq * (4294967296.0 / SAMPLE_RATE));
  voiceCmds[t].samples = (int32_t)ms * SAMPLE_RATE / 1000;
  voiceCmds[t].wave = wave;
  voiceCmds[t].pending = true;
  portEXIT_CRITICAL(&synthMux);
}

// 音を作り続けるタスク（ゲーム本体とは別に、裏でずっと動く）
void synthTask(void*) {
  static int16_t buf[128 * 2];  // 左右2チャンネルぶん
  uint32_t rnd = 0x1234ABCD;
  for (;;) {
    portENTER_CRITICAL(&synthMux);
    for (int t = 0; t < NUM_TRACKS; t++) {
      VoiceCmd& c = voiceCmds[t];
      if (!c.pending) continue;
      c.pending = false;
      Voice& v = voices[t];
      if (c.inc == 0 || c.samples <= 0) {
        v.samplesLeft = 0;  // 音を止める
        continue;
      }
      v.inc = c.inc;
      v.samplesLeft = c.samples;
      v.wave = c.wave;
      v.attack = true;
    }
#if HAS_SAMPLES
    for (int k = 0; k < NUM_SVOICES; k++) {
      if (!scmds[k].pending) continue;
      scmds[k].pending = false;
      svoices[k].data = scmds[k].data;
      svoices[k].len = scmds[k].len;
      svoices[k].step = scmds[k].step;
      svoices[k].pos = 0;
      sbusy[k] = true;
    }
#endif
    portEXIT_CRITICAL(&synthMux);

    for (int i = 0; i < 128; i++) {
      int32_t mix = 0;
      for (auto& v : voices) {
        if (v.samplesLeft > 0) {
          v.samplesLeft--;
          if (v.attack) {  // 鳴り始め：約2ミリ秒で最大音量へ（プチッという雑音を防ぐ）
            v.level += 750;
            if (v.level >= 65535) {
              v.level = 65535;
              v.attack = false;
            }
          } else {
            v.level -= v.level >> 13;  // ゆっくり小さく（鐘のような余韻）
          }
        } else {
          if (v.level == 0) continue;
          v.level -= (v.level >> 8) + 1;  // 鳴り終わり：すばやく消す
          if (v.level < 0) v.level = 0;
        }

        int32_t s;
        if (v.wave == W_NOISE) {
          // ノイズ：でたらめな値を、音の高さに合わせた速さで切り替える
          uint32_t p = v.phase + v.inc * 8;
          if (p < v.phase) {
            rnd ^= rnd << 13;
            rnd ^= rnd >> 17;
            rnd ^= rnd << 5;
            v.noise = (int16_t)(rnd >> 17) - 16384;
          }
          v.phase = p;
          s = v.noise;
        } else {
          // 澄んだ音：サイン波に、1オクターブ上の音を少し混ぜる
          v.phase += v.inc;
          s = (sineTab[v.phase >> 24] * 3 + sineTab[(v.phase >> 23) & 0xFF]) >> 2;
        }
        mix += (s * (v.level >> 1)) >> 15;
      }
#if HAS_SAMPLES
      for (int k = 0; k < NUM_SVOICES; k++) {
        SVoice& sv = svoices[k];
        if (!sv.data) continue;
        uint32_t i0 = sv.pos >> 12;
        if (i0 + 1 >= sv.len) {  // 最後まで鳴らした
          sv.data = nullptr;
          sbusy[k] = false;
          continue;
        }
        // となり合う2つの値のあいだを、なめらかにつなぐ
        int32_t a = mulawTab[sv.data[i0]], b = mulawTab[sv.data[i0 + 1]];
        mix += a + (((b - a) * (int32_t)(sv.pos & 4095)) >> 12);
        sv.pos += sv.step;
      }
#endif
      mix = mix * VOLUME >> 9;
      if (mix > 32767) mix = 32767;
      if (mix < -32768) mix = -32768;
      buf[i * 2] = buf[i * 2 + 1] = (int16_t)mix;
    }
    i2s.write((const uint8_t*)buf, sizeof(buf));  // アンプが受け取れるようになるまで待つ
  }
}

void synthBegin() {
  for (int i = 0; i < 256; i++) sineTab[i] = (int16_t)(sinf(i * 6.2831853f / 256) * 32767);
#if HAS_SAMPLES
  for (int i = 0; i < 256; i++) {  // μ-law という圧縮を元に戻す表
    uint8_t u = ~i;
    int s = ((((u & 0x0F) << 3) + 0x84) << ((u >> 4) & 7)) - 0x84;
    mulawTab[i] = (u & 0x80) ? -s : s;
  }
#endif
  i2s.setPins(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN);
  if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S init failed");
    return;
  }
  xTaskCreate(synthTask, "synth", 4096, nullptr, 5, nullptr);
}
#endif  // USE_I2S

#if !HAS_SAMPLES
bool playSample(uint8_t, uint16_t = 100) {
  return false;  // 録音された効果音なし → いつもスケッチが作る音を鳴らす
}
#endif

void buzz(uint16_t freq) {
#if !USE_I2S && BUZZER_PIN >= 0
#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, freq);
#elif defined(ESP32)
  ledcWriteTone(0, freq);
#else
  if (freq) tone(BUZZER_PIN, freq);
  else noTone(BUZZER_PIN);
#endif
#endif
}

// 1つの音を鳴らす（freq = 0 で止める）
void soundNote(uint8_t t, uint16_t freq, uint16_t ms, uint8_t wave) {
#if USE_I2S
  synthNote(t, freq, ms, wave);
#else
  buzz(freq);
#endif
}

void updateSound(uint32_t now) {
  for (uint8_t t = 0; t < NUM_TRACKS; t++) {
    Track& k = tracks[t];
    if (!k.seq || now < k.noteEnd) continue;
    if (k.idx >= k.len) {
      soundNote(t, 0, 0, 0);
      k.seq = nullptr;
      continue;
    }
    const Note& n = k.seq[k.idx++];
    soundNote(t, n.freq, n.ms, n.wave);
    k.noteEnd = now + n.ms;
  }
}

// force=false の音は、空きがないときは鳴らさない（発射音など）
// force=true の音は、空きがなければ一番古い音を止めて鳴らす
void playSound(const Note* s, uint8_t n, bool force = true) {
  uint32_t now = millis();
  int t = -1;
  for (int i = 0; i < NUM_TRACKS; i++) {
    if (!tracks[i].seq) {
      t = i;
      break;
    }
  }
  if (t < 0) {
    if (!force) return;
    t = 0;
    for (int i = 1; i < NUM_TRACKS; i++)
      if (now - tracks[i].started > now - tracks[t].started) t = i;
  }
  tracks[t].seq = s;
  tracks[t].len = n;
  tracks[t].idx = 0;
  tracks[t].noteEnd = 0;
  tracks[t].started = now;
  updateSound(now);
}
// 録音された効果音があればそれを、なければスケッチが作る音を鳴らす
#define PLAY(s) \
  do { \
    if (!playSample(SMP_##s)) playSound(s, sizeof(s) / sizeof(Note)); \
  } while (0)

// ---------- 7セグ：一時メッセージ ----------
// 普段はスコアなどを表示し、イベント時だけ短い文字を出す（"HEAL" "PErF" など）
char segMsg[5] = "";
uint32_t segMsgUntil = 0;

void segMessage(const char* text, uint32_t ms) {
  strncpy(segMsg, text, 4);
  segMsg[4] = '\0';
  segMsgUntil = millis() + ms;
}

// ---------- ボタン ----------
bool btnState[NUM_COLORS];
uint32_t btnChanged[NUM_COLORS];
bool justPressed[NUM_COLORS];

uint32_t lastInput = 0;  // 最後にボタンを押した時刻（自動で電源を切る判定用）

void readButtons(uint32_t now) {
  for (int i = 0; i < NUM_COLORS; i++) {
    justPressed[i] = false;
    bool down = digitalRead(BUTTON_PINS[i]) == LOW;
    if (down) lastInput = now;
    if (down != btnState[i] && now - btnChanged[i] > DEBOUNCE_MS) {
      btnState[i] = down;
      btnChanged[i] = now;
      if (down) justPressed[i] = true;
    }
  }
}

bool anyPressed() {
  for (int i = 0; i < NUM_COLORS; i++)
    if (justPressed[i]) return true;
  return false;
}

// ---------- ゲーム処理 ----------
void addBlast(int pos, CRGB color, uint32_t now) {
  for (auto& b : blasts) {
    if (!b.active) {
      b = { true, pos, color, now };
      return;
    }
  }
}

void addSweep(CRGB color, uint32_t now) {
  for (auto& s : sweeps) {
    if (!s.active) {
      s = { true, color, now };
      return;
    }
  }
}

void applyLevel() {
  arrowSpeed = MIN2(ARROW_SPEED_START + (level - 1) * ARROW_SPEED_STEP, ARROW_SPEED_MAX);
  spawnInterval = MAX2(SPAWN_MS_START - (level - 1) * SPAWN_MS_STEP, SPAWN_MS_MIN);
}

void gameOver(uint32_t now) {
  state = GAMEOVER;
  stateStart = now;
  newRecord = score > hiScores[mode];
  recordPlayed = false;
  if (newRecord) {
    hiScores[mode] = score;
    saveHiScore(mode);
  }
  PLAY(SND_GAMEOVER);
  logf("GAME OVER  %s  score=%d  %s=%d  maxCombo=%d  hi=%d%s\n",
       MODE_NAMES[mode], score,
       mode == MODE_RHYTHM ? "songs" : "level", level, maxCombo,
       hiScores[mode], newRecord ? "  NEW RECORD!" : "");
}

void takeDamage(uint32_t now) {
  damage++;
  combo = 0;
  damageFlashUntil = now + 180;
  logf("DAMAGE %d/%d\n", damage, MAX_DAMAGE);
  if (damage >= MAX_DAMAGE) gameOver(now);
}

// ---------- ボス ----------
uint32_t bossShotInterval() {
  return MAX2(BOSS_SHOT_MS - (bossCount - 1) * BOSS_SHOT_STEP, BOSS_SHOT_MIN);
}

float bossSpeed() {
  return MIN2(BOSS_SPEED_START + (bossCount - 1) * BOSS_SPEED_STEP, BOSS_SPEED_MAX);
}

void startBossWarning(uint32_t now) {
  bossPhase = BOSS_WARNING;
  bossPhaseStart = now;
  PLAY(SND_WARNING);
  Serial.println("WARNING: BOSS APPROACHING");
}

void spawnBoss(uint32_t now) {
  bossCount++;
  boss.len = MIN2(BOSS_BASE_LEN + (bossCount - 1) * BOSS_LEN_STEP, BOSS_MAX_LEN);
  for (int i = 0; i < boss.len; i++) boss.colors[i] = random(NUM_COLORS);
  boss.pos = NUM_LEDS - 1;
  boss.nextShot = now + bossShotInterval();
  boss.flashUntil = 0;
  bossPhase = BOSS_FIGHT;
  logf("BOSS #%d  length=%d\n", bossCount, boss.len);
}

void addComboScore(int pos, CRGB color, uint32_t now);

void defeatBoss(uint32_t now) {
  bossPhase = BOSS_NONE;
  score += BOSS_BONUS * bossCount;
  damage = MAX2(damage - BOSS_HEAL, 0);
  // 残っている子分もまとめて破壊
  for (auto& a : arrows) {
    if (!a.active) continue;
    a.active = false;
    addBlast((int)(a.pos + 0.5f), COLORS[a.color], now);
  }
  for (int i = 0; i < 6; i++) addBlast(random(NUM_LEDS), CRGB(CHSV(random8(), 255, 255)), now);
  addSweep(CRGB(255, 180, 0), now);
  bombFlashUntil = now + 400;
  lastSpawn = now;
  PLAY(SND_BOSS_CLEAR);
  segMessage("nICE", 1500);
  logf("BOSS #%d DEFEATED!  score=%d  damage=%d\n", bossCount, score, damage);
}

void hitBoss(uint8_t color, uint32_t now) {
  int hitPos = (int)(boss.pos + 0.5f);
  if (color == boss.colors[0]) {
    // 一番下の節を破壊 → ボスが短くなる
    addComboScore(hitPos, COLORS[color], now);
    memmove(boss.colors, boss.colors + 1, boss.len - 1);
    boss.len--;
    boss.pos = MIN2(boss.pos + 1.0f + BOSS_KNOCKBACK, (float)(NUM_LEDS - 1));
    boss.flashUntil = now + 80;
    if (boss.len == 0) defeatBoss(now);
  } else {
    // 色を間違えるとボスが前に出てくる
    addBlast(hitPos, CRGB(60, 60, 60), now);
    boss.pos -= 1.0f;
    PLAY(SND_MISS);
    logf("BOSS MISS (%s vs %s)\n", COLOR_NAMES[color], COLOR_NAMES[boss.colors[0]]);
    takeDamage(now);
  }
}

void addComboScore(int pos, CRGB color, uint32_t now) {
  combo++;
  maxCombo = MAX2(maxCombo, combo);
  int points = 1 + MIN2(combo / 10, 2);  // 10コンボで2点、20コンボで3点
  score += points;
  addBlast(pos, color, now);

  // コンボが続くほど音が高くなる
  if (!playSample(SMP_SND_HIT, 100 + MIN2(combo, 20) * 3)) {
    sndHit[0] = { (uint16_t)(800 + MIN2(combo, 20) * 60), 40 };
    playSound(sndHit, 1);
  }

  if (combo % HEAL_COMBO == 0 && damage > 0) {
    damage--;
    addSweep(CRGB(0, 255, 60), now);
    segMessage("HEAL", 800);
    PLAY(SND_HEAL);
    logf("COMBO %d -> HEAL (damage %d)\n", combo, damage);
  }
}

void onKill(int pos, CRGB color, uint32_t now) {
  kills++;
  addComboScore(pos, color, now);
  if (bossPhase != BOSS_NONE) return;  // ボス戦中はレベルアップしない

  int newLevel = 1 + kills / KILLS_PER_LEVEL;
  if (newLevel > level) {
    level = newLevel;
    applyLevel();
    addSweep(CRGB(255, 255, 255), now);
    PLAY(SND_LEVELUP);
    logf("LEVEL UP -> %d\n", level);
    char buf[5];
    snprintf(buf, sizeof(buf), "L%3d", level);
    segMessage(buf, 1200);
    if (level % BOSS_EVERY_LEVELS == 0) startBossWarning(now);
  }
}

// レインボー：画面上の矢印を全部破壊
void bomb(uint32_t now) {
  bombFlashUntil = now + 250;
  for (auto& a : arrows) {
    if (!a.active) continue;
    a.active = false;
    onKill((int)(a.pos + 0.5f), COLORS[a.color], now);
  }
  PLAY(SND_BOMB);
  Serial.println("RAINBOW BOMB!");
}

void startRhythm(uint32_t now);
void startPong(uint32_t now);
void startSimon(uint32_t now);

// モード選択中のボタン（長押し判定用）
#define SIMON_HOLD_MS 1000
int selBtn = -1;
uint32_t selStart = 0;

void startGame(Mode m, uint32_t now) {
  mode = m;
  memset(arrows, 0, sizeof(arrows));
  memset(missiles, 0, sizeof(missiles));
  for (auto& b : blasts) b.active = false;
  for (auto& s : sweeps) s.active = false;
  damage = score = kills = combo = maxCombo = 0;
  level = 1;
  bossPhase = BOSS_NONE;
  bossCount = 0;
  applyLevel();
  lastSpawn = now - spawnInterval + 800;  // 開始0.8秒後に最初の矢印
  state = PLAYING;
  stateStart = now;
  PLAY(SND_START);
  if (m == MODE_NORMAL) segMessage("GO  ", 800);
  if (mode == MODE_RHYTHM) startRhythm(now);
  if (mode == MODE_PONG) startPong(now);
  if (mode == MODE_SIMON) startSimon(now);
  selBtn = -1;
  if (mode == MODE_PONG && pongCpu) Serial.println("PONG vs CPU");
  logf("GAME START (%s)\n", MODE_NAMES[mode]);
}

// 短く押して離す: 赤 = ノーマル、緑 = ポン（2人対戦）、青 = リズム、黄 = ポン（CPU対戦）
// どれかを1秒長押し: サイモン（記憶ゲーム）
bool handleModeSelect(uint32_t now) {
  if (selBtn < 0) {
    for (int i = 0; i < NUM_COLORS; i++) {
      if (justPressed[i]) {
        selBtn = i;
        selStart = now;
        break;
      }
    }
    return false;
  }
  if (now - selStart >= SIMON_HOLD_MS) {
    startGame(MODE_SIMON, now);
    return true;
  }
  if (btnState[selBtn]) return false;  // まだ押している
  switch (selBtn) {                    // 離した → 短押し
    case 0: startGame(MODE_NORMAL, now); break;
    case 1:
      pongCpu = false;
      startGame(MODE_PONG, now);
      break;
    case 2: startGame(MODE_RHYTHM, now); break;
    case 3:
      pongCpu = true;
      startGame(MODE_PONG, now);
      break;
  }
  return true;
}

void addArrow(float pos, uint8_t color, uint8_t type, uint32_t now) {
  for (auto& a : arrows) {
    if (!a.active) {
      a = { true, pos, color, type, (uint8_t)(type == T_ARMOR ? 2 : 1), now + CHAMELEON_MS };
      return;
    }
  }
}

void spawnArrow(uint32_t now) {
  uint8_t type = T_NORMAL;
  int r = random(100);
  if (level >= 2 && r < 5) type = T_RAINBOW;
  else if (level >= 2 && r < 20) type = T_ARMOR;
  else if (level >= 3 && r < 35) type = T_CHAMELEON;
  addArrow(NUM_LEDS - 1, random(NUM_COLORS), type, now);
}

void fireMissile(uint8_t color, uint32_t now) {
  if (now - lastFire < FIRE_COOLDOWN_MS) return;
  for (auto& m : missiles) {
    if (!m.active) {
      m = { true, (float)(BASE_POS + 1), color };
      lastFire = now;
      if (!playSample(SMP_SND_FIRE)) playSound(SND_FIRE, 1, false);
      return;
    }
  }
}

// 一番下にいる矢印（＝ミサイルが最初にぶつかる矢印）
int lowestArrow() {
  int idx = -1;
  float lowest = 1e9;
  for (int i = 0; i < MAX_ARROWS; i++) {
    if (arrows[i].active && arrows[i].pos < lowest) {
      lowest = arrows[i].pos;
      idx = i;
    }
  }
  return idx;
}

void updateGame(uint32_t now, float dt) {
  // 入力
  for (int i = 0; i < NUM_COLORS; i++) {
    if (justPressed[i]) fireMissile(i, now);
  }

  // 矢印の出現（ボス戦中は通常の矢印は出ない）
  if (bossPhase == BOSS_NONE && now - lastSpawn >= spawnInterval) {
    spawnArrow(now);
    lastSpawn = now;
  }

  // ボス登場：警告が終わり、残っている矢印がなくなったら
  if (bossPhase == BOSS_WARNING && now - bossPhaseStart >= BOSS_WARNING_MS && lowestArrow() < 0) {
    spawnBoss(now);
  }

  // ボスの移動・子分の発射・基地への到達
  if (bossPhase == BOSS_FIGHT) {
    boss.pos -= bossSpeed() * dt;
    if (now >= boss.nextShot && boss.pos > 8) {
      addArrow(boss.pos - 1, random(NUM_COLORS), T_NORMAL, now);
      boss.nextShot = now + bossShotInterval();
    }
    if (boss.pos <= BASE_POS + 0.5f) {
      boss.pos += BOSS_BOUNCE;
      addBlast(BASE_POS + 1, CRGB::Red, now);
      PLAY(SND_BOSS_CRASH);
      Serial.println("BOSS CRASHED INTO BASE!");
      for (int i = 0; i < BOSS_CRASH_DAMAGE && state == PLAYING; i++) takeDamage(now);
      if (state != PLAYING) return;
    }
  }

  // 矢印の移動・色変化・基地への到達
  for (auto& a : arrows) {
    if (!a.active) continue;
    a.pos -= arrowSpeed * dt;
    if (a.type == T_CHAMELEON && now >= a.nextChange) {
      a.color = (a.color + 1 + random(NUM_COLORS - 1)) % NUM_COLORS;  // 必ず別の色に
      a.nextChange = now + CHAMELEON_MS;
    }
    if (a.pos <= BASE_POS + 0.5f) {
      a.active = false;
      addBlast(BASE_POS + 1, CRGB::Red, now);
      PLAY(SND_DAMAGE);
      takeDamage(now);
      if (state != PLAYING) return;
    }
  }

  // ミサイルの移動・衝突判定
  for (auto& m : missiles) {
    if (!m.active) continue;
    m.pos += MISSILE_SPEED * dt;
    if (m.pos >= NUM_LEDS) {
      m.active = false;
      continue;
    }

    // 一番下にいる相手（矢印 or ボス）に当たる
    int t = lowestArrow();
    bool targetBoss = bossPhase == BOSS_FIGHT && (t < 0 || boss.pos < arrows[t].pos);
    float targetPos = targetBoss ? boss.pos : (t >= 0 ? arrows[t].pos : 1e9f);
    if (m.pos < targetPos) continue;
    m.active = false;

    if (targetBoss) {
      hitBoss(m.color, now);
      if (state != PLAYING) return;
      continue;
    }

    Arrow& a = arrows[t];
    int hitPos = (int)(a.pos + 0.5f);

    if (a.type == T_RAINBOW) {
      // どの色でもOK → 全滅ボム
      bomb(now);
    } else if (m.color == a.color) {
      if (--a.hp > 0) {
        // アーマーが剥がれて押し戻される
        a.type = T_NORMAL;
        a.pos = MIN2(a.pos + ARMOR_KNOCKBACK, (float)(NUM_LEDS - 1));
        addBlast(hitPos, CRGB(120, 120, 120), now);
        PLAY(SND_ARMOR);
      } else {
        a.active = false;
        onKill(hitPos, COLORS[a.color], now);
        logf("HIT %s  score=%d  combo=%d\n", COLOR_NAMES[m.color], score, combo);
      }
    } else {
      // 違う色 → 矢印は無傷、ダメージ
      addBlast(hitPos, CRGB(60, 60, 60), now);
      PLAY(SND_MISS);
      logf("MISS (%s vs %s)\n", COLOR_NAMES[m.color], COLOR_NAMES[a.color]);
      takeDamage(now);
      if (state != PLAYING) return;
    }
  }
}

// ---------- 描画 ----------
void setPixel(int i, CRGB c) {
  if (i >= 0 && i < NUM_LEDS) leds[i] += c;
}

void drawSweeps(uint32_t now) {
  const uint32_t DURATION = 600;
  for (auto& s : sweeps) {
    if (!s.active) continue;
    uint32_t t = now - s.start;
    if (t > DURATION) {
      s.active = false;
      continue;
    }
    int head = t * NUM_LEDS / DURATION;
    for (int k = 0; k < 4; k++) {
      CRGB c = s.color;
      c.nscale8(200 >> k);
      setPixel(head - k, c);
    }
  }
}

void drawEffectsAndBase(uint32_t now);

void drawGame(uint32_t now) {
  if (now < bombFlashUntil) fill_solid(leds, NUM_LEDS, CRGB(50, 50, 50));
  else if (now < damageFlashUntil) fill_solid(leds, NUM_LEDS, CRGB(40, 0, 0));
  else FastLED.clear();

  // ボス警告：上半分が赤く脈打つ
  if (bossPhase == BOSS_WARNING) {
    uint8_t v = beatsin8(120, 0, 120);
    for (int i = NUM_LEDS / 2; i < NUM_LEDS; i++) setPixel(i, CRGB(v, 0, 0));
  }

  // ボス：一番下の節（次に撃つ色）が脈打つ。上端には紫の頭
  if (bossPhase == BOSS_FIGHT) {
    int bottom = (int)(boss.pos + 0.5f);
    bool flash = now < boss.flashUntil;
    for (int i = 0; i < boss.len; i++) {
      CRGB c = flash ? CRGB(255, 255, 255) : COLORS[boss.colors[i]];
      if (!flash) c.nscale8(i == 0 ? beatsin8(240, 120, 255) : 110);
      setPixel(bottom + i, c);
    }
    setPixel(bottom + boss.len, CRGB(120, 0, 160));
  }

  // 矢印：先端が明るく、上に向かって尾が薄くなる（▼のイメージ）
  for (auto& a : arrows) {
    if (!a.active) continue;
    int head = (int)(a.pos + 0.5f);
    // カメレオンは色が変わる直前に点滅して知らせる
    bool warn = a.type == T_CHAMELEON && (int32_t)(a.nextChange - now) < 200 && (now / 50) % 2;
    for (int k = 0; k < ARROW_LEN; k++) {
      CRGB c;
      if (a.type == T_RAINBOW) c = CHSV(now / 3 + k * 50, 255, 255);
      else if (a.type == T_ARMOR && k) c = CRGB(160, 160, 160);  // 白い尾 = アーマー
      else c = COLORS[a.color];
      c.nscale8(255 >> (k * 2));
      if (warn) c.nscale8(40);
      setPixel(head + k, c);
    }
  }

  // ミサイル：先端が明るく、下に尾。コンボ10以上で尾が伸びる
  int mlen = MISSILE_LEN + (combo >= 10 ? 1 : 0) + (combo >= 20 ? 1 : 0);
  for (auto& m : missiles) {
    if (!m.active) continue;
    int head = (int)m.pos;
    for (int k = 0; k < mlen; k++) {
      CRGB c = COLORS[m.color];
      c.nscale8(k == 0 ? 255 : 70);
      setPixel(head - k, c);
    }
  }

  drawEffectsAndBase(now);
}

// 爆発・走る光（全モード共通）
void drawEffects(uint32_t now) {
  // 爆発：中心から広がって消える
  for (auto& b : blasts) {
    if (!b.active) continue;
    uint32_t t = now - b.start;
    if (t > 300) {
      b.active = false;
      continue;
    }
    int r = t / 60;
    uint8_t fade = 255 - t * 255 / 300;
    CRGB c = b.color;
    c.nscale8(fade);
    setPixel(b.pos + r, c);
    setPixel(b.pos - r, c);
    if (r == 0) setPixel(b.pos, CRGB(fade, fade, fade));
  }

  drawSweeps(now);
}

// 爆発・走る光・基地（ノーマル・リズム共通）
void drawEffectsAndBase(uint32_t now) {
  drawEffects(now);

  // 基地：残りHPを 緑→黄→赤 で表示、ピンチ時は点滅
  int hp = MAX_DAMAGE - damage;
  uint8_t hue = map(hp, 0, MAX_DAMAGE, 0, 96);  // 0=赤, 96=緑
  bool blink = (hp <= 3) && ((now / 200) % 2);
  leds[BASE_POS] = blink ? CRGB(CRGB::Black) : CRGB(CHSV(hue, 255, 255));
}

void drawTitle(uint32_t now) {
  // 下から 赤（ノーマル）／緑（ポン2人）／青（リズム）／黄（ポンCPU）の帯が流れる
  //   ＝ そのボタンを押すとそのモード
  int band = NUM_LEDS / NUM_COLORS;
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB c = COLORS[MIN2(i / band, NUM_COLORS - 1)];
    c.nscale8(((i + now / 80) % 4 == 0) ? 255 : 25);
    leds[i] = c;
  }
  for (int k = 1; k < NUM_COLORS; k++) leds[k * band] = CRGB::Black;  // 区切り
}

// ボタン長押し中：下から白いバーが伸びる（てっぺんまで伸びるとサイモン開始）
void drawHoldBar(uint32_t now) {
  if (selBtn < 0) return;
  uint32_t held = now - selStart;
  if (held < 150) return;  // 普通の短押しでは出さない
  int n = (int)((held - 150) * NUM_LEDS / (SIMON_HOLD_MS - 150));
  for (int i = 0; i < MIN2(n, NUM_LEDS); i++) leds[i] = CRGB(150, 150, 150);
}

// 数字をLEDで表示（下から 百の位=紫, 十の位=水色, 一の位=白。0 は暗い1個）
void drawNumber(int n) {
  n = constrain(n, 0, 999);
  const int digits[3] = { n / 100, (n / 10) % 10, n % 10 };
  const CRGB dc[3] = { CRGB(200, 0, 200), CRGB(0, 180, 255), CRGB(150, 150, 150) };
  int first = n >= 100 ? 0 : (n >= 10 ? 1 : 2);
  int p = 0;
  for (int d = first; d < 3; d++) {
    if (digits[d] == 0) {
      CRGB c = dc[d];
      c.nscale8(40);
      setPixel(p++, c);
    } else {
      for (int k = 0; k < digits[d]; k++) setPixel(p++, dc[d]);
    }
    p++;  // 桁の区切り
  }
}

void drawPongResult(uint32_t now);
void drawSimonAnswer(uint32_t now);

void drawGameOver(uint32_t now) {
  if (mode == MODE_PONG) {
    drawPongResult(now);
    return;
  }
  uint32_t t = now - stateStart;
  FastLED.clear();
  if (mode == MODE_SIMON && t < 2000) {
    drawSimonAnswer(now);
    return;
  }  // 正解を見せる
  if (t < 1500) {
    // 赤く点滅
    if ((t / 150) % 2 == 0) fill_solid(leds, NUM_LEDS, CRGB(120, 0, 0));
    return;
  }
  drawNumber(score);
  if (newRecord) {
    // ハイスコア更新：上側で虹色がきらめく
    if (!recordPlayed) {
      PLAY(SND_RECORD);
      recordPlayed = true;
    }
    for (int i = NUM_LEDS - 15; i < NUM_LEDS; i++) {
      leds[i] = CHSV(now / 4 + i * 20, 255, ((now / 120 + i) % 3 == 0) ? 255 : 60);
    }
  }
}

// =============================================================
//  リズムモード
//  曲に合わせて音符が落ちてくる。判定ライン（下から5個目の白いLED）に
//  重なった瞬間に同じ色のボタンを押すと、ブザーがその音を鳴らす＝自分で演奏！
//  色は ド・レ・ミ・ファ = 赤・緑・青・黄、ソ・ラ・シ・ド = 赤・緑・青・黄 …の繰り返し
//  PERFECT 3点 / GOOD 1点（＋コンボボーナス）。見逃し・色違いはダメージ
//  3曲を1周するごとにテンポアップ。ダメージ10回でゲームオーバー
// =============================================================
#define JUDGE_POS 4           // 判定ラインのLED
#define R_PERFECT_MS 60       // ±この時間内なら PERFECT
#define R_GOOD_MS 140         // ±この時間内なら GOOD
#define R_FALL_MS_START 2200  // 音符が上端から判定ラインまで落ちる時間
#define R_FALL_MS_MIN 1000
#define R_TEMPO_UP 1.12f   // 1周ごとのテンポ倍率
#define R_LEAD_IN_MS 2500  // 曲が始まるまでの待ち時間
#define R_MAX_NOTES 64

// 楽譜の書き方: 音名+オクターブ（例 C4=ド, F#4）、R=休符
//               ":数字" で長さ（8分音符いくつ分。省略時は2=4分音符）
struct Song {
  const char* name;
  uint16_t bpm;
  const char* notes;
};
const Song SONGS[] = {
  { "Kirakira Boshi", 100,
    "C4 C4 G4 G4 A4 A4 G4:4 F4 F4 E4 E4 D4 D4 C4:4 "
    "G4 G4 F4 F4 E4 E4 D4:4 G4 G4 F4 F4 E4 E4 D4:4 "
    "C4 C4 G4 G4 A4 A4 G4:4 F4 F4 E4 E4 D4 D4 C4:4" },
  { "Kaeru no Uta", 120,
    "C4 D4 E4 F4 E4 D4 C4:4 E4 F4 G4 A4 G4 F4 E4:4 "
    "C4 R C4 R C4 R C4 R "
    "C4:1 C4:1 D4:1 D4:1 E4:1 E4:1 F4:1 F4:1 E4 D4 C4:4" },
  { "Ode to Joy", 120,
    "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 E4:3 D4:1 D4:4 "
    "E4 E4 F4 G4 G4 F4 E4 D4 C4 C4 D4 E4 D4:3 C4:1 C4:4" },
};
#define NUM_SONGS (int)(sizeof(SONGS) / sizeof(Song))

struct RNote {
  uint32_t time;
  uint16_t freq;
  uint16_t lenMs;
  uint8_t color;
  uint8_t eighths;
  bool done;
};
RNote rnotes[R_MAX_NOTES];
int rnoteCount = 0;
int songIdx = 0;
float tempoScale = 1.0f;
uint32_t eighthMs, fallMs, songFirstBeat, songEnd;
uint32_t judgeFlashUntil = 0;
CRGB judgeFlashColor;
Note sndNote[1];
const Note SND_RMISS[] = { { 110, 90 } };

void loadSong(int idx, uint32_t now) {
  static const int8_t SEMI[] = { 9, 11, 0, 2, 4, 5, 7 };  // A B C D E F G の半音位置
  static const int8_t DEG[] = { 5, 6, 0, 1, 2, 3, 4 };    // ドを0とした音階の番号

  const Song& s = SONGS[idx];
  eighthMs = (uint32_t)(60000.0f / (s.bpm * tempoScale) / 2);
  fallMs = MAX2((int)(R_FALL_MS_START / tempoScale), R_FALL_MS_MIN);
  songFirstBeat = now + R_LEAD_IN_MS;

  rnoteCount = 0;
  uint32_t t = songFirstBeat;
  const char* p = s.notes;
  while (*p) {
    if (*p == ' ') {
      p++;
      continue;
    }
    char letter = *p++;
    bool rest = (letter == 'R');
    int semi = 0, degree = 0, octave = 4;
    if (!rest) {
      semi = SEMI[letter - 'A'];
      degree = DEG[letter - 'A'];
      if (*p == '#') {
        semi++;
        p++;
      }
      if (*p >= '0' && *p <= '9') octave = *p++ - '0';
    }
    int len = 2;
    if (*p == ':') {
      p++;
      len = *p++ - '0';
    }

    if (!rest && rnoteCount < R_MAX_NOTES) {
      int midi = 12 * (octave + 1) + semi;
      int step = degree + 7 * (octave - 4);
      RNote& n = rnotes[rnoteCount++];
      n.time = t;
      n.freq = (uint16_t)(440.0f * powf(2.0f, (midi - 69) / 12.0f));
      n.lenMs = (uint16_t)(len * eighthMs * 9 / 10);
      n.color = ((step % NUM_COLORS) + NUM_COLORS) % NUM_COLORS;
      n.eighths = len;
      n.done = false;
    }
    t += len * eighthMs;
  }
  songEnd = t;
  logf("SONG %d: %s  (tempo %d%%)\n", level, s.name, (int)(tempoScale * 100));
  char buf[5];
  snprintf(buf, sizeof(buf), "So%2d", level);
  segMessage(buf, R_LEAD_IN_MS);
}

void startRhythm(uint32_t now) {
  songIdx = 0;
  tempoScale = 1.0f;
  loadSong(0, now);
}

void rhythmMiss(uint32_t now) {
  PLAY(SND_RMISS);
  segMessage("----", 300);
  Serial.println("MISS");
  takeDamage(now);
}

void rhythmPress(uint8_t btn, uint32_t now) {
  // 判定範囲内にある一番早い音符を探す（音符は時間順に並んでいる）
  int idx = -1;
  for (int i = 0; i < rnoteCount; i++) {
    if (rnotes[i].done) continue;
    int32_t diff = (int32_t)(now - rnotes[i].time);
    if (diff < -R_GOOD_MS) break;
    if (abs(diff) <= R_GOOD_MS) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return;  // 何もないところで押してもペナルティなし

  RNote& n = rnotes[idx];
  n.done = true;
  if (n.color != btn) {
    rhythmMiss(now);
    return;
  }

  bool perfect = abs((int32_t)(now - n.time)) <= R_PERFECT_MS;
  combo++;
  maxCombo = MAX2(maxCombo, combo);
  score += (perfect ? 3 : 1) + MIN2(combo / 10, 2);

  sndNote[0] = { n.freq, n.lenMs };  // その音符の音を鳴らす
  playSound(sndNote, 1);
  judgeFlashUntil = now + 150;
  judgeFlashColor = perfect ? CRGB(255, 255, 255) : COLORS[n.color];
  addBlast(JUDGE_POS, COLORS[n.color], now);
  segMessage(perfect ? "PErF" : "Good", 300);

  if (combo % HEAL_COMBO == 0 && damage > 0) {
    damage--;
    addSweep(CRGB(0, 255, 60), now);
    segMessage("HEAL", 800);
  }
}

void updateRhythm(uint32_t now) {
  for (int i = 0; i < NUM_COLORS; i++) {
    if (justPressed[i]) {
      rhythmPress(i, now);
      if (state != PLAYING) return;
    }
  }

  // 見逃した音符
  for (int i = 0; i < rnoteCount; i++) {
    RNote& n = rnotes[i];
    if (n.done || (int32_t)(now - n.time) <= R_GOOD_MS) continue;
    n.done = true;
    rhythmMiss(now);
    if (state != PLAYING) return;
  }

  // 曲が終わったら次の曲へ（1周ごとにテンポアップ）
  if ((int32_t)(now - songEnd) > 800) {
    songIdx++;
    if (songIdx >= NUM_SONGS) {
      songIdx = 0;
      tempoScale *= R_TEMPO_UP;
    }
    level++;
    addSweep(CRGB(255, 255, 255), now);
    PLAY(SND_LEVELUP);
    loadSong(songIdx, now);
  }
}

void drawRhythm(uint32_t now) {
  if (now < damageFlashUntil) fill_solid(leds, NUM_LEDS, CRGB(40, 0, 0));
  else FastLED.clear();

  // 判定ライン：拍（4分音符）に合わせて脈打つ
  uint32_t beat = eighthMs * 2;
  int32_t d = (int32_t)(now - songFirstBeat);
  uint32_t phase = ((d % (int32_t)beat) + beat) % beat;
  uint8_t v = 90 - phase * 70 / beat;
  leds[JUDGE_POS] = (now < judgeFlashUntil) ? judgeFlashColor : CRGB(v, v, v);

  // 音符：判定ラインに向かって落ちる。長い音符ほど尾が長い
  float span = NUM_LEDS - 1 - JUDGE_POS;
  for (int i = 0; i < rnoteCount; i++) {
    RNote& n = rnotes[i];
    if (n.done) continue;
    float pos = JUDGE_POS + (int32_t)(n.time - now) * span / fallMs;
    if (pos > NUM_LEDS + 8) break;
    int head = (int)(pos + 0.5f);
    int tail = constrain((int)(n.eighths * eighthMs * span / fallMs) - 2, 0, 6);
    setPixel(head, COLORS[n.color]);
    for (int k = 1; k <= tail; k++) {
      CRGB c = COLORS[n.color];
      c.nscale8(50);
      setPixel(head + k, c);
    }
  }

  drawEffectsAndBase(now);
}

// =============================================================
//  ポン（2人対戦テニス）
//  下側プレイヤー = 赤 or 緑ボタン、上側プレイヤー = 青 or 黄ボタン
//  光の玉が自分の陣地（端の光っている部分）に来たらボタンで打ち返す
//  端ギリギリで打つほど球が速くなる！ 早すぎる空振りは少しの間ボタンが効かない
//  打ち返せずに玉が端から出たら相手の得点。先に5点取った方の勝ち
//
//  CPU対戦（タイトルで黄ボタン）: 上側をCPUが操作。自分はどのボタンでも打てる
//    CPUは速い球ほどミスしやすい → 端ギリギリで打って球を速くするのが攻略のコツ
//    CPUがリードすると少し弱く、負けていると少し強くなる（接戦になりやすい）
// =============================================================
#define PONG_ZONE ((NUM_LEDS + 5) / 10)  // 打ち返せる陣地の長さ（LED数。60個で6、90個で9）
#define PONG_SPEED_START (18.0f * LEN_SCALE)  // LED/秒
#define PONG_SPEED_GROW 1.06f   // ラリーが続くごとに加速
#define PONG_SPEED_MAX (110.0f * LEN_SCALE)
#define PONG_EDGE_BONUS 0.8f  // 端ギリギリで打ったときの加速（最大+80%）
#define PONG_WIN 5
#define PONG_LOCK_MS 400         // 空振りしたときにボタンが効かない時間
#define PONG_SERVE_AUTO_MS 3000  // サーブ待ちがこの時間を超えたら自動サーブ
#define CPU_MISS_MIN 5           // 遅い球でのCPUのミス率(%)
#define CPU_MISS_MAX 70          // 最高速の球でのCPUのミス率(%)
#define CPU_MISS_PER_PT 8        // 点差1点あたりのミス率の補正(%)
#define CPU_SERVE_MS 1000        // CPUがサーブするまでの待ち時間

const CRGB PONG_COLORS[2] = { CRGB(255, 0, 0), CRGB(0, 0, 255) };  // 下側=赤, 上側=青

float pongPos, pongVel, pongRally;
int pongScore[2];
int pongServer;
int pongWinner;
int pongRallyCount;
bool pongServing;
uint32_t pongServeStart;
uint32_t pongLockUntil[2];
uint32_t pongHitFlash[2];
Note sndPong[1];
const Note SND_PONG_POINT[] = { { 400, 80 }, { 200, 200 } };

// CPU：球が自分に向かってきた時点で「どこで打つか」「ミスするか」を決める
float cpuTarget;  // 端からこの距離まで来たら打つ
bool cpuWillMiss;

void cpuPlan() {
  float t = constrain((fabsf(pongVel) - PONG_SPEED_START) / (PONG_SPEED_MAX - PONG_SPEED_START), 0.0f, 1.0f);
  int missPct = CPU_MISS_MIN + (int)(t * (CPU_MISS_MAX - CPU_MISS_MIN));
  missPct += (pongScore[1] - pongScore[0]) * CPU_MISS_PER_PT;  // CPUがリード → ミスしやすく
  missPct = constrain(missPct, 3, 85);
  cpuWillMiss = random(100) < missPct;
  cpuTarget = random(30, (PONG_ZONE - 1) * 100) / 100.0f;  // 陣地内のランダムな位置で打つ
}

void startPong(uint32_t now) {
  pongScore[0] = pongScore[1] = 0;
  pongLockUntil[0] = pongLockUntil[1] = 0;
  pongHitFlash[0] = pongHitFlash[1] = 0;
  pongServer = random(2);
  pongServing = true;
  pongServeStart = now;
}

void pongServe(uint32_t now) {
  pongServing = false;
  pongRally = PONG_SPEED_START;
  pongRallyCount = 0;
  pongPos = (pongServer == 0) ? PONG_ZONE : NUM_LEDS - 1 - PONG_ZONE;
  pongVel = (pongServer == 0) ? pongRally : -pongRally;
  if (pongCpu && pongServer == 0) cpuPlan();
  if (!playSample(SMP_SND_FIRE)) playSound(SND_FIRE, 1);
}

void pongPoint(int scorer, uint32_t now) {
  pongScore[scorer]++;
  int loserEnd = (scorer == 0) ? NUM_LEDS - 1 : 0;
  addBlast(loserEnd, PONG_COLORS[scorer], now);
  logf("POINT P%d  %d-%d  (rally %d)\n", scorer + 1, pongScore[0], pongScore[1], pongRallyCount);

  if (pongScore[scorer] >= PONG_WIN) {
    pongWinner = scorer;
    state = GAMEOVER;
    stateStart = now;
    PLAY(SND_RECORD);
    if (pongCpu) Serial.println(scorer == 0 ? "YOU WIN!" : "CPU WINS");
    else logf("P%d WINS!\n", scorer + 1);
    return;
  }
  PLAY(SND_PONG_POINT);
  pongServer = 1 - scorer;  // 取られた側がサーブ
  pongServing = true;
  pongServeStart = now;
}

void updatePong(uint32_t now, float dt) {
  bool press[2] = { justPressed[0] || justPressed[1], justPressed[2] || justPressed[3] };
  if (pongCpu) {
    press[0] = press[0] || press[1];  // 1人用はどのボタンでもOK
    press[1] = false;
    if (pongServing) {
      press[1] = (pongServer == 1) && (now - pongServeStart > CPU_SERVE_MS);
    } else if (pongVel > 0 && !cpuWillMiss && (NUM_LEDS - 1 - pongPos) <= cpuTarget) {
      press[1] = true;
    }
  }

  if (pongServing) {
    if (press[pongServer] || now - pongServeStart > PONG_SERVE_AUTO_MS) pongServe(now);
    return;
  }

  pongPos += pongVel * dt;

  for (int p = 0; p < 2; p++) {
    if (!press[p] || now < pongLockUntil[p]) continue;
    bool coming = (p == 0) ? (pongVel < 0) : (pongVel > 0);
    float dist = (p == 0) ? pongPos : (NUM_LEDS - 1 - pongPos);  // 自分の端までの距離
    if (coming && dist <= PONG_ZONE && dist >= -0.5f) {
      // 打ち返し成功
      float depth = 1.0f - constrain(dist / PONG_ZONE, 0.0f, 1.0f);  // 0=陣地の入口, 1=端ギリギリ
      pongRally = MIN2(pongRally * PONG_SPEED_GROW, PONG_SPEED_MAX);
      float speed = MIN2(pongRally * (1.0f + PONG_EDGE_BONUS * depth), PONG_SPEED_MAX);
      pongVel = (p == 0) ? speed : -speed;
      pongRallyCount++;
      pongHitFlash[p] = now + 120;
      if (depth > 0.8f) addBlast((int)(pongPos + 0.5f), CRGB(255, 255, 255), now);  // ギリギリ！
      sndPong[0] = { (uint16_t)(500 + speed * 15), 30 };
      playSound(sndPong, 1);
      if (pongCpu && p == 0) cpuPlan();
    } else {
      // 空振り：少しの間ボタンが効かない
      pongLockUntil[p] = now + PONG_LOCK_MS;
    }
  }

  if (pongPos < -0.5f) pongPoint(1, now);
  else if (pongPos > NUM_LEDS - 0.5f) pongPoint(0, now);
}

void drawPong(uint32_t now) {
  FastLED.clear();

  // 陣地：打ち返した瞬間は明るく、空振り中は暗く
  for (int p = 0; p < 2; p++) {
    uint8_t v = (now < pongHitFlash[p]) ? 160 : (now < pongLockUntil[p] ? 3 : 18);
    for (int i = 0; i < PONG_ZONE; i++) {
      CRGB c = PONG_COLORS[p];
      c.nscale8(v);
      setPixel(p == 0 ? i : NUM_LEDS - 1 - i, c);
    }
  }

  // 得点：中央から外側へ点で表示
  int mid = NUM_LEDS / 2;
  for (int s = 0; s < pongScore[0]; s++) {
    CRGB c = PONG_COLORS[0];
    c.nscale8(90);
    setPixel(mid - 2 - s * 2, c);
  }
  for (int s = 0; s < pongScore[1]; s++) {
    CRGB c = PONG_COLORS[1];
    c.nscale8(90);
    setPixel(mid + 2 + s * 2, c);
  }

  // 玉：速いほど 黄→赤 に。後ろに尾を引く
  if (pongServing) {
    if ((now / 200) % 2) {
      int sp = (pongServer == 0) ? PONG_ZONE : NUM_LEDS - 1 - PONG_ZONE;
      setPixel(sp, CRGB(255, 255, 255));
    }
  } else {
    float speed = fabsf(pongVel);
    uint8_t hue = map((long)speed, (long)PONG_SPEED_START, (long)PONG_SPEED_MAX, 64, 0);
    int head = (int)(pongPos + 0.5f);
    int dir = (pongVel > 0) ? -1 : 1;  // 尾は進行方向の逆
    setPixel(head, CRGB(255, 255, 255));
    for (int k = 1; k <= 3; k++) setPixel(head + dir * k, CRGB(CHSV(hue, 255, 200 >> (k - 1))));
  }

  drawEffects(now);
}

// 勝った側の半分が勝者の色でキラキラ
void drawPongResult(uint32_t now) {
  FastLED.clear();
  int from = (pongWinner == 0) ? 0 : NUM_LEDS / 2;
  for (int i = from; i < from + NUM_LEDS / 2; i++) {
    CRGB c = PONG_COLORS[pongWinner];
    c.nscale8(((now / 60 + i) % 4 == 0) ? 255 : 40);
    leds[i] = c;
  }
}

// =============================================================
//  サイモン（記憶ゲーム）
//  テープを4色のエリアに分け、光る順番（＋音）を覚えて同じ順にボタンを押す。
//  正解するたびに1つずつ長くなり、だんだん速くなる。1回でも間違えたら終了
//  （正解だった色を見せてくれる）。スコア = 覚えられた長さ
// =============================================================
#define SIMON_MAX 99
#define S_ON_START 550  // 1つの色が光っている時間（最初）
#define S_ON_STEP 25    // 1つ長くなるごとに短縮
#define S_ON_MIN 220
#define S_INPUT_TIMEOUT 5000  // この時間押さないと失敗
#define S_PRESS_MS 250        // 自分で押したときに光る時間
#define S_READY_MS 900        // 出題前の間

enum SimonPhase { S_SHOW,
                  S_INPUT,
                  S_CLEAR };
// ド・ミ・ソ・ド（4色を同時に覚えやすい和音）
const uint16_t SIMON_FREQ[NUM_COLORS] = { 262, 330, 392, 523 };
uint8_t simonSeq[SIMON_MAX];
int simonLen = 0, simonIdx = 0, simonShown = -1;
SimonPhase simonPhase = S_SHOW;
uint32_t simonT = 0;
int simonLit = -1;  // 今光っているエリア（-1 = なし）
uint32_t simonLitUntil = 0;
uint8_t simonExpect = 0;  // 間違えたとき、本当は何色だったか
Note sndSimon[1];

uint32_t simonOnMs() {
  return (uint32_t)MAX2(S_ON_START - (simonLen - 1) * S_ON_STEP, S_ON_MIN);
}

void simonTone(int c, uint32_t ms) {
  sndSimon[0] = { SIMON_FREQ[c], (uint16_t)ms };
  playSound(sndSimon, 1);
}

void simonNextRound(uint32_t now) {
  simonSeq[simonLen++] = random(NUM_COLORS);
  simonPhase = S_SHOW;
  simonT = now + S_READY_MS;
  simonShown = -1;
  simonLit = -1;
}

void startSimon(uint32_t now) {
  simonLen = 0;
  simonNextRound(now);
}

void simonFail(uint32_t now) {
  simonExpect = simonSeq[simonIdx];
  simonLit = -1;
  logf("SIMON MISS (answer %s)\n", COLOR_NAMES[simonExpect]);
  gameOver(now);
}

void updateSimon(uint32_t now) {
  if (simonLit >= 0 && simonPhase != S_SHOW && now >= simonLitUntil) simonLit = -1;

  switch (simonPhase) {
    case S_SHOW:
      {  // お手本を順番に光らせる
        if (now < simonT) return;
        uint32_t on = simonOnMs(), step = on + on * 2 / 5;
        int k = (now - simonT) / step;
        if (k >= simonLen) {
          simonPhase = S_INPUT;
          simonIdx = 0;
          simonT = now;
          simonLit = -1;
          return;
        }
        bool lit = (now - simonT) % step < on;
        simonLit = lit ? simonSeq[k] : -1;
        if (lit && k != simonShown) {
          simonShown = k;
          simonTone(simonSeq[k], on);
        }
        break;
      }
    case S_INPUT:
      {  // プレイヤーの番
        for (int i = 0; i < NUM_COLORS; i++) {
          if (!justPressed[i]) continue;
          if (i != simonSeq[simonIdx]) {
            simonFail(now);
            return;
          }
          simonLit = i;
          simonLitUntil = now + S_PRESS_MS;
          simonTone(i, S_PRESS_MS);
          simonT = now;
          if (++simonIdx == simonLen) {
            score = simonLen;
            simonPhase = S_CLEAR;
            logf("SIMON CLEAR %d\n", score);
          }
          return;
        }
        if (now - simonT > S_INPUT_TIMEOUT) simonFail(now);
        break;
      }
    case S_CLEAR:  // 1つ長くして次へ
      if (now - simonT < S_PRESS_MS + 150) return;
      addSweep(CRGB(0, 255, 60), now);
      if (score % 5 == 0) {
        PLAY(SND_LEVELUP);
        segMessage("Good", 900);
      }
      if (simonLen >= SIMON_MAX) {
        gameOver(now);
        return;
      }  // 全部覚えた！
      simonNextRound(now);
      break;
  }
}

// 4色のエリア（タイトルの帯と同じ並び）
void drawSimonZone(int c, uint8_t scale) {
  int band = NUM_LEDS / NUM_COLORS;
  int from = c * band, to = (c == NUM_COLORS - 1) ? NUM_LEDS : from + band;
  for (int i = from; i < to; i++) {
    CRGB col = COLORS[c];
    col.nscale8(scale);
    leds[i] = col;
  }
  if (c > 0) leds[from] = CRGB::Black;  // 区切り
}

void drawSimon(uint32_t now) {
  FastLED.clear();
  // 自分の番はエリアが少し明るい（残り2秒を切ると点滅で急かす）
  uint8_t dim = 8;
  if (simonPhase == S_INPUT) {
    bool hurry = now - simonT > S_INPUT_TIMEOUT - 2000;
    dim = (hurry && (now / 150) % 2) ? 8 : 35;
  }
  for (int c = 0; c < NUM_COLORS; c++) drawSimonZone(c, c == simonLit ? 255 : dim);
  drawEffects(now);
}

// 間違えたとき：正解だったエリアを点滅
void drawSimonAnswer(uint32_t now) {
  if ((now / 200) % 2) drawSimonZone(simonExpect, 255);
}

// =============================================================
//  7セグ表示（4桁）
//   タイトル    : "nor" → ハイスコア → "Pon" → "rHY" → ハイスコア → "CPU" → "nEno" → ハイスコア（1秒ごと）
//   プレイ中    : スコア。イベント時は "L  5"(レベル) "HEAL" "boSS"(点滅) "nICE"(ボス撃破)
//                 リズムでは "So 1"(曲番号) "PErF" "Good" "----"(ミス)
//   ゲームオーバー: "End" ⇔ スコア（ハイスコア更新時は "bESt" ⇔ スコア）
// =============================================================
#if USE_7SEG
// bitDelay はライブラリ標準の100µs（短くすると、コンデンサ付きの安いモジュールで表示されないことがある）
TM1637Display seg7(SEG_CLK_PIN, SEG_DIO_PIN, 100);
#endif
uint8_t segShown[4] = { 0xFF, 0xFF, 0xFF, 0xFF };  // 前回表示した内容（変化した時だけ書き込む）

// 7セグで表せる文字（a=bit0 … g=bit6）
uint8_t segChar(char c) {
  if (c >= '0' && c <= '9') {
    static const uint8_t DIGITS[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    return DIGITS[c - '0'];
  }
  switch (c) {
    case 'A': return 0x77;
    case 'b': return 0x7C;
    case 'C': return 0x39;
    case 'c': return 0x58;
    case 'd': return 0x5E;
    case 'E': return 0x79;
    case 'F': return 0x71;
    case 'G': return 0x3D;
    case 'H': return 0x76;
    case 'I': return 0x30;
    case 'i': return 0x04;
    case 'L': return 0x38;
    case 'n': return 0x54;
    case 'O': return 0x3F;
    case 'o': return 0x5C;
    case 'P': return 0x73;
    case 'r': return 0x50;
    case 'S': return 0x6D;
    case 't': return 0x78;
    case 'U': return 0x3E;
    case 'u': return 0x1C;
    case 'Y': return 0x6E;
    case '-': return 0x40;
    case '_': return 0x08;
    default: return 0x00;
  }
}

void segText(const char* t, uint8_t out[4]) {
  bool ended = false;
  for (int i = 0; i < 4; i++) {
    if (!t[i]) ended = true;
    out[i] = ended ? 0 : segChar(t[i]);
  }
}

void segNumber(int n, uint8_t out[4]) {  // 右寄せ
  char buf[5];
  snprintf(buf, sizeof(buf), "%4d", constrain(n, 0, 9999));
  segText(buf, out);
}

void updateSeg(uint32_t now) {
#if USE_7SEG
  uint8_t out[4];
  if (now < segMsgUntil) {
    segText(segMsg, out);
  } else if (state == TITLE) {
    // ボタンの色の順（赤→緑→青→黄）にモード名を表示
    // 最後に長押しのサイモン（"nEno" = メモ）
    switch ((now / 1000) % 8) {
      case 0: segText("nor ", out); break;
      case 1: segNumber(hiScores[MODE_NORMAL], out); break;
      case 2: segText("Pon ", out); break;
      case 3: segText("rHY ", out); break;
      case 4: segNumber(hiScores[MODE_RHYTHM], out); break;
      case 5: segText("CPU ", out); break;
      case 6: segText("nEno", out); break;
      case 7: segNumber(hiScores[MODE_SIMON], out); break;
    }
  } else if (mode == MODE_PONG) {
    // プレイ中は "3--2"、終了後は "P1" ⇔ 最終スコア
    char buf[5];
    if (state == GAMEOVER && (now / 800) % 2 == 0) {
      if (pongCpu) strcpy(buf, pongWinner == 0 ? "YOU " : "CPU ");
      else snprintf(buf, sizeof(buf), "P%d  ", pongWinner + 1);
    } else {
      snprintf(buf, sizeof(buf), "%d--%d", pongScore[0], pongScore[1]);
    }
    segText(buf, out);
  } else if (state == PLAYING) {
    if (mode == MODE_NORMAL && bossPhase == BOSS_WARNING) segText((now / 250) % 2 ? "    " : "boSS", out);
    else segNumber(score, out);
  } else {  // GAMEOVER
    bool label = (now - stateStart < 1500) || ((now / 800) % 2 == 0);
    if (label) segText(newRecord ? "bESt" : "End ", out);
    else segNumber(score, out);
  }

  // 内容が変わった時と、念のため1秒ごとに書き込む（1回取りこぼしても表示が戻る）
  static uint32_t segLastWrite = 0;
  if (memcmp(out, segShown, 4) != 0 || now - segLastWrite > 1000) {
    memcpy(segShown, out, 4);
    seg7.setBrightness(SEG_BRIGHT);
    seg7.setSegments(out);
    segLastWrite = now;
  }
#endif
}

// ---------- メイン ----------
#if AUTO_SLEEP
// テープLEDとDACの電源を切って眠る。どれかのボタンを長押しすると、電源を入れ直したときと同じように起きる
void powerOff() {
  Serial.println("SLEEP");
  FastLED.clear(true);
#if USE_7SEG
  const uint8_t blank[4] = { 0, 0, 0, 0 };
  seg7.setBrightness(0, false);  // 7セグは 3V3 につながったままなので、表示を消しておく
  seg7.setSegments(blank);
#endif
  digitalWrite(POWER_PIN, LOW);  // MOSFET オフ
  gpio_hold_en((gpio_num_t)POWER_PIN);  // 眠っている間も LOW のままにする
  // ボタンを押したままだとすぐ起きてしまうので、全部離すまで待つ
  for (bool held = true; held;) {
    held = false;
    for (int i = 0; i < NUM_COLORS; i++)
      if (digitalRead(BUTTON_PINS[i]) == LOW) held = true;
    delay(50);
  }
  // ライトスリープを使う（ディープスリープは GPIO 0〜5 でしか起こせず、黄ボタン＝GPIO 6 が使えないため）
  for (int i = 0; i < NUM_COLORS; i++) {
    gpio_sleep_sel_dis((gpio_num_t)BUTTON_PINS[i]);  // 眠っている間もプルアップを保つ
    gpio_wakeup_enable((gpio_num_t)BUTTON_PINS[i], GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  Serial.flush();
  for (;;) {
    esp_light_sleep_start();  // どれかのボタンが押されるまで、ここで止まる
    // 長押しされたときだけ起きる。すぐ離したら、また眠る（かばんの中などで勝手に電源が入らないように）
    uint32_t t0 = millis();
    for (bool held = true; held;) {
      held = false;
      for (int i = 0; i < NUM_COLORS; i++)
        if (digitalRead(BUTTON_PINS[i]) == LOW) held = true;
      if (held && millis() - t0 >= POWER_ON_HOLD_MS) ESP.restart();  // 最初からやり直す（電源を入れ直したときと同じ）
      delay(10);
    }
  }
}
#endif

void setup() {
#if POWER_PIN >= 0
  // 最初にテープLEDとDACの電源を入れる（MOSFET オン）
#if AUTO_SLEEP
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)POWER_PIN);
#endif
  pinMode(POWER_PIN, OUTPUT);
  digitalWrite(POWER_PIN, HIGH);
  delay(100);
#endif
  Serial.begin(115200);
  for (int i = 0; i < NUM_COLORS; i++) pinMode(BUTTON_PINS[i], INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MAX_MILLIAMPS);
  FastLED.clear(true);

#if USE_I2S
  synthBegin();
#elif BUZZER_PIN >= 0
#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, 2000, 8);
#elif defined(ESP32)
  ledcSetup(0, 2000, 8);
  ledcAttachPin(BUZZER_PIN, 0);
#endif
  buzz(0);
#endif

#if USE_7SEG
  seg7.setBrightness(SEG_BRIGHT);
  seg7.clear();
#endif

  loadHiScores();

#if defined(NRF52840_XXAA) && defined(PIN_CHARGING_CURRENT) && FAST_CHARGE
  // XIAO nRF52840：充電電流を 50mA → 100mA に（このピンを LOW にすると切り替わる）
  pinMode(PIN_CHARGING_CURRENT, OUTPUT);
  digitalWrite(PIN_CHARGING_CURRENT, LOW);
#endif

#if defined(ESP32)
  randomSeed(esp_random());
#else
  randomSeed(analogRead(A0) ^ micros());  // A0(D0) は未接続のまま
#endif
  lastFrame = millis();
  logf("Arrow Defense - RED: normal (hi %d) / GREEN: pong / BLUE: rhythm (hi %d) / YELLOW: pong vs CPU\n",
       hiScores[MODE_NORMAL], hiScores[MODE_RHYTHM]);
  logf("HOLD any button 1s: simon (hi %d)\n", hiScores[MODE_SIMON]);

  // ボタンを押して起こしたとき、そのままゲームが始まらないように、離すまで待つ
  for (bool held = true; held;) {
    held = false;
    for (int i = 0; i < NUM_COLORS; i++)
      if (digitalRead(BUTTON_PINS[i]) == LOW) held = true;
    delay(10);
  }
  lastFrame = lastInput = millis();
}

void loop() {
  uint32_t now = millis();
  updateSound(now);

  float dt = (now - lastFrame) / 1000.0f;
  if (dt < 0.010f) return;  // 約100fps
  lastFrame = now;

  readButtons(now);

#if AUTO_SLEEP
  // 赤と黄（両はしのボタン）を同時に長押し → 電源を切る。ゲーム中でも効く
  static bool offHold = false;
  static uint32_t offStart = 0;
  if (btnState[0] && btnState[NUM_COLORS - 1]) {
    if (!offHold) {
      offHold = true;
      offStart = now;
    }
    selBtn = -1;  // 長押しでサイモンが始まらないようにする
    if (now - offStart >= POWER_HOLD_MS) powerOff();
  } else offHold = false;
#endif

  switch (state) {
    case TITLE:
      if (handleModeSelect(now)) break;
      drawTitle(now);
      drawHoldBar(now);
      break;
    case PLAYING:
      if (mode == MODE_RHYTHM) updateRhythm(now);
      else if (mode == MODE_PONG) updatePong(now, dt);
      else if (mode == MODE_SIMON) updateSimon(now);
      else updateGame(now, dt);
      if (state != PLAYING) drawGameOver(now);
      else if (mode == MODE_RHYTHM) drawRhythm(now);
      else if (mode == MODE_PONG) drawPong(now);
      else if (mode == MODE_SIMON) drawSimon(now);
      else drawGame(now);
      break;
    case GAMEOVER:
      // 演出中の誤操作を防ぐため2.5秒後から再スタート可能（タイトルと同じ操作）
      if (now - stateStart > 2500 && handleModeSelect(now)) break;
      drawGameOver(now);
      drawHoldBar(now);
      break;
  }

  updateSeg(now);
  FastLED.show();

#if AUTO_SLEEP
  // ゲーム中以外で、しばらくボタンを押さなかったら電源を切って眠る
  if (state == PLAYING) lastInput = now;
  else if (now - lastInput > SLEEP_AFTER_MIN * 60000UL) powerOff();
#endif
}
