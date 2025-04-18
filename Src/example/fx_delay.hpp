#ifndef FX_DELAY_HPP
#define FX_DELAY_HPP

#include "common.h"
#include "lib_calc.hpp"
#include "lib_filter.hpp"
#include "lib_delay.hpp"

class fx_delay : public fx_base
{
private:
  const string name = "DELAY";
  const uint16_t color = COLOR_RB; // 赤青
  const string paramName[20] = {"TIM", "E.LV", "F.BACK", "TONE", "TRAIL", "TAPDIV"};
  enum paramName {DTIME, ELEVEL, FBACK, TONE, TRAIL, TAPDIV};
  float param[20] = {1, 1, 1, 1, 1, 1};
  const int16_t paramMax[20] = {1500,100, 99,100,  1,  5};
  const int16_t paramMin[20] = {  10,  0,  0,  0,  0,  0};
  const uint8_t paramNumMax = 6;
  const string exFuncName = "TAP TEMPO";

  // 最大ディレイタイム 16bit モノラルで2.5秒程度まで
  const float maxDelayTime = 1500.0f;

  // タップテンポ DIV定数 0←→5で循環させ、実際使うのは1～4
  const string tapDivStr[6]  = {"1/1", "1/1", "1/2", "1/3", "3/4", "1/1"};
  const float tapDivFloat[6] = {1.0f, 1.0f, 0.5f, 0.333333f, 0.75f, 1.0f};

  signalSw bypassIn, bypassOut;
  delayBuf del1;
  lpf2nd lpf2ndTone;

public:
  void init() override
  {
    fxName = name;
    fxColor = color;
    fxExFuncName = exFuncName;
    fxParamNumMax = paramNumMax;
    for (int i = 0; i < 20; i++)
    {
      fxParamName[i] = paramName[i];
      fxParamMax[i] = paramMax[i];
      fxParamMin[i] = paramMin[i];
      if (fxAllData[fxNum][i] < paramMin[i] || fxAllData[fxNum][i] > paramMax[i]) fxParam[i] = paramMin[i];
      else fxParam[i] = fxAllData[fxNum][i];
    }

    del1.set(maxDelayTime); // 最大ディレイタイム分のメモリ確保
  }

  void deinit() override
  {
    del1.erase();
  }

  void setParamStr(uint8_t paramNum) override
  {
    switch(paramNum)
    {
      case 0:
        fxParamStr[DTIME] = std::to_string(fxParam[DTIME]);
        break;
      case 1:
        fxParamStr[ELEVEL] = std::to_string(fxParam[ELEVEL]);
        break;
      case 2:
        fxParamStr[FBACK] = std::to_string(fxParam[FBACK]);
        break;
      case 3:
        fxParamStr[TONE] = std::to_string(fxParam[TONE]);
        break;
      case 4:
        if (fxParam[TRAIL]) fxParamStr[TRAIL] = "ON";
        else fxParamStr[TRAIL] = "OFF";
        break;
      case 5:
        fxParamStr[TAPDIV] = tapDivStr[fxParam[TAPDIV]];
        break;
      default:
        fxParamStr[paramNum] = "";
        break;
    }
  }

  void setParam() override
  {
    float divTapTime = tapTime * tapDivFloat[fxParam[TAPDIV]]; // DIV計算済タップ時間
    static uint8_t count = 0;
    count = (count + 1) % 10; // 負荷軽減のためパラメータ計算を分散させる
    switch(count)
    {
      case 0:
        if (divTapTime > 10.0f && divTapTime < maxDelayTime) // タップテンポ設定時
        {
          fxParam[DTIME] = (int16_t)divTapTime;
        }
        param[DTIME] = (float)fxParam[DTIME]; // DELAYTIME 10 ～ 1500 ms
        break;
      case 1:
        param[ELEVEL] = logPot(fxParam[ELEVEL], -20.0f, 20.0f); // EFFECT LEVEL -20 ～ +20dB
        break;
      case 2:
        param[FBACK] = (float)fxParam[FBACK] / 100.0f; // Feedback 0 ～ 99 %
        break;
      case 3:
        param[TONE] = 800.0f * logPot(fxParam[TONE], 0.0f, 20.0f); // HI CUT FREQ 800 ～ 8000 Hz
        break;
      case 4:
        param[TRAIL] = (float)fxParam[TRAIL]; // TRAIL ディレイ音を残す機能 ON OFF
        break;
      case 5:
        lpf2ndTone.set(param[TONE]);
        break;
      case 6:
        if (fxParam[TAPDIV] < 1) fxParam[TAPDIV] = 4; // TAPDIV 0←→5で循環させ、実際使うのは1～4
        if (fxParam[TAPDIV] > 4) fxParam[TAPDIV] = 1;
        break;
      default:
        break;
    }
  }

  void process(float xL[], float xR[]) override
  {
    setParam();

    for (uint16_t i = 0; i < BLOCK_SIZE; i++)
    {
      float fxL;

      fxL = del1.read(param[DTIME]); // ディレイ音読込
      fxL = lpf2ndTone.process(fxL); // ディレイ音のTONE（ハイカット）

      // ディレイ音と原音をディレイバッファに書込、原音はエフェクトオン時のみ書込
      del1.write(param[FBACK] * fxL + bypassIn.process(0.0f, xL[i], fxOn));

      fxL = param[ELEVEL] * fxL; // ディレイ音レベル

      xL[i] = xL[i] + bypassOut.process(param[TRAIL] * fxL, fxL, fxOn); // TRAIL ON時ディレイ音残す
    }
  }
};

#endif // FX_DELAY_HPP
