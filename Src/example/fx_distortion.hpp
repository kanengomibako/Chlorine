#ifndef FX_DISTORTION_HPP
#define FX_DISTORTION_HPP

#include "common.h"
#include "lib_calc.hpp"
#include "lib_filter.hpp"
#include "lib_sampling.hpp"

class fx_distortion : public fx_base
{
private:
  const string name = "DISTORTION";
  const uint16_t color = COLOR_R; // 赤
  const string paramName[20] = {"LEVEL", "GAIN", "TONE", "BOOST"};
  enum paramName {LEVEL, GAIN, TONE, BOOST};
  float param[20] = {1, 1, 1, 1};
  const int16_t paramMax[20] = {100,100,100,100};
  const int16_t paramMin[20] = {  0,  0,  0,  0};
  const uint8_t paramNumMax = 4;
  const string exFuncName = "BOOST";

  signalSw bypass, boost;
  hpf hpf1, hpf2, hpfTone; // ローカット1・2、トーン調整用
  lpf lpf1, lpf2, lpfTone; // ハイカット1・2、トーン調整用
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
        fxParamStr[TONE] = std::to_string(fxParam[TONE]);
        break;
      case 3:
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
        param[LEVEL] = logPot(fxParam[LEVEL], -50.0f, 0.0f);  // LEVEL -50...0 dB
        break;
      case 1:
        param[GAIN] = logPot(fxParam[GAIN], 5.0f, 45.0f); // GAIN 5...+45 dB
        break;
      case 2:
        param[TONE] = mixPot(fxParam[TONE], -20.0f); // TONE 0～1 LPF側とHPF側をミックス
        break;
      case 3:
        lpf1.set(5000.0f); // ハイカット1 固定値
        break;
      case 4:
        lpf2.set(600.0f); // ハイカット2 固定値 ※8倍オーバーサンプリング内 実質4800Hz
        break;
      case 5:
        lpfTone.set(240.0f); // TONE用ハイカット 固定値
        break;
      case 6:
        hpf1.set(40.0f); // ローカット1 固定値
        break;
      case 7:
        hpf2.set(5.0f); // ローカット2 固定値 ※8倍オーバーサンプリング内 実質20Hz
        break;
      case 8:
        hpfTone.set(1000.0f); // TONE用ローカット 固定値
        break;
      case 9:
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
      fxL = hpf1.process(fxL); // ローカット1
      fxL = lpf1.process(fxL); // ハイカット1
      fxL = 10.0f * fxL; // 1段目固定ゲイン
      up.process(fxL, upArray, 8 * BLOCK_SIZE); // 8倍アップサンプリングした配列作成
    }

    // アップサンプリングした配列を処理
    for (uint16_t i = 0; i < 8 * BLOCK_SIZE; i++)
    {
      float x = upArray[i];
      if (x < -0.5f) x = -0.25f; // 2次関数による波形の非対称変形
      else x = x * x + x;
      x = hpf2.process(x); // ローカット2 直流カット
      x = lpf2.process(x); // ハイカット2
      x = param[GAIN] * x; // GAIN
      x = tanhf(x); // tanhによる対称クリッピング
      down.process(x, downArray, 8 * BLOCK_SIZE); // ダウンサンプリングした配列作成
    }

    for (uint16_t i = 0; i < BLOCK_SIZE; i++)
    {
      fxL = downArray[i];
      fxL = param[TONE] * hpfTone.process(fxL)        // TONE
          + (1.0f - param[TONE]) * lpfTone.process(fxL); // LPF側とHPF側をミックス

      fxL = param[LEVEL] * fxL; // LEVEL
      fxL = boost.process(fxL, param[BOOST] * fxL, exOn); // BOOST

      xL[i] = bypass.process(xL[i], fxL, fxOn);
    }
  }

};

#endif // FX_DISTORTION_HPP
