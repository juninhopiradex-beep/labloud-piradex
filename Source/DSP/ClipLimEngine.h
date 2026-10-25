// =============================================================================
//  LABLOUD by Piradex — motor auxiliar (C++17 puro, sem JUCE)
//  Crossover LR4 de 3 bandas + clipper multibanda, oversampler de meia-banda (até 32x),
//  analisador de harmónicos por FFT (THD, THD+N, componentes não-harmónicas) e perfis de carácter.
//  Nada aqui aloca memória em process(); prepare() é a única fase que aloca.
// =============================================================================
#pragma once
#include "LoudnexDsp.h"
#include <array>
#include <complex>
#include <vector>

namespace lx::dsp
{
// ---- Biquad Butterworth e crossover Linkwitz-Riley de 4.ª ordem ---------------------------------------
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
    void design (double fc, double fs, bool highpass)
    {
        fc = clampd (fc, 10.0, 0.45 * fs);
        const double w0 = 2.0 * kPi * fc / fs, cw = std::cos (w0), sw = std::sin (w0), alpha = sw / (2.0 * 0.70710678118654752);
        const double a0 = 1.0 + alpha;
        if (highpass) { b0 = (1 + cw) / 2 / a0; b1 = -(1 + cw) / a0; b2 = b0; }
        else          { b0 = (1 - cw) / 2 / a0; b1 = (1 - cw) / a0;  b2 = b0; }
        a1 = -2.0 * cw / a0; a2 = (1.0 - alpha) / a0;
    }
};
struct LR4
{
    Biquad lp[2], hp[2];
    void set (double fc, double fs) { for (auto& b : lp) b.design (fc, fs, false); for (auto& b : hp) b.design (fc, fs, true); }
    void reset() { for (auto& b : lp) b.reset(); for (auto& b : hp) b.reset(); }
    void split (double x, double& lo, double& hi) { lo = lp[1].process (lp[0].process (x)); hi = hp[1].process (hp[0].process (x)); }
};
// 3 bandas cuja soma é passa-tudo (módulo plano): a banda grave passa também pelo passa-tudo do 2.º cruzamento
struct Crossover3
{
    LR4 x1, x2, x2c;
    void set (double f1, double f2, double fs) { f2 = std::max (f2, f1 * 1.5); x1.set (f1, fs); x2.set (f2, fs); x2c.set (f2, fs); }
    void reset() { x1.reset(); x2.reset(); x2c.reset(); }
    void split (double x, double& lo, double& mid, double& hi)
    {
        double l, rest, l1, l2; x1.split (x, l, rest); x2.split (rest, mid, hi); x2c.split (l, l1, l2); lo = l1 + l2;
    }
};

// ---- Clipper multibanda -------------------------------------------------------------------------------
struct MultibandParams
{
    double xo1 = 150.0, xo2 = 2500.0;
    double bandGainDb[3] = { 0, 0, 0 };       // ganho extra por banda (antes do clip)
    double bandCeilOffDb[3] = { 0, 0, 0 };    // <= 0: teto de cada banda abaixo do Ceiling global
    ClipperParams base;
};
class MultibandClipper
{
public:
    void prepare (double fs) { sr = fs; for (auto& c : band) c.prepare (fs); xo.reset(); dcX = dcY = 0; dcR = std::exp (-2.0 * kPi * 5.0 / fs); setParams (MultibandParams()); settle(); }
    void reset() { xo.reset(); for (auto& c : band) c.reset(); dcX = dcY = 0; }
    void setParams (const MultibandParams& p)
    {
        mp = p; xo.set (p.xo1, p.xo2, sr);
        for (int b = 0; b < 3; ++b)
        {
            ClipperParams c = p.base; c.gainDb = p.base.gainDb + p.bandGainDb[b]; c.ceilingDb = p.base.ceilingDb + std::min (0.0, p.bandCeilOffDb[b]);
            c.dcBlock = false; band[(size_t) b].setParams (c);
        }
        lim = dbToLin (clampd (p.base.ceilingDb, -12.0, 12.0)) * dbToLin (clampd (p.base.postGainDb, -12.0, 0.0));
    }
    void settle() { for (auto& c : band) c.settle(); }
    float process (float xin)
    {
        double lo, mid, hi; xo.split (sanitize ((double) xin), lo, mid, hi);
        double y = (double) band[0].process ((float) lo) + (double) band[1].process ((float) mid) + (double) band[2].process ((float) hi);
        if (mp.base.dcBlock) { const double o = y - dcX + dcR * dcY; dcX = y; dcY = o; y = o; }
        return (float) clampd (y, -lim, lim);     // segurança a jusante: a soma das bandas nunca passa o teto
    }
    ClipStats takeStats() { ClipStats s; for (auto& c : band) s.merge (c.takeStats()); return s; }
private:
    MultibandParams mp; Crossover3 xo; std::array<Clipper, 3> band; double sr = 48000.0, dcX = 0, dcY = 0, dcR = 0.9999, lim = 0.89;
};

// ---- Oversampler de meia-banda em cascata (1x..32x), 65 taps por estágio -----------------------------------
class HalfbandOversampler
{
public:
    static constexpr int kT = 65, kH = 32, kMaxLatency = 62;   // latência do 32x (estágios 5)
    void prepare (int stagesN)        // 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x, 4 = 16x, 5 = 32x
    {
        stages = std::min (5, std::max (0, stagesN));
        double h[kT], s = 0;
        for (int i = 0; i < kT; ++i)
        {
            const int n = i - kH; const double w = 0.42 + 0.5 * std::cos (kPi * n / (kH + 1)) + 0.08 * std::cos (2 * kPi * n / (kH + 1));
            h[i] = (n == 0 ? 0.5 : (n % 2 == 0 ? 0.0 : 0.5 * std::sin (kPi * n / 2.0) / (kPi * n / 2.0))) * w; s += h[i];
        }
        for (int i = 0; i < kT; ++i) coef[i] = h[i] / s;   // ganho DC unitário; a subida multiplica por 2 (zero-stuffing)
        for (int j = 0; j < 16; ++j) odd[j] = coef[2 * j + 1];
        for (auto& st : up) st.reset ();
        for (auto& st : dn) st.reset ();
    }
    int stagesCount() const { return stages; }
    int factor() const { return 1 << stages; }
    int latencySamples() const { int l = 0; for (int i = 0; i < stages; ++i) l += (kT - 1) >> (i + 1); return l; }   // em amostras de entrada
    template <class F> float process (float x, F&& f) { return (float) run ((double) x, 0, f); }
private:
    // Subida polifásica: a fase alinhada é só um atraso (tap central) e a outra é um FIR simétrico de 32 taps no ritmo de entrada (16 multiplicações)
    struct UpStage { double h[64] = {}; int pos = 0; void reset() { for (auto& v : h) v = 0; pos = 0; }
        void push (double x, const double* co, double c32, double& a, double& b)
        { pos = pos == 0 ? 31 : pos - 1; h[pos] = h[pos + 32] = x; const double* p = h + pos;
          a = 2.0 * c32 * p[16]; double s0 = 0, s1 = 0, s2 = 0, s3 = 0;   // 4 acumuladores independentes: a soma deixa de ser uma cadeia serial
          for (int j = 0; j < 16; j += 4) { s0 += co[j] * (p[j] + p[31 - j]); s1 += co[j + 1] * (p[j + 1] + p[30 - j]); s2 += co[j + 2] * (p[j + 2] + p[29 - j]); s3 += co[j + 3] * (p[j + 3] + p[28 - j]); }
          b = 2.0 * ((s0 + s1) + (s2 + s3)); } };
    // Decimação: escreve as duas amostras, mas só calcula o FIR (17 multiplicações) para a amostra que se mantém
    struct DownStage { double h[2 * kT] = {}; int pos = 0; void reset() { for (auto& v : h) v = 0; pos = 0; }
        void write (double x) { pos = pos == 0 ? kT - 1 : pos - 1; h[pos] = h[pos + kT] = x; }
        double compute (const double* c) const { const double* p = h + pos; double s0 = c[kH] * p[kH], s1 = 0, s2 = 0, s3 = 0;
            for (int i = 1; i < kH; i += 8) { s0 += c[i] * (p[i] + p[2 * kH - i]); s1 += c[i + 2] * (p[i + 2] + p[2 * kH - i - 2]); s2 += c[i + 4] * (p[i + 4] + p[2 * kH - i - 4]); s3 += c[i + 6] * (p[i + 6] + p[2 * kH - i - 6]); }
            return (s0 + s1) + (s2 + s3); } };
    template <class F> double run (double x, int level, F& f)
    {
        if (level >= stages) return (double) f ((float) x);
        double a, b; up[(size_t) level].push (x, odd, coef[kH], a, b);
        const double ya = run (a, level + 1, f), yb = run (b, level + 1, f);
        auto& d = dn[(size_t) level]; d.write (ya); const double out = d.compute (coef); d.write (yb); return out;   // fase alinhada com 'ya' (latência inteira)
    }
    int stages = 0; double coef[kT] = {}, odd[16] = {}; std::array<UpStage, 6> up; std::array<DownStage, 6> dn;   // 6 (e não 5): o nível 5 nunca é usado, mas evita o falso positivo -Warray-bounds da recursão expandida
};

// ---- Analisador de harmónicos (FFT radix-2 própria) ---------------------------------------------------------
inline void fftInPlace (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap (a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len; const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len) { std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) { const auto u = a[i + k], v = a[i + k + len / 2] * w; a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl; } }
    }
}
struct HarmonicReport
{
    double fundDb = -200, thdPct = 0, thdDb = -200, thdnDb = -200, nonHarmDb = -200;   // relativos à fundamental
    double hDb[6] = { 0, -200, -200, -200, -200, -200 };                              // hDb[k-1] = harmónico k relativo à fundamental
};
// n potência de 2; ideal: f0 coerente com a janela (f0 = k * fs / n). "nonHarm" inclui aliasing mas também harmónicos
// rebatidos que caiam fora das posições harmónicas — é uma ESTIMATIVA, não uma separação exata.
inline HarmonicReport analyzeHarmonics (const float* x, int n, double fs, double f0)
{
    HarmonicReport r; std::vector<std::complex<double>> a ((size_t) n);
    for (int i = 0; i < n; ++i) a[(size_t) i] = { (double) x[i] * (0.5 - 0.5 * std::cos (2.0 * kPi * i / n)), 0.0 };
    fftInPlace (a);
    const int half = n / 2; std::vector<double> p ((size_t) half + 1);
    for (int k = 0; k <= half; ++k) p[(size_t) k] = std::norm (a[(size_t) k]);
    auto band = [&] (int c) { double s = 0; for (int k = std::max (4, c - 3); k <= std::min (half, c + 3); ++k) s += p[(size_t) k]; return s; };
    const int k0 = (int) std::lround (f0 * n / fs); const double fund = band (k0);
    if (fund <= 0) return r;
    double harm = 0, total = 0; for (int k = 4; k <= half; ++k) total += p[(size_t) k];
    for (int h = 2; h * k0 + 3 < half; ++h) { const double ph = band (h * k0); harm += ph; if (h <= 6) r.hDb[h - 1] = 10 * std::log10 (ph / fund + 1e-20); }
    r.fundDb = 10 * std::log10 (fund);
    r.thdPct = 100.0 * std::sqrt (harm / fund); r.thdDb = 10 * std::log10 (harm / fund + 1e-20);
    r.thdnDb = 10 * std::log10 (std::max (total - fund, 0.0) / fund + 1e-20);
    r.nonHarmDb = 10 * std::log10 (std::max (total - fund - harm, 0.0) / fund + 1e-20);
    return r;
}

// ---- Perfis de carácter harmónico (PDF §6) -------------------------------------------------------------------
enum class HarmChar { Clean = 0, Warm = 1, Punch = 2, Aggressive = 3 };
inline ClipperParams applyCharacter (ClipperParams p, HarmChar c)
{
    switch (c)
    {
        case HarmChar::Clean:      p.shape = ClipShape::Soft;   p.knee = 0.75; p.bias = 0.0;  p.transSel = 0.0; p.dual = false; p.dcBlock = false; break;
        case HarmChar::Warm:       p.shape = ClipShape::Custom; p.knee = 0.45; p.cexp = 2.0;  p.bias = 0.35; p.transSel = 0.0; p.dual = false; p.dcBlock = true;  break;
        case HarmChar::Punch:      p.shape = ClipShape::Custom; p.knee = 0.30; p.cexp = 6.0;  p.bias = 0.0;  p.transSel = 0.6; p.dual = false; p.dcBlock = false; break;
        case HarmChar::Aggressive: p.shape = ClipShape::Custom; p.knee = 0.0;  p.cexp = 16.0; p.bias = 0.0;  p.transSel = 0.0; p.dual = true; p.shape2 = ClipShape::Hard; p.dualOffsetDb = 2.0; p.dcBlock = false; break;
    }
    return p;
}
} // namespace lx::dsp
