#include "common.h"
#include "user_main.h"
#include "main.h"
#include "display.hpp"
#include "fx.hpp"
#include "eeprom.hpp"

extern SAI_HandleTypeDef hsai_BlockA1;
extern SAI_HandleTypeDef hsai_BlockB1;
extern TIM_HandleTypeDef htim1;
extern UART_HandleTypeDef huart3;

// DMA用バッファ配列 RAM_D2に置く
#define __ATTR_RAM_D2  __attribute__ ((section(".RAM_D2"))) __attribute__ ((aligned (4)))
int32_t RxBuffer[BLOCK_SIZE*4] __ATTR_RAM_D2 = {}; // 音声信号受信バッファ配列 Lch前半 Lch後半 Rch前半 Rch後半
int32_t TxBuffer[BLOCK_SIZE*4] __ATTR_RAM_D2 = {}; // 音声信号送信バッファ配列

uint32_t callbackCount = 0; // I2Sの割り込みごとにカウントアップ タイマとして利用
extern uint32_t cpuUsageCycleMax[]; // CPU使用サイクル数
const float i2sInterruptInterval = (float)BLOCK_SIZE / SAMPLING_FREQ; // I2Sの割り込み間隔時間

// スイッチ短押し、スイッチ長押し、ステータス情報表示時間のカウント数
const uint32_t shortPushCount = 1 + SHORT_PUSH_MSEC / (8 * 1000 * i2sInterruptInterval); // 1つのスイッチは8回に1回の読取のため8をかける
const uint32_t longPushCount = 1 + LONG_PUSH_MSEC / (8 * 1000 * i2sInterruptInterval);
const uint32_t statusDispCount = 1 + STATUS_DISP_MSEC / (1000 * i2sInterruptInterval);

bool fxOn = false; // エフェクトオン・オフ状態
bool exOn = false; // EX機能オン・オフ状態
string fxName = ""; // 現在のエフェクト名 11文字まで
string fxExFuncName = "TAP TEMPO"; // 現在のエフェクトのEX機能名 9文字まで
uint16_t fxColor = 0; // 現在のエフェクトLED色 RGB565
string fxParamName[20] = {}; // エフェクトパラメータ名 LEVEL, GAIN等
int16_t fxParam[20] = {}; // 現在のエフェクトパラメータ数値
string fxParamStr[20] = {}; // 現在のエフェクトパラメータ数値 文字列
int16_t fxParamMax[20] = {}; // エフェクトパラメータ最大値
int16_t fxParamMin[20] = {}; // エフェクトパラメータ最小値

//uint8_t fxParamNum = 0; // エフェクトパラメータ 現在何番目か ※0から始まる
uint8_t fxParamNumMax = 0; // エフェクトパラメータ数 最大値
uint8_t fxPage = 0; // エフェクトパラメータページ番号

uint8_t fxNum = 0; // 現在のエフェクト番号 ※0から始まる
int8_t fxChangeFlag = 0; // エフェクト種類変更フラグ 次エフェクトへ: 1 前エフェクトへ: -1

// 処理が重いエフェクトの場合、画面描画が遅くなる→画面描画が完了するまでエフェクトが切り替わらないようにする
uint8_t fxDispCpltFlag = 0; // エフェクト画面描画完了フラグ メインループ2回でエフェクト切替可能

string statusStr = PEDAL_NAME; // ステータス表示文字列

float tapTime = 0.0f; // タップテンポ入力時間 ms

int8_t re[5] = {};      // ロータリーエンコーダの状態 5番目re[4]は予備
int8_t last_re[5] = {}; // 前回のロータリーエンコーダの状態

const uint8_t fxNameXY[2] = {0,0};   // エフェクト名 表示位置
const uint8_t fxPageXY[2] = {110,0}; // エフェクトパラメータ ページ 表示位置
const uint8_t statusXY[2] = {110,10}; // ステータス 表示位置
const uint16_t percentXY[2] = {140,0}; // 処理時間% 表示位置
const uint8_t fxParamNameXY[6][2] = {{0,20},{39,20},{79,20},{119,20},{0,0},{0,0}}; // エフェクトパラメータ名表示位置
const uint8_t fxParamBarXY[6][2] = {{0,32},{39,32},{79,32},{119,32},{0,0},{0,0}}; // エフェクトパラメータバー表示位置
const uint8_t fxParamStrXY[6][2]  = {{2,49},{41,49},{81,49},{121,49},{0,0},{0,0}}; // エフェクトパラメータ数値表示 左端の文字位置

const uint8_t exFuncXY[2] = {9, 87}; // EX機能名称 表示位置
const uint8_t tapmsXY[2] = {69,87}; // タップテンポ ms 表示位置
const uint8_t tapbpmXY[2] = {111,87}; // タップテンポ bpm 表示位置

uint8_t clipDetect[4] = {}; // 入出力クリップ検出用 Lin Rin Lout Rout

bool saveDataFlag = false; // データ保存実行フラグ

uint16_t brightness[11] = {32,45,64,91,128,181,256,362,512,724,1024}; // LCD 明るさ
int8_t brightnessNum = 10; // LCD 明るさ段階数値 0～10

uint8_t midiRecievedBuf; // MIDI受信バッファ

void loadScreen() // 画面固定部分表示
{
  ST7735_FillScreen(ST7735_BLACK);

  // パラメーターバー枠表示
  for (int i = 0; i < 4; i++) ST7735_DrawLineX(40*i, 38+40*i, 32, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineX(40*i, 38+40*i, 46, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineY(0+40*i, 33, 45, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineY(38+40*i, 33, 45, ST7735_WHITE);

  // EX機能 枠表示
  ST7735_WriteString(9, 72, "EXTRA FUNCTION", Font_7x10, ST7735_WHITE, ST7735_BLACK);
  ST7735_DrawLineX(4, 7, 76, ST7735_WHITE);
  ST7735_DrawLineX(95, 154, 76, ST7735_WHITE);
  ST7735_DrawLineX(4, 154, 104, ST7735_WHITE);
  ST7735_DrawLineY(3, 77, 103, ST7735_WHITE);
  ST7735_DrawLineY(155, 77, 103, ST7735_WHITE);

  // ボタン名表示
  ST7735_WriteString(9, 115, "<FX", Font_7x10, ST7735_WHITE, ST7735_BLACK);
  ST7735_WriteString(49, 115, ">FX", Font_7x10, ST7735_WHITE, ST7735_BLACK);
  ST7735_WriteString(87, 115, "SAVE", Font_7x10, ST7735_WHITE, ST7735_BLACK);
  ST7735_WriteString(127, 115, "PAGE", Font_7x10, ST7735_WHITE, ST7735_BLACK);

  // ボタン枠表示
  for (int i = 0; i < 4; i++) ST7735_DrawLineX(2+40*i, 36+40*i, 110, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineX(2+40*i, 36+40*i, 127, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineY(1+40*i, 111, 126, ST7735_WHITE);
  for (int i = 0; i < 4; i++) ST7735_DrawLineY(37+40*i, 111, 126, ST7735_WHITE);
}

void dispBar(uint8_t i, int16_t param, int16_t min, int16_t max) // エフェクトパラメータバー表示
{
  uint8_t bar;

  // 割合を計算 ※ゼロ除算を除外
  if (max - min != 0) bar = 37 * (param - min) / (max- min);
  else bar = 0;

  for (int j = 33; j <= 45; j++)
  {
    ST7735_DrawLineX(40*i + 1, 40*i + bar, j, fxColor);
    ST7735_DrawLineX(40*i + bar + 1, 40*i + 37, j, ST7735_BLACK);
  }
}

void mainInit() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<最初に1回のみ行う処理
{
  // Denormalized numbers（非正規化数）を0として扱うためFPSCRレジスタ変更
  asm("VMRS r0, FPSCR");
  asm("ORR r0, r0, #(1 << 24)");
  asm("VMSR FPSCR, r0");

  // 処理時間計測用設定
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  // ディスプレイ初期化
  ST7735_Init();
  ST7735_FillScreen(ST7735_BLACK);

  // LCDバックライト PWM開始
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0);

  // 起動時左フットスイッチ、左下スイッチ、右下スイッチを押していた場合、データ全消去
  if (!HAL_GPIO_ReadPin(SW0_GPIO_Port, SW0_Pin) &&
      !HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin) &&
      !HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin))
  {
    ST7735_FillScreen(ST7735_BLACK);
    ST7735_WriteString(0, 0, "ERASE ALL DATA", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    eraseData();
    HAL_Delay(1000);
  }

  // 保存済パラメータ読込
  loadData();
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 1024 - brightness[brightnessNum]);

  // 初期エフェクト読込 ※信号処理が始まる前にメモリ確保
  fxInit();

  // 起動時右フットスイッチを押し続けている間、明るさ調整
  while (!HAL_GPIO_ReadPin(SW5_GPIO_Port, SW5_Pin))
  {
    HAL_Delay(100);
    if (!HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin)) brightnessNum++;
    if (!HAL_GPIO_ReadPin(SW0_GPIO_Port, SW0_Pin)) brightnessNum--;
    brightnessNum = clip(brightnessNum, 0, 10);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 1024 - brightness[brightnessNum]);
    string tmpStr = "BRIGHTNESS:" + std::to_string(brightnessNum);
    if (brightnessNum < 10) tmpStr.insert(11, "0");
    ST7735_WriteString(10, 10, tmpStr, Font_11x18, ST7735_WHITE, ST7735_BLACK);
    saveDataFlag = true;
  }

  // 明るさデータを保存
  if (saveDataFlag)
  {
    saveDataFlag = false;
    saveData();
    ST7735_WriteString(10, 50, "BRIGHTNESS DATA STORED!", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    HAL_Delay(1000);
  }

  // コーデック設定  M0 PB8 SET  M1 PB9 RESET
  HAL_GPIO_WritePin(M0_GPIO_Port, M0_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(M1_GPIO_Port, M1_Pin, GPIO_PIN_RESET);

  // SAIのDMA開始
  HAL_SAI_Receive_DMA(&hsai_BlockA1, (uint8_t*)RxBuffer, BLOCK_SIZE*4);
  HAL_SAI_Transmit_DMA(&hsai_BlockB1, (uint8_t*)TxBuffer, BLOCK_SIZE*4);

  // オーディオコーデック リセット
  HAL_GPIO_WritePin(CODEC_RST_GPIO_Port, CODEC_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(100);
  HAL_GPIO_WritePin(CODEC_RST_GPIO_Port, CODEC_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(100);

  // I2Sのフレームエラー発生の場合、リセットを繰り返す
  while(__HAL_SAI_GET_FLAG(&hsai_BlockA1, SAI_FLAG_AFSDET)
     || __HAL_SAI_GET_FLAG(&hsai_BlockA1, SAI_FLAG_LFSDET)
     || __HAL_SAI_GET_FLAG(&hsai_BlockB1, SAI_FLAG_AFSDET)
     || __HAL_SAI_GET_FLAG(&hsai_BlockB1, SAI_FLAG_LFSDET))
  {
    ST7735_WriteString(0, 0, "ERROR", Font_11x18, ST7735_WHITE, ST7735_BLACK);
    HAL_GPIO_WritePin(CODEC_RST_GPIO_Port, CODEC_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(CODEC_RST_GPIO_Port, CODEC_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(100);
  }

  // MIDI受信開始
  HAL_UART_Receive_IT(&huart3, &midiRecievedBuf,1);

  // 画面固定表示部分の描画
  loadScreen();

}

void mainLoop() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<メインループ
{
  // 前回の変数を記録しておき、変更があった時のみ描画する
  static string last_fxParamStr[20] = {};
  static uint8_t last_fxPage = 222;
  static uint8_t last_cpuUsagePercent = 111;
  static uint8_t last_fxNum = 123;

  // ページ切替、エフェクト切替時の画面再描画
  if (fxNum != last_fxNum || fxPage != last_fxPage)
  {
    // エフェクト名称表示
    ST7735_WriteString(fxNameXY[0], fxNameXY[1], paddingLeft(fxName, 11), Font_11x18, ST7735_WHITE, ST7735_BLACK);

    for (int i = 0; i < 4; i++)
    { // エフェクトパラメータ名称表示
      ST7735_WriteString(fxParamNameXY[i][0], fxParamNameXY[i][1], paddingLeft(fxParamName[i+4*fxPage], 6), Font_7x10, ST7735_WHITE, ST7735_BLACK);

      // エフェクトパラメータ数値表示
      fxSetParamStr(i+4*fxPage); // パラメータ数値を文字列に変換
      ST7735_WriteStringNarrow(fxParamStrXY[i][0], fxParamStrXY[i][1], paddingRight(fxParamStr[i+4*fxPage], 4), Font_11x18, ST7735_WHITE, ST7735_BLACK);

      // バー表示
      dispBar(i, fxParam[i+4*fxPage], fxParamMin[i+4*fxPage], fxParamMax[i+4*fxPage]);
    }

    // EX機能 名称表示
    ST7735_WriteString(exFuncXY[0], exFuncXY[1], paddingRight(fxExFuncName, 9), Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_FillRectangleFast(tapmsXY[0], tapmsXY[1]-7, 84, 24, ST7735_BLACK);

    // エフェクトパラメータページ番号表示
    string fxPageStr = "P" + std::to_string(fxPage + 1) + "/" + std::to_string((fxParamNumMax-1) / 4 + 1);
    ST7735_WriteString(fxPageXY[0], fxPageXY[1], fxPageStr, Font_7x10, ST7735_WHITE, ST7735_BLACK);

    last_fxPage = fxPage;
    last_fxNum = fxNum;
  }

  // ステータス表示 ------------------------------
  static bool statusChangeStart = false; // ステータス表示変更開始フラグ

  if (statusStr != " ")
  {
    if (!statusChangeStart) // ステータス表示変更開始処理
    {
      callbackCount = 0;
      statusChangeStart = true;
    }

    if (callbackCount > statusDispCount) // 一定時間経過後、空白に戻す
    {
      ST7735_WriteString(statusXY[0], statusXY[1], "        ", Font_7x10, ST7735_WHITE, ST7735_BLACK);
      statusChangeStart = false;
      statusStr = " ";
    }
    else if ((callbackCount / (statusDispCount/6)) % 2 == 0) // 一定時間経過まで点滅
    {
      ST7735_WriteString(statusXY[0], statusXY[1], paddingLeft(statusStr, 8), Font_7x10, ST7735_WHITE, ST7735_BLACK);
    }
    else
    {
      ST7735_WriteString(statusXY[0], statusXY[1], "        ", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    }
  }

  // エフェクトパラメータ数値表示------------------------------
  for (int i = 0; i < 4; i++)
  {
    fxSetParamStr(i+4*fxPage); // パラメータ数値を文字列に変換
    if (last_fxParamStr[i+4*fxPage] != fxParamStr[i+4*fxPage])
    {
      ST7735_WriteStringNarrow(fxParamStrXY[i][0], fxParamStrXY[i][1], paddingRight(fxParamStr[i+4*fxPage], 4), Font_11x18, ST7735_WHITE, ST7735_BLACK);
      last_fxParamStr[i+4*fxPage] = fxParamStr[i+4*fxPage];
      dispBar(i, fxParam[i+4*fxPage], fxParamMin[i+4*fxPage], fxParamMax[i+4*fxPage]); // バー表示
    }
  }

  // ロータリーエンコーダスイッチ押下時 エフェクトパラメータ下線表示------------------------------
  if (!HAL_GPIO_ReadPin(SW6_GPIO_Port, SW6_Pin)) ST7735_DrawLineX(0, 38, 66, ST7735_WHITE);
  else ST7735_DrawLineX(0, 38, 66, ST7735_BLACK);
  if (!HAL_GPIO_ReadPin(SW7_GPIO_Port, SW7_Pin)) ST7735_DrawLineX(40, 78, 66, ST7735_WHITE);
  else ST7735_DrawLineX(40, 78, 66, ST7735_BLACK);
  if (!HAL_GPIO_ReadPin(SW8_GPIO_Port, SW8_Pin)) ST7735_DrawLineX(80, 118, 66, ST7735_WHITE);
  else ST7735_DrawLineX(80, 118, 66, ST7735_BLACK);
  if (!HAL_GPIO_ReadPin(SW9_GPIO_Port, SW9_Pin)) ST7735_DrawLineX(120, 158, 66, ST7735_WHITE);
  else ST7735_DrawLineX(120, 158, 66, ST7735_BLACK);

  // CPU使用率表示------------------------------
  uint8_t cpuUsagePercent = 100.0f * cpuUsageCycleMax[fxNum] / SystemCoreClock / i2sInterruptInterval;
  if (last_cpuUsagePercent != cpuUsagePercent)
  {
    string percentStr = std::to_string(cpuUsagePercent);
    ST7735_WriteString(percentXY[0], percentXY[1], paddingRight(percentStr, 2) + "%", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    last_cpuUsagePercent = cpuUsagePercent;
  }

  // EX機能表示------------------------------
  if (fxExFuncName == "TAP TEMPO")
  {
    string tmpStr = std::to_string((uint16_t)tapTime);
    ST7735_WriteString(tapmsXY[0], tapmsXY[1], paddingRight(tmpStr, 4) + "ms", Font_7x10, ST7735_WHITE, ST7735_BLACK); // タップ間隔時間を表示
    if (tapTime > 60.0f) tmpStr = std::to_string((uint16_t)(60000.0f / tapTime)); // bpmを計算
    ST7735_WriteString(tapbpmXY[0], tapbpmXY[1], paddingRight(tmpStr, 3) + "bpm", Font_7x10, ST7735_WHITE, ST7735_BLACK); // bpm表示

    uint16_t blinkCount = 1 + tapTime / (1000 * i2sInterruptInterval); // 点滅用カウント数
    if (callbackCount % blinkCount < 60 / (1000 * i2sInterruptInterval)) // 60msバーを表示、点滅
    {
      ST7735_DrawLineX(tapbpmXY[0], tapbpmXY[0] + 36, tapbpmXY[1] + 10, ST7735_WHITE);
      ST7735_DrawLineX(tapbpmXY[0], tapbpmXY[0] + 36, tapbpmXY[1] + 11, ST7735_WHITE);
    }
    else
    {
      ST7735_DrawLineX(tapbpmXY[0], tapbpmXY[0] + 36, tapbpmXY[1] + 10, ST7735_BLACK);
      ST7735_DrawLineX(tapbpmXY[0], tapbpmXY[0] + 36, tapbpmXY[1] + 11, ST7735_BLACK);
    }
  }
  else
  {
    if (exOn) // EX機能 ON表示
    {
      ST7735_DrawLineX(tapmsXY[0]+32, tapmsXY[0] + 70, tapmsXY[1]-7, fxColor);
      ST7735_DrawLineX(tapmsXY[0]+31, tapmsXY[0] + 71, tapmsXY[1]-6, fxColor);
      ST7735_DrawLineX(tapmsXY[0]+31, tapmsXY[0] + 71, tapmsXY[1]-5, fxColor);
      ST7735_WriteString(tapmsXY[0]+31, tapmsXY[1]-4, " ON ", Font_11x18, ST7735_BLACK, fxColor);
      ST7735_DrawLineX(tapmsXY[0]+32, tapmsXY[0] + 70, tapmsXY[1]+14, fxColor);
    }
    else // EX機能 OFF表示
    {
      ST7735_DrawLineX(tapmsXY[0]+32, tapmsXY[0] + 70, tapmsXY[1]-7, ST7735_COLOR565(80, 80, 80));
      ST7735_DrawLineX(tapmsXY[0]+31, tapmsXY[0] + 71, tapmsXY[1]-6, ST7735_COLOR565(80, 80, 80));
      ST7735_DrawLineX(tapmsXY[0]+31, tapmsXY[0] + 71, tapmsXY[1]-5, ST7735_COLOR565(80, 80, 80));
      ST7735_WriteString(tapmsXY[0]+36, tapmsXY[1]-4, "OFF", Font_11x18, ST7735_BLACK, ST7735_COLOR565(80, 80, 80));
      ST7735_DrawLineX(tapmsXY[0]+32, tapmsXY[0] + 70, tapmsXY[1]+14, ST7735_COLOR565(80, 80, 80));
      for (int i = 0; i < 5; i++) ST7735_DrawLineY(tapmsXY[0]+31+i, tapmsXY[1]-4, tapmsXY[1]+13, ST7735_COLOR565(80, 80, 80));
      for (int i = 0; i < 5; i++) ST7735_DrawLineY(tapmsXY[0]+67+i, tapmsXY[1]-4, tapmsXY[1]+13, ST7735_COLOR565(80, 80, 80));
    }
  }

  // 出力クリップ表示------------------------------
  uint8_t detect = clipDetect[0] << 3 | clipDetect[1] << 2 | clipDetect[2] << 1 | clipDetect[3];
  if (detect == 0b00001000) statusStr = "L i    ";
  if (detect == 0b00000100) statusStr = " Ri    ";
  if (detect == 0b00000010) statusStr = "    L o";
  if (detect == 0b00000001) statusStr = "     Ro";
  if (detect == 0b00001100) statusStr = "LRi    ";
  if (detect == 0b00001010) statusStr = "L i L o";
  if (detect == 0b00001001) statusStr = "L i  Ro";
  if (detect == 0b00000110) statusStr = " Ri L o";
  if (detect == 0b00000101) statusStr = " Ri  Ro";
  if (detect == 0b00000011) statusStr = "    LRo";
  if (detect == 0b00001011) statusStr = "L i LRo";
  if (detect == 0b00001101) statusStr = "LRi  Ro";
  if (detect == 0b00001110) statusStr = "LRi L o";
  if (detect == 0b00000111) statusStr = " Ri LRo";
  if (detect == 0b00001111) statusStr = "LRi LRo";

  // データ保存-----------------------------
  if (saveDataFlag)
  {
    saveDataFlag = false;
    saveData();
    statusStr = "STORED!";
  }

  // LED表示------------------------------
  uint8_t r = (fxColor >> 8) & 0b0000000011111000; // RGB565を変換 PWMで色を制御する場合使えるかも
  uint8_t g = (fxColor >> 3) & 0b0000000011111100;
  uint8_t b = (fxColor << 3) & 0b0000000011111000;
  if (r && fxOn) HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_SET);
  else HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET);
  if (g && fxOn) HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_SET);
  else HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET);
  if (b && fxOn) HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_SET);
  else HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET);

  // ループ終了、ディスプレイ表示完了
  if (fxDispCpltFlag < 2) fxDispCpltFlag++;
}

void mute() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<エフェクト切替時のミュート
{
  for (uint16_t i = 0; i < BLOCK_SIZE*4; i++)
  {
    TxBuffer[i] = 0;
  }
}

void fxChange() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<エフェクト変更
{
  mute();
  fxDeinit();
  fxNum = (fxNumMax + fxNum + fxChangeFlag) % fxNumMax; // 最大値←→最小値で循環
  fxPage = 0;
  fxInit();
  fxChangeFlag = 0;
  fxDispCpltFlag = 0;
}

void swProcess(uint8_t num) // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<スイッチ処理
{
  static uint32_t swCount[10] = {}; // スイッチが押されている間カウントアップ

  switch(num)
  {
    case 0: // 左端スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW0_GPIO_Port, SW0_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          if (fxDispCpltFlag > 1) fxChangeFlag = -1; // 前エフェクトへ
        }
        swCount[num] = 0;
      }
      break;
    case 1: // 左中スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW1_GPIO_Port, SW1_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          if (fxDispCpltFlag > 1) fxChangeFlag = 1; // 次エフェクトへ
        }
        swCount[num] = 0;
      }
      break;
    case 2: // 右中スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW2_GPIO_Port, SW2_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          saveDataFlag = true; // データ保存
        }
        swCount[num] = 0;
      }
      break;
    case 3: // 右端スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW3_GPIO_Port, SW3_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          fxPage++; // ページ切替
          if (fxPage > (fxParamNumMax-1) / 4) fxPage = 0;
        }
        swCount[num] = 0;
      }
      break;
    case 4: // 左端上スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW6_GPIO_Port, SW6_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          // 処理なし
        }
        swCount[num] = 0;
      }
      break;
    case 5: // 左中上スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW7_GPIO_Port, SW7_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          // 処理なし
        }
        swCount[num] = 0;
      }
      break;
    case 6: // 右中上スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW8_GPIO_Port, SW8_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          // 処理なし
        }
        swCount[num] = 0;
      }
      break;
    case 7: // 右端上スイッチ --------------------------------------------------------
      if (!HAL_GPIO_ReadPin(SW9_GPIO_Port, SW9_Pin))
      {
        swCount[num]++;
        if (swCount[num] == longPushCount) // 長押し 1回のみ動作
        {
          // 処理なし
        }
      }
      else
      {
        if (swCount[num] >= shortPushCount && swCount[num] < longPushCount) // 短押し 離した時の処理
        {
          // 処理なし
        }
        swCount[num] = 0;
      }
      break;
    default:
      break;
  }

}

void footSwProcess0() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<左フットスイッチ処理
{
  static uint32_t footSwCount = 0; // スイッチが押されている間カウントアップ

  if (!HAL_GPIO_ReadPin(SW4_GPIO_Port, SW4_Pin))
  {
    footSwCount++;
    if (footSwCount == 8*longPushCount) // 長押し
    {
      // 処理なし
    }
    if (footSwCount == 24*longPushCount) // 3倍長押し
    {
      // 処理なし
    }
  }
  else
  {
    if (footSwCount >= 8*shortPushCount && footSwCount < 8*longPushCount) // 短押し 離した時の処理
    {
      fxOn = !fxOn; // エフェクト オン／オフ
    }
    footSwCount = 0;
  }

}

void footSwProcess1() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<右フットスイッチ処理
{
  static uint32_t footSwCount = 0; // スイッチが押されている間カウントアップ
  static float tmpTapTime = 0; // タップ間隔時間 一時保存用

  if (!HAL_GPIO_ReadPin(SW5_GPIO_Port, SW5_Pin))
  {
    footSwCount++;
    if (footSwCount == 8*shortPushCount) // スイッチを押した時のタップ間隔時間を記録
    {
      tmpTapTime = (float)callbackCount * i2sInterruptInterval * 1000.0f;
      callbackCount = 0;
    }
    if (footSwCount == 8*longPushCount) // 長押し
    {
      // 処理なし
    }
    if (footSwCount == 24*longPushCount) // 3倍長押し
    {
      // 処理なし
    }
  }
  else
  {
    if (footSwCount >= 8*shortPushCount && footSwCount < 8*longPushCount) // 短押し 離した時の処理
    {
      if (fxExFuncName == "TAP TEMPO")
      { // スイッチを押した時記録していたタップ間隔時間をスイッチを離した時に反映させる
        if (100.0f < tmpTapTime && tmpTapTime < MAX_TAP_TIME) tapTime = tmpTapTime;
        else tapTime = 0.0f;
      }
      else exOn = !exOn; // EX機能 オン／オフ
    }
    footSwCount = 0;
  }

}


uint8_t reState(GPIO_TypeDef* GPIOxA, uint16_t GPIO_PinA, GPIO_TypeDef* GPIOxB, uint16_t GPIO_PinB)
{ // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<ロータリーエンコーダの状態を読み取り、4パターンのいずれかで返す
  uint8_t a = HAL_GPIO_ReadPin(GPIOxA, GPIO_PinA);
  uint8_t b = HAL_GPIO_ReadPin(GPIOxB, GPIO_PinB);
  if      ( a &&  b) return 0;
  else if (!a &&  b) return 1;
  else if (!a && !b) return 2;
  else if ( a && !b) return 3;
  else return 4;
}

void reProcess(uint8_t num) // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<ロータリーエンコーダ回転時処理
{
  uint8_t add = 0; // パラメータ変更数値
  int8_t  reCW = 0; // ロータリーエンコーダ 時計回りで1 反時計回りで-1

  switch (last_re[num])
  {
    case 0:
      if (re[num] == 1) reCW = 1; // ロータリーエンコーダ C.W.
      else if (re[num] == 3) reCW = -1; // ロータリーエンコーダ C.C.W.
      else reCW = 0; // ロータリーエンコーダ 回転なし
      break;
    case 1:
      if (re[num] == 2) reCW = 0;
      else if (re[num] == 0) reCW = -0;
      else reCW = 0;
      break;
    case 2:
      if (re[num] == 3) reCW = 1;
      else if (re[num] == 1) reCW = -1;
      else reCW = 0;
      break;
    case 3:
      if (re[num] == 0)  reCW = 0;
      else if (re[num] == 2)  reCW = -0;
      else reCW = 0;
      break;
    default:
      break;
  }

  switch (num)
  {
    case 0: // ------------------------------------左端ロータリーエンコーダ
      if (HAL_GPIO_ReadPin(SW6_GPIO_Port, SW6_Pin)) add = 1;
      else add = 10; // スイッチを押しながら回転させるとパラメータを10ずつ変更
      break;
    case 1: // ------------------------------------左中ロータリーエンコーダ
      if (HAL_GPIO_ReadPin(SW7_GPIO_Port, SW7_Pin)) add = 1;
      else add = 10;
      break;
    case 2: // ------------------------------------右中ロータリーエンコーダ
      if (HAL_GPIO_ReadPin(SW8_GPIO_Port, SW8_Pin)) add = 1;
      else add = 10;
      break;
    case 3: // ------------------------------------右端ロータリーエンコーダ
      if (HAL_GPIO_ReadPin(SW9_GPIO_Port, SW9_Pin)) add = 1;
      else add = 10;
      break;
    default:
      break;
  }

  fxParam[4*fxPage + num] = clip(fxParam[4*fxPage + num] + add * reCW, fxParamMin[4*fxPage + num], fxParamMax[4*fxPage + num]);

  last_re[num] = re[num];
}


void mainProcess(uint16_t start_sample) // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<メイン信号処理等
{
  DWT->CYCCNT = 0; // CPU使用率計算用 CPUサイクル数をリセット

  float xL[BLOCK_SIZE] = {}; // Lch float計算用データ
  float xR[BLOCK_SIZE] = {}; // Rch float計算用データ

  // 入出力クリップ定義 リセット
  for (int i = 0; i < 4; i++) clipDetect[i] = 0;

  for (uint16_t i = 0; i < BLOCK_SIZE; i++)
  {
     uint16_t m = (start_sample + i) * 2; // データ配列の偶数添字計算 Lch

    // 受信データを計算用データ配列へ 値を-1～+1(float)へ変更
    xL[i] = (float)RxBuffer[m] / 2147483648.0f;
    xR[i] = (float)RxBuffer[m+1] / 2147483648.0f;

    // 入力クリップ検出
    if (xL[i] < -0.999f || xL[i] > 0.999f) clipDetect[0] = 1;
    if (xR[i] < -0.999f || xR[i] > 0.999f) clipDetect[1] = 1;
  }

  fxProcess(xL, xR); // エフェクト処理 計算用配列を渡す

  for (uint16_t i = 0; i < BLOCK_SIZE; i++)
  {
    // 出力クリップ検出とオーバーフロー防止
    if (xL[i] < -1.0f)
    {
      clipDetect[2] = 1;
      xL[i] = -1.0f;
    }
    if (xL[i] > 0.999f)
    {
      clipDetect[2] = 1;
      xL[i] = 0.999f;
    }
    if (xR[i] < -1.0f)
    {
      clipDetect[3] = 1;
      xR[i] = -1.0f;
    }
    if (xR[i] > 0.999f)
    {
      clipDetect[3] = 1;
      xR[i] = 0.999f;
    }

    uint16_t m = (start_sample + i) * 2; // データ配列の偶数添字計算 Lch

    // 計算済データを送信バッファへ 値を32ビット整数へ戻す
    TxBuffer[m] = (int32_t)(2147483648.0f * xL[i]);
    TxBuffer[m+1] = (int32_t)(2147483648.0f * xR[i]);
  }

  callbackCount++; // I2Sの割り込みごとにカウントアップ タイマとして利用
  footSwProcess0(); // 左フットスイッチ処理
  footSwProcess1(); // 右フットスイッチ処理
  swProcess(callbackCount % 8); // 割り込みごとにスイッチ処理するが、スイッチ1つずつを順番に行う
  cpuUsageCycleMax[fxNum] = max(cpuUsageCycleMax[fxNum], DWT->CYCCNT); // CPU使用率計算用
  if (fxChangeFlag) fxChange(); // エフェクト変更 ※ディレイメモリ確保前に信号処理に進まないように割り込み内で行う

}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<I2Sの受信バッファに半分データがたまったときの割り込み
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef* hsai_BlockA1)
{
  mainProcess(0); // 0 ～ 15 を処理(0 ～ BLOCK_SIZE-1)

  // ロータリーエンコーダの処理
  re[3] = reState(RE3A_GPIO_Port, RE3A_Pin, RE3B_GPIO_Port, RE3B_Pin);
  reProcess(3);
  re[1] = reState(RE1A_GPIO_Port, RE1A_Pin, RE1B_GPIO_Port, RE1B_Pin);
  reProcess(1);
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<I2Sの受信バッファに全データがたまったときの割り込み
void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef* hsai_BlockA1)
{
  mainProcess(BLOCK_SIZE); // 16 ～ 31 を処理(BLOCK_SIZE ～ BLOCK_SIZE*2-1)

  // ロータリーエンコーダの処理
  re[0] = reState(RE0A_GPIO_Port, RE0A_Pin, RE0B_GPIO_Port, RE0B_Pin);
  reProcess(0);
  re[2] = reState(RE2A_GPIO_Port, RE2A_Pin, RE2B_GPIO_Port, RE2B_Pin);
  reProcess(2);
}

// MIDI受信したときの割り込み
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  statusStr = "MIDI";
  HAL_UART_Receive_IT(&huart3, &midiRecievedBuf,1);
}
