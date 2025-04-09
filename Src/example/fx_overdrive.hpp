#ifndef FX_OVERDRIVE_HPP
#define FX_OVERDRIVE_HPP

#include "common.h"
#include "lib_calc.hpp"
#include "lib_filter.hpp"
#include "lib_sampling.hpp"

class fx_overdrive : public fx_base
{
private:
  const string name = "OVERDRIVE";
  const uint16_t color = COLOR_RG; // 赤緑
  const string paramName[20] = {"LEVEL", "GAIN", "TREBLE", "BASS", "BOOST"};
  enum paramName {LEVEL, GAIN, TREBLE, BASS, BOOST};
  float param[20] = {1, 1, 1, 1, 1};
  const int16_t paramMax[20] = {100,100,100,100,100};
  const int16_t paramMin[20] = {  0,  0,  0,  0,  0};
  const uint8_t paramNumMax = 5;
  const string exFuncName = "BOOST";

  signalSw bypass, boost;
  hpf hpfFixed, hpfBass; // 出力ローカット、入力BASS調整
  lpf lpfFixed, lpfTreble; // 入力ハイカット、出力TREBLE調整
  upsampling up;
  downsampling down;
  float upArray[8*BLOCK_SIZE]; // 8倍分のアップサンプリング用配列
  float downArray[BLOCK_SIZE]; // ダウンサンプリング用配列

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

    up.set(8, 20000.0f); // アップサンプリングのLPF設定
    down.set(8, 20000.0f); // ダウンサンプリングのLPF設定
  }

  void deinit() override
  {
  }

  void setParamStr(uint8_t paramNum) override
  {
    switch(paramNum)
    {
      case 0:
        fxParamStr[LEVEL] = std::to_string(fxParam[LEVEL]);
        break;
      case 1:
        fxParamStr[GAIN] = std::to_string(fxParam[GAIN]);
        break;
      case 2:
        fxParamStr[TREBLE] = std::to_string(fxParam[TREBLE]);
        break;
      case 3:
        fxParamStr[BASS] = std::to_string(fxParam[BASS]);
        break;
      case 4:
        fxParamStr[BOOST] = std::to_string(fxParam[BOOST]);
        break;
      default:
        fxParamStr[paramNum] = "";
        break;
    }
  }

  void setParam() override
  {
    static uint8_t count = 0;
    count = (count + 1) % 10; // 負荷軽減のためパラメータ計算を分散させる
    switch(count)
    {
      case 0:
        param[LEVEL] = logPot(fxParam[LEVEL], -50.0f, 0.0f);  // LEVEL -50～0 dB
        break;
      case 1:
        param[GAIN] = logPot(fxParam[GAIN], 20.0f, 60.0f); // GAIN 20～60 dB
        break;
      case 2:
        param[TREBLE] = 10000.0f * logPot(fxParam[TREBLE], -30.0f, 0.0f); // TREBLE LPF 320～10k Hz
        break;
      case 3:
        param[BASS] = 2000.0f * logPot(fxParam[BASS], 0.0f, -20.0f); // BASS HPF 200～2000 Hz
        break;
      case 4:
        lpfTreble.set(param[TREBLE]);
        break;
      case 5:
        hpfBass.set(param[BASS]);
        break;
      case 6:
        lpfFixed.set(4000.0f); // 入力ハイカット 固定値
        break;
      case 7:
        hpfFixed.set(30.0f); // 出力ローカット 固定値
        break;
      case 8:
        param[BOOST] = logPot(fxParam[BOOST], 0.0f, 12.0f);  // BOOST 0～12 dB
        break;
      default:
        break;
    }
  }

  void process(float xL[], float xR[]) override
  {
    setParam();
    float fxL;

    for (uint16_t i = 0; i < BLOCK_SIZE; i++)
    {
      fxL = xL[i];
      fxL = hpfBass.process(fxL);   // 入力ローカット BASS
      fxL = lpfFixed.process(fxL);  // 入力ハイカット 固定値
      fxL = param[GAIN] * fxL;      // GAIN
      up.process(fxL, upArray, 8 * BLOCK_SIZE); // 8倍アップサンプリングした配列作成
    }

    // アップサンプリングした配列を処理
    for (uint16_t i = 0; i < 8 * BLOCK_SIZE; i++)
    {
      float x = upArray[i];
      x = atanf(x + 0.5f);      // arctanによるクリッピング、非対称化
      down.process(x, downArray, 8 * BLOCK_SIZE); // ダウンサンプリングした配列作成
    }

    for (uint16_t i = 0; i < BLOCK_SIZE; i++)
    {
      fxL = downArray[i];
      fxL = hpfFixed.process(fxL);  // 出力ローカット 固定値 直流カット
      fxL = lpfTreble.process(fxL); // 出力ハイカット TREBLE
      fxL = param[LEVEL] * fxL;     // LEVEL
      fxL = boost.process(fxL, param[BOOST] * fxL, exOn); // BOOST

      xL[i] = bypass.process(xL[i], fxL, fxOn);
    }
  }

};

#endif // FX_OVERDRIVE_HPP
