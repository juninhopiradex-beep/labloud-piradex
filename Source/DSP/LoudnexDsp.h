// =============================================================================
//  LABLOUD by Piradex — núcleo DSP (C++17 puro, sem JUCE, testável na linha de comandos)
//
//  Conteúdo:
//    TruePeakEstimator  estimador de pico verdadeiro 8x (FIR sinc com janela Blackman, 48 taps por fase)
//    SlidingMin         mínimo deslizante em O(1) (deque monótona)
//    Boxcar             média móvel em O(1)
//    Limiter            limitador "brickwall" com lookahead, release de dois estágios e bass guard
//    Clipper            formas de clip (Soft / Cubic / Hard) por amostra, para correr sobreamostrado
//    LoudnessMeter      LUFS momentâneo (400 ms) e de curto prazo (3 s) com K-weighting da ITU-R BS.1770
//    DelayLine          atraso inteiro para compensação de latência entre modos
//
//  Regras (ver docs/DSP.md): nada aqui aloca memória depois de prepare(); nada bloqueia; nada escreve em disco.
//  Qualquer não-linearidade é explícita: Clip Assist (limitador) e as formas do Clipper.
// =============================================================================
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lx { namespace dsp
{
constexpr double kPi = 3.14159265358979323846;

inline double dbToLin (double db)   { return std::pow (10.0, db / 20.0); }
inline double linToDb (double lin)  { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }
inline double clampd (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double sanitize (double v)   { return std::isfinite (v) ? v : 0.0; }

// -----------------------------------------------------------------------------
//  Curvas de clip (entrada e saída normalizadas ao teto: u = x / ceiling)
// -----------------------------------------------------------------------------
enum class ClipShape { Soft = 0, Cubic = 1, Hard = 2, Custom = 3 };

// linear até ao joelho k; depois k + (1-k)*tanh((|u|-k)/(1-k)). Derivada contínua em k, saída < 1.
inline double softCurve (double u, double k)
{
    const double a = std::fabs (u);
    k = std::min (k, 0.999);                    // joelho mínimo de 0.1%: r = 1 - k exacto, logo |y| < 1
    if (a <= k) return u;
    const double r = 1.0 - k;
    const double y = k + r * std::tanh ((a - k) / r);
    return u < 0 ? -y : y;
}
// y = 1.5 (v - v^3/3), v = clamp(u/1.5): inclinação 1 na origem, atinge 1 em u = 1.5
inline double cubicCurve (double u)
{
    const double v = clampd (u / 1.5, -1.0, 1.0);
    return 1.5 * (v - v * v * v / 3.0);
}
// Curva personalizada (PDF §6 CUSTOM CLIPPING CURVE): linear até ao joelho k; depois k + (1-k) g(v), v = (|u|-k)/(1-k),
// g(v) = v / (1 + v^p)^(1/p). g'(0) = 1 (derivada contínua no joelho), g -> 1, monótona. p = 1 (suave) .. 16 (quase duro).
// bias 0..1 desloca o ponto de operação: y = f(u + b) - f(b), b = bias/2, o que gera harmónicos pares (precisa de DC blocker).
inline double customCurve (double u, double k, double p, double bias = 0.0)
{
    auto f = [&] (double x)
    {
        const double a = std::fabs (x); k = std::min (k, 0.999); if (a <= k) return x;
        const double r = 1.0 - k, v = (a - k) / r, y = k + r * v / std::pow (1.0 + std::pow (v, p), 1.0 / p);
        return x < 0 ? -y : y;
    };
    if (bias <= 0.0) return f (u);
    const double b = 0.5 * clampd (bias, 0.0, 1.0);
    return clampd (f (u + b) - f (b), -1.0, 1.0);
}
// knee 0..1 (0 = joelho duro, 1 = muito suave) → joelho k da curva Soft
inline double clipCurve (double u, ClipShape s, double knee, double cexp = 4.0, double bias = 0.0)
{
    switch (s)
    {
        case ClipShape::Soft:   return softCurve (u, 1.0 - 0.95 * clampd (knee, 0.0, 1.0));
        case ClipShape::Cubic:  return cubicCurve (u);
        case ClipShape::Custom: return customCurve (u, 1.0 - 0.95 * clampd (knee, 0.0, 1.0), clampd (cexp, 1.0, 16.0), bias);
        default:                return clampd (u, -1.0, 1.0);
    }
}

// -----------------------------------------------------------------------------
//  Estimador de pico verdadeiro (8x: 7 fases entre amostras). Atraso fixo de kDelay amostras.
//  Não é o filtro normativo da BS.1770: é um estimador próprio, com erro medido em tests/test_dsp.cpp.
// -----------------------------------------------------------------------------
class TruePeakEstimator
{
public:
    static constexpr int kTaps = 48;
    static constexpr int kPhases = 7;       // 8x: fases 1/8 .. 7/8
    static constexpr int kCenter = kTaps / 2 - 1;
    static constexpr int kDelay = kTaps - 1 - kCenter;   // o valor devolvido refere-se à amostra empurrada há kDelay passos

    TruePeakEstimator() { reset(); }
    void reset() { std::fill (buf, buf + 2 * kTaps, 0.0); idx = 0; }

    // devolve o pico (amostra ou intersample) do intervalo centrado na amostra w[kCenter]
    // Janela em buffer duplo (sem deslocar 47 valores por amostra) e produtos escalares com 4 acumuladores independentes.
    double push (double x, bool interpolate)
    {
        buf[idx] = buf[idx + kTaps] = x;
        const double* w = buf + idx + 1;                 // w[0] = mais antigo ... w[kTaps-1] = x
        idx = idx + 1 == kTaps ? 0 : idx + 1;
        double peak = std::fabs (w[kCenter]);
        if (interpolate)
        {
            const auto& c = coeffs();
            for (int p = 0; p < kPhases; ++p)
            {
                const double* k = c[(size_t) p].data(); double s0 = 0, s1 = 0, s2 = 0, s3 = 0;
                for (int j = 0; j < kTaps; j += 4) { s0 += k[j] * w[j]; s1 += k[j + 1] * w[j + 1]; s2 += k[j + 2] * w[j + 2]; s3 += k[j + 3] * w[j + 3]; }
                peak = std::max (peak, std::fabs ((s0 + s1) + (s2 + s3)));
            }
        }
        return peak;
    }

private:
    double buf[2 * kTaps]; int idx = 0;
    static const std::array<std::array<double, kTaps>, kPhases>& coeffs()
    {
        static const std::array<std::array<double, kTaps>, kPhases> table = []
        {
            std::array<std::array<double, kTaps>, kPhases> t {};
            for (int p = 0; p < kPhases; ++p)
            {
                const double f = (p + 1) / 8.0; double sum = 0.0;
                for (int j = 0; j < kTaps; ++j)
                {
                    const double x = j - kCenter - f, u = x / (kTaps / 2);
                    const double sinc = std::fabs (x) < 1e-12 ? 1.0 : std::sin (kPi * x) / (kPi * x);
                    const double win = 0.42 + 0.5 * std::cos (kPi * u) + 0.08 * std::cos (2.0 * kPi * u);
                    t[(size_t) p][(size_t) j] = sinc * win; sum += sinc * win;
                }
                for (int j = 0; j < kTaps; ++j) t[(size_t) p][(size_t) j] /= sum;   // ganho DC = 1
            }
            return t;
        }();
        return table;
    }
};

// -----------------------------------------------------------------------------
//  Mínimo deslizante: janela de L amostras, O(1) amortizado, sem alocação depois de init()
// -----------------------------------------------------------------------------
class SlidingMin
{
public:
    void init (int maxWindow)
    {
        cap = 1; while (cap < maxWindow + 2) cap <<= 1;
        mask = cap - 1; vals.assign ((size_t) cap, 1.0); idxs.assign ((size_t) cap, 0);
        window = 1; reset();
    }
    void reset() { head = tail = 0; count = 0; n = 0; }
    void setWindow (int L) { window = std::max (1, L); }
    double push (double v)
    {
        while (count > 0 && vals[(size_t) ((tail - 1) & mask)] >= v) { tail = (tail - 1) & mask; --count; }
        vals[(size_t) tail] = v; idxs[(size_t) tail] = n; tail = (tail + 1) & mask; ++count;
        while (idxs[(size_t) head] <= n - window) { head = (head + 1) & mask; --count; }
        ++n;
        return vals[(size_t) head];
    }
private:
    std::vector<double> vals; std::vector<int64_t> idxs;
    int cap = 2, mask = 1, head = 0, tail = 0, count = 0, window = 1; int64_t n = 0;
};

// -----------------------------------------------------------------------------
//  Média móvel de L amostras (soma corrida em double, recalculada de vez em quando)
// -----------------------------------------------------------------------------
class Boxcar
{
public:
    void init (int maxLen) { cap = maxLen + 1; buf.assign ((size_t) cap, 1.0); len = 1; pos = 0; sum = 1.0; tick = 0; }
    void setLength (int L, double fill)
    {
        len = std::max (1, std::min (L, cap - 1));
        std::fill (buf.begin(), buf.end(), fill); sum = fill * len; pos = 0; tick = 0;
    }
    double process (double v)
    {
        const double old = buf[(size_t) ((pos + cap - len) % cap)];
        buf[(size_t) pos] = v; pos = (pos + 1) % cap; sum += v - old;
        if (++tick >= 32768) { tick = 0; double s = 0; for (int i = 1; i <= len; ++i) s += buf[(size_t) ((pos + cap - i) % cap)]; sum = s; }
        return sum / len;
    }
private:
    std::vector<double> buf; int cap = 2, len = 1, pos = 0, tick = 0; double sum = 1.0;
};

// -----------------------------------------------------------------------------
//  Atraso inteiro (compensação de latência entre modos)
// -----------------------------------------------------------------------------
class DelayLine
{
public:
    void init (int maxDelay) { cap = maxDelay + 2; buf.assign ((size_t) cap, 0.0f); pos = 0; }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    float process (float x, int d)
    {
        d = std::max (0, std::min (d, cap - 1));
        buf[(size_t) pos] = x;
        const float y = buf[(size_t) ((pos + cap - d) % cap)];
        pos = (pos + 1) % cap; return y;
    }
private:
    std::vector<float> buf; int cap = 2, pos = 0;
};

// -----------------------------------------------------------------------------
//  Limitador
//
//   gReq[n]  = min(1, teto / pico[n])                      ganho necessário (pico = amostra ou pico verdadeiro)
//   m[n]     = min(gReq[n-L+1 .. n])                        mínimo deslizante de L amostras
//   r[n]     = min(m[n], r[n-1] + (1 - r[n-1]) * coef)      queda instantânea, subida exponencial (release)
//   g[n]     = média(r[n-L+1 .. n])                         média móvel de L amostras = ataque suave em L amostras
//   y        = áudio atrasado de (kDelay + L - 1) * g
//
//  Como r <= m <= gReq e a média cobre exactamente a janela do mínimo, g nunca excede o ganho necessário
//  no instante do pico: não há "overs" por construção (o grampo final conta ocorrências, e deve ser 0).
// -----------------------------------------------------------------------------
enum class ReleaseMode { Single = 0, Dual = 1, Adaptive = 2 };          // PDF §3 RELEASE ENVELOPE (Adaptive = AUTO RELEASE INTELLIGENT)
enum class Spectral    { Flat = 0, Perceptual = 1, BassSafe = 2 };      // PDF §3 SPECTRAL SENSITIVITY
enum class GuardMode   { Off = 0, Soft = 1, Normal = 2, Strict = 3 };   // PDF §4 DISTORTION GUARD

struct LimiterParams
{
    double gainDb      = 0.0;     // 0..+24
    double ceilingDb   = -1.0;    // -12..+6 (acima de 0 só no Hybrid Limit>Clip)
    double releaseMs   = 60.0;    // 0.1..1000
    double lookaheadMs = 1.5;     // 0.2..5
    double clipAssist  = 0.0;     // 0..1 (mistura de soft clip antes do limitador)
    double mix         = 1.0;     // 0..1
    double stereoLink  = 1.0;     // 0..1  TRANSIENT LINK: liga a deteção do ganho necessário entre canais
    double releaseLink = 0.0;     // 0..1  RELEASE LINK: liga a recuperação entre canais (0 = comportamento anterior)
    int    character   = 0;       // 0 Clean, 1 Punch, 2 Loud
    bool   bassGuard   = true;
    double bassAmount  = 1.0;     // 0..1  BASS GUARD (%): quanto o conteúdo grave alarga o release
    double bassFreqHz  = 120.0;   // 40..400 Hz: abaixo desta frequência o release alarga (Bass Guard)
    Spectral    spectral    = Spectral::BassSafe;
    ReleaseMode releaseMode = ReleaseMode::Single;
    double punchProtect = 0.0;    // 0..1  PUNCH PROTECT: release mais rápido após picos curtos + ajuda de clip nos transientes; nunca viola o teto
    GuardMode guard     = GuardMode::Off;
    bool   truePeak    = true;
};

class Limiter
{
public:
    static constexpr int kMaxCh = 2;
    static constexpr int kTpDelay = TruePeakEstimator::kDelay;

    void prepare (double sampleRate, int channels, double maxLookaheadMs = 5.0)
    {
        sr = sampleRate; nCh = std::max (1, std::min (channels, kMaxCh));
        maxL = std::max (1, (int) std::ceil (maxLookaheadMs * 1.0e-3 * sr)) + 2;
        const int maxDelay = maxL + kTpDelay + 4;
        for (int c = 0; c < kMaxCh; ++c)
        {
            ch[c].audio.init (maxDelay); ch[c].dry.init (maxDelay);
            ch[c].smin.init (maxL); ch[c].box.init (maxL);
        }
        smoothCoef = 1.0 - std::exp (-1.0 / (0.005 * sr));
        lpCoef = 1.0 - std::exp (-2.0 * kPi * 120.0 / sr);
        envCoef = 1.0 - std::exp (-1.0 / (0.05 * sr));
        fastRel = 1.0 - std::exp (-1.0 / (0.002 * sr));      // envelope rápida (picos): release 2 ms
        rmsCoef = 1.0 - std::exp (-1.0 / (0.030 * sr));      // energia: 30 ms
        depthCoef = 1.0 - std::exp (-1.0 / (0.300 * sr));    // profundidade média de redução: 300 ms
        modLp = 1.0 - std::exp (-2.0 * kPi * 20.0 / sr);     // separa a modulação lenta (bombear) da rápida (distorção)
        modMs = 1.0 - std::exp (-1.0 / (0.050 * sr));
        guardCoef = 1.0 - std::exp (-1.0 / (0.100 * sr));
        transDecay = std::exp (-1.0 / (0.080 * sr));
        L = 1; params = LimiterParams(); applyLookahead (true);
        gainSm = 1.0; ceilSm = dbToLin (params.ceilingDb);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < kMaxCh; ++c)
        {
            auto& s = ch[c]; s.audio.reset(); s.dry.reset(); s.tp.reset(); s.smin.reset(); s.r = 1.0; s.rF = 1.0; s.rS = 1.0;
            s.lp = 0; s.eLow = 0; s.eAll = 1.0e-6; s.eFast = 0; s.eRms = 1.0e-9; s.transEnv = 0; s.depth = 0; s.gLp = 1.0; s.modE = 0; s.guardF = 1.0;
            s.box.setLength (L, 1.0);
        }
        safetyClamps = 0; grMinBlock = 1.0; peakOut = 0; grSumDb = 0; grCount = 0; distIdx = -120.0;
    }
    void setParams (const LimiterParams& p)
    {
        const int newL = lookaheadSamples (p.lookaheadMs);
        params = p;
        lpCoef = 1.0 - std::exp (-2.0 * kPi * clampd (p.bassFreqHz, 40.0, 400.0) / sr);
        if (newL != L) { L = newL; applyLookahead (false); }
        gainTarget = dbToLin (clampd (p.gainDb, 0.0, 24.0));
        ceilTarget = dbToLin (clampd (p.ceilingDb, -12.0, 6.0));     // até +6 dB: o Hybrid Limit>Clip deixa o limitador passar um pouco acima do teto final
    }
    // fixa já os valores suavizados nos alvos (usar após setParams quando não há áudio a correr, ex.: no reset)
    void settle() { gainSm = gainTarget; ceilSm = ceilTarget; }
    int latencySamples() const { return kTpDelay + L - 1; }
    int maxLatencySamples() const { return kTpDelay + (maxL - 2) - 1; }   // latência com o lookahead máximo
    int lookaheadSamplesNow() const { return L; }
    int64_t safetyClampCount() const { return safetyClamps; }

    // limpa as leituras acumuladas desde a última chamada e devolve o menor ganho aplicado no bloco (0..1)
    double takeGainMin() { const double v = grMinBlock; grMinBlock = 1.0; return v; }
    double takePeakOut() { const double v = peakOut; peakOut = 0; return v; }
    // redução média (dB, >= 0) desde a última chamada: custo do loudness em picos (LOUDNESS EFFICIENCY)
    double takeGrAvgDb() { const double v = grCount ? -grSumDb / (double) grCount : 0.0; grSumDb = 0; grCount = 0; return v; }
    // estimativa de distorção por modulação de ganho (dB re sinal; ~ nível das bandas laterais de um tom). É uma estimativa técnica, não um medidor perceptivo.
    double distortionIndexDb() const { return distIdx; }

    void process (float* const* io, int numCh, int numSamples)
    {
        const int C = std::max (1, std::min (numCh, nCh));
        const double relBase = clampd (params.releaseMs, 0.1, 1000.0) * 1.0e-3;
        static const double kChar[3] = { 1.0, 0.55, 0.30 };
        const double charF = kChar[std::max (0, std::min (params.character, 2))];
        const double link = clampd (params.stereoLink, 0.0, 1.0), relLink = clampd (params.releaseLink, 0.0, 1.0);
        const double punch = clampd (params.punchProtect, 0.0, 1.0);
        const double assist = clampd (params.clipAssist + 0.25 * punch, 0.0, 1.0);
        const double mixW = clampd (params.mix, 0.0, 1.0);
        const int dly = kTpDelay + L - 1;
        const bool tpOn = params.truePeak;
        static const double kSpec[3] = { 0.0, 1.0, 2.0 };
        const double bassK = (params.bassGuard ? clampd (params.bassAmount, 0.0, 1.0) : 0.0) * kSpec[std::max (0, std::min ((int) params.spectral, 2))];
        static const double kGuardThr[4] = { 0.0, -40.0, -46.0, -52.0 };
        const int gm = std::max (0, std::min ((int) params.guard, 3));
        const ReleaseMode rm = params.releaseMode;
        double idxMax = -120.0;

        for (int i = 0; i < numSamples; ++i)
        {
            gainSm += (gainTarget - gainSm) * smoothCoef;
            ceilSm += (ceilTarget - ceilSm) * smoothCoef;
            double gReq[kMaxCh] = { 1.0, 1.0 };

            for (int c = 0; c < C; ++c)
            {
                auto& s = ch[c];
                const double x = sanitize ((double) io[c][i]) * gainSm;
                double xs = x;
                if (assist > 0.0) { const double cl = softCurve (x / ceilSm, 0.6) * ceilSm; xs = x + (cl - x) * assist; }
                s.lp += (x - s.lp) * lpCoef;
                s.eLow += (std::fabs (s.lp) - s.eLow) * envCoef;
                s.eAll += (std::fabs (x) - s.eAll) * envCoef;
                const double ax = std::fabs (x);
                s.eFast = ax > s.eFast ? ax : s.eFast + (ax - s.eFast) * fastRel;
                s.eRms += (x * x - s.eRms) * rmsCoef;
                const double det = s.tp.push (xs, tpOn);
                s.xsNow = s.audio.process ((float) xs, dly);
                s.dryNow = s.dry.process ((float) x, dly);
                gReq[c] = std::min (1.0, ceilSm / std::max (det, 1.0e-9));
            }
            const double gMinAll = (C == 2) ? std::min (gReq[0], gReq[1]) : gReq[0];

            double rC[kMaxCh] = { 1.0, 1.0 };
            for (int c = 0; c < C; ++c)
            {
                auto& s = ch[c];
                const double gl = (C == 2) ? link * gMinAll + (1.0 - link) * gReq[c] : gReq[c];
                const double m = s.smin.push (gl);
                // quão "transiente" é o material agora: crista (pico rápido / RMS). Seno ≈ 1.41, pico isolado de kick ≈ 4-8.
                const double crest = s.eFast / (std::sqrt (s.eRms) + 1.0e-9);
                const double tNow = clampd ((crest - 1.8) / 3.5, 0.0, 1.0);
                s.transEnv = std::max (tNow, s.transEnv * transDecay);                     // retém o transiente ~80 ms: o release é decidido depois do pico, não durante
                const double trans = s.transEnv;
                double tau = relBase * charF;
                if (params.character == 0) tau *= 1.0 + 3.0 * (1.0 - s.r);              // Clean: mais lento quanto mais fundo
                if (bassK > 0.0) tau *= 1.0 + bassK * clampd ((s.eLow / s.eAll - 0.5) * 2.0, 0.0, 1.0);
                if (punch > 0.0) tau *= 1.0 - 0.7 * punch * trans;                      // PUNCH PROTECT: recupera depressa depois de picos curtos
                if (gm > 0) { const double tgt = 1.0 + 2.5 * clampd ((distIdxCh[c] - kGuardThr[gm]) / 8.0, 0.0, 1.0); s.guardF += (tgt - s.guardF) * guardCoef; tau *= s.guardF; }
                tau = std::max (tau, 5.0e-5);
                if (rm == ReleaseMode::Single)
                {
                    const double coef = 1.0 - std::exp (-1.0 / (sr * tau));
                    s.r = std::min (m, s.r + (1.0 - s.r) * coef);
                    s.rF = s.rS = s.r;
                }
                else
                {
                    // recuperação bi-exponencial: componente rápida + componente lenta; ambas <= m, logo a mistura <= m (sem overs)
                    const double depthF = clampd (s.depth * 4.0, 0.0, 1.0);                 // redução profunda e prolongada alarga a parte lenta
                    double tauF = tau * 0.25, tauS = tau * 2.0 * (rm == ReleaseMode::Adaptive ? 1.0 + 2.0 * depthF : 1.0);
                    const double wF = rm == ReleaseMode::Dual ? 0.5 : clampd (0.15 + 0.7 * trans, 0.0, 1.0);
                    tauF = std::max (tauF, 5.0e-5);
                    s.rF = std::min (m, s.rF + (1.0 - s.rF) * (1.0 - std::exp (-1.0 / (sr * tauF))));
                    s.rS = std::min (m, s.rS + (1.0 - s.rS) * (1.0 - std::exp (-1.0 / (sr * tauS))));
                    s.r = wF * s.rF + (1.0 - wF) * s.rS;
                }
                s.depth += ((1.0 - s.r) - s.depth) * depthCoef;
                rC[c] = s.r;
            }
            const double rMin = (C == 2) ? std::min (rC[0], rC[1]) : rC[0];

            for (int c = 0; c < C; ++c)
            {
                auto& s = ch[c];
                const double rIn = relLink > 0.0 ? relLink * rMin + (1.0 - relLink) * rC[c] : rC[c];   // <= rC[c]: só pode reduzir mais, nunca menos
                const double g = s.box.process (rIn);
                double y = (double) s.xsNow * g;
                if (std::fabs (y) > ceilSm)
                {
                    if (std::fabs (y) > ceilSm * (1.0 + 1.0e-5)) ++safetyClamps;   // só conta excessos > 1e-5 (0.00009 dB); abaixo disso é arredondamento float
                    y = y < 0 ? -ceilSm : ceilSm;
                }
                y = y * mixW + (double) s.dryNow * (1.0 - mixW);
                io[c][i] = (float) y;
                grMinBlock = std::min (grMinBlock, g);
                peakOut = std::max (peakOut, std::fabs (y));
                // estimativa de distorção: modulação de ganho acima de ~20 Hz
                s.gLp += (g - s.gLp) * modLp; const double dev = g - s.gLp; s.modE += (dev * dev - s.modE) * modMs;
                distIdxCh[c] = 10.0 * std::log10 (s.modE + 1.0e-12);
                idxMax = std::max (idxMax, distIdxCh[c]);
                if (c == 0) { grSumDb += 20.0 * std::log10 (std::max (g, 1.0e-6)); ++grCount; }
            }
        }
        distIdx = idxMax;
    }

private:
    struct Chan
    {
        DelayLine audio, dry; TruePeakEstimator tp; SlidingMin smin; Boxcar box;
        double r = 1.0, rF = 1.0, rS = 1.0, lp = 0, eLow = 0, eAll = 1.0e-6, eFast = 0, eRms = 1.0e-9, depth = 0, gLp = 1.0, modE = 0, guardF = 1.0, transEnv = 0;
        float xsNow = 0, dryNow = 0;
    };
    int lookaheadSamples (double ms) const
    {
        const int v = (int) std::lround (clampd (ms, 0.2, 5.0) * 1.0e-3 * sr);
        return std::max (1, std::min (v, maxL - 2));
    }
    void applyLookahead (bool first)
    {
        for (int c = 0; c < kMaxCh; ++c)
        {
            ch[c].smin.setWindow (L); ch[c].smin.reset();
            ch[c].box.setLength (L, first ? 1.0 : ch[c].r);   // continuidade do ganho ao mudar o lookahead
        }
    }
    Chan ch[kMaxCh];
    LimiterParams params;
    double sr = 48000.0, smoothCoef = 0.01, lpCoef = 0.01, envCoef = 0.001, fastRel = 0.01, rmsCoef = 0.001, depthCoef = 0.0001, modLp = 0.003, modMs = 0.0005, guardCoef = 0.0002, transDecay = 0.999;
    double gainTarget = 1.0, ceilTarget = 0.891, gainSm = 1.0, ceilSm = 0.891;
    double grMinBlock = 1.0, peakOut = 0.0, grSumDb = 0.0, distIdx = -120.0, distIdxCh[kMaxCh] = { -120.0, -120.0 };
    int64_t grCount = 0;
    int nCh = 2, L = 1, maxL = 8; int64_t safetyClamps = 0;
};

// -----------------------------------------------------------------------------
//  Clipper por amostra (corre à taxa sobreamostrada, escolhida pelo anfitrião do DSP)
// -----------------------------------------------------------------------------
struct ClipperParams
{
    double gainDb = 0.0, ceilingDb = -1.0, knee = 0.55, mix = 1.0;
    ClipShape shape = ClipShape::Soft;
    double cexp = 4.0;            // 1..16  dureza da curva Custom
    double bias = 0.0;            // 0..1   viés harmónico (harmónicos pares) da curva Custom
    double posOffsetDb = 0.0;     // <= 0   teto da metade positiva em relação ao Ceiling   (ASYMMETRIC CLIP)
    double negOffsetDb = 0.0;     // <= 0   teto da metade negativa
    bool   dual = false;          // DUAL-STAGE: 1.º estágio (shape) a ceiling+dualOffsetDb, 2.º estágio (shape2) ao teto
    ClipShape shape2 = ClipShape::Hard;
    double dualOffsetDb = 2.0;    // 0..6
    double transSel = 0.0;        // 0..1   TRANSIENT-SELECTIVE: 1 = só os picos rápidos são cortados pelo clipper
    double postGainDb = 0.0;      // -12..0 PRE/POST-GAIN: compensação de saída depois do clip (o teto efetivo desce com ela)
    bool   dcBlock = false;       // DC BLOCKER (~5 Hz) depois do clip; ligar com bias (harmónicos pares). Tem clamp de segurança ao teto.
};

// PEAK DENSITY ANALYZER + CLIPPED dB METER (PDF §5/§7): medidos amostra a amostra à taxa de processamento
struct ClipStats
{
    int64_t samples = 0, affected = 0, events = 0, runSum = 0, run = 0;
    double  maxClipDb = 0.0;      // maior corte de pico (dB, >= 0), preenchido por Clipper::takeStats()
    double  maxRatio = 1.0;       // maior razão entrada/saída (sem log10 por amostra)
    double  densityPct() const { return samples ? 100.0 * (double) affected / (double) samples : 0.0; }
    double  avgEventSamples() const { return events ? (double) runSum / (double) events : 0.0; }
    void note (bool hit, double ratio)
    {
        ++samples;
        if (hit) { ++affected; ++runSum; if (!run) ++events; ++run; if (ratio > maxRatio) maxRatio = ratio; } else run = 0;
    }
    void finalize() { maxClipDb = std::max (maxClipDb, 20.0 * std::log10 (maxRatio)); }
    void merge (const ClipStats& o) { samples += o.samples; affected += o.affected; events += o.events; runSum += o.runSum; maxClipDb = std::max (maxClipDb, o.maxClipDb); maxRatio = std::max (maxRatio, o.maxRatio); }
};

class Clipper
{
public:
    void prepare (double sampleRateAtProcessing)
    {
        sr = sampleRateAtProcessing;
        smoothCoef = 1.0 - std::exp (-1.0 / (0.005 * sr));
        fastRel = 1.0 - std::exp (-1.0 / (0.002 * sr)); rmsCoef = 1.0 - std::exp (-1.0 / (0.030 * sr)); transDecay = std::exp (-1.0 / (0.004 * sr));
        dcR = std::exp (-2.0 * kPi * 5.0 / sr);
        gainSm = 1.0; ceilSm = dbToLin (-1.0); params = ClipperParams();
        gainTarget = 1.0; ceilTarget = ceilSm; reset();
    }
    void reset() { eFast = 0; eRms = 1.0e-9; transEnv = 0; dcX = dcY = 0; stats = ClipStats(); lastHit = false; lastRatio = 1.0; }
    void settle() { gainSm = gainTarget; ceilSm = ceilTarget; steady = false; }     // 'steady=false' recalcula os recíprocos na próxima amostra
    void setParams (const ClipperParams& p)
    {
        params = p; steady = false; gainTarget = dbToLin (clampd (p.gainDb, -24.0, 24.0)); ceilTarget = dbToLin (clampd (p.ceilingDb, -12.0, 12.0));
        posScale = dbToLin (clampd (p.posOffsetDb, -12.0, 0.0)); negScale = dbToLin (clampd (p.negOffsetDb, -12.0, 0.0));
        dualScale = dbToLin (clampd (p.dualOffsetDb, 0.0, 6.0)); postLin = dbToLin (clampd (p.postGainDb, -12.0, 0.0));
        softK = 1.0 - 0.95 * clampd (p.knee, 0.0, 1.0); customK = std::min (softK, 0.999);
        if (p.shape == ClipShape::Custom || (p.dual && p.shape2 == ClipShape::Custom)) { const double pe = clampd (p.cexp, 1.0, 16.0); if (std::fabs (pe - lutP) > 1.0e-6) buildLut (pe); }
        biasShift = 0.5 * clampd (p.bias, 0.0, 1.0); biasBase = biasShift > 0.0 ? customRaw (biasShift) : 0.0;
    }
    float process (float xin)
    {
        if (! steady)
        {
            gainSm += (gainTarget - gainSm) * smoothCoef; ceilSm += (ceilTarget - ceilSm) * smoothCoef;
            if (std::fabs (gainTarget - gainSm) < 1.0e-9 * gainTarget && std::fabs (ceilTarget - ceilSm) < 1.0e-9 * ceilTarget) { gainSm = gainTarget; ceilSm = ceilTarget; steady = true; }
            cPos = ceilSm * posScale; cNeg = ceilSm * negScale; iPos = 1.0 / cPos; iNeg = 1.0 / cNeg;
        }
        const double x = sanitize ((double) xin) * gainSm;
        const double ax = std::fabs (x);
        double w = 1.0;
        if (params.transSel > 0.0)
        {
            eFast = ax > eFast ? ax : eFast + (ax - eFast) * fastRel; eRms += (x * x - eRms) * rmsCoef;
            const double tNow = clampd ((eFast / (std::sqrt (eRms) + 1.0e-9) - 1.8) / 3.5, 0.0, 1.0);
            transEnv = std::max (tNow, transEnv * transDecay);
            w = 1.0 - clampd (params.transSel, 0.0, 1.0) * (1.0 - transEnv);
        }
        const bool neg = x < 0.0; const double c = neg ? cNeg : cPos, ic = neg ? iNeg : iPos;   // curvas ímpares: trabalha-se com o sinal (o bias precisa dele)
        double y;
        if (params.dual)
        {
            const double c1 = c * dualScale;
            const double y1 = c1 * shapeEval (x * ic / dualScale, params.shape, true);
            y = c * shapeEval (y1 * ic, params.shape2, false);
        }
        else y = c * shapeEval (x * ic, params.shape, true);
        const double wet = x + (y - x) * w;
        lastHit = ax > std::fabs (wet) * 1.0001 + 1.0e-9;
        lastRatio = lastHit ? ax / std::max (std::fabs (wet), 1.0e-9) : 1.0;
        stats.note (lastHit, lastRatio);
        double out = (wet * params.mix + x * (1.0 - params.mix)) * postLin;
        if (params.dcBlock)
        {
            const double o = out - dcX + dcR * dcY; dcX = out; dcY = o;
            const double lim = ceilSm * std::max (posScale, negScale) * postLin;   // o passa-alto pode fazer overshoot: o teto continua garantido
            out = clampd (o, -lim, lim);
        }
        return (float) out;
    }
    // estatísticas desde a última chamada
    ClipStats takeStats() { ClipStats s = stats; s.finalize(); stats = ClipStats(); return s; }
    bool   lastSampleClipped() const { return lastHit; }
    double lastSampleClipDb() const { return 20.0 * std::log10 (lastRatio); }
    double currentGain() const { return gainSm; }
private:
    ClipperParams params; double sr = 48000.0, smoothCoef = 0.01, gainTarget = 1, ceilTarget = 0.89, gainSm = 1, ceilSm = 0.89;
    double fastRel = 0.01, rmsCoef = 0.001, transDecay = 0.99, dcR = 0.9999, eFast = 0, eRms = 1.0e-9, transEnv = 0, dcX = 0, dcY = 0;
    double posScale = 1.0, negScale = 1.0, dualScale = 1.26, postLin = 1.0, lastRatio = 1.0, cPos = 0.89, cNeg = 0.89, iPos = 1.12, iNeg = 1.12; bool lastHit = false, steady = false; ClipStats stats;
    // ---- curvas: Custom por tabela (interpolação linear em t = v/(1+v)); o valor exato está em customCurve() e é comparado nos testes ----
    // tanh por tabela (0..6, 1024 passos, interpolação linear; erro < 2e-5); acima de 6 devolve tanh(6) = 0.99998771 (nunca > 1)
    struct TanhTab { double v[1026]; TanhTab() { for (int i = 0; i <= 1025; ++i) v[i] = std::tanh (6.0 * i / 1024.0); } };
    static double tanhLut (double a) { static const TanhTab tab; const double f = a * (1024.0 / 6.0); if (f >= 1024.0) return tab.v[1024]; const int i = (int) f; return tab.v[i] + (tab.v[i + 1] - tab.v[i]) * (f - i); }
    static constexpr int kLut = 2048;
    double softK = 0.5, customK = 0.5, biasShift = 0.0, biasBase = 0.0, lutP = -1.0; double lut[kLut + 1] = {};
    void buildLut (double pe)
    {
        lutP = pe; for (int i = 0; i < kLut; ++i) { const double tt = (double) i / kLut, v = tt / (1.0 - tt); lut[i] = v / std::pow (1.0 + std::pow (v, pe), 1.0 / pe); } lut[kLut] = 1.0;
    }
    double gOf (double v) const { const double f = (v / (1.0 + v)) * kLut; const int i = (int) f; if (i >= kLut) return 1.0; return lut[i] + (lut[i + 1] - lut[i]) * (f - i); }
    double customRaw (double x) const { const double a = std::fabs (x); if (a <= customK) return x; const double r = 1.0 - customK, y = customK + r * gOf ((a - customK) / r); return x < 0 ? -y : y; }
    double shapeEval (double u, ClipShape s, bool useBias) const
    {
        switch (s)
        {
            case ClipShape::Soft:   { const double a = std::fabs (u); if (a <= softK) return u; const double r = 1.0 - softK, y = softK + r * tanhLut ((a - softK) / r); return u < 0 ? -y : y; }
            case ClipShape::Cubic:  return cubicCurve (u);
            case ClipShape::Custom: return (useBias && biasShift > 0.0) ? clampd (customRaw (u + biasShift) - biasBase, -1.0, 1.0) : customRaw (u);
            default:                return clampd (u, -1.0, 1.0);
        }
    }
};

// -----------------------------------------------------------------------------
//  LUFS (ITU-R BS.1770): K-weighting + janela rectangular de 400 ms (momentâneo) e 3 s (curto prazo).
//  Sem gating (o integrado com gating entra na fase de medidores).
// -----------------------------------------------------------------------------
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int channels)
    {
        sr = sampleRate; nCh = std::max (1, std::min (channels, 2));
        makeShelf (1681.974450955533, 3.999843853973347, 0.7071752369554196);
        makeHighPass (38.13547087602444, 0.5003270373238773);
        nM = std::max (1, (int) std::lround (0.4 * sr)); nS = std::max (1, (int) std::lround (3.0 * sr));
        ringM.assign ((size_t) nM, 0.0); ringS.assign ((size_t) nS, 0.0);
        reset();
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 4; ++k) z[c][k] = 0.0;
        std::fill (ringM.begin(), ringM.end(), 0.0); std::fill (ringS.begin(), ringS.end(), 0.0);
        posM = posS = 0; sumM = sumS = 0.0; filled = 0;
        hop = std::max (1, (int) std::lround (0.1 * sr)); hopCount = 0;
        std::fill (histBlk.begin(), histBlk.end(), 0); std::fill (histSt.begin(), histSt.end(), 0);
    }
    void process (const float* const* in, int numCh, int n)
    {
        const int C = std::max (1, std::min (numCh, nCh));
        for (int i = 0; i < n; ++i)
        {
            double p = 0.0;
            for (int c = 0; c < C; ++c)
            {
                double x = sanitize ((double) in[c][i]);
                double y = sb0 * x + z[c][0]; z[c][0] = sb1 * x - sa1 * y + z[c][1]; z[c][1] = sb2 * x - sa2 * y;   // shelf (DF2T)
                double v = hb0 * y + z[c][2]; z[c][2] = hb1 * y - ha1 * v + z[c][3]; z[c][3] = hb2 * y - ha2 * v;   // passa-alto
                p += v * v;
            }
            sumM += p - ringM[(size_t) posM]; ringM[(size_t) posM] = p; if (++posM >= nM) posM = 0;
            sumS += p - ringS[(size_t) posS]; ringS[(size_t) posS] = p; if (++posS >= nS) posS = 0;
            if (filled < nS) ++filled;
            if (++hopCount >= hop) { hopCount = 0; onHop(); }   // passo de 100 ms (blocos de 400 ms com 75 %% de sobreposição)
        }
        if (sumM < 0) sumM = 0;
        if (sumS < 0) sumS = 0;
    }
    double momentaryLufs() const { return toLufs (sumM / nM); }
    double shortTermLufs() const { return toLufs (sumS / std::min (nS, std::max (filled, 1))); }

    // ---- BS.1770-4 / EBU R128: loudness integrado com dupla porta (-70 LUFS absoluta, -10 LU relativa) ----
    // Histograma de 0,1 LU (como a libebur128): erro máximo ~0,05 LU. Devolve -120 enquanto não houver blocos válidos.
    double integratedLufs() const
    {
        double sum = 0.0; long long n = 0;
        for (int i = 0; i < kBins; ++i) { sum += (double) histBlk[(size_t) i] * binEnergy (i); n += histBlk[(size_t) i]; }
        if (n == 0) return -120.0;
        const double rel = toLufs (sum / (double) n) - 10.0;
        double s2 = 0.0; long long n2 = 0;
        for (int i = 0; i < kBins; ++i) if (binLufs (i) >= rel) { s2 += (double) histBlk[(size_t) i] * binEnergy (i); n2 += histBlk[(size_t) i]; }
        return n2 ? toLufs (s2 / (double) n2) : -120.0;
    }
    // ---- EBU Tech 3342: gama de loudness (LRA) = percentil 95 - percentil 10 do curto prazo, porta relativa -20 LU ----
    double loudnessRange() const
    {
        double sum = 0.0; long long n = 0;
        for (int i = 0; i < kBins; ++i) { sum += (double) histSt[(size_t) i] * binEnergy (i); n += histSt[(size_t) i]; }
        if (n == 0) return 0.0;
        const double rel = toLufs (sum / (double) n) - 20.0;
        long long tot = 0; for (int i = 0; i < kBins; ++i) if (binLufs (i) >= rel) tot += histSt[(size_t) i];
        if (tot < 2) return 0.0;
        auto pct = [&] (double q) { const long long target = (long long) std::ceil (q * (double) tot); long long acc = 0;
            for (int i = 0; i < kBins; ++i)
                if (binLufs (i) >= rel) { acc += histSt[(size_t) i]; if (acc >= std::max<long long> (target, 1)) return binLufs (i); }
            return -120.0; };
        return std::max (0.0, pct (0.95) - pct (0.10));
    }
    long long gatedBlockCount() const { long long n = 0; for (auto c : histBlk) n += c; return n; }
private:
    static constexpr int kBins = 800;    // -70 .. +10 LUFS em passos de 0,1
    static double binLufs (int i) { return -70.0 + 0.1 * (i + 0.5); }
    static double binEnergy (int i) { return std::pow (10.0, (binLufs (i) + 0.691) / 10.0); }
    static int binOf (double l) { return (int) std::floor ((l + 70.0) * 10.0); }
    void onHop()
    {
        if (filled >= nM) { const double l = toLufs (sumM / nM); const int b = binOf (l); if (b >= 0) ++histBlk[(size_t) std::min (b, kBins - 1)]; }
        if (filled >= nS) { const double l = toLufs (sumS / nS); const int b = binOf (l); if (b >= 0) ++histSt[(size_t) std::min (b, kBins - 1)]; }
    }
    std::vector<long long> histBlk = std::vector<long long> (kBins, 0), histSt = std::vector<long long> (kBins, 0);
    int hop = 4800, hopCount = 0;
    static double toLufs (double meanSquare) { return meanSquare < 1.0e-12 ? -120.0 : -0.691 + 10.0 * std::log10 (meanSquare); }
    // shelf de alta frequência da BS.1770 (forma de libebur128; reproduz os coeficientes normativos a 48 kHz)
    void makeShelf (double f0, double G, double Q)
    {
        const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        sb0 = (Vh + Vb * K / Q + K * K) / a0; sb1 = 2.0 * (K * K - Vh) / a0; sb2 = (Vh - Vb * K / Q + K * K) / a0;
        sa1 = 2.0 * (K * K - 1.0) / a0;       sa2 = (1.0 - K / Q + K * K) / a0;
    }
    void makeHighPass (double f0, double Q)
    {
        const double K = std::tan (kPi * f0 / sr), d = 1.0 + K / Q + K * K;
        hb0 = 1.0; hb1 = -2.0; hb2 = 1.0; ha1 = 2.0 * (K * K - 1.0) / d; ha2 = (1.0 - K / Q + K * K) / d;
    }
    double sr = 48000, sb0 = 1, sb1 = 0, sb2 = 0, sa1 = 0, sa2 = 0, hb0 = 1, hb1 = -2, hb2 = 1, ha1 = 0, ha2 = 0;
    double z[2][4] {}; std::vector<double> ringM, ringS; double sumM = 0, sumS = 0;
    int nCh = 2, nM = 1, nS = 1, posM = 0, posS = 0, filled = 0;
};

// -----------------------------------------------------------------------------
//  Medidor de pico verdadeiro (8x, mesmo estimador do limitador) com máximo retido
// -----------------------------------------------------------------------------
class TruePeakMeter
{
public:
    void reset() { for (auto& e : est) e.reset(); peak = 0.0; }
    void process (const float* const* in, int numCh, int n)
    {
        for (int c = 0; c < std::min (numCh, 2); ++c)
            for (int i = 0; i < n; ++i) peak = std::max (peak, est[c].push (sanitize ((double) in[c][i]), true));
    }
    double maxDbTp() const { return peak < 1.0e-6 ? -120.0 : 20.0 * std::log10 (peak); }
private:
    TruePeakEstimator est[2]; double peak = 0.0;
};

// -----------------------------------------------------------------------------
//  Auto-Dose: sugestão de parâmetros a partir de uma medição (LUFS integrado, pico verdadeiro) e de um alvo.
//  É uma HEURÍSTICA determinística, não um modelo treinado: valida-se ouvindo, não com números.
// -----------------------------------------------------------------------------
struct DoseSuggestion
{
    double gainDb = 0, releaseMs = 100, lookaheadMs = 2, clipAssist = 0, expectedGrDb = 0;
    int    character = 0;
    bool   reachable = true;     // false: o alvo exige mais de ~10 dB de redução ou mais de 24 dB de ganho
};
inline DoseSuggestion suggestDose (double integratedLufs, double truePeakDb, double targetLufs, double ceilingDb)
{
    DoseSuggestion d;
    const double need = targetLufs - integratedLufs;
    d.gainDb = clampd (need, 0.0, 24.0);
    d.expectedGrDb = std::max (0.0, truePeakDb + d.gainDb - ceilingDb);
    d.reachable = (need <= 24.0) && (d.expectedGrDb <= 10.0);
    if      (d.expectedGrDb < 3.0) { d.character = 0; d.releaseMs = 150; d.lookaheadMs = 2.5; d.clipAssist = 0.00; }
    else if (d.expectedGrDb < 6.0) { d.character = 0; d.releaseMs = 100; d.lookaheadMs = 2.0; d.clipAssist = 0.10; }
    else if (d.expectedGrDb < 9.0) { d.character = 1; d.releaseMs = 60;  d.lookaheadMs = 1.5; d.clipAssist = 0.25; }
    else                           { d.character = 2; d.releaseMs = 40;  d.lookaheadMs = 1.0; d.clipAssist = 0.40; }
    return d;
}
}} // namespace lx::dsp
