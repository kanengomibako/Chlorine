#ifndef FX_CHORUS_HPP
#define FX_CHORUS_HPP

#include "common.h"
#include "lib_calc.hpp"
#include "lib_filter.hpp"
#include "lib_osc.hpp"

class fx_chorus : public fx_base
{
private:
  const string name = "CHORUS";
  const uint16_t color = COLOR_B; // 青
  const string paramName[20] = {"LEVEL", "MIX", "F.BACK", "RATE", "DEPTH", "TONE", "TAPDIV"};
  enum paramName {LEVEL, MIX, FBACK, RATE, DEPTH, TONE, TAPDIV};
  float param[20] = {1, 1, 1, 1, 1, 1, 1};
  const int16_t paramMax[20] = {100,100, 99,100,100,100,  5};
  const int16_t paramMin[20] = {  0,  0,  0,  0,  0,  0,  0};
  const uint8_t paramNumMax = 7;
  const string exFuncName = "TAP TEMPO";

  // タップテンポ DIV定数 0←→5で循環させ、実際使うのは1～4
  const string tapDivStr[6]  = {"1/1", "1/1", "1/2", "1/3", "3/4", "1/1"};
  const float tapDivFloat[6] = {1.0f, 1.0f, 0.5f, 0.333333f, 0.75f, 1.0f};

  signalSw bypass;
  sineWave sin1;
  delayBufF del1;
  hpf hpf1;
  lpf2nd lpf2nd1, lpf2nd2;

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

    del1.set(20.0f);  // 最大ディレイタイム設定
    hpf1.set(100.0f); // ディレイ音のローカット設定
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
        fxParamStr[LEVEL] = std::to_string(fxParam[LEVEL]);
        break;
      case 1:
        fxParamStr[MIX] = std::to_string(fxParam[MIX]);
        break;
      case 2:
        fxParamStr[FBACK] = std::to_string(fxParam[FBACK]);
        break;
      case 3:
        fxParamStr[RATE] = std::to_string(fxParam[RATE]);
        break;
      case 4:
        fxParamStr[DEPTH] = std::to_string(fxParam[DEPTH]);
        break;
      case 5:
        fxParamStr[TONE] = std::to_string(fxParam[TONE]);
        break;
      case 6:
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
        param[LEVEL] = logPot(fxParam[LEVEL], -20.0f, 20.0f); // LEVEL -20 ～ 20dB
        break;
      case 1:
        param[MIX] = mixPot(fxParam[MIX], -20.0f); // MIX
        break;
      case 2:
        param[FBACK] = (float)fxParam[FBACK] / 100.0f; // Feedback 0～99%
        break;
      case 3:
        if (divTapTime > 100.0f && divTapTime < 2100.0f) // タップテンポ設定時
        {
          fxParam[RATE] = (int16_t)(105.0f - 0.05f * divTapTime); // RATE 0～100に逆算
          param[RATE] = 0.001f * divTapTime;
        }
        else param[RATE] = 0.02f * (105.0f - (float)fxParam[RATE]); // RATE 周期 2.1～0.1 秒
        break;
      case 4:
        param[DEPTH] = 0.05f * (float)fxParam[DEPTH]; // Depth ±5ms
        break;
      case 5:
        param[TONE] = 800.0f * logPot(fxParam[TONE], 0.0f, 20.0f); // HI CUT FREQ 800 ～ 8000 Hz
        break;
      case 6:
        lpf2nd1.set(param[TONE]);
        break;
      case 7:
        lpf2nd2.set(param[TONE]);
        break;
      case 8:
        sin1.set(1.0f / param[RATE]);
        break;
      case 9:
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

      float dtime = 5.0f + param[DEPTH] * (1.0f + sin1.output()); // ディレイタイム 5～15ms
      fxL = del1.readLerp(dtime); // ディレイ音読込(線形補間)
      fxL = lpf2nd1.process(fxL); // ディレイ音のTONE(ハイカット)
      fxL = lpf2nd2.process(fxL);

      // ディレイ音と原音をディレイバッファに書込、原音はローカットして書込
      del1.write(param[FBACK] * fxL + hpf1.process(xL[i]));

      fxL = (1.0f - param[MIX]) * xL[i] + param[MIX] * fxL; // MIX
      fxL = 1.4f * param[LEVEL] * fxL; // LEVEL

      xL[i] = bypass.process(xL[i], fxL, fxOn);
    }
  }

};

#endif // FX_CHORUS_HPP
