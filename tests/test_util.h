// utilitários comuns aos testes do LABLOUD
#pragma once
#include "DSP/LoudnexDsp.h"
#include <cstdio>
#include <random>
#include <vector>
using namespace lx::dsp;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("FALHOU  "); std::printf (__VA_ARGS__); std::printf ("   (linha %d)\n", __LINE__); } else { std::printf ("ok      "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::vector<float> sineV (double fs, double f, double amp, int n, double ph = 0)
{ std::vector<float> v ((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = (float) (amp * std::sin (2 * kPi * f * i / fs + ph)); return v; }
static double peakAbs (const std::vector<float>& v, int from = 0) { double p = 0; for (size_t i = (size_t) from; i < v.size(); ++i) p = std::max (p, (double) std::fabs (v[i])); return p; }
static double rmsOf (const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) v[(size_t) i] * v[(size_t) i]; return std::sqrt (s / std::max (1, b - a)); }

// pico verdadeiro com medidor independente (8x, 64 taps)
static double truePeak8xV (const std::vector<float>& x, int from = 0)
{
    const int H = 32; double peak = 0; for (size_t i = (size_t) from; i < x.size(); ++i) peak = std::max (peak, (double) std::fabs (x[i]));
    std::vector<double> h[8];
    for (int k = 1; k < 8; ++k) { const double f = k / 8.0; h[k].resize (2 * H); double s = 0;
        for (int j = 0; j < 2 * H; ++j) { const double t = (j - (H - 1)) - f, u = t / H; const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            h[k][(size_t) j] = sinc * (0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2 * kPi * u)); s += h[k][(size_t) j]; }
        for (auto& c : h[k]) c /= s; }
    for (int n = from + H; n < (int) x.size() - H - 1; ++n) for (int k = 1; k < 8; ++k)
    { double y = 0; for (int j = 0; j < 2 * H; ++j) y += h[k][(size_t) j] * x[(size_t) (n - (H - 1) + j)]; peak = std::max (peak, std::fabs (y)); }
    return peak;
}
// processa em blocos de 512 (como um anfitrião)
static void runLimiter (Limiter& L, std::vector<float>& a, std::vector<float>* b = nullptr)
{
    const int n = (int) a.size(); const int nc = b ? 2 : 1;
    for (int i = 0; i < n; i += 512) { const int m = std::min (512, n - i); float* io[2] = { a.data() + i, b ? b->data() + i : nullptr }; L.process (io, nc, m); }
}
// kick com queda de pitch + corpo; "click" é o pico inicial acima do teto
static std::vector<float> kickTrain (double fs, double seconds, double peakAmp, double bodyAmp, double bpm = 124.0)
{
    const int n = (int) (fs * seconds); std::vector<float> v ((size_t) n, 0.f); const int beat = (int) (fs * 60.0 / bpm);
    for (int s = 0; s < n; s += beat) { double ph = 0;
        for (int i = 0; i < (int) (0.25 * fs) && s + i < n; ++i) { const double t = i / fs, f = 45 + 110 * std::exp (-t * 30); ph += 2 * kPi * f / fs;
            const double env = std::exp (-t * 9) * bodyAmp + (i < (int) (0.003 * fs) ? peakAmp * (1.0 - i / (0.003 * fs)) : 0.0); v[(size_t) (s + i)] += (float) (env * std::sin (ph)); } }
    return v;
}
