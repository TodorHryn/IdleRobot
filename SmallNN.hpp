#pragma once

#define MAX(a, b, c) ((a > b && a > c) ? a : (b > c ? b : c))

template<int L0, int L1, int L2>
class SmallNN
{
public:
  float toFloat(uint8_t val)
  {
    return static_cast<float>(val) / UINT8_MAX * (maxv - minv) + minv;
  }

  void runLayer(uint8_t size, uint8_t prevSize, uint8_t *w, uint8_t *a)
  {
    for (uint8_t j = 0; j < size; ++j)
    {
      float sum = toFloat(a[j]);
      for (uint8_t k = 0; k < prevSize; ++k)
        sum += inputs[k] * toFloat(w[j * prevSize + k]);
      outputs[j] = tanh(sum);
    }

    memcpy(inputs, outputs, sizeof(inputs));
  }

  void run()
  {
    runLayer(L1, L0, &w1[0][0], a1);
    runLayer(L2, L1, &w2[0][0], a2);
  }
  
  float minv = 1e9, maxv = -1e9;
  float inputs[MAX(L0, L1, L2)], outputs[MAX(L0, L1, L2)];
  uint8_t w1[L1][L0];
  uint8_t w2[L2][L1];
  uint8_t a1[L1];
  uint8_t a2[L2];
};
