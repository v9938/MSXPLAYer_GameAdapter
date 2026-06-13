////////////////////////////////////////////////////////////////////////
//
// MSXPLAYer Gaming Cartridge Reader/Writer
// Copyright 2025 @V9938
//	
//	25/10/20 V0.5		1st version (for Test)
//	25/11/05 V0.6		暫定Release版
//                      PCB Version B対応 (1Slot,2Slot版)
//                      コマンドリストを commands.c/h に分割
//                      ポートリストを ports.c/h に分割
//                      stdarg.h を追加 (cdc_printf の可変引数対応)
//                      z80AddressVaild / bufAddressVaild の不適切な戻り値を修正
//                      tud_cdc_rx_cb 内の skipByte の扱い修正
// (以降公開版)
//  26/04/06 V1.0       正式Release版(1SLOT版) 
//                      Single Slot版 PCB Rev C/D対応
//                      SLOT IFの電圧を3.3V>5Vに変更
//                      CLOCK端子の対応
//                      量産テスト向け機能の追加
//                      Scriptモードの追加など
//  26/04/13 V1.1       DTR信号監視の修正 
//                      受信キャラクタLFCR判定を接続毎にリセットするように変更
//                      CDCのENDPOINTのバッファーサイズを1024>64に変更
//  26/04/14 V1.11      POWER OFF時の電源安定待ち時間を1ms>100msに変更
//  26/04/18 V1.12      LSCRを追加、スクリプトのLoop回数を外部から設定可能にした。（出荷FW)
//  26/05/20 V1.20      SMTHコマンドの追加、スクリプトのPUSH命令を追加
//  26/05/20 V1.21      MSX初期のROMカセットではアクセス速度が<180ns以上ものが散見されたので
//                      読み込みタイミングを調整した
//  26/05/28 v1.30      BS6101を採用したASCII3 ROMの動作不良対策で書き込み時のRD信号をPushPullに変更
//  26/05/31 v1.40      HVERの表示変更、Megarom Read支援コマンドRMSET/RMRDを追加
//  26/06/01 v1.41      IOTRのIOアドレス加算の仕様を変更
//  26/06/01 v1.42      RMRDのBANK指定のバグを修正
//  26/06/01 v1.43      CDC OUTPUT BUFFERのmutex制御を追加
//
////////////////////////////////////////////////////////////////////////

/*
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * 修正点:
 *  - コマンドリストを commands.c/h に分割
 *  - ポートリストを ports.c/h に分割
 *  - parse_hex_string 等の挙動は保持
 *  - stdarg.h を追加 (cdc_printf の可変引数対応)
 *  - z80AddressVaild / bufAddressVaild の不適切な戻り値を修正 (bool を返す)
 *  - tud_cdc_rx_cb 内の skipByte の扱い修正（スキップ時に書き込みを行わない）
 *
 */

#include <stdio.h>                              // printf 用 // 標準入出力
#include <stdlib.h>                             // 標準ライブラリ
#include <bsp/board_api.h>                      // ボード API // TinyUSB/board 初期化
#include <tusb.h>                               // TinyUSB core

#include <string.h>                             // 文字列操作 // strcpy/strtok 等
#include <stdbool.h>                            // bool 型
#include <ctype.h>                              // toupper() 等
#include <stdarg.h>                             // va_list 等 (cdc_printf 用) // 追加

#include "hardware/pwm.h"
#include "hardware/gpio.h"                      // GPIO API // RP2040
#include "hardware/timer.h"                     // タイミング API
#include "hardware/structs/io_bank0.h"
#include "pico/multicore.h"                     // マルチコア API // core1 起動
#include "pico/stdio.h"                         // stdio 初期化
#include "pico/mutex.h"                         // mutex
#include "hardware/clocks.h" // set_sys_clock_khz  // クロック API
#include "ws2812.pio.h"                         // WS2812 PIO プログラム
#include "hardware/structs/systick.h"           // systick 直接操作 (shortpause マクロ)
#include "pico/bootrom.h"                       // reset_usb_boot 等
#include "boot/picoboot.h"
#include "boot/picobin.h"
#include "pwm_low_hiz.pio.h"                    // コンパイル済みPIOプログラムヘッダ
#include "pico/version.h"                       // SDKのバージョン情報
#include "build_timestamp.h"                    // TimeStampのヘッダーファイル

#include "commands.h"                           // 別ファイルに分割したコマンドテーブル // commands.c 参照
#include "ports.h"                              // 別ファイルに分割したボードピン定義 // ports.c 参照

//#define HW_NAME     "ILF ROM Cassette Reader V2" // ハード名
#define HW_NAME     "ILF ROM Cassette Reader V2" // ハード名

// Hardware Infomation
#define HWINFO_SLOTNUM          1           // スロット数
#define HWINFO_POWERCTRL        1           // SLOT Power Control          (0無/1有)
#define HWINFO_SLOTPHY          2           // SLOTの物理配置情報　bit0 Slot0 / bit1 Slot1 / bit2 Slot2 /bit3 Slot3     
//#define HWINFO_CURRENTSENSOR    1           // 電流センサ                   (0無/1有)
#define HWINFO_PWR12V           0           // 12V/-12V DCDC Unit          (0無/1有)
#define HWINFO_SLOTCLOCK        0           // スロットのクロック供給        (0無/1有)
#define HWINFO_LINEOUT          0           // スロットのLineOut            (0無/1有)
#define HWINFO_PSGUNIT          0           // PSGUNIT                     (0無/1有)

//#define PCBVER_B
#define PCBVER_B2

#ifdef PCBVER_B2 
//Rev B2
    #define HW_VERSION  "REV_B2"                      // ハードリビジョン
    #define HWINFO_CURRENTSENSOR    1           // 電流センサ                   (0無/1有)
    #define SlotPowerON()            gpio_put(2, 0)
    #define SlotPowerOFF()           gpio_put(2, 1)
#endif

#ifdef PCBVER_B 
//Rev B
    #define HW_VERSION  "REV_B"                      // ハードリビジョン
    #define HWINFO_CURRENTSENSOR    0           // 電流センサ                   (0無/1有)
    #define SlotPowerON()            gpio_put(2, 1)
    #define SlotPowerOFF()           gpio_put(2, 0)
#endif


#define IS_RGBW true
#define NUM_PIXELS 1
#define CLOCK_PIN 7

#ifdef PICO_DEFAULT_WS2812_PIN
#define WS2812_PIN PICO_DEFAULT_WS2812_PIN      // デフォルトの WS2812 ピン
#else
    #ifdef PCBVER_B 
    //Rev B
        #define WS2812_PIN 6                            // デフォルト GP6 を使用 // 元の定義に合わせる
    #endif
    #ifdef PCBVER_B2 
    // Rev B2-
        #define WS2812_PIN 4                            // デフォルト GP4 を使用 // 元の定義に合わせる
    #endif
#endif

#define BIN_DEBUG 1
// #define MAX_STRING   32                          // (注) commands.h と整合
#define MAX_ARG      4
#define RX_BUF_SIZE  2048
#define CMD_BUF_NUM  2048
#define DATABUF_SIZE 64*1024                     // 64KB バッファ
#define CMP_LOOP     1000
uint16_t cmpLoopMax = CMP_LOOP;


#define LED_PIN      25
#define shortpause(a){systick_hw->cvr=0;while(systick_hw->cvr>a){};} // systick を使った短待ち

//HSET定義
#define HSET_ADDRESS_MEMWAIT 0
#define HSET_DEFNUM_MEMWAIT 0                     // MEMWAIT初期値(0ns)

#define HSET_ADDRESS_RDWAIT 1                     
#define HSET_DEFNUM_RDWAIT 100                   // MEM Read時のRD信号幅 初期値(1000ns)

#define HSET_ADDRESS_WRWAIT 2
#define HSET_DEFNUM_WRWAIT 18                    // MEM Read時のWr信号幅 初期値(180ns)

#define HSET_ADDRESS_P6MODE 3
#define HSET_DEFNUM_P6MODE false                // PC6001 modeの 初期値(無効)


uint8_t slotMem[DATABUF_SIZE];                    // ワークバッファ (64KB) // スロットデータ保存領域
uint8_t defaultSlot = 1;                          // デフォルトスロット番号 // SSEL 未指定時使用
bool displayFlag = true;                          // 結果出力フラグ // true: OK/FAIL を表示
uint32_t passCount = 0;                           // 表示OFF時の成功カウント(スクリプトモードのモードでも使います)
uint32_t errCount = 0;                            // 表示OFF時の失敗カウント(スクリプトモードのモードでも使います)
int lastError = 0;                                // 最後のエラーコード
bool sltPower = false;                            // スロット電源状態フラグ // true = ON
bool sltAcc = false;                              // スロットアクセス中フラグ // true = アクセス中
bool crlfFlag = false;                            // CR/LF 判定フラグ (BRCV 用)
bool comdbgFlag = false;                          // CMD実行LOGフラグ
int wrWait = HSET_DEFNUM_WRWAIT;                  // Memory Write時のwr信号Low時間(n x 10ns)
int rdWait = HSET_DEFNUM_RDWAIT;                  // Memory Read時のrd信号Low時間(n x 10ns)
int memWait = HSET_DEFNUM_MEMWAIT;                // Memory R/W時のアクセスのWait時間(n x 10ns)
bool p6_16kbMode = false;                         // PC6001 mode

uint slice_num;

PIO pio = pio0;                                  // PIO0 を使用
// ===== ステートマシン 0: WS2812 制御 (GPIO4) =====
uint sm_ws2812 = 0;
// ===== ステートマシン 1: CLK3.58MHz (GPIO7) =====
uint sm_slotclk = 1;

#define LED_STATUS_IDLE  0
#define LED_STATUS_READY 1
#define LED_STATUS_PON   2
#define LED_STATUS_ACC   3
bool ledChgReq  = false;
uint8_t ledColorR[4];
uint8_t ledColorG[4];
uint8_t ledColorB[4];

// スクリプトモードのエラーコード定義
typedef enum {
    SUCCESS = 0,
    ERR_INVALID_COMMAND = -1,
    ERR_FUNCTION_FAILED = -2,
    ERR_INVALID_SLOT = -3,
    ERR_OUT_OF_BOUNDS = -4,
    ERR_LOOP_OUT = -5
} ErrorCode;

// スクリプト実行エンジン状態を保存する構造体
typedef struct {
    uint8_t lastData;      // 直近の命令で使用したdata
    uint16_t instructionPC; // 現在の命令ポインタ
    uint32_t waitEndTime;   // Wait終了時刻（マイクロ秒）
    uint8_t isWaiting;      // Wait状態フラグ
} ExecutionState;

ExecutionState execState = {0, 0, 0, 0};

// GPIO組み合わせテーブル
typedef struct {
    uint gpio_out;
    uint gpio_check;
} GPIO_PAIR;

const GPIO_PAIR gpio_pairs[] = {
    {7, 9},      // GPIO7-9
    {8, 10},     // GPIO8-10
    {11, 13},    // GPIO11-13
    {12, 14},    // GPIO12-14
    {15, 17},    // GPIO15-17
    {16, 18},    // GPIO16-18
    {19, 21},    // GPIO19-21
    {20, 22},    // GPIO20-22
    {23, 25},    // GPIO23-25
    {24, 26},    // GPIO24-26
    {27, 32},    // GPIO27-32
    {31, 33},    // GPIO31-33
    {34, 36},    // GPIO34-36
    {35, 37},    // GPIO35-37
    {38, 40},    // GPIO38-40
    {39, 41},    // GPIO39-41
    {42, 45},    // GPIO42-45
    {43, 46},    // GPIO43-46
    {44, 47}     // GPIO44-47
};

const int NUM_GPIO_PAIRS = sizeof(gpio_pairs) / sizeof(GPIO_PAIR);

// MEGAROM Mapperの設定値
typedef struct {
    bool megarom;            // 非MegaROM or MegaROM 
    uint16_t mapSelAddress;  // MEGAROM Mapperの選択アドレス 
    uint16_t romReadAddress; // Mapper Read Address
    uint16_t romReadSize;    // Mapper Read size
} MegaROM_Mapper;

MegaROM_Mapper romMapper ;



// MUTEX
auto_init_mutex(cmdcount_mutex);
auto_init_mutex(cdc_output_mutex);


// PIO にデータを流すラッパー
static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio0, sm_ws2812, pixel_grb << 8u); // PIO0 SM0 にデータをブロッキングで送信 // WS2812 用データ送信
}

// gpioc_hi のマスク書き換えヘルパ
static inline void gpiohi_put_masked(uint32_t mask, uint32_t value) {
    gpioc_hi_out_xor((gpioc_hi_out_get() ^ value) & mask); // 現在値と XOR → マスク適用で出力変更
    gpioc_hi_oe_xor((gpioc_hi_oe_get() ^ (~value)) & mask); // 現在値と XOR → マスク適用で出力変更
}

static inline void gpiolo_put_masked(uint32_t mask, uint32_t value) {
    gpioc_lo_out_xor((gpioc_lo_out_get() ^ value) & mask); // 現在値と XOR → マスク適用で出力変更
    gpioc_lo_oe_xor((gpioc_lo_oe_get() ^ (~value)) & mask); // 現在値と XOR → マスク適用で出力変更
}

// GRB を 32bit に合成するユーティリティ
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return  ((uint32_t) (r) << 16)  | ((uint32_t) (g) << 8) | (uint32_t) (b); // GRB 順にビットシフト
}

//void custom_cdc_task(void);
void cdc_task(void); // 前方宣言


// コマンドリングバッファは commands.h の Command_t を使用 // 実体はここにある
volatile Command_t commandBufs[CMD_BUF_NUM]; // コマンドキュー配列 // Core0 が書き込み Core1 が読む
volatile int write_idx = 0;                  // 次に書き込むインデックス // produce index
volatile int read_idx = 0;                   // 次に読むインデックス // consume index
volatile int count = 0;                      // キュー内の有効エントリ数 // 同期注意

// RX/TX バッファ
//uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE];      // TinyUSB CDC の RX バッファ // 受信一時格納
//uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE];      // TinyUSB CDC の TX バッファ // 使用箇所あり
static uint8_t rx_buf[CFG_TUD_CDC_RX_BUFSIZE] __attribute__((aligned(4)));
static uint8_t tx_buf[CFG_TUD_CDC_TX_BUFSIZE] __attribute__((aligned(4)));
// RX tmp DATA BUFFER [UART>Reader](64K)
// uint8_t temp_rx_data[DATABUF_SIZE];          // 一時データバッファ（未使用箇所がある） // 将来用
// Line Buffer
#define LINE_BUF_SIZE 2048
char lineBuf[LINE_BUF_SIZE];                 // 1 行のコマンドを蓄えるバッファ // 改行まで保持
size_t lineLen = 0;                          // 現在の lineBuf 長 // インデックス

// 結果戻り値定義 (元のまま)
#define CMD_OK      0
#define CMD_NULL 1
#define CMD_FAIL    -1
#define CMD_BADPARM -2
#define CMD_UNKNOW  -3

// GPIO ピン定義は ports.c に移動 // board_pins を使用する際 ports.h を include
// bool overCurrent フラグ
bool overCurrent = false;                    // 過電流検出フラグ // 割り込みで立てられる

#define CDC_PRINTF_BUF_SIZE 1024
#define CDC_PRINTF_QSIZE    16

typedef struct {
    char buf[CDC_PRINTF_BUF_SIZE];            // 出力データバッファ // テキスト/バイナリを格納
    bool valid;                               // 有効フラグ // キューの有効性
    size_t binLength;                         // バイナリ長 (0 => テキスト) // 0 のときは strlen 使用
} cdc_msg_t;

static cdc_msg_t cdc_queue[CDC_PRINTF_QSIZE]; // CDC 出力キュー配列 // リングバッファ
static volatile int cdc_q_write = 0;          // 書き込みインデックス // produce index
static volatile int cdc_q_read  = 0;          // 読み出しインデックス // consume index
static volatile int cdc_q_count = 0;          // キュー内件数 // 同期注意

// CDCへ文字列出力 + 書式化（64バイト分割出力対応 ※MAX256Byte）
// Tiny-USBのCDC出力はUSB規格の都合で1回の送信MAXは64Byteそのため分割して送付する。
void cdc_printf(const char *fmt, ...) {

    while (cdc_q_count >= CDC_PRINTF_QSIZE-1) { // キュー満杯なら待機 // ブロッキング挙動
        sleep_ms(1);                             // 少し待って再試行
    }
    char tmp[CDC_PRINTF_BUF_SIZE];              // 書式展開バッファ
    va_list args;
    va_start(args, fmt);                        // 可変引数開始
    vsnprintf(tmp, sizeof(tmp), fmt, args);    // フォーマット出力
    va_end(args);                               // 可変引数終了

    strncpy(cdc_queue[cdc_q_write].buf, tmp, CDC_PRINTF_BUF_SIZE-1); // バッファコピー // NUL 終端保証
    cdc_queue[cdc_q_write].buf[CDC_PRINTF_BUF_SIZE-1] = 0; // 終端保証
    cdc_queue[cdc_q_write].binLength = 0;        // テキストモード指定
    cdc_queue[cdc_q_write].valid = true;         // 有効化
    cdc_q_write = (cdc_q_write + 1) % CDC_PRINTF_QSIZE; // 次の書き込み位置へ
    mutex_enter_blocking(&cdc_output_mutex);  // ロック
    cdc_q_count++;                                // 件数インクリメント
    mutex_exit(&cdc_output_mutex);  // ロック解除
}

void cdc_bufOutput(int address,int len) {
    int offset = 0;                              // バッファ送信オフセット // 送信済みバイト数
    int i;


    while (offset < len) {
        while (cdc_q_count >= CDC_PRINTF_QSIZE-1) { // キュー満杯時待機
            sleep_ms(1);
        }
        int chunk = ((len - offset) < CDC_PRINTF_BUF_SIZE) ? (len - offset) : CDC_PRINTF_BUF_SIZE; // 分割サイズ決定

        for (i=0;i<chunk;i++){
            if ((i+offset+address)<DATABUF_SIZE)            // 範囲チェック // バッファ外なら 0x00 を入れる
                cdc_queue[cdc_q_write].buf[i] = slotMem[i+offset+address]; // slotMem からコピー
            else
                cdc_queue[cdc_q_write].buf[i] = 0x00;       // 範囲外はゼロ埋め
        }

        cdc_queue[cdc_q_write].binLength = chunk; // バイナリ長を設定
        cdc_queue[cdc_q_write].valid = true;      // 有効化
        cdc_q_write = (cdc_q_write + 1) % CDC_PRINTF_QSIZE; // 次の書き込み位置へ
        mutex_enter_blocking(&cdc_output_mutex);  // ロック
        cdc_q_count++;                             // 件数インクリメント
        mutex_exit(&cdc_output_mutex);  // ロック解除
        offset += chunk;                           // オフセット進める
    }
    
}

int powerCheck(){
    // 電源ONかチェック
    if (sltPower == false) return CMD_FAIL;  // 電源OFF
    // 過電流時のフラグチェック
    if (overCurrent != false) return CMD_FAIL;                    // 過電流発生
    return CMD_OK;
}

void ledTask(bool ledChange){
    static int ledStatusOld = LED_STATUS_IDLE; // 直前表示状態保持
    static int ledStatuDelay = 0;             // アクセス表示保持カウンタ

    int ledStatus;

    if (ledChange == true) ledStatusOld = LED_STATUS_IDLE;

    if (ledStatuDelay != 0 ) {
        ledStatuDelay--;               // タイマをデクリメント
//      ledStatus = ledStatusOld;      // タイマ動作中は前の表示継続
        return;
    }
    ledStatus = LED_STATUS_IDLE;
    if (tud_cdc_ready()) ledStatus = LED_STATUS_READY;

    if (sltPower == true){
        ledStatus = LED_STATUS_PON;                        // 電源 ON なら PON
        if (sltAcc == true) {
            ledStatuDelay = 50000;                         // アクセス時は一定時間 ACC 表示を維持
            ledStatus = LED_STATUS_ACC;                    // アクセス表示に設定
        }
    }

    if (ledStatusOld != ledStatus){                        // 状態変化時のみ出力更新
        if(ledStatus == LED_STATUS_IDLE)     put_pixel(urgb_u32(ledColorG[LED_STATUS_IDLE], ledColorR[LED_STATUS_IDLE], ledColorB[LED_STATUS_IDLE])); // LED 消灯
        if(ledStatus == LED_STATUS_READY)    put_pixel(urgb_u32(ledColorG[LED_STATUS_READY], ledColorR[LED_STATUS_READY], ledColorB[LED_STATUS_READY])); // READY 色
        if(ledStatus == LED_STATUS_PON)      put_pixel(urgb_u32(ledColorG[LED_STATUS_PON], ledColorR[LED_STATUS_PON], ledColorB[LED_STATUS_PON]));   // PON 色
        if(ledStatus == LED_STATUS_ACC)      put_pixel(urgb_u32(ledColorG[LED_STATUS_ACC], ledColorR[LED_STATUS_ACC], ledColorB[LED_STATUS_ACC]));   // ACC 色
        ledStatusOld = ledStatus;                        // 前回状態を更新
    }
}



// 七色の基本色を定義
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    char *name;
} COLOR;

const COLOR rainbow_colors[] = {
    {255, 0, 0},      // 赤
    {255, 165, 0},    // オレンジ
    {255, 255, 0},    // 黄
    {0, 255, 0},      // 緑
    {0, 255, 255},    // シアン
    {0, 0, 255},      // 青
    {128, 0, 128}     // 紫
};

#define NUM_COLORS 7
#define COLOR_TRANSITION_STEPS 30  // 各色への遷移ステップ数

// LED色をスムーズに遷移（0～255の値で進行）
void set_led_smooth_color(uint16_t progress) {
    // progressは0～(NUM_COLORS * COLOR_TRANSITION_STEPS - 1)の値
    uint16_t max_progress = NUM_COLORS * COLOR_TRANSITION_STEPS;
    progress = progress % max_progress;
    
    // 現在と次の色のインデックスを計算
    int current_color_idx = progress / COLOR_TRANSITION_STEPS;
    int next_color_idx = (current_color_idx + 1) % NUM_COLORS;
    int transition_step = progress % COLOR_TRANSITION_STEPS;
    
    // 遷移比率（0.0～1.0）
    float transition_ratio = (float)transition_step / COLOR_TRANSITION_STEPS;
    
    // 現在の色と次の色から線形補間
    COLOR current = rainbow_colors[current_color_idx];
    COLOR next = rainbow_colors[next_color_idx];
    
    // RGB値を線形補間
    uint8_t r = (uint8_t)(current.r + (next.r - current.r) * transition_ratio);
    uint8_t g = (uint8_t)(current.g + (next.g - current.g) * transition_ratio);
    uint8_t b = (uint8_t)(current.b + (next.b - current.b) * transition_ratio);
    
    ledColorR[LED_STATUS_PON] = r;
    ledColorG[LED_STATUS_PON] = g;
    ledColorB[LED_STATUS_PON] = b;
    ledTask(true);

}



// アドレス/バッファ検証ユーティリティ（不正時は false を返すよう修正）
bool z80AddressVaild(int address){
    if (address == -1)  return false;            // 未指定は無効
    if (address >= 0x10000) return false;        // 16bit 上限超過は無効
    return true;                                  // 正常
}


uint8_t slotVaild(int slot){
    if (slot == -1)  return defaultSlot;         // 指定無しはデフォルト
    else if ((slot ==  1)|| (slot ==  2))  return (uint8_t)slot; // 有効値は 1/2
    return 0;                                     // 不正値
}

bool bufAddressVaild(int address){
    if (address == -1)  return false;            // 未指定は無効
    if (address >= DATABUF_SIZE) return false;   // 範囲外は無効
    return true;                                  // 正常
}

int slotInit() {
    uint64_t data_mask = 0;
    uint64_t address_mask = 0;
    address_mask = (((uint64_t) 0xffff    << 34) | ((uint64_t) 0x7f    << 25) | ((uint64_t) 0xffff    << 9)); // アドレス用マスク設定
    data_mask    = ((uint64_t) 0xff      << 40); // データ用マスク設定
    gpio_set_dir_in_masked64(data_mask);         // データピンを入力に設定
    gpio_put_masked64(address_mask, address_mask); // アドレスピンを既定値にプット
    return CMD_OK;                               // 初期化成功
}

int slotReadData(uint8_t slot, uint16_t address, uint8_t *data) {
    uint32_t address_mask = 0; // 32bit 用マスク
    uint32_t address_value = 0;
    uint32_t data_mask = 0;
    uint32_t slot_mask = 0;
    uint32_t slot_data = 0;
    uint32_t rd_mask = 0;
    uint32_t mrq_mask = 0;
    uint32_t wr_mask = 0;

    sltAcc = true;                                 // アクセス開始フラグ立てる

    data_mask       = ((uint32_t) 0xff      << 8); // データピンマスク(8bit)
    wr_mask         = ((uint32_t) 1ULL << 5);      // WR 信号マスク
    rd_mask         = ((uint32_t) 1ULL << 6);     // RD 信号マスク
    mrq_mask        = ((uint32_t) 1ULL << 4);     // MRQ 信号マスク

    if (powerCheck() != CMD_OK) return CMD_FAIL;  // 電源異常チェック

    #ifdef PCBVER_B 
   //Rev B
        address_mask    = ((uint32_t) 0xffff    << 9); // アドレスマスク
        address_value   = ((uint32_t) address   << 9); // アドレス値
    #endif

    #ifdef PCBVER_B2 
    //Rev B2
        address_mask    = ((uint32_t) 0xffff    << 8); // アドレスマスク
        address_value   = ((uint32_t) address   << 8); // アドレス値
    #endif

    gpioc_hi_oe_set(wr_mask);                       // WRをPushPullに変更する(BS6101対策)

    gpiolo_put_masked(address_mask, address_value);  // アドレス出力
    gpiohi_put_masked(mrq_mask, 0x0);               // MRQ LOW (アサート)
    gpioc_hi_oe_clr(data_mask);                     // データピンを入力に設定 (OE クリア)

    #ifdef PCBVER_B2 
    // Rev B2-
        slot_mask       = ((uint32_t) 0x3f      << 24); // スロット選択マスク
        slot_data       = ((uint32_t) 0x3f      << 24); // デフォルト値

        if (slot == 1) {
            if(p6_16kbMode){
                if ((address >= 0x6000) && (address < 0x8000))      slot_data = ((uint32_t) 0x37 << 24); // スロット1のバンク切替
                else slot_data = ((uint32_t) 0x3e << 24);

            }else{
                if ((address >= 0x4000) && (address < 0x8000))      slot_data = ((uint32_t) 0x38 << 24); // スロット1のバンク切替
                else if ((address >= 0x8000) && (address < 0xC000)) slot_data = ((uint32_t) 0x34 << 24);
                else slot_data = ((uint32_t) 0x3e << 24);
            }
        } 
    #endif



    #ifdef PCBVER_B 
    //-RevB
        slot_mask       = ((uint32_t) 0x3f      << 25); // スロット選択マスク
        slot_data       = ((uint32_t) 0x3f      << 25); // デフォルト値
        if (slot == 1) {
            if ((address >= 0x4000) && (address < 0x8000))      slot_data = ((uint32_t) 0x3c << 25); // スロット1のバンク切替
            else if ((address >= 0x8000) && (address < 0xC000)) slot_data = ((uint32_t) 0x3a << 25);
            else slot_data = ((uint32_t) 0x3e << 25);
        } 
        if (slot == 2)  {
            if ((address >= 0x4000) && (address < 0x8000))      slot_data = ((uint32_t) 0x27 << 25); // スロット2のバンク切替
            else if ((address >= 0x8000) && (address < 0xC000)) slot_data = ((uint32_t) 0x17 << 25);
            else slot_data = ((uint32_t) 0x37 << 25);
        }
    #endif 
    gpiolo_put_masked(slot_mask, slot_data);          // スロット選択値を出力
    gpiohi_put_masked(rd_mask, 0x0);                // RD LOW (アサート)


//    busy_wait_at_least_cycles(72);                  // タイミング確保 (720ns)
    busy_wait_at_least_cycles(rdWait);                  // タイミング確保 (1000ns)
    uint32_t gpio_hi = gpioc_hi_in_get();           // 高位 GPIO の入力を取得
    gpiohi_put_masked(rd_mask, rd_mask);            // RD を解除 (HIGH)

//  gpiolo_put_masked(slot_mask, slot_mask);          // スロット選択を解除 (トライ状態に戻す)
//  SLOTにDATAのHOLD確保のためコンデンサーが付いている機種があるその対策でPush pull時間を延ばす
    gpioc_lo_out_xor((gpioc_lo_out_get() ^ slot_mask) & slot_mask); // 現在値と XOR → マスク適用で出力変更
    gpiohi_put_masked(mrq_mask, mrq_mask);          // MRQ を解除 (HIGH)

    uint8_t d = 0;
    d = (uint8_t)((gpio_hi >> 8) & 0xff);           // 取得した GPIO の該当ビットを抽出
    *data = d;                                      // 呼び出し側へ返す
//    busy_wait_at_least_cycles(10);                  // タイミング確保 (約561ns)
    
    gpioc_lo_oe_xor((gpioc_lo_oe_get() ^ (~slot_mask)) & slot_mask); // 現在値と XOR → マスク適用で出力変更
    gpiohi_put_masked(wr_mask, wr_mask);            // WR HIGH (BS6101対策)(リリース)

    busy_wait_at_least_cycles(memWait);             // waitタイミング確保

    sltAcc = false;                                 // アクセス終了フラグクリア
    return CMD_OK;
}


int slotWriteData(uint8_t slot, uint16_t address, uint8_t data) {
    uint32_t address_mask = 0;
    uint32_t address_value = 0;
    uint32_t data_mask = 0;
    uint32_t data_data = 0;
    uint32_t slot_mask = 0;
    uint32_t slot_data = 0;
    uint32_t rd_mask = 0;
    uint32_t wr_mask = 0;
    uint32_t mrq_mask = 0;


    sltAcc = true;                                  // アクセス開始フラグ立てる

    data_mask       = ((uint32_t) 0xff      << 8);  // データマスク
    data_data       = ((uint32_t) data      << 8);  // データをシフトして出力用値にする
    wr_mask         = ((uint32_t) 1ULL << 5);      // WR 信号マスク
    rd_mask         = ((uint32_t) 1ULL << 6);     // RD 信号マスク
    mrq_mask        = ((uint32_t) 1ULL << 4);      // MRQ 信号マスク
    if (powerCheck() != CMD_OK) return CMD_FAIL;   // 電源チェック
    #ifdef PCBVER_B 
    //-RevB
        address_mask    = ((uint32_t) 0xffff    << 9); // アドレスマスク
        address_value   = ((uint32_t) address   << 9); // アドレス値
    #endif

    #ifdef PCBVER_B2 
    // Rev B2-
        address_mask    = ((uint32_t) 0xffff    << 8); // アドレスマスク
        address_value   = ((uint32_t) address   << 8); // アドレス値
    #endif
    gpioc_hi_oe_set(rd_mask);                       // RDをPushPullに変更する(BS6101対策)
    gpiolo_put_masked(address_mask, address_value);  // アドレス出力
    gpiohi_put_masked(mrq_mask, 0x0);               // MRQ LOW
    gpioc_hi_oe_set(data_mask);                     // データピンを出力にする (OE 設定)
    gpiohi_put_masked(data_mask, data_data);        // データを高速 IO へ書き込む

    #ifdef PCBVER_B 
    //-RevB
        slot_mask       = ((uint32_t) 0x3f      << 25); // スロットマスク
        slot_data       = ((uint32_t) 0x3f      << 25); // デフォルト値

        if (slot == 1) {
        slot_data = ((uint32_t) 0x3e << 25);       // スロット1 の設定
        } 
        if (slot == 2)  {
            slot_data = ((uint32_t) 0x37 << 25);       // スロット2 の設定
        }
    #endif
    #ifdef PCBVER_B2 
    //-RevB2
        slot_mask       = ((uint32_t) 0x3f      << 24); // スロットマスク
        slot_data       = ((uint32_t) 0x3f      << 24); // デフォルト値

        if (slot == 1) {
            if(p6_16kbMode){
                if ((address >= 0x6000) && (address < 0x8000))      slot_data = ((uint32_t) 0x37 << 24); // スロット1のバンク切替
                else slot_data = ((uint32_t) 0x3e << 24);
            }else{
                slot_data = ((uint32_t) 0x3e << 24);       // スロット1 の設定
            }
        }
    #endif

    gpiolo_put_masked(slot_mask, slot_data);          // スロット選択値を出力
    busy_wait_at_least_cycles(36);                  // タイミング確保 (360ns)

    gpiohi_put_masked(wr_mask, 0x0);                // WR LOW (アサート)
    busy_wait_at_least_cycles(wrWait);                  // タイミング確保 (180ns)
    gpiohi_put_masked(wr_mask, wr_mask);            // WR HIGH (リリース)
    gpiohi_put_masked(rd_mask, rd_mask);            // RD を解除 (HIGH)
    gpiolo_put_masked(slot_mask, slot_mask);          // スロット選択解除
    gpiohi_put_masked(mrq_mask, mrq_mask);          // MRQ HIGH (リリース)
    gpioc_hi_oe_clr(data_mask);                     // データピンを入力に戻す
    gpiohi_put_masked(rd_mask, rd_mask);            // RD を解除(BS6101対策) (HIGH)
    busy_wait_at_least_cycles(memWait);             // waitタイミング確保

    sltAcc = false;                                 // アクセス終了フラグクリア
    return CMD_OK;
}

int slotReadIO(uint16_t address, uint8_t *data) {
    uint32_t address_mask = 0;
    uint32_t address_value = 0;
    uint32_t data_mask = 0;
    uint32_t rd_mask = 0;
    uint32_t io_mask = 0;

    sltAcc = true;                                 // アクセス開始フラグ
    data_mask       = ((uint32_t) 0xff      << 8); // データマスク
    rd_mask         = ((uint32_t) 1ULL << 6);     // RD マスク
    io_mask         = ((uint32_t) 1ULL << 3);     // IO マスク

    if (powerCheck() != CMD_OK) return CMD_FAIL;  // 電源チェック

    #ifdef PCBVER_B 
    //-RevB
        address_mask    = ((uint32_t) 0xffff    << 9); // アドレスマスク
        address_value   = ((uint32_t) address   << 9); // アドレス値
    #endif

    #ifdef PCBVER_B2 
    // Rev B2-
        address_mask    = ((uint32_t) 0xffff    << 8); // アドレスマスク
        address_value   = ((uint32_t) address   << 8); // アドレス値
    #endif
    gpiolo_put_masked(address_mask, address_value);  // アドレス出力
    gpiohi_put_masked(io_mask, 0x0);                // IO LOW (アサート)
    gpioc_hi_oe_clr(data_mask);                     // データピンを入力に切替

    gpiohi_put_masked(rd_mask, 0x0);                // RD LOW
    busy_wait_at_least_cycles(72);                  // 待ち時間
    uint32_t gpio_hi = gpioc_hi_in_get();           // GPIO 入力読み取り
    gpiohi_put_masked(rd_mask, rd_mask);            // RD リリース
    gpiohi_put_masked(io_mask, io_mask);            // IO リリース

    uint8_t d = 0;
    d = (uint8_t)((gpio_hi >> 8) & 0xff);           // データ抽出
    *data = d;                                      // 出力
    sltAcc = false;                                 // アクセス終了
    return CMD_OK;
}

int slotWriteIO(uint16_t address, uint8_t data) {
    uint32_t address_mask = 0;
    uint32_t address_value = 0;
    uint32_t data_mask = 0;
    uint32_t data_data = 0;
    uint32_t wr_mask = 0;
    uint32_t io_mask = 0;

    sltAcc = true;                                  // アクセス開始
    data_mask       = ((uint32_t) 0xff      << 8);  // データマスク
    data_data       = ((uint32_t) data      << 8);  // 出力データ
    wr_mask         = ((uint32_t) 1ULL << 5);      // WR マスク
    io_mask         = ((uint32_t) 1ULL << 3);      // IO マスク

    if (powerCheck() != CMD_OK) return CMD_FAIL;   // 電源チェック
    #ifdef PCBVER_B 
    //-RevB
        address_mask    = ((uint32_t) 0xffff    << 9); // アドレスマスク
        address_value   = ((uint32_t) address   << 9); // アドレス値
    #endif

    #ifdef PCBVER_B2 
    // Rev B2-
        address_mask    = ((uint32_t) 0xffff    << 8); // アドレスマスク
        address_value   = ((uint32_t) address   << 8); // アドレス値
    #endif

    gpiolo_put_masked(address_mask, address_value);  // アドレス出力
    gpiohi_put_masked(io_mask, 0x0);                // IO LOW
    gpioc_hi_oe_set(data_mask);                     // データピン出力
    gpiohi_put_masked(data_mask, data_data);        // データ書込

    busy_wait_at_least_cycles(36);                  // タイミング
    gpiohi_put_masked(wr_mask, 0x0);                // WR LOW
    busy_wait_at_least_cycles(18);                  // タイミング
    gpiohi_put_masked(wr_mask, wr_mask);            // WR HIGHsm
    gpiohi_put_masked(io_mask, io_mask);            // IO リリース
    gpioc_hi_oe_clr(data_mask);                     // データピン入力に戻す

    sltAcc = false;                                 // アクセス終了
    return CMD_OK;
}


// GPIOテストシーケンス（すべてのテスト処理を1つの関数にまとめ）
int gpio_test_sequence(void) {
    cdc_printf("\n========================================\n");
    cdc_printf("Factory GPIO Test Started\n");
    cdc_printf("========================================\n\n");

    sltPower = true;                                       // 電源 ON フラグ
    SlotPowerON();
    busy_wait_ms(5);

    // ステップ1: GPIO7-47をすべて入力に設定
    cdc_printf("Switch GPIO7-47 to input mode...\n");
    for (uint pin = 7; pin <= 47; pin++) {
        // GPIO28-30は除外
        if (pin >= 28 && pin <= 30) {
            continue;
        }
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
    }
    cdc_printf("Complete\n\n");
    busy_wait_ms(10);
    
    // GPIO組み合わせごとにテスト実行
    for (int i = 0; i < NUM_GPIO_PAIRS; i++) {
        uint pin_out = gpio_pairs[i].gpio_out;
        uint pin_check = gpio_pairs[i].gpio_check;
        
        cdc_printf("========================================\n");
        cdc_printf("Test %d/%d: GPIO%d-GPIO%d\n", i + 1, NUM_GPIO_PAIRS, pin_out, pin_check);
        cdc_printf("========================================\n");
        
        // ステップ2: GPIO7-47がすべて1であることを確認
        cdc_printf("Verify all GPIO7-47 are high (1)...\n");
        bool all_high = true;
        for (uint pin = 7; pin <= 47; pin++) {
            if (pin >= 28 && pin <= 30) {
                continue;
            }
            if (!gpio_get(pin)) {
                cdc_printf("Error: GPIO%d = 0 (Expected: 1)\n", pin);
                all_high = false;
                break;
            }
        }
        
        if (!all_high) {
            cdc_printf("Test %d Failed: Step 2\n\n", i + 1);
            return CMD_FAIL;
        }
        cdc_printf("Complete: All GPIO are high\n");
        busy_wait_ms(50);
        
        // ステップ3: GPIO出力を0に設定
        cdc_printf("Switch GPIO%d to output mode and set to 0...\n", pin_out);
        gpio_init(pin_out);
        gpio_set_dir(pin_out, GPIO_OUT);
        gpio_put(pin_out, 0);
        busy_wait_ms(5);
        
        if (gpio_get(pin_out) != 0) {
            cdc_printf("Error: GPIO%d output value is not 0\n", pin_out);
            cdc_printf("Test %d Failed: Step 3\n\n", i + 1);
            return CMD_FAIL;
        }
        cdc_printf("GPIO%d = 0 Set Complete\n", pin_out);
//        busy_wait_ms(50);
        
        // ステップ4: GPIO checkが0であることを確認、それ以外が1であることを確認
        cdc_printf("Verify GPIO%d is low (0)...\n", pin_check);
        if (gpio_get(pin_check) != 0) {
            cdc_printf("Error: GPIO%d Expected: 0 (Actual: 1)\n", pin_check);
            cdc_printf("Test %d Failed: Step 4-1\n\n", i + 1);
            return CMD_FAIL;
        }
        cdc_printf("GPIO%d = 0 Verified\n", pin_check);
        
        cdc_printf("Verify GPIO%d, GPIO%d and others are high (1)...\n", pin_out, pin_check);
        bool other_all_high = true;
        for (uint pin = 7; pin <= 47; pin++) {
            if (pin >= 28 && pin <= 30) {
                continue;
            }
            if (pin == pin_out || pin == pin_check) {
                continue;
            }
            if (!gpio_get(pin)) {
                cdc_printf("[STEP 4] Error: GPIO%d = 0 (Expected: 1)\n", pin);
                other_all_high = false;
                break;
            }
        }
        
        if (!other_all_high) {
            cdc_printf("Test %d Failed: Step 4-2\n\n", i + 1);
            return CMD_FAIL;;
        }
        cdc_printf("GPIO%d, GPIO%d and others all high verified\n", pin_out, pin_check);
//      busy_wait_ms(50);
        
        // ステップ5: GPIO出力を入力に切り替え
        cdc_printf("Switch GPIO%d to input mode...\n", pin_out);
        gpio_init(pin_out);
        gpio_set_dir(pin_out, GPIO_IN);
        cdc_printf("Complete\n");
        cdc_printf("Test %d: Success\n\n", i + 1);
//        busy_wait_ms(100);
    }
    
    // ステップ6: LED & Current Sensor Check
    cdc_printf("========================================\n");
    cdc_printf("LED & Current Sensor Check\n");
    cdc_printf("-- Push OverCurrent SW--\n");
    cdc_printf("========================================\n");
    
    uint16_t color_progress = 0;

    // ステップ7: overCurrent == true かつ sltPower == false まで待機
    cdc_printf("Changing LED to rainbow colors, waiting for check...\n");
    while (!(overCurrent && !sltPower)) {
        set_led_smooth_color(color_progress);
        color_progress++;
        busy_wait_ms(2);
        
        // タイムアウト防止（シミュレーション用）
        if (color_progress > 40000) {
            cdc_printf("Timeout: Condition not met\n");
            return CMD_FAIL;
        }
    }
    cdc_printf("Complete\n");
    
    // ステップ8: テスト終了を表示
    cdc_printf("\n========================================\n");
    cdc_printf("ALL Test Finished (PASS) \n");
    cdc_printf("========================================\n");

    SlotPowerOFF();
    sltPower = false;

    return CMD_OK;
}
// Factory Test               	FTST	cmd_readerFactoryTest      	FTST                                            	戻値: OK                      	Slotの電源を入れて、状態のセルフチェック,各信号の有効化,RESETをします。
int cmd_readerFactoryTest(const Command_t* cmd) {
    return gpio_test_sequence();
}

// Slot Power ON               	SPON	cmd_slotPowerOn      	SPON                                            	戻値: OK                      	Slotの電源を入れて、状態のセルフチェック,各信号の有効化,RESETをします。
int cmd_slotPowerOn(const Command_t* cmd) {
    uint64_t data_mask = 0;
    uint64_t address_mask = 0;
    overCurrent = false;   
    gpio_set_oeover(39, DIR_OUTPUT);
    gpio_put(39, 0);        //SLT_RESET LOW                         // RESET を LOW にする
//    gpio_put(2, 0);         //CH217K EN# LOW                        // 電源制御 EN を Low に (ON)
    SlotPowerON();
    busy_wait_ms(100);                                              // 電源立ち上がり/安定待ち
    #if HWINFO_CURRENTSENSOR == 1                                     
    if (gpio_get(3) == 0){      // Over Current Check                // PWR_FLG チェック
//        SlotPowerOFF();

//        gpio_put(2, ~HWINFO_PWREN);         //CH217K EN# LOW                        // 電源制御 EN を Low に (ON)
//        gpio_put(2, 1);                                     // 異常時は EN を HIGH にして電源 OFF
        cdc_printf("Power ON Fail...\n");                      // 通知
        return CMD_FAIL;        
    };
    #endif
    #ifdef PCBVER_B 
    //-RevB
        address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x7f    << 25) | ((uint64_t) 0xffff    << 9)); // アドレスマスク
    #endif
    #ifdef PCBVER_B2 
    // Rev B2-
       address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x9f    << 24) | ((uint64_t) 0xffff    << 8)); // アドレスマスク
    #endif    
    data_mask    = ((uint64_t) 0xff      << 40);           // データマスク

    gpio_set_dir_in_masked64(data_mask);                   // データピン入力
    gpiolo_put_masked((uint32_t)address_mask,(uint32_t)address_mask);
    gpiohi_put_masked((uint32_t)(address_mask >> 32u),(uint32_t)(address_mask >> 32u));

//    gpio_put_masked64(address_mask, address_mask);         // アドレスピン設定
    pio_sm_set_enabled(pio, sm_slotclk, true);             //Clock enable
    busy_wait_ms(10);                                      // 少し待つ
    gpio_put(39, 1);        //SLT_RESET High                        // RESET リリース
    gpio_set_oeover(39, DIR_INPUT);
    busy_wait_ms(5);
    sltPower = true;                                       // 電源 ON フラグ
    return CMD_OK;        
}

// Slot Reset                  	SRST	cmd_slotReset       	SRST                                            	戻値: OK                      	SlotのReset
int cmd_slotReset(const Command_t* cmd) {
    gpio_set_oeover(39, DIR_OUTPUT);
    gpio_put(39, 0);        //SLT_RESET LOW                         // RESET LOW
    busy_wait_ms(10);                                      // 10ms ウェイト
    gpio_put(39, 1);        //SLT_RESET High                        // RESET HIGH
    gpio_set_oeover(39, DIR_INPUT);
    return CMD_OK;        
}

// Slot Power OFF              	SPOFF	cmd_slotPowerOff    	SPOFF                                           	戻値: OK                      	Slotの電源をOFFにします。
int cmd_slotPowerOff(const Command_t* cmd) {
    uint64_t data_mask = 0;
    uint64_t address_mask = 0;

    data_mask    = ((uint64_t) 0xff      << 40);           // データマスク
    #ifdef PCBVER_B 
    //-RevB
        address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x7f    << 25) | ((uint64_t) 0xffff    << 9)); // アドレスマスク
    #endif
    #ifdef PCBVER_B2 
    // Rev B2-
       address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x9f    << 24) | ((uint64_t) 0xffff    << 8)); // アドレスマスク
    #endif
    gpio_set_dir_in_masked64(data_mask);                   // データピン入力
    gpiolo_put_masked((uint32_t)address_mask,(uint32_t)0x0);
    gpiohi_put_masked((uint32_t)(address_mask >> 32u),(uint32_t)(0x0));

//    gpio_put_masked64(address_mask, address_mask);         // アドレス設定
    pio_sm_set_enabled(pio, sm_slotclk, false);             //Clock disable
    gpio_put(CLOCK_PIN, 0);  // GPIO7 を LOW に設定
    SlotPowerOFF();
//    gpio_put(2, 1);         //CH217K EN# HIGH                    // 電源 OFF
    busy_wait_ms(100);                                     // DisCharge                                      
    gpio_put_masked64(address_mask, 0);                    // アドレスリセット
    sltPower = false;                                      // 電源フラグをクリア

    return CMD_OK;        
}
int cmd_loopScript(const Command_t* cmd)
{
    // パラメータ
    if (cmd->arg_val[0] == -1)  cmpLoopMax = CMP_LOOP;
    else                        cmpLoopMax = cmd->arg_val[0];
    return CMD_OK;         
};

// GPIO 割り込みハンドラ
void gpio_callback(uint gpio, uint32_t events) {
    uint64_t data_mask = 0;
    uint64_t address_mask = 0;

    if (gpio == 3 && (events & GPIO_IRQ_EDGE_FALL)) { // PWR_FLG のフォールで過電流と判断
        overCurrent = true;                            // フラグを立てる
        sltPower = false;                              // スロット電源フラグを 0 にする

        data_mask    = ((uint64_t) 0xff      << 40);   // データマスク
    #ifdef PCBVER_B 
    //-RevB
        address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x7f    << 25) | ((uint64_t) 0xffff    << 9)); // アドレスマスク
    #endif
    #ifdef PCBVER_B2 
    // Rev B2-
       address_mask = (((uint64_t) 0xff7f    << 34) | ((uint64_t) 0x9f    << 24) | ((uint64_t) 0xffff    << 8)); // アドレスマスク
    #endif
        gpio_set_dir_in_masked64(data_mask);           // データピン入力
        pio_sm_set_enabled(pio, sm_slotclk, false);             //Clock disable
        SlotPowerOFF();
        gpio_put(CLOCK_PIN, 0);  // GPIO7 を LOW に設定

        busy_wait_ms(1);
        gpio_put_masked64(address_mask, 0);            // アドレスリセット
        sltPower = false;

        // スイッチが押されたときにLEDの状態をトグル(立ち上がりエッジ)
        printf("Over Current!!\n");                    // デバッグ出力
    }
}

// ========================================
// スクリプト実行エンジン
// ========================================

/*
 * スクリプト形式:
 *  [Command],[Address(2Byte)],[DATA]...
 *  4バイト単位：[命令][上位Addr][下位Addr][Data]
 *
 * コマンド一覧:
 *  0x00: NOP        - 何もしない
 *  0x01: Read Mem   - slotReadData実行
 *  0x02: Write Mem  - slotWriteData実行
 *  0x03: Read IO    - slotReadIO実行
 *  0x04: Write IO   - slotWriteIO実行
 *  0x05: Wait       - addressで指定したms時間待機
 *  0x06: Compare    - lastDataとdataを比較→一致なら次の命令をスキップ
 *  0x07: AND        - lastData & dataを計算→Zeroなら次の命令をスキップ
 *  0x08: OR         - lastData | dataを計算→Zeroなら次の命令をスキップ
 *  0x09: XOR        - lastData ^ dataを計算→Zeroなら次の命令をスキップ
 *  0x0A: JMP        - data値だけ命令をスキップ（0x00-7F=先/0x80-FF=前）
 *  0x0B: PUSH       - lastDataをBufferのaddressに指定された場所に書き込みます。
 *  0xFE: Abort      - スクリプト実行を失敗で終了
 *  0xFF: End        - スクリプト実行を成功で終了
 */

int executeCommands(uint8_t *slotMem, size_t bufSize, uint16_t startAddress,uint8_t slot) {
    execState.instructionPC = 0;
    execState.isWaiting = 0;
    execState.waitEndTime = 0;

    lastError = 0;
    
    // グローバル統計変数をリセット
    passCount = 0;
    errCount = 0;
    
    uint16_t i = startAddress;
    uint16_t loopCount = 0;
    
    while (i < bufSize) {
        uint8_t command = slotMem[i];
        uint16_t address = 0;
        uint8_t data = 0;
        
        // フォーマット: [命令コード][アドレス上位][アドレス下位][データ]
        if (i + 3 < bufSize) {
            address = ((uint16_t)slotMem[i + 1] << 8) | slotMem[i + 2];
            data = slotMem[i + 3];
        }
        
        uint8_t commandCode = command;
        
        int cmdResult = CMD_OK;
        uint8_t readData = 0;
        
        switch (commandCode) {
            case 0x00:  // NOP
                // なにもしない
                if (comdbgFlag) printf("NOP\n");
                break;
                
            case 0x01:  // Read Memory
                 cmdResult = slotReadData(slot, address, &readData);
                if (cmdResult == CMD_OK) {
                    if (comdbgFlag) printf("%d: Read Memory %x:%x\n",i,address, readData);
                    execState.lastData = readData;
                }
                break;
                
            case 0x02:  // Write Memory
                cmdResult = slotWriteData(slot, address, data);
                if (cmdResult == CMD_OK) {
                    if (comdbgFlag) printf("%d: Write Memory %x:%x\n",i,address, data);
                    execState.lastData = data;
                }
                break;
                
            case 0x03:  // Read IO
                cmdResult = slotReadIO(address, &readData);
                if (cmdResult == CMD_OK) {
                    if (comdbgFlag) printf("%d: Read IO %x:%x\n",i,address, readData);
                    execState.lastData = readData;
                }
                break;
                
            case 0x04:  // Write IO
                cmdResult = slotWriteIO(address, data);
                if (cmdResult == CMD_OK) {
                    if (comdbgFlag) printf("%d: Write IO %x:%x\n",i,address, data);
                    execState.lastData = data;
                }
                break;
                
            case 0x05:  // Wait
                if (comdbgFlag) printf("Wait\n");

                if (!execState.isWaiting) {
                    // Wait開始、addressをマイクロ秒として使用
                    execState.waitEndTime = time_us_32() + address;
                    execState.isWaiting = 1;
                }
                
                // Wait終了時刻に達するまでループ
                while (time_us_32() < execState.waitEndTime) {
                    // ビジーウェイト
                }
                execState.isWaiting = 0;
                break;
                
            case 0x06:  // Compare
                if (comdbgFlag) printf("%d: Compare %x=%x\n",i,execState.lastData,data);

                loopCount++;
                if (execState.lastData == data) {
                    loopCount = 0;
                    i += 4;  // 次の命令をスキップ
                }
                if (loopCount == cmpLoopMax){
                    if (comdbgFlag) printf("Loop Out Error\n");
                    lastError = ERR_LOOP_OUT;
                    errCount = execState.instructionPC;
                    return CMD_FAIL;
                }
                break;
                
            case 0x07:  // AND
                if (comdbgFlag) printf("%d: AND %x=%x\n",i,execState.lastData,data);

                loopCount++;
                if ((execState.lastData & data) == 0) {
                    loopCount = 0;
                    i += 4;  // 次の命令をスキップ
                }
                if (loopCount == cmpLoopMax){
                    if (comdbgFlag) printf("Loop Out Error\n");
                    lastError = ERR_LOOP_OUT;
                    errCount = execState.instructionPC;
                    return CMD_FAIL;
                }
                break;
                
            case 0x08:  // OR
                if (comdbgFlag) printf("%d: OR %x=%x\n",i,execState.lastData,data);

                loopCount++;
                if ((execState.lastData | data) == 0) {
                    loopCount = 0;
                    i += 4;  // 次の命令をスキップ
                }
                if (loopCount == cmpLoopMax){
                    if (comdbgFlag) printf("Loop Out Error\n");
                    lastError = ERR_LOOP_OUT;
                    errCount = execState.instructionPC;
                    return CMD_FAIL;
                }
                break;
                
            case 0x09:  // XOR
                if (comdbgFlag) printf("%d: XOR %x=%x\n",i,execState.lastData,data);

                loopCount++;
                if ((execState.lastData ^ data) == 0) {
                    loopCount = 0;
                    i += 4;  // 次の命令をスキップ
                }
                if (loopCount == cmpLoopMax){
                    if (comdbgFlag) printf("Loop Out Error\n");
                    lastError = ERR_LOOP_OUT;
                    errCount = execState.instructionPC;
                    return CMD_FAIL;
                }
                break;
                
            case 0x0A:  // JMP
                if (comdbgFlag) printf("%d: JMP\n",i);

                if (data < 0x80) {
                    i += (data * 4);  // 先の命令へジャンプ
                } else {
                    int jumpBack = ((int)data - 256) * 4;
                    if (i + jumpBack < 0) {
                        if (comdbgFlag) printf("Out of Bounds\n");
                        lastError = ERR_OUT_OF_BOUNDS;
                        errCount = execState.instructionPC;
                        return CMD_FAIL;
                    }
                    i += jumpBack;  // 前の命令へジャンプ
                }
                i -= 4;  // ループ終了時の i += 4 と相殺
                break;

            case 0x0B:  // PUSH
                if (comdbgFlag) printf("%d: PUSH  %x=%x\n",i,address,execState.lastData);
                slotMem[address] = execState.lastData;

                break;

            case 0xFE:  // Abort Script(Fail)
                if (comdbgFlag) printf("Abort Script(Fail)\n");
                passCount = execState.instructionPC;
                return CMD_FAIL;

            case 0xFF:  // End of Data
                if (comdbgFlag) printf("End Script(PASS)\n");
                passCount = execState.instructionPC;
                return CMD_OK;
                
            default:
                if (comdbgFlag) printf("Script Error\n");
                lastError = ERR_INVALID_COMMAND;
                errCount = execState.instructionPC;
                return CMD_FAIL;
        }
        
        // 関数実行エラーチェック
        if (cmdResult != CMD_OK) {
            lastError = ERR_FUNCTION_FAILED;
            errCount = execState.instructionPC;
            return CMD_FAIL;
        }
        
        i += 4;  // 次の命令へ
        execState.instructionPC++;
    }
    
    passCount = execState.instructionPC;
    return CMD_OK;
}


void init_all_pins(void) {
    uint64_t out_mask = 0;
    uint64_t in_mask = 0;
    for (int i = 0; i < NUM_PINS; i++) {
        gpio_init(board_pins[i].gpio_num);           // GPIO 初期化 // ports.c の配列を使用
        if(board_pins[i].dir == DIR_OUTPUT) {
            out_mask |= (1ULL << board_pins[i].gpio_num); // 出力マスクに追加
            gpio_set_pulls(board_pins[i].gpio_num,false,false); // プル無し
            gpio_set_drive_strength (board_pins[i].gpio_num,GPIO_DRIVE_STRENGTH_4MA); // ドライブ強度設定
        } else {
            in_mask |= (1ULL << board_pins[i].gpio_num);  // 入力マスクに追加
        }
    }
    gpio_set_dir_out_masked64(out_mask);           // output一括設定
    gpio_set_dir_in_masked64(in_mask);             // input一括設定
    gpio_set_pulls(4,true,false);                  //SW1 PULLUP
    gpio_set_pulls(5,true,false);                  //SW2 PULLUP

    gpio_put_masked64(out_mask,0x81);              // output 一括初期化（具体値はボード依存）
    SlotPowerOFF();         //CH217K EN# High       // 電源制御デフォルト
//    gpio_put(2, 0);         //CH217K EN# High       // 電源制御デフォルト
//    gpio_put(2, 1);         //CH217K EN# High       // 電源制御デフォルト
    gpio_put(7, 1);         //PSG EN                // PSG 有効化
#if HWINFO_CURRENTSENSOR == 1
    gpio_set_irq_enabled_with_callback(3, GPIO_IRQ_EDGE_FALL, true, &gpio_callback); // PWR_FLG の割り込み登録
#endif
}

int is_hex_digit(char c) {
    return
        (c >= '0' && c <= '9') ||
        (c >= 'A' && c <= 'F') ||
        (c >= 'a' && c <= 'f');
}
// 16進文字列チェック＆変換
// s: 入力文字列
// *val: 変換結果出力（成功時のみ）
// 戻り値: 1=成功, 0=不正（桁不正, "G"～など）
int parse_hex_string(const char *s, int *val) {
    if (s == NULL || *s == '\0') {                 // NULL または空文字列のとき
        *val = 0;                                   // 0 を返す (既定)
        return 1;                                   // 成功扱い
    }
    size_t start = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { // 0x プレフィックス対応
        start = 2;
        if (s[2] == '\0') return 0;                 // "0x" のみは不正
    }
    for (size_t i = start; s[i]; ++i) {             // 文字ごとに 16 進判定
        if (!is_hex_digit(s[i])) return 0;          // 不正文字があれば失敗
    }
    int v = 0;
    if (sscanf(s, "%x", &v) == 1) {                 // 16 進で読み込み
        *val = v;                                   // 結果格納
        return 1;                                   // 成功
    }
    return 0;                                       // それ以外は失敗
}

int cmd_hwVersion(const Command_t* cmd) {
   cdc_printf("HARDWARE NAME : %s\n",HW_NAME);      // ハード名/バージョン出力
   cdc_printf("HARDWARE VER : %s\n",HW_VERSION);      // ハード名/バージョン出力
   cdc_printf("FIRMWARE VER : %s\n",PROJECT_VERSION);     // Firmware Version
   return CMD_OK;                                  // 成功
}
// HW information              	HINF	cmd_hwInfomation    	HINF                                            	戻値: Hardware 情報   + OK    	Hardware Information
int cmd_hwInfomation(const Command_t* cmd) {
   cdc_printf("SLOTNUM,%d\n",HWINFO_SLOTNUM);      // スロット数出力
   cdc_printf("SLOTPHY,%d\n",HWINFO_SLOTPHY);      // 基本スロットの物理割り付け情報
   cdc_printf("POWERCTRL,%d\n",HWINFO_POWERCTRL);  // 電源制御有無出力
   cdc_printf("CURRENTSENSOR,%d\n",HWINFO_CURRENTSENSOR); // 電流センサ有無
   cdc_printf("PWR12V,%d\n",HWINFO_PWR12V);        // 12V 電源有無
   cdc_printf("SLOTCLOCK,%d\n",HWINFO_SLOTCLOCK);  // スロットクロック供給有無
   cdc_printf("LINEOUT,%d\n",HWINFO_LINEOUT);      // LineOut 有無
   cdc_printf("PSGUNIT,%d\n",HWINFO_PSGUNIT);      // PSG ユニット有無
   cdc_printf("LFCR,%d\n",crlfFlag);               // CRLF フラグ出力
   cdc_printf("COMDBG,%d\n",comdbgFlag);           // シリアルポートに対するデバッグ出力フラグ
   cdc_printf("SCRLOOP,%d\n",cmpLoopMax);          // スクリプトのLOOP回数
   cdc_printf("MEMWAIT,%d\n",memWait);          // Memory アクセスWait CMDtoCMD
   cdc_printf("RDWAIT,%d\n",rdWait);          // Memory アクセスWait Read
   cdc_printf("WRWAIT,%d\n",wrWait);          // Memory アクセスWait Write
   cdc_printf("P6MODDE,%d\n",p6_16kbMode);          // PC6001 mode flag
   return CMD_OK;
}

int null_format(const Command_t* cmd) {
    cdc_printf("NULL [%s] [%s] [%x][%x][%x][%x]\n", cmd->cmd,cmd->arg, cmd->arg_val[0],cmd->arg_val[1],cmd->arg_val[2],cmd->arg_val[3]); // デバッグ出力
    return 0;
}

int cmd_hset(const Command_t* cmd) {

    switch (cmd->arg_val[0]){
        case HSET_ADDRESS_MEMWAIT:
            if (cmd->arg_val[1] == -1) memWait = HSET_DEFNUM_MEMWAIT;
            else memWait = cmd->arg_val[1];
            return CMD_OK;
            break;

        case HSET_ADDRESS_RDWAIT:
            if (cmd->arg_val[1] == -1) rdWait = HSET_DEFNUM_RDWAIT;
            else rdWait = cmd->arg_val[1];
            return CMD_OK;
            break;

        case HSET_ADDRESS_WRWAIT:
            if (cmd->arg_val[1] == -1) wrWait = HSET_DEFNUM_WRWAIT;
            else wrWait = cmd->arg_val[1];
            return CMD_OK;
            break;

        case HSET_ADDRESS_P6MODE:
            if (cmd->arg_val[1] == -1) p6_16kbMode = HSET_DEFNUM_P6MODE;
            else if (cmd->arg_val[1] >= 1) p6_16kbMode = true;
            else p6_16kbMode = false;
            return CMD_OK;
            break;
    }
    return CMD_FAIL;
}

// HW Status                   	HSTS	cmd_hwIntenalStatus 	HSTS                                            	戻値: 内部STATUS  + OK        	SlotのCMD実行状態を表示
int cmd_hwIntenalStatus(const Command_t* cmd) {
   cdc_printf("%d\n",count-1);                     // キュー残数表示 (自明ではないので注意)
   if (errCount != 0) return CMD_FAIL;             // エラーありなら FAIL を返す
   return CMD_OK;
}

// BUFFER Clear                	BCLR	cmd_bufClear        	BCLR(, [Buffer Address],[Length],[Data])          	戻値: OK                      	Buffer RAMのClear
int cmd_bufClear(const Command_t* cmd) {
    int startAddress;
    int length;
    int fillData;
    int i;

    // パラメータ
    if (cmd->arg_val[0] == -1)  startAddress = 0;   // 第1パラが無ければ先頭
    else                        startAddress = cmd->arg_val[0]; // 指定があれば使用
    if (startAddress >= DATABUF_SIZE) return CMD_FAIL; // 範囲チェック

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE; // length 指定なし => 全域
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  fillData = 0xff; // fill デフォルト 0xff
    else                        fillData = cmd->arg_val[2];

    if (startAddress + length >= DATABUF_SIZE)  length = length - startAddress; // 範囲超過時補正
 
    for (i = startAddress ; i < (startAddress + length) ; i++){
        slotMem[i] = (uint8_t)fillData;          // 指定値で埋める
    }
    return CMD_OK;
}

// BUFFER Dump                 	BDMP	cmd_bufDump         	BDMP,[Buffer Address]                           	戻値: 128Byte DUMP値 +OK      	Bufferのデータ内容の表示(DEBUG用)
int cmd_bufScript(const Command_t* cmd) {
    int startAddress;
    int length;
    int i;
    uint8_t slot;

    // パラメータ
    if (cmd->arg_val[0] == -1)  startAddress = 0; // デフォルト 0
    else                        startAddress = cmd->arg_val[0];

    slot = slotVaild(cmd->arg_val[1]);
    if (slot == 0) {
        return CMD_FAIL;
    }


    return executeCommands(slotMem, sizeof(slotMem)-startAddress,startAddress,slot);
}


// BUFFER Dump                 	BDMP	cmd_bufDump         	BDMP,[Buffer Address]                           	戻値: 128Byte DUMP値 +OK      	Bufferのデータ内容の表示(DEBUG用)
int cmd_bufDump(const Command_t* cmd) {
    int startAddress;
    int length;
    int i;

    // パラメータ
    if (cmd->arg_val[0] == -1)  startAddress = 0; // デフォルト 0
    else                        startAddress = cmd->arg_val[0];
    if (cmd->arg_val[1] == -1)  length = 128;    // デフォルト 128 bytes
    else                        length = cmd->arg_val[1];

    for (size_t i = startAddress; i < (startAddress + length); i += 16) {
        if (i < DATABUF_SIZE) cdc_printf("%08X  ", (unsigned int)(i)); // アドレス表示
        for (size_t j = 0; j < 16; j++) {
            if (i + j < (startAddress + length)) {
                if((i + j) < DATABUF_SIZE) cdc_printf("%02X ", slotMem[i + j]); // 16進表示
            } else {
                cdc_printf("   "); // 埋めスペース
            }
        }
        cdc_printf(" ");
        for (size_t j = 0; j < 16; j++) {
            if ((i + j) < (startAddress + length)) {
                if((i + j) < DATABUF_SIZE) {
                    uint8_t c = slotMem[i + j];
                    cdc_printf("%c", (c >= 0x20 && c <= 0x7E) ? c : '.'); // ASCII 表示（印字可能か）
                }
            } else {
                cdc_printf(" ");
            }
        }
        if (i < DATABUF_SIZE) cdc_printf("\n"); // 行末改行
    }
    return CMD_OK;
}

// Slot Select                 	SSEL	cmd_slotSelect      	SSEL,[Slot]                                     	戻値: なし                    	Slot選択
int cmd_slotSelect(const Command_t* cmd) {
    if (cmd->arg_val[0] == -1)  return CMD_FAIL;  // 引数必須
    if ((cmd->arg_val[0] ==  1)|| (cmd->arg_val[0] ==  2))  {
        defaultSlot = (uint8_t)cmd->arg_val[0];   // デフォルトスロットを設定
        return CMD_OK;
    }else{
        return CMD_FAIL;
    }
}

// Slot Mem Write               	SMWR	cmd_slotWriteMem     	SMWR,[Address](,[Data],[Slot])                         	戻値: Read値（ASCII16進) +OK  	Slotから1Byte Readします
int cmd_slotWriteMem(const Command_t* cmd) {
    uint16_t address;
    uint8_t slot;
    uint8_t data;

    if (!z80AddressVaild(cmd->arg_val[0]))  return CMD_FAIL; // アドレス判定
    address = (uint16_t)cmd->arg_val[0];

    if (cmd->arg_val[1] == -1)  return CMD_FAIL;            // データ必須
    data = (uint8_t) cmd->arg_val[1];

    slot = slotVaild(cmd->arg_val[2]);
    if (slot == 0) return CMD_FAIL;                         // スロット不正
    if (displayFlag == true) cdc_printf("%04x : %02x\n",address,data);               // デバッグ出力
    if (slotWriteData(slot, address, data) != CMD_OK) return CMD_FAIL; // 実書き込み
    return CMD_OK;
}

// Slot Mem Read               	SMRD	cmd_slotReadMem     	SMRD,[Address](,[Slot])                         	戻値: Read値（ASCII16進) +OK  	Slotから1Byte Readします
int cmd_slotReadMem(const Command_t* cmd) {
    uint16_t address;
    uint8_t slot;
    uint8_t data;

    if (!z80AddressVaild(cmd->arg_val[0]))  return CMD_FAIL; // アドレス判定
    address = (uint16_t)cmd->arg_val[0];

    slot = slotVaild(cmd->arg_val[1]);                     // スロット判定
    if (slot == 0) return CMD_FAIL;

    if (slotReadData(slot, address, &data) != CMD_OK) return CMD_FAIL; // 実読出し
    cdc_printf("%04x : %02x\n",address,data);               // 出力
    return CMD_OK;
}

// BUFFER Dump (スロット直接読み出し)
int cmd_slotDump(const Command_t* cmd) {
    int startAddress;
    int length;
    int slot;
    uint8_t data;

    // パラメータ
    if (cmd->arg_val[0] == -1)  startAddress = 0;
    else                        startAddress = cmd->arg_val[0];

    if (!z80AddressVaild(startAddress))  return CMD_FAIL;   // アドレス検査

    if (cmd->arg_val[1] == -1)  length = 128;
    else                        length = cmd->arg_val[1];

    slot = slotVaild(cmd->arg_val[2]);                      // スロット取得

    for (size_t i = startAddress; i < (startAddress + length); i += 16) {
        if (i < DATABUF_SIZE) cdc_printf("%08X  ", (unsigned int)(i)); // 行先頭にアドレス出力
        for (size_t j = 0; j < 16; j++) {
            if (i + j < (startAddress + length)) {
                if (slotReadData(slot, (i+j), &data) != CMD_OK) return CMD_FAIL; // 1byte 読み出し
                if((i + j) < DATABUF_SIZE) cdc_printf("%02X ", data);            // 16 進表示
            } else {
                cdc_printf("   ");
            }
        }
        cdc_printf(" ");
        for (size_t j = 0; j < 16; j++) {
            if ((i + j) < (startAddress + length)) {
                if((i + j) < DATABUF_SIZE) {
                    if (slotReadData(slot, (i+j), &data) != CMD_OK) return CMD_FAIL; // 再読出し（可読性のため）
                    cdc_printf("%c", (data >= 0x20 && data <= 0x7E) ? data : '.'); // ASCII 表示
                }
            } else {
                cdc_printf(" ");
            }
        }
        if (i < DATABUF_SIZE) cdc_printf("\n"); // 行末改行
    }
    return CMD_OK;
}

// Slot Mem Transfer Read      	SMTR	cmd_slotReadTransfer	SMTR,[Address](,[Length],[Buffer Address],[Slot])	戻値: なし                    	SlotからBufferへ一括Readします
int cmd_slotReadTransfer(const Command_t* cmd) {
    int bufAddress;
    uint16_t slotAddress;
    int length;
    int i;
    uint8_t slot;
    uint8_t data;

    // パラメータ
    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    slotAddress = (uint16_t)cmd->arg_val[0]; // スロット側の開始アドレス

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE; // length デフォルト
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  bufAddress = 0;        // バッファ先頭デフォルト
    else                        bufAddress = cmd->arg_val[2];

    if (cmd->arg_val[2] >= DATABUF_SIZE) {
        return CMD_FAIL;
    }
    if (bufAddress + length >= DATABUF_SIZE)  length = DATABUF_SIZE - bufAddress; // 範囲補正

    slot = slotVaild(cmd->arg_val[3]);                    // スロット判定
    if (slot == 0) {
        return CMD_FAIL;
    }
    
    for (i = bufAddress ; i < (bufAddress + length) ; i++){
        if (slotReadData(slot, slotAddress, &data) != CMD_OK) { // 1バイト読んで
            return CMD_FAIL;
        }
//        if (i==0) printf ("SM0:%02x %d,%04X",data,slot,slotAddress);    //debugd
//        if (i==1) printf ("SM1:%02x %d,%04X",data,slot,slotAddress);    //debugd
        slotMem[i] = data;                     // バッファへ格納
        slotAddress++;                         // スロット側アドレスインクリメント
    }
    return CMD_OK;
}

// Slot Mem Transfer Write     	SMTW	cmd_slotWriteTransfer	SMTW,[Address],[Length],[Buffer Address](,[Slot])	戻値: なし                    	BufferからSlotへ一括Writeします
int cmd_slotWriteTransfer(const Command_t* cmd) {
    int bufAddress;
    uint16_t slotAddress;
    int length;
    int i;
    uint8_t slot;
    uint8_t data;

    // パラメータ
    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    slotAddress = (uint16_t)cmd->arg_val[0];

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE;
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  bufAddress = 0;
    else                        bufAddress = cmd->arg_val[2];

    if (cmd->arg_val[2] >= DATABUF_SIZE) {
        return CMD_FAIL;
    }
    if (bufAddress + length >= DATABUF_SIZE)  length = DATABUF_SIZE - bufAddress; // 補正

    slot = slotVaild(cmd->arg_val[3]);
    if (slot == 0) {
        return CMD_FAIL;
    }
    
    for (i = bufAddress ; i < (bufAddress + length) ; i++){
        data = slotMem[i];                       // バッファから読み出し
        if (slotWriteData(slot, slotAddress, data) != CMD_OK) { // スロットに書き込み
            return CMD_FAIL;
        }
        slotAddress++;
    }
    return CMD_OK;
}

// IO Transfer Read            	IOTR	cmd_io2buf          	IOTR,[IO],[Buffer Address],[Num]                	戻値: なし                    	IOへBufferへ一括Readします
int cmd_io2buf(const Command_t* cmd) {
    int bufAddress;
    uint16_t slotAddress;
    int length;
    int i;
    uint8_t data;
    bool incIOaddress;

    // パラメータ
    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    slotAddress = (uint16_t)cmd->arg_val[0]; // IO 番号

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE;
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  bufAddress = 0;
    else                        bufAddress = cmd->arg_val[2];

    if (cmd->arg_val[2] >= DATABUF_SIZE) {
        return CMD_FAIL;
    }

    if (cmd->arg_val[3] == -1)  incIOaddress = false;
    else                        incIOaddress = true;

    if (bufAddress + length >= DATABUF_SIZE)  length = DATABUF_SIZE - bufAddress; // 補正

    for (i = bufAddress ; i < (bufAddress + length) ; i++){
        if (slotReadIO(slotAddress, &data) != CMD_OK) { // IO 読み出し
            return CMD_FAIL;
        }
        slotMem[i] = data;                             // バッファへ格納
        if (incIOaddress) slotAddress++;
    }
    return CMD_OK;
}

// IO Transfer Write           	IOTW	cmd_io2buf          	IOTW,[Buffer Address],[IO],[Num]                	戻値: なし                    	BufferからIOへ一括Writeします
int cmd_buf2io(const Command_t* cmd) {
    int bufAddress;
    uint16_t slotAddress;
    int length;
    int i;
    uint8_t data;
    bool incIOaddress;

    // パラメータ
    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    slotAddress = (uint16_t)cmd->arg_val[0]; // IO 番号

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE;
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  bufAddress = 0;
    else                        bufAddress = cmd->arg_val[2];

    if (cmd->arg_val[2] >= DATABUF_SIZE) {
        return CMD_FAIL;
    }

    if (cmd->arg_val[3] == -1)  incIOaddress = false;
    else                        incIOaddress = true;


    if (bufAddress + length >= DATABUF_SIZE)  length = DATABUF_SIZE - bufAddress; // 補正

    for (i = bufAddress ; i < (bufAddress + length) ; i++){
        
        data = slotMem[i];                             // バッファから読み出し
        if (slotWriteIO(slotAddress, data) != CMD_OK) { // IO に書き込み
            return CMD_FAIL;
        }
        if (incIOaddress) slotAddress++;
    }
    return CMD_OK;
}

// Slot Check                  	SCHK	cmd_slotCassetteCheck	SCHK                                            	戻値: SLT1/2の接続状態 +OK    	SlotのCassette接続状態を表示(Power ON不要)
int cmd_slotCassetteCheck(const Command_t* cmd) {
    uint32_t data;
//-Rev B
//    data = ((gpioc_lo_in_get() & 0x00000030) >> 4); // GPIO の該当ビットを抽出

#if HWINFO_SLOTNUM==1 
//Rev B2-
    data = ((gpioc_lo_in_get() & 0x00000020) >> 5); // GPIO の該当ビットを抽出
    if (data == 1) cdc_printf("0000\n");
    if (data == 0) cdc_printf("0010\n");
#endif

#if HWINFO_SLOTNUM==2 
//Rev B2-
    data = ((gpioc_lo_in_get() & 0x00000020) >> 5); // GPIO の該当ビットを抽出
    if (data == 3) cdc_printf("0000\n"); // 接続状態パターン出力
    if (data == 2) cdc_printf("0010\n");
    if (data == 1) cdc_printf("0100\n");
    if (data == 0) cdc_printf("0110\n");
#endif


    return CMD_OK;
}

// BUFFER Send Host            	BSND	cmd_buf2Host        	BSND,[Buffer Address],[Length]                  	戻値: CSUM(ASCII) + OK        	Buffer RAMへのHOSTからのデータ転送
int cmd_buf2Host(const Command_t* cmd) {
    int startAddress;
    int length;
    
    // パラメータ
    if (cmd->arg_val[0] == -1)  startAddress = 0; // デフォルト 0
    else                        startAddress = cmd->arg_val[0];
    if (startAddress > DATABUF_SIZE) return CMD_FAIL; // 範囲超えは失敗

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE; // デフォルト全域
    else                        length = cmd->arg_val[1];

    if (startAddress + length > DATABUF_SIZE)  return CMD_FAIL; // 範囲チェック
//    printf("cmd_buf2Host\n");                   // デバッグ
    cdc_bufOutput(startAddress,length);         // 実データを CDC キューへ積む
    return CMD_OK;
}

// BUFFER Receive Host         	BRCV	cmd_host2buf        	BRCV,[Buffer Address],[Length]                  	戻値: CSUM(ASCII) + OK        	Buffer RAMからHOSTへのデータ転送
int cmd_host2buf(const Command_t* cmd) {
    // メインの処理はCPU0で行われるのでこっちはPASS/FAILのみ判定している
    if (cmd->arg_val[1] != 0)  return CMD_FAIL;
    return CMD_OK;
}

// Move2Boot Mode    	_FFU	cmd_ffuMode        	FFUON            	戻値: 無し       	Bootloaderを起動します。
int cmd_ffuMode(const Command_t* cmd) {
    cdc_printf("Move to Boot mode after 3Sec.\nBye...\n"); // 通知
    busy_wait_ms(3000);                                       // 3秒待機
    reset_usb_boot(REBOOT2_FLAG_REBOOT_TYPE_NORMAL ,0);       // Bootloader 起動
    return CMD_OK;
}

// IO Write                    	IOWR	cmd_ioWrite         	IOWR,[IO],[Data]                                	戻値: OK                      	IOから1Byte Writeします
int cmd_ioWrite(const Command_t* cmd) {
    uint16_t address;
    uint8_t data;

    if (!z80AddressVaild(cmd->arg_val[0]))  return CMD_FAIL; // IO 番号チェック
    address = (uint16_t)cmd->arg_val[0];

    if (cmd->arg_val[1] == -1)  return CMD_FAIL;            // データ必須
    data = (uint8_t) cmd->arg_val[1];

    if (displayFlag == true) cdc_printf("%04x : %02x\n",address,data);               // デバッグ出力
    if (slotWriteIO(address, data) != CMD_OK) return CMD_FAIL; // 実 IO 書き込み
    return CMD_OK;
}

// IO Read                     	IORD	cmd_ioRead          	IORD,[IO]                                       	戻値: Read値 + OK             	IOへ1Byte Readします
int cmd_ioRead(const Command_t* cmd) {
    uint16_t address;
    uint8_t data;

    if (!z80AddressVaild(cmd->arg_val[0]))  return CMD_FAIL; // IO 番号チェック
    address = (uint16_t)cmd->arg_val[0];

    if (slotReadIO(address, &data) != CMD_OK) return CMD_FAIL; // 実 IO 読み出し
    cdc_printf("%04x : %02x\n",address,data);                   // 出力
    return CMD_OK;
}

// シリアルデバッグコードの表示
int cmd_setdebuglog(const Command_t* cmd) {
    comdbgFlag = true;
    return CMD_OK;
}
// DISPON　CMD後のOK表示無し
int cmd_err_displayOff(const Command_t* cmd) {
    passCount = 0;                         // カウントリセット
    errCount = 0;
    displayFlag = false;                   // 表示OFF
    return CMD_OK;
}

// DISPON　CMD後のOK表示無し
int cmd_err_displayOn(const Command_t* cmd) {
    cdc_printf("PASS : %d\n",passCount);   // PASS 表示
    cdc_printf("FAIL : %d\n",errCount);    // FAIL 表示
    passCount = 0;                         // カウントリセット
    displayFlag = true;                    // 表示 ON
    if (errCount != 0){
        errCount = 0;
        return CMD_FAIL;                   // エラーがあれば FAIL
    }

    errCount = 0;
    return CMD_OK;
}

// Slot Mem Read make Hash     	SMTH	cmd_slotReadTransfer	SMTR,[Address](,[Length],[Buffer Address],[Slot])	戻値: なし                    	SlotからBufferへ一括Readします
int cmd_slotReadTransferWithHash(const Command_t* cmd) {
    int bufAddress;
    uint16_t slotAddress;
    int length;
    int i;
    uint8_t slot;
    uint8_t data;
    int hash = 0x5381;

    // パラメータ
    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    slotAddress = (uint16_t)cmd->arg_val[0]; // スロット側の開始アドレス

    if (cmd->arg_val[1] == -1)  length = DATABUF_SIZE; // length デフォルト
    else                        length = cmd->arg_val[1];

    if (cmd->arg_val[2] == -1)  bufAddress = 0;        // バッファ先頭デフォルト
    else                        bufAddress = cmd->arg_val[2];

    if (cmd->arg_val[2] >= DATABUF_SIZE) {
        return CMD_FAIL;
    }
    if (bufAddress + length >= DATABUF_SIZE)  length = DATABUF_SIZE - bufAddress; // 範囲補正

    slot = slotVaild(cmd->arg_val[3]);                    // スロット判定
    if (slot == 0) {
        return CMD_FAIL;
    }
    
    for (i = bufAddress ; i < (bufAddress + length) ; i++){
        if (slotReadData(slot, slotAddress, &data) != CMD_OK) { // 1バイト読んで
            return CMD_FAIL;
        }
//        if (i==0) printf ("SM0:%02x %d,%04X",data,slot,slotAddress);    //debugd
//        if (i==1) printf ("SM1:%02x %d,%04X",data,slot,slotAddress);    //debugd
        slotMem[i] = data;                     // バッファへ格納
        hash = ((hash << 5) + hash) ^ data;
        slotAddress++;                         // スロット側アドレスインクリメント
    }
    cdc_printf("%04x : %08x\n",length,hash);   // データサイズとHash値出力
    return CMD_OK;
}
// RMSET,[Mapper Selecter Address],[Bank Address],[Bank size]   (追加 V1.40～) Mega ROM Mapperの設定
int cmd_romMapperSet(const Command_t* cmd) {

    if (!z80AddressVaild(cmd->arg_val[0]))  {
        return CMD_FAIL;
    }
    romMapper.mapSelAddress = (uint16_t)cmd->arg_val[0];
    if (romMapper.mapSelAddress != 0xffff) romMapper.megarom = true;
    else  romMapper.megarom = false;

   if (!z80AddressVaild(cmd->arg_val[1]))  {
        return CMD_FAIL;
    }
    romMapper.romReadAddress = (uint16_t)cmd->arg_val[1];
 
  if (!z80AddressVaild(cmd->arg_val[2]))  {
        return CMD_FAIL;
    }
    romMapper.romReadSize = (uint16_t)cmd->arg_val[2];
 
    return CMD_OK;

}

// RMRD,[Mapper Start],[Mapper End](,[Slot])                    (追加 V1.40～) Mega ROMの一括Read
int cmd_romMapperRead(const Command_t* cmd) {

    uint8_t bankStart,Banklength;
    uint8_t slot;
    uint8_t bank;
    uint8_t readData;
    uint16_t address;

    int length = 0;
    int totalLength = 0;
    bool cmdResult;
        
    if (cmd->arg_val[0] == -1)  return CMD_FAIL;            // データ必須
    bankStart = (uint8_t) cmd->arg_val[0];

    if (cmd->arg_val[1] == -1)  return CMD_FAIL;            // データ必須
    Banklength = (uint8_t) cmd->arg_val[1];

    slot = slotVaild(cmd->arg_val[2]);
    if (slot == 0) return CMD_FAIL;                         // スロット不正

    totalLength = romMapper.romReadSize * Banklength;
    cmdResult = CMD_OK;

    for (bank = bankStart;bank < (bankStart+Banklength);bank++){
        address = romMapper.romReadAddress;
        if (romMapper.megarom) {
            slotWriteData(slot, romMapper.mapSelAddress, bank);
        }

        length = 0;
        while(length < (int) romMapper.romReadSize){
            while (cdc_q_count >= CDC_PRINTF_QSIZE-1) { // キュー満杯時待機
                sleep_ms(1);
            }

            int chunk = ((totalLength - length) < CDC_PRINTF_BUF_SIZE) ? (totalLength - length) : CDC_PRINTF_BUF_SIZE; // 分割サイズ決定

            for (int i=0;i<chunk;i++){

                if (cmdResult == CMD_OK) {
                    cmdResult = slotReadData(slot, address, &readData);
                    if (cmdResult != CMD_OK) readData = 0x00;
                }
                cdc_queue[cdc_q_write].buf[i] = readData;
                address++;
            }

            cdc_queue[cdc_q_write].binLength = chunk; // バイナリ長を設定
            cdc_queue[cdc_q_write].valid = true;      // 有効化
            cdc_q_write = (cdc_q_write + 1) % CDC_PRINTF_QSIZE; // 次の書き込み位置へ
            mutex_enter_blocking(&cdc_output_mutex);  // ロック
            cdc_q_count++;                             // 件数インクリメント
            mutex_exit(&cdc_output_mutex);  // ロック解除
            length += chunk;                           // オフセット進める
        }
    }
    return cmdResult;

}

void ledColorInit(void){
    ledColorR[LED_STATUS_IDLE] =   0x0; ledColorG[LED_STATUS_IDLE] =   0x0; ledColorB[LED_STATUS_IDLE] =   0x0;
    ledColorR[LED_STATUS_READY] =  0x0; ledColorG[LED_STATUS_READY] =  0x0; ledColorB[LED_STATUS_READY] =  0x0;
    ledColorR[LED_STATUS_PON] =    0x7; ledColorG[LED_STATUS_PON] =   0x40; ledColorB[LED_STATUS_PON] =    0x7;
    ledColorR[LED_STATUS_ACC] =   0x40; ledColorG[LED_STATUS_ACC] =    0x7; ledColorB[LED_STATUS_ACC] =    0x7;
}


int cmd_ledColorReady(const Command_t* cmd) {

    if (cmd->arg_val[0] == -1)  ledColorR[LED_STATUS_READY] = 0x10;
    else                        ledColorR[LED_STATUS_READY] = (uint8_t) cmd->arg_val[0];
    if (cmd->arg_val[1] == -1)  ledColorG[LED_STATUS_READY] = 0x10;
    else                        ledColorG[LED_STATUS_READY] = (uint8_t) cmd->arg_val[1];
    if (cmd->arg_val[2] == -1)  ledColorB[LED_STATUS_READY] = 0x10;
    else                        ledColorB[LED_STATUS_READY] = (uint8_t) cmd->arg_val[2];
    ledTask(true);
    return CMD_OK;
}

int cmd_ledColorPowerOn(const Command_t* cmd) {

    if (cmd->arg_val[0] == -1)  ledColorR[LED_STATUS_PON] = 0x07;
    else                        ledColorR[LED_STATUS_PON] = (uint8_t) cmd->arg_val[0];
    if (cmd->arg_val[1] == -1)  ledColorG[LED_STATUS_PON] = 0x40;
    else                        ledColorG[LED_STATUS_PON] = (uint8_t) cmd->arg_val[1];
    if (cmd->arg_val[2] == -1)  ledColorB[LED_STATUS_PON] = 0x07;
    else                        ledColorB[LED_STATUS_PON] = (uint8_t) cmd->arg_val[2];
    ledTask(true);
    return CMD_OK;
}

int cmd_ledColorAcc(const Command_t* cmd) {

    if (cmd->arg_val[0] == -1)  ledColorR[LED_STATUS_ACC] = 0x40;
    else                        ledColorR[LED_STATUS_ACC] = (uint8_t) cmd->arg_val[0];
    if (cmd->arg_val[1] == -1)  ledColorG[LED_STATUS_ACC] = 0x07;
    else                        ledColorG[LED_STATUS_ACC] = (uint8_t) cmd->arg_val[1];
    if (cmd->arg_val[2] == -1)  ledColorB[LED_STATUS_ACC] = 0x07;
    else                        ledColorB[LED_STATUS_ACC] = (uint8_t) cmd->arg_val[2];
    ledTask(true);
    return CMD_OK;
}



// ---- Core1 コマンド実行ループ ----
void core1_entry(); // commands.c のテーブルを使うので先に宣言 (実体は下の core1_entry 実装が使う)

void core1_entry() {
    while (true) {
        if (count > 0) {
            // バッファからコマンド取り出し
            if(commandBufs[read_idx].valid) {
                bool dispatched = false;
                for (size_t i=0; i<cmd_table_size; ++i) { // commands.c で定義されたテーブルを参照
                    if (strcmp((const char *)commandBufs[read_idx].cmd, cmd_table[i].name)==0) { // コマンド名比較
                        int cmd_err = cmd_table[i].func((const Command_t *)&commandBufs[read_idx]); // 実行
                        if (displayFlag){
//                          printf("C:%s R:%d %02x %02x\n",commandBufs[read_idx].cmd,cmd_err,slotMem[0],slotMem[1]); //debugd
                            if (cmd_err == CMD_OK)  cdc_printf("OK\n"); // 成功表示
                            else if (cmd_err == CMD_FAIL) cdc_printf("FAIL\n"); // 失敗表示
                            else if (cmd_err == CMD_BADPARM) cdc_printf("Bad Parameter\n"); // bad param
                            else if (cmd_err == CMD_UNKNOW) cdc_printf("Unknow Error\n"); // unknown
                        }else{
                            if (cmd_err == CMD_OK)  passCount++; // 表示OFF時はカウントのみ
                            else errCount++;
                        }
                        dispatched = true;
                        break;
                    }
                }
                if(!dispatched)
                    cdc_printf("Unknown command: [%s]\n", commandBufs[read_idx].cmd); // 未登録コマンド
            }
            mutex_enter_blocking(&cmdcount_mutex);
            commandBufs[read_idx].valid = false;        // 使用済みにする
            read_idx = (read_idx + 1) % CMD_BUF_NUM;    // 次の読み出し位置へ
            count--;                                    // 件数デクリメント
            mutex_exit(&cmdcount_mutex);
        } else {
//            sleep_ms(1); // 無処理時待機
        }
    }
}

// ---- Core0 (USB受信&パース&キュー保存) ----
int main(void)
{

    // Initialize TinyUSB stack
    board_init();                                   // board 初期化

    // mutex_init(&usb_task_mutex);                 // mutex 初期化
    // gpio_init(LED_PIN); gpio_set_dir(LED_PIN, GPIO_OUT);
    // gpio_init(MOTOR_PIN); gpio_set_dir(MOTOR_PIN, GPIO_OUT);

    // let pico sdk use the first cdc interface for std io
    stdio_init_all();                                // stdio を USB CDC にルーティング (可能なら)

    init_all_pins();                                 // GPIO を全初期化 (ports.c の配列を使用)

    pio_gpio_init(pio, CLOCK_PIN);
    gpio_disable_pulls(CLOCK_PIN);


    // PIOプログラムをロード
    uint clkoffset = pio_add_program(pio, &pwm_low_hiz_program);
    // 初期化用インライン関数を使用
    pwm_low_hiz_program_init(pio, sm_slotclk, clkoffset, CLOCK_PIN);

//    SlotPowerON();

    uint offset = pio_add_program(pio, &ws2812_program); // PIO プログラムを追加
    ws2812_program_init(pio, sm_ws2812, offset, WS2812_PIN, 800000, IS_RGBW); // WS2812 初期化
//   ws2812_program_init(pio0, 0, offset, WS2812_PIN, 800000, IS_RGBW); // WS2812 初期化

    tusb_init();                                     // TinyUSB スタック初期化

    // TinyUSB board init callback after init
    if (board_init_after_tusb) {
        board_init_after_tusb();                     // ボード固有の後処理呼び出し (存在する場合)
    }

    // コマンドバッファ初期化
    for(int i=0;i<CMD_BUF_NUM;i++) commandBufs[i].valid = false; // 全エントリ無効化
    write_idx = 0;                                    // 索引初期化
    read_idx = 0;
    count = 0;
    ledColorInit();                                    //LED Color Table初期化

    multicore_launch_core1(core1_entry);              // Core1 にコマンド処理ループを起動
//    enable_clock();

    printf("\nMSXPlayer Game Card Adapter for RP2350\nCopyright @v9938\n"); // 起動画面
    printf("Release Data: %s\n\n",__DATE__);

    printf("Ready to Command\n");                     // 準備完了表示
    // main run loop
    while (1) {
        // TinyUSB device task | must be called regurlarly
        tud_task();                                   // TinyUSB の定期処理 (USB イベント処理)
       
        // cdc tasks
        cdc_task();                                   // CDC キュー送受信処理
        ledTask(false);                                    // LED 更新
    }

    // indicate no error
    return 0;
}

void cdc_task(void)
{
    static size_t len = 0;                            // 送信予定長
    static bool outoutMode = false;                   // 出力中フラグ
    static size_t offset = 0;                         // 送信オフセット (未使用のまま)

//  if (tud_cdc_connected()){                         // CDC 接続されているか
  if (tud_ready()){                         // CDC 接続されているか(DTS無し対策)
//        printf("tud_ready\n");
        //受信TASK
        if (tud_cdc_available()) {                    // 受信データがあるか
            tud_cdc_rx_cb(0);                         // 受信コールバック処理 (read とパースを行う)
        }else{
//            sleep_ms(1);                              // 無ければ少し待つ
        }
    }else{
//         printf("Not Ready\n");
    }

    // 送信TASK
    // CDC出力バッファから実際にUSBに出力
    if ((cdc_q_count > 0) && (cdc_queue[cdc_q_read].valid)) { // キューにデータがあるか

        if (outoutMode==false)
        { 
            // Binモード時はLength分送付する
            if (cdc_queue[cdc_q_read].binLength == 0) len = strlen(cdc_queue[cdc_q_read].buf); // テキスト長
            else len = cdc_queue[cdc_q_read].binLength; // バイナリ長
            outoutMode = true;                         // 出力中フラグセット
            offset = 0;                                // オフセット初期化
        }
        // USB の書き込みバッファに必要量が確保されているか確認
        if ((outoutMode==true) && (tud_cdc_n_write_available(0) >= (int)len)){ // 十分な空きがあるか
            tud_cdc_write(&cdc_queue[cdc_q_read].buf[offset], len); // まとめて書き込み
            tud_cdc_write_flush();                    // フラッシュして送信ブロックへ

            cdc_queue[cdc_q_read].valid = false;      // 送信済みとして無効化
            cdc_q_read = (cdc_q_read + 1) % CDC_PRINTF_QSIZE; // 次を指す
            mutex_enter_blocking(&cdc_output_mutex);  // ロック
            cdc_q_count--;                             // 件数デクリメント
            mutex_exit(&cdc_output_mutex);  // ロック解除
            outoutMode = false;                        // 出力終了
        }

    }

}

// callback when data is received on a CDC interface

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    //CDC interfaseが再設定された場合は一度CDC Queを空にします。
   
    printf("Set CDC Parameter...\n");
    displayFlag = true;
    cdc_queue[cdc_q_read].valid = false;
    cdc_q_write = 0;          // 書き込みインデックス // produce index
    cdc_q_read  = 0;          // 読み出しインデックス // consume index
    cdc_q_count = 0;          // キュー内件数 // 同期注意
    crlfFlag = false;         // CR/LF 判定フラグ (BRCV 用)

}

void tud_cdc_send_break_cb(uint8_t itf, uint16_t wValue) {

    //Break信号が来た場合はCDC Queを空にします。
    printf("Breaked CDC...\n");
    displayFlag = true;
    cdc_queue[cdc_q_read].valid = false;
    cdc_q_write = 0;          // 書き込みインデックス // produce index
    cdc_q_read  = 0;          // 読み出しインデックス // consume index
    cdc_q_count = 0;          // キュー内件数 // 同期注意

}


void tud_cdc_rx_cb(uint8_t itf)
{
    static int startAddress;                          // BRCV 受信開始アドレス
    static int length;                                // BRCV 残り長
    static bool bcvCmd = false;                       // BRCV 受信モードフラグ
    static bool bcvFail = false;                      // BRCV チェック失敗フラグ
    static bool skipByte = false;                     // CRLF 対策で先頭バイトをスキップするか
    uint32_t len = tud_cdc_read(rx_buf, RX_BUF_SIZE-1); // CDC からデータ読み出し
    char c;

    for (uint32_t i = 0; i < len; ++i) {

        if (bcvCmd == true){
            char c = rx_buf[i]; 
            if (skipByte == true){
                skipByte = false;                      // スキップフラグクリア
#ifdef BIN_DEBUG
            if (comdbgFlag)
                printf("SKIP : %02x \n", (unsigned char)c); // デバッグ表示
#endif
                continue;                              // スキップ時は書き込みしないで次へ
            }

            slotMem[startAddress++] = (uint8_t)c;      // BRCV モード: 生バイトを slotMem に格納
            length--;                                  // 残り長デクリメント
#ifdef BIN_DEBUG
//            printf("MEM:%04x %02x\n",startAddress, (unsigned char)c);       // デバッグ表示
//            printf("%02x-%d \n", (unsigned char)c,length);       // デバッグ表示
#endif
            commandBufs[write_idx].arg_val[1] = length; // キュー上のコマンドに残長を反映
            if (length==0){
                mutex_enter_blocking(&cmdcount_mutex);
                commandBufs[write_idx].valid = true;   // 受信完了 => コマンドを有効化
                write_idx = (write_idx + 1) % CMD_BUF_NUM; // 次の書き込み位置へ
                count++;                                // キュー件数インクリメント
                mutex_exit(&cmdcount_mutex);
                bcvCmd = false;                         // BRCV モード終了
#ifdef BIN_DEBUG
                if (comdbgFlag)
                    printf("BIN End\n");       // デバッグ表示
#endif
            }
        }else{

            char c = toupper(rx_buf[i]);                   // 受信文字を大文字化 (コマンドは大文字前提)
#ifdef BIN_DEBUG
            if (comdbgFlag)
            //for Debug
                printf("%c",c);                            // 受信文字をデバッグ出力
#endif
 
            // 改行トリガー
            if (c == '\n' || c == '\r') {
                
                if (lineLen == 0) crlfFlag = true;      // このFlagが立つ場合は改行は2Byte (CRLF 対策)

                if (lineLen > 0) {                      // 行バッファに何か入っていればパース開始
                    lineBuf[lineLen] = '\0';           // NUL 終端
                    // パースしてキューに格納
                    char tmpCmd[MAX_STRING] = {0};     // 一時 CMD 保持
                    char tmpArg[MAX_STRING] = {0};     // 一時 Arg 保持
                    char tmpData[MAX_STRING] = {0};    // tmpData (元コードの用途に合わせる)
                    int tmp_arg_val[MAX_ARG];          // 第2,3,4,5 の 16 進変換結果

                    for (uint j = 0; j< MAX_ARG;j++){
                        tmp_arg_val[j] = -1;          // 格納BUFFER初期化 // -1 は未指定
                    }

                    int idx=0;
                    char* token = strtok(lineBuf, ","); // カンマ区切り

                    if (token == NULL)  strncpy(tmpCmd, lineBuf, MAX_STRING-1);   // 第1パラのみの特例処理

                    while (token && idx<5) {
                        if(idx==0) strncpy(tmpCmd, token, MAX_STRING-1);       // 第1パラはコマンド
                        else       strncpy(tmpArg, token, MAX_STRING-1);       // 第2パラはそのまま保持
        
                        // 第二パラメータがある場合、16進変換
                        if ((idx >= 1) && (idx < MAX_ARG) && (strlen(tmpArg) > 0)) {
                            if (!parse_hex_string(tmpArg, &(tmp_arg_val[idx-1]))) {
                                tmp_arg_val[idx-1] = -1; // 変換失敗は -1 を設定
                            }
                        }
                        // 第2パラは文字列としても利用するケースがあるため保持
                        if (idx==1) {
                            strncpy(tmpData, tmpCmd, MAX_STRING-1); // （元コードの意図を保つ）
                        }
                        token = strtok(NULL, ","); // 次トークン
                        idx++;
                    }
                    
                    // 空パラメータも含めバッファに入れる
                    if(count<CMD_BUF_NUM) {                   // キューに空きがあるかチェック
                        strncpy((char*)commandBufs[write_idx].cmd, tmpCmd, MAX_STRING-1); // コマンド文字列コピー
                        strncpy((char*)commandBufs[write_idx].arg, tmpData, MAX_STRING-1); // arg コピー

                        for (uint j = 0; j< MAX_ARG;j++){
                                commandBufs[write_idx].arg_val[j]  =  tmp_arg_val[j]; // arg 値コピー
                        }

                        // BRCVのみCore0で処理するのでこの場で判定しておく
                        if (strcmp((const char *)commandBufs[write_idx].cmd,"BRCV")==0) {
                            bcvFail = false;
#ifdef BIN_DEBUG
                            if (comdbgFlag)
                                printf("BRCV_OK\n");               // デバッグ
#endif
                                // BRCVパラメータチェック
                            if (commandBufs[write_idx].arg_val[0] == -1)  startAddress = 0; // デフォルトアドレス
                            else startAddress = commandBufs[write_idx].arg_val[0];
                            if (startAddress >= DATABUF_SIZE) bcvFail = true; // 範囲チェック

                            if (commandBufs[write_idx].arg_val[1] == -1)  length = DATABUF_SIZE; // デフォルト長
                            else  length = commandBufs[write_idx].arg_val[1];
                            if (length >= DATABUF_SIZE) length = DATABUF_SIZE; // 制限

                            if (startAddress + length >= DATABUF_SIZE)  bcvFail = true; // 合算で範囲チェック
                            if (bcvFail == false) {
                                bcvCmd = true;                   // BRCV 受信モードに移行
                                if (crlfFlag== true) skipByte = true;       // LFCR 対策: 余分な改行バイトをスキップ
                            } 
                        }else{
                            bcvCmd = false;                     // BRCV 以外は通常キュー登録
                        }
                        if (bcvCmd == false){
                            
                            mutex_enter_blocking(&cmdcount_mutex);
                            commandBufs[write_idx].valid = true; // キューエントリを有効化
                            write_idx = (write_idx + 1) % CMD_BUF_NUM; // 書き込みインデックス更新
                            count++;                              // 件数インクリメント
                            mutex_exit(&cmdcount_mutex);

                        }
                    } else {
                        cdc_printf("Buffer full! Dropped: %s\r\n", lineBuf); // キュー満杯時の通知
                    }
                    lineLen = 0; // 行バッファクリア
                }
            } else {
                if (lineLen < LINE_BUF_SIZE-1) {
                    lineBuf[lineLen++] = c;            // 行バッファへ追加
                } else {
                    // オーバーフロー時はlineバッファクリア
                    lineLen = 0;                        // バッファ溢れ検出 -> クリア
                }
            }
        }
    }
}
