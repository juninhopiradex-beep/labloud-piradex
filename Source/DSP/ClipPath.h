// =============================================================================
//  LABLOUD by Piradex — caminho do clipper com oversampling e troca AUTO sem cliques (C++17 puro, sem JUCE)
//  * ClipPath      : clipper (ou multibanda) dentro de um oversampler de meia-banda, SEMPRE com a mesma latência
//                    (kMaxLatency amostras), qualquer que seja o estágio de oversampling.
//  * ClipSwitcher  : dois ClipPath; para mudar de estágio (Aliasing Guard AUTO) o novo caminho aquece em paralelo
//                    (pré-roll) e faz-se um crossfade — nenhuma descontinuidade, nenhuma alocação em tempo real.
// =============================================================================
#pragma once
#include "Assist.h"

namespace lx::dsp
{
class ClipPath
{
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kLatency = HalfbandOversampler::kMaxLatency;
    void prepare (double fs, int channels, int stages)
    {
        sr = fs; nCh = std::max (1, std::min (channels, kMaxCh));
        for (auto& d : pad) d.init (kLatency + 4);                     // única alocação
        setStages (stages); setParams (ClipperParams(), MultibandParams(), false); settle();
    }
    void setStages (int s)
    {
        stages = std::max (0, std::min (s, 5));
        for (int c = 0; c < kMaxCh; ++c)
        {
            os[(size_t) c].prepare (stages); const double r = sr * os[(size_t) c].factor();
            clip[(size_t) c].prepare (r); mb[(size_t) c].prepare (r); pad[(size_t) c].reset();
        }
        padLen = kLatency - os[0].latencySamples();
    }
    void setParams (const ClipperParams& p, const MultibandParams& m, bool useMultiband)
    {
        useMb = useMultiband; MultibandParams mm = m; mm.base = p;
        for (int c = 0; c < kMaxCh; ++c) { clip[(size_t) c].setParams (p); mb[(size_t) c].setParams (mm); }
    }
    void settle() { for (int c = 0; c < kMaxCh; ++c) { clip[(size_t) c].settle(); mb[(size_t) c].settle(); } }
    float process (int c, float x)
    {
        auto& o = os[(size_t) c];
        const float y = useMb ? o.process (x, [&] (float v) { return mb[(size_t) c].process (v); })
                              : o.process (x, [&] (float v) { return clip[(size_t) c].process (v); });
        return pad[(size_t) c].process (y, padLen);
    }
    ClipStats takeStats() { ClipStats s; for (int c = 0; c < nCh; ++c) s.merge (useMb ? mb[(size_t) c].takeStats() : clip[(size_t) c].takeStats()); return s; }
    int stage() const { return stages; }
    int factor() const { return 1 << stages; }
private:
    double sr = 48000.0; int nCh = 2, stages = 0, padLen = 0; bool useMb = false;
    std::array<HalfbandOversampler, kMaxCh> os; std::array<Clipper, kMaxCh> clip; std::array<MultibandClipper, kMaxCh> mb; std::array<DelayLine, kMaxCh> pad;
};

class ClipSwitcher
{
public:
    static constexpr int kPreroll = 256, kCross = 256;
    void prepare (double fs, int channels, int stages) { nCh = std::max (1, std::min (channels, ClipPath::kMaxCh)); A.prepare (fs, nCh, stages); B.prepare (fs, nCh, stages); state = 0; pending = -1; }
    void setParams (const ClipperParams& p, const MultibandParams& m, bool useMb) { cp = p; mp = m; umb = useMb; A.setParams (p, m, useMb); if (state) B.setParams (p, m, useMb); }
    void settle() { A.settle(); }
    // recomeça limpo no estágio dado (sem alocação): troca de modo / paragem
    void restart (int stages) { A.setStages (stages); A.setParams (cp, mp, umb); A.settle(); state = 0; pending = -1; count = 0; }
    void requestStage (int s)
    {
        s = std::max (0, std::min (s, 5));
        if (state != 0) { pending = s; return; }
        if (s == A.stage()) return;
        B.setStages (s); B.setParams (cp, mp, umb); B.settle(); state = 1; count = 0; pending = -1;
    }
    int stage() const { return A.stage(); }
    bool switching() const { return state != 0; }
    void processBlock (float* const* io, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = state == 2 ? (float) count / (float) kCross : 0.f;
            for (int c = 0; c < nCh; ++c)
            {
                const float x = io[c][i], a = A.process (c, x);
                io[c][i] = state == 0 ? a : (state == 1 ? (B.process (c, x), a) : a + (B.process (c, x) - a) * w);
            }
            if (state == 1) { if (++count >= kPreroll) { state = 2; count = 0; } }
            else if (state == 2 && ++count >= kCross) { std::swap (A, B); state = 0; if (pending >= 0) { const int s = pending; pending = -1; requestStage (s); } }
        }
    }
    ClipStats takeStats() { return A.takeStats(); }
private:
    ClipPath A, B; ClipperParams cp; MultibandParams mp; bool umb = false; int nCh = 2, state = 0, count = 0, pending = -1;
};

// ---- Monitor de análise: espectro em 64 bandas logarítmicas + relatório harmónico se o sinal for um tom --------------------------
struct SpectrumFrame { float bandDb[64] = {}; bool tone = false; double f0 = 0; HarmonicReport h; };
inline SpectrumFrame computeSpectrumFrame (const float* x, int n, double fs)   // n potência de 2 (ex.: 8192); aloca: chamar fora da thread de áudio
{
    SpectrumFrame f; std::vector<std::complex<double>> a ((size_t) n);
    for (int i = 0; i < n; ++i) a[(size_t) i] = { (double) x[i] * (0.5 - 0.5 * std::cos (2.0 * kPi * i / n)), 0.0 };
    fftInPlace (a); const int half = n / 2; std::vector<double> p ((size_t) half + 1); double tot = 0; int kmax = 4;
    for (int k = 0; k <= half; ++k) { p[(size_t) k] = std::norm (a[(size_t) k]); if (k >= 4) { tot += p[(size_t) k]; if (p[(size_t) k] > p[(size_t) kmax]) kmax = k; } }
    for (int b = 0; b < 64; ++b)
    {
        const double lo = 20.0 * std::pow (std::min (20000.0, 0.48 * fs) / 20.0, b / 64.0), hi = 20.0 * std::pow (std::min (20000.0, 0.48 * fs) / 20.0, (b + 1) / 64.0);
        int k0 = std::max (1, (int) std::floor (lo * n / fs)), k1 = std::max (k0, (int) std::ceil (hi * n / fs)); double s = 0; for (int k = k0; k <= std::min (k1, half); ++k) s += p[(size_t) k];
        f.bandDb[b] = (float) (10.0 * std::log10 (s / ((double) n * n / 4.0) + 1e-14));
    }
    double pk = 0; for (int k = std::max (4, kmax - 3); k <= std::min (half, kmax + 3); ++k) pk += p[(size_t) k];
    f.tone = tot > 1e-12 && pk / tot > 0.6;                                   // 60 % da energia numa só linha = tom de teste
    if (f.tone) { f.f0 = kmax * fs / n; f.h = analyzeHarmonics (x, n, fs, f.f0); }
    return f;
}
} // namespace lx::dsp
