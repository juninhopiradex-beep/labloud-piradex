// =============================================================================
//  LABLOUD by Piradex — assistentes (heurísticas determinísticas, C++17 puro)
//  Todos são OFFLINE ou por-bloco, nunca correm na thread de áudio com alocação:
//  as funções que ordenam/alocam (smartClipAmount, qualityOptimize) são chamadas a partir do buffer capturado (~8 s)
//  numa thread de análise. NÃO são IA: são regras explícitas, com a pontuação visível, e dão sempre o mesmo resultado.
// =============================================================================
#pragma once
#include "ClipLimEngine.h"
#include <algorithm>

namespace lx::dsp
{
// ---- Hybrid: o ratio Clip/Limit define onde o clipper corta (r=1: no teto; r=0: 6 dB acima, i.e. praticamente inativo)
inline double hybridClipThresholdDb (double ceilingDb, double ratio) { return ceilingDb + (1.0 - clampd (ratio, 0.0, 1.0)) * 6.0; }

// ---- Perfil do material (percentis de |x| e crest factor), sobre um buffer mono/estéreo intercalado por canal --------
struct MaterialProfile { double rmsDb = -120, p98Db = -120, p999Db = -120, peakDb = -120, crestDb = 0, transientness = 0; };
inline MaterialProfile profileMaterial (const float* x, int n, double fs)
{
    MaterialProfile m; if (n <= 0) return m;
    std::vector<float> a ((size_t) n); double s = 0, pk = 0;
    for (int i = 0; i < n; ++i) { const double v = sanitize ((double) x[i]); a[(size_t) i] = (float) std::fabs (v); s += v * v; pk = std::max (pk, std::fabs (v)); }
    std::sort (a.begin(), a.end());
    auto pct = [&] (double q) { return (double) a[(size_t) std::min ((double) (n - 1), q * (n - 1))]; };
    m.rmsDb = linToDb (std::sqrt (s / n)); m.p98Db = linToDb (pct (0.98)); m.p999Db = linToDb (pct (0.999)); m.peakDb = linToDb (pk); m.crestDb = m.peakDb - m.rmsDb;
    // transientness: 0 = contínuo (crest <= 9 dB), 1 = muito percussivo (crest >= 18 dB); medida simples e previsível
    (void) fs; m.transientness = clampd ((m.crestDb - 9.0) / 9.0, 0.0, 1.0);
    return m;
}

// ---- Smart Clip Amount: ganho pré-clip para que ~2 % das amostras (percentil 98) cheguem ao teto -------------------
inline double smartClipAmountDb (const MaterialProfile& m, double ceilingDb) { return clampd (ceilingDb - m.p98Db, 0.0, 8.0); }

// ---- Auto Routing: picos rápidos (crest alto) → clip primeiro, o limiter vê menos pico; material denso → limiter primeiro ----
enum class HybridOrder { ClipThenLimit = 0, LimitThenClip = 1 };
inline HybridOrder autoRouting (const MaterialProfile& m) { return (m.crestDb >= 12.0 || m.transientness >= 0.5) ? HybridOrder::ClipThenLimit : HybridOrder::LimitThenClip; }

// ---- Smart Distribution: reparte o ganho total (dB) entre clipper e limiter ---------------------------------------
struct Distribution { double clipDb, limitDb, ratio; };
inline Distribution smartDistribution (double totalGainDb, const MaterialProfile& m)
{
    const double share = clampd (0.25 + 0.5 * m.transientness, 0.2, 0.8);          // mais percussivo → o clipper leva mais
    return { totalGainDb * share, totalGainDb * (1.0 - share), share };
}

// ---- Balance macro (0 = Punch, 1 = Loud) ----------------------------------------------------------------------------
struct BalanceMap { double punchProtect, releaseScale, clipRatio, clipAssist; };
inline BalanceMap mapBalance (double b)
{
    b = clampd (b, 0.0, 1.0);
    return { 1.0 - b, 1.6 - 1.0 * b, 0.25 + 0.5 * b, 0.05 + 0.40 * b };
}

// ---- Aliasing Guard com histerese: escolhe o oversampling do clipper (estágios 0,2,4 = 1x,4x,16x) -----------------------
enum class AliasMode { Eco = 0, Auto = 1, Max = 2 };
class AliasingGuard
{
public:
    // densityPct = % de amostras cortadas no último bloco; hfRatio = fração (0..1) da energia acima de ~6 kHz
    int update (double densityPct, double hfRatio, AliasMode mode)
    {
        if (mode == AliasMode::Max) { stage = 4; calm = 0; return stage; }
        const double demand = densityPct * (0.5 + 1.5 * clampd (hfRatio, 0.0, 1.0));   // mais HF = aliasing mais audível
        int want = demand >= 4.0 ? 4 : demand >= 0.5 ? 2 : 0;
        if (mode == AliasMode::Eco) want = std::min (want, 2);
        if (want > stage) { stage = want; calm = 0; }
        else if (want < stage)
        {
            // para descer, a procura tem de estar abaixo de 60 % do limiar do estágio atual durante 'holdBlocks' blocos seguidos
            const double thr = stage == 4 ? 4.0 : 0.5;
            if (demand < 0.6 * thr) { if (++calm >= holdBlocks) { stage = want; calm = 0; } } else calm = 0;
        }
        else calm = 0;
        return stage;
    }
    void reset() { stage = 0; calm = 0; }
    int  current() const { return stage; }
    int  holdBlocks = 40;
private:
    int stage = 0, calm = 0;
};

// ---- Loudness Efficiency: três números honestos, sem pontuação única ---------------------------------------------------
struct Efficiency { double deltaLufs, grAvgDb, distortionIndexDb, lufsPerGrDb; };
inline Efficiency loudnessEfficiency (double inLufs, double outLufs, double grAvgDb, double distortionIndexDb)
{ return { outLufs - inLufs, grAvgDb, distortionIndexDb, (outLufs - inLufs) / std::max(0.1, std::fabs (grAvgDb)) }; }

// ---- Quality Optimizer: testa os 4 perfis num buffer capturado e devolve a pontuação de cada um ---------------------------
// score = ganhoDeLoudness(dB RMS) - k * max(0, DSR + 30)      onde DSR = 10 log10( soma (y - x_alinhado)^2 / soma x^2 )  (distorção relativa ao sinal)
struct QoCandidate { HarmChar ch; double loudGainDb, dsrDb, score; };
struct QoResult { QoCandidate cand[4]; int best = 0; };
inline QoResult qualityOptimize (const float* x, int n, double fs, ClipperParams base, double kPenalty = 1.0, int osStages = 0)
{
    QoResult r; double ex = 0; for (int i = 0; i < n; ++i) ex += (double) x[i] * x[i];
    for (int c = 0; c < 4; ++c)
    {
        ClipperParams p = applyCharacter (base, (HarmChar) c); p.mix = 1.0;
        HalfbandOversampler os; os.prepare (osStages); Clipper cl; cl.prepare (fs * os.factor()); cl.setParams (p); cl.settle();
        auto f = [&] (float v) { return cl.process (v); };
        const int L = os.latencySamples(); double ey = 0, ed = 0;
        std::vector<float> y ((size_t) n); for (int i = 0; i < n; ++i) y[(size_t) i] = os.process (x[i], f);
        const double g = dbToLin (base.gainDb);
        for (int i = L; i < n; ++i) { ey += (double) y[(size_t) i] * y[(size_t) i]; const double d = y[(size_t) i] - g * x[i - L]; ed += d * d; }
        QoCandidate q; q.ch = (HarmChar) c; q.loudGainDb = 10 * std::log10 ((ey + 1e-30) / (ex + 1e-30)); q.dsrDb = 10 * std::log10 ((ed + 1e-30) / (ex * g * g + 1e-30));
        q.score = q.loudGainDb - kPenalty * std::max (0.0, q.dsrDb + 30.0); r.cand[c] = q;
        if (q.score > r.cand[r.best].score) r.best = c;
    }
    return r;
}
} // namespace lx::dsp
