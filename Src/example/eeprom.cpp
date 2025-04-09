#include "main.h"
#include "common.h"
#include "eeprom.hpp"

extern I2C_HandleTypeDef hi2c2;
extern int8_t brightnessNum;

const uint8_t devAddress = 0b10100000; // EEPROM アドレス
const uint8_t dataAddress = 0; // データ保存先 初期アドレス

void loadData() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<データ読み込み
{
  uint32_t addr = dataAddress; // データ保存先アドレス
  int16_t receiveData[4+fxNumMax*20] = {}; // 受信データ 全エフェクト分
  uint8_t dataCount = 0; // receiveDataの添字用

  // EEPROMから全データ読込
  HAL_I2C_Mem_Read(&hi2c2, devAddress, addr, I2C_MEMADD_SIZE_16BIT, (uint8_t*)receiveData, sizeof(receiveData), 1000);

  fxNum = receiveData[0]; // 保存時のエフェクト読込
  if (fxNum > fxNumMax - 1) fxNum = 0;
  fxOn = receiveData[1];  // エフェクトオンオフ状態読込
  exOn = receiveData[2];  // EX機能オンオフ状態読込
  brightnessNum = receiveData[3]; // LCD明るさ読込
  dataCount = 4;

  // エフェクトデータ読込
  for (int i = 0; i < fxNumMax; i++)
  {
    for (int j = 0; j < 20; j++)
    {
      fxAllData[i][j] = receiveData[dataCount];
      dataCount++;
      if (dataCount == 4+fxNumMax*20) return; // 配列の範囲外にならないか念のためチェック
    }
  }

}

void saveData() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<データ保存
{
  HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin); // 処理開始 赤LED点灯切替

  uint32_t addr = dataAddress; // データ保存先アドレス
  int16_t sendData[16] = {}; // EEPROM書き込み用256ビット配列
  uint8_t dataCount = 0; // sendDataの添字用

  for (uint16_t j = 0; j < 20; j++) // 現在のパラメータを保存用配列へ移す
  {
    fxAllData[fxNum][j] = fxParam[j];
  }

  sendData[0] = fxNum; // 現在のエフェクトを記録
  sendData[1] = fxOn;  // エフェクトオンオフ状態を記録
  sendData[2] = exOn;  // EX機能オンオフ状態を記録
  sendData[3] = brightnessNum; // LCD明るさを記録
  dataCount = 4;

  for (int i = 0; i < fxNumMax; i++) // EEPROM書込 256ビットずつ
  {
    for (int j = 0; j < 20; j++)
    {
      sendData[dataCount] = fxAllData[i][j];
      dataCount++;
      if (dataCount == 16 || (i == fxNumMax - 1 && j == 19)) // データが16個たまったとき または最後のデータ
      {
        HAL_I2C_Mem_Write(&hi2c2, devAddress, addr, I2C_MEMADD_SIZE_16BIT, (uint8_t*)sendData, sizeof(sendData), 1000);
        HAL_Delay(5);
        addr += 32;
        dataCount = 0;
      }
    }
  }

  HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin); // 処理終了 赤LED点灯切替
}

void eraseData() // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<データ全消去(0を保存)
{
  HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin); // 処理開始 赤LED点灯切替

  uint32_t addr = dataAddress; // データ保存先アドレス
  int16_t sendData[16] = {}; // EEPROM書き込み用256ビット配列
  uint8_t dataCount = 0; // tmpArrayの添字用

  sendData[0] = 0; // 現在のエフェクト
  sendData[1] = 0;  // エフェクトオンオフ状態
  sendData[2] = 0;  // EX機能オンオフ状態
  sendData[3] = 10; // LCD明るさ
  dataCount = 4;

  for (int i = 0; i < fxNumMax; i++) // EEPROM書込 256ビットずつ
  {
    for (int j = 0; j < 20; j++)
    {
      sendData[dataCount] = 0;
      dataCount++;
      if (dataCount == 16 || (i == fxNumMax - 1 && j == 19)) // データが16個たまったとき または最後のデータ
      {
        HAL_I2C_Mem_Write(&hi2c2, devAddress, addr, I2C_MEMADD_SIZE_16BIT, (uint8_t*)sendData, sizeof(sendData), 1000);
        HAL_Delay(5);
        addr += 32;
        dataCount = 0;
      }
    }
  }

  HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin); // 処理終了 赤LED点灯切替
}
