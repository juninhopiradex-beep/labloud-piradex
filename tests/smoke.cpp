// Teste de fumo do processador LABLOUD (JUCE, sem anfitrião): latência constante, tetos, estado, bloqueio e render do editor.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstdio>
#include <random>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <malloc.h>
#include <new>

// contador de alocações (operator new) ativado só durante as medições em tempo real
static std::atomic<long> g_allocs { 0 }; static std::atomic<bool> g_track { false };
void* operator new (std::size_t n) { if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("FALHOU  "); std::printf (__VA_ARGS__); std::printf ("\n"); } else { std::printf ("ok      "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static void setParam (LoudnexProcessor& p, const char* id, float realValue)
{
    auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

// corre 'n' amostras em blocos de 'bs'; devolve a saída do canal 0
static std::vector<float> run (LoudnexProcessor& p, const std::vector<float>& in, int bs)
{
    std::vector<float> out (in.size()); juce::MidiBuffer midi;
    for (size_t off = 0; off < in.size(); off += (size_t) bs)
    {
        const int n = (int) std::min<size_t> ((size_t) bs, in.size() - off);
        juce::AudioBuffer<float> b (2, n);
        for (int c = 0; c < 2; ++c) std::copy (in.begin() + (long) off, in.begin() + (long) off + n, b.getWritePointer (c));
        p.processBlock (b, midi);
        std::copy (b.getReadPointer (0), b.getReadPointer (0) + n, out.begin() + (long) off);
    }
    return out;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000; const int bs = 512;
    LoudnexProcessor p; p.setPlayConfigDetails (2, 2, fs, bs); p.prepareToPlay (fs, bs);
    const int lat = p.reportedLatency();
    std::printf ("latência reportada: %d amostras = %.2f ms\n", lat, 1000.0 * lat / fs);

    // 1) bloqueado: passa o áudio atrasado, sem processar
    AccessManager::get().logout();
    { std::vector<float> in (8000, 0.f); in[1000] = 0.9f; auto out = run (p, in, bs);
      CHECK (std::fabs (out[1000 + (size_t) lat] - 0.9f) < 1e-6f, "bloqueado: impulso 0.9 passa sem alteração, atrasado %d", lat); }

    // 2) desbloquear (índice 0 = master; o PBKDF2 já é testado em test_licensing)
    AccessManager::get().applyLogin ({ 0, false }, false);
    CHECK (AccessManager::get().isUnlocked(), "master desbloqueia");

    // 3) latência constante em todas as combinações
    auto latencyOf = [&] (int mode, int os, int tp, float la)
    {
        setParam (p, ids::mode, (float) mode); setParam (p, ids::os, (float) os); setParam (p, ids::tp, (float) tp); setParam (p, ids::lookahead, la);
        setParam (p, ids::gain, 0.f); setParam (p, ids::ceiling, 0.f); setParam (p, ids::mix, 100.f);
        p.prepareToPlay (fs, bs);
        std::vector<float> in (12000, 0.f); in[2000] = 0.1f; auto out = run (p, in, bs);
        int at = -1; float best = 0; for (size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > best) { best = std::fabs (out[i]); at = (int) i; }
        return at - 2000;
    };
    bool constant = true; int worst = 0;
    for (int mode = 0; mode < 2; ++mode) for (int os = 0; os < 3; ++os) for (int tp = 0; tp < 2; ++tp) for (float la : { 0.2f, 1.5f, 5.0f })
    {
        const int l = latencyOf (mode, os, tp, la);
        if (std::abs (l - lat) > 1) { constant = false; std::printf ("        modo %d os %d tp %d la %.1f: latência medida %d (esperado %d)\n", mode, os, tp, la, l, lat); }
        worst = std::max (worst, std::abs (l - lat));
    }
    CHECK (constant, "latência medida == reportada (±1 amostra) em 2 modos x 3 os x 2 tp x 3 lookaheads (desvio máx %d)", worst);

    // 4) tetos
    std::vector<float> prog (96000); for (size_t i = 0; i < prog.size(); ++i)
    { const double t = (double) i / fs, bt = std::fmod (t, 0.4839); prog[i] = (float) (0.9 * std::sin (2 * M_PI * (45 * bt + 20 * (1 - std::exp (-30 * bt)))) * std::exp (-bt * 6) + 0.3 * std::sin (2 * M_PI * 3100 * t)); }
    for (int mode = 0; mode < 2; ++mode)
    {
        setParam (p, ids::mode, (float) mode); setParam (p, ids::gain, 12.f); setParam (p, ids::ceiling, -1.f); setParam (p, ids::tp, 1.f); setParam (p, ids::os, 1.f);
        p.prepareToPlay (fs, bs); auto out = run (p, prog, bs);
        float pk = 0; for (size_t i = 24000; i < out.size(); ++i) pk = std::max (pk, std::fabs (out[i]));
        CHECK (pk <= std::pow (10.f, -1.f / 20.f) * 1.01f, "%s +12 dB, teto -1: pico de amostra %.2f dBFS", mode == 0 ? "Limiter" : "Clipper", 20 * std::log10 (pk));
    }
    // 5) troca de modo com áudio a correr: tudo finito
    { bool fin = true; juce::MidiBuffer midi;
      for (int k = 0; k < 40; ++k) { setParam (p, ids::mode, (float) (k % 2)); setParam (p, ids::os, (float) (k % 3));
        juce::AudioBuffer<float> b (2, bs); for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) b.setSample (c, i, (float) std::sin (0.05 * (i + k * bs)));
        p.processBlock (b, midi); for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) if (! std::isfinite (b.getSample (c, i))) fin = false; }
      CHECK (fin, "40 trocas de modo/oversampling com áudio: saída sempre finita"); }
    // 6) estado
    { setParam (p, ids::gain, 7.5f); juce::MemoryBlock mb; p.getStateInformation (mb); setParam (p, ids::gain, 0.f); p.setStateInformation (mb.getData(), (int) mb.getSize());
      CHECK (std::fabs (p.apvts.getRawParameterValue (ids::gain)->load() - 7.5f) < 1e-3f, "estado guardado e restaurado (ganho 7.5 dB)"); }
    // 6b) laudo (LUFS integrado, LRA, pico verdadeiro), Auto-Dose, gain-match e Bass Guard — com um tom conhecido de -20 dBFS (= -20 LUFS em estéreo)
    {
        auto tone = [&] (double seconds) { std::vector<float> v ((size_t) (fs * seconds)); for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (0.1 * std::sin (2 * 3.14159265358979 * 997.0 * (double) i / fs)); return v; };
        setParam (p, ids::mode, 0.f); setParam (p, ids::gain, 0.f); setParam (p, ids::ceiling, 0.f); setParam (p, ids::mix, 100.f); setParam (p, ids::gainmatch, 0.f); setParam (p, ids::os, 1.f);
        p.prepareToPlay (fs, bs); p.requestMeasurementReset();
        run (p, tone (14.0), bs);
        const float I = p.meters.integrated.load(), L = p.meters.lra.load(), T = p.meters.tpMaxDb.load();
        CHECK (std::fabs (I + 20.0f) < 0.3f, "laudo: tom -20 dBFS estéreo -> LUFS integrado %.2f (esperado -20.0)", I);
        CHECK (L < 0.5f, "laudo: tom constante -> LRA %.2f LU", L);
        CHECK (std::fabs (T + 20.0f) < 0.6f, "laudo: pico verdadeiro máximo %.2f dBTP (esperado -20.0)", T);
        p.requestMeasurementReset(); run (p, tone (0.2), bs);
        CHECK (p.meters.tpMaxDb.load() <= -19.0f, "laudo: botão Zerar limpa o pico máximo (%.1f)", p.meters.tpMaxDb.load());

        setParam (p, ids::target, 3.f); setParam (p, ids::ceiling, -1.f);          // alvo -14 LUFS
        p.startAutoDose(); run (p, tone (10.0), bs);
        CHECK (p.dose.state.load() == 2, "Auto-Dose: análise concluída após ~8 s de sinal (estado %d)", p.dose.state.load());
        CHECK (std::fabs (p.dose.gainDb.load() - 6.0f) < 0.4f && p.dose.reachable.load(), "Auto-Dose: -20 -> -14 LUFS sugere +%.2f dB (esperado +6.0)", p.dose.gainDb.load());
        p.applyAutoDose();
        CHECK (std::fabs (p.apvts.getRawParameterValue (ids::gain)->load() - p.dose.gainDb.load()) < 0.05f && p.apvts.getRawParameterValue (ids::mode)->load() < 0.5f, "Auto-Dose: aplicar escreve o ganho sugerido nos parâmetros");

        setParam (p, ids::gain, 6.f); setParam (p, ids::ceiling, 0.f); setParam (p, ids::gainmatch, 1.f); p.prepareToPlay (fs, bs);
        run (p, tone (30.0), bs);
        const float dIn = p.meters.inShortTerm.load(), dOut = p.meters.shortTerm.load();
        CHECK (std::fabs (dOut - dIn) < 0.5f, "gain match: saída %.2f LUFS-S vs entrada %.2f LUFS-S (compensação %.2f dB)", dOut, dIn, p.meters.trimDb.load());
        setParam (p, ids::gainmatch, 0.f);

        auto bgRun = [&] (float hz) { setParam (p, ids::gain, 12.f); setParam (p, ids::bass, 1.f); setParam (p, ids::bassfreq, hz); setParam (p, ids::release, 60.f); p.prepareToPlay (fs, bs);
            std::vector<float> v ((size_t) fs * 2); for (size_t i = 0; i < v.size(); ++i) { const double a = i < (size_t) (fs * 0.3) ? 0.9 : 0.06; v[i] = (float) (a * std::sin (2 * 3.14159265358979 * 200.0 * (double) i / fs)); } return run (p, v, bs); };
        auto o60 = bgRun (60.f), o300 = bgRun (300.f); double dmax = 0; for (size_t i = 0; i < o60.size(); ++i) dmax = std::max (dmax, (double) std::fabs (o60[i] - o300[i]));
        double pk = 0; for (float v : o60) pk = std::max (pk, (double) std::fabs (v));
        CHECK (pk > 0.1 && dmax > 1e-4, "Bass Guard: 60 Hz e 300 Hz dão saídas diferentes na recuperação após rajada de 200 Hz (diferença máx %.5f, pico %.3f)", dmax, pk);
        setParam (p, ids::gain, 0.f); setParam (p, ids::bassfreq, 120.f); setParam (p, ids::ceiling, -1.f);
    }
    // 6c) modos novos: Hybrid, M/S, multibanda, delta, slots, assistentes, aliasing AUTO, HQ offline, vários sample rates
    {
        auto peakOf = [] (const std::vector<float>& v, size_t from) { double m = 0; for (size_t i = from; i < v.size(); ++i) m = std::max (m, (double) std::fabs (v[i])); return m; };
        auto reset = [&] { setParam (p, ids::mode, 0.f); setParam (p, ids::gain, 0.f); setParam (p, ids::ceiling, -1.f); setParam (p, ids::mix, 100.f); setParam (p, ids::tp, 1.f); setParam (p, ids::ms, 0.f); setParam (p, ids::mb, 0.f);
                           setParam (p, ids::delta, 0.f); setParam (p, ids::alias, 0.f); setParam (p, ids::os, 1.f); setParam (p, ids::route, 0.f); setParam (p, ids::ratio, 50.f); setParam (p, ids::hchar, 0.f); setParam (p, ids::gainmatch, 0.f);
                           setParam (p, ids::ceilock, 0.f); setParam (p, ids::lookahead, 1.5f); setParam (p, ids::release, 60.f); setParam (p, ids::shape, 0.f); setParam (p, ids::postgain, 0.f); setParam (p, ids::dual, 0.f); setParam (p, ids::transel, 0.f); p.setNonRealtime (false); };
        reset(); p.prepareToPlay (fs, bs);
        const int L0 = p.reportedLatency();

        // latência constante: 3 modos x 2 rotas x M/S x 4 oversampling
        bool constant = true; int worst = 0, count = 0;
        for (int mode = 0; mode < 3; ++mode) for (int route = 0; route < 2; ++route) for (int ms = 0; ms < 2; ++ms) for (int os = 0; os < 4; ++os)
        {
            if (mode != 2 && route == 1) continue;
            reset(); setParam (p, ids::mode, (float) mode); setParam (p, ids::route, (float) route); setParam (p, ids::ms, (float) ms); setParam (p, ids::os, (float) os); setParam (p, ids::ceiling, 0.f); setParam (p, ids::ratio, 50.f);
            p.prepareToPlay (fs, bs); std::vector<float> in (12000, 0.f); in[2000] = 0.1f; auto out = run (p, in, bs);
            int at = -1; float best = 0; for (size_t i = 0; i < out.size(); ++i) if (std::fabs (out[i]) > best) { best = std::fabs (out[i]); at = (int) i; }
            const int l = at - 2000; worst = std::max (worst, std::abs (l - L0)); ++count;
            if (std::abs (l - L0) > 1) { constant = false; std::printf ("        modo %d rota %d ms %d os %d: latência %d (esperado %d)\n", mode, route, ms, os, l, L0); }
        }
        CHECK (constant, "latência constante em %d combinações (3 modos x rotas x M/S x 1x/4x/16x/32x): reportada %d, desvio máx %d", count, L0, worst);

        // tetos em Hybrid (ambas as rotas), com M/S e multibanda
        struct Cfg { const char* name; int mode, route, ms, mb, tp; };
        for (Cfg c : { Cfg { "Hybrid Clip>Limit", 2, 0, 0, 0, 1 }, Cfg { "Hybrid Limit>Clip", 2, 1, 0, 0, 1 }, Cfg { "Hybrid Clip>Limit M/S", 2, 0, 1, 0, 1 }, Cfg { "Hybrid Limit>Clip M/S TP off", 2, 1, 1, 0, 0 },
                       Cfg { "Clipper multibanda", 1, 0, 0, 1, 1 }, Cfg { "Clipper multibanda M/S", 1, 0, 1, 1, 1 }, Cfg { "Limiter M/S", 0, 0, 1, 0, 1 } })
        {
            reset(); setParam (p, ids::mode, (float) c.mode); setParam (p, ids::route, (float) c.route); setParam (p, ids::ms, (float) c.ms); setParam (p, ids::mb, (float) c.mb); setParam (p, ids::tp, (float) c.tp);
            setParam (p, ids::gain, 12.f); setParam (p, ids::ceiling, -1.f); p.prepareToPlay (fs, bs);
            auto o = run (p, prog, bs); const double pk = peakOf (o, 3000), c1 = std::pow (10.0, -1.0 / 20.0);
            CHECK (pk <= c1 * 1.0006 && pk > 0.5, "%s +12 dB, teto -1: pico %.4f (teto %.4f), TP máx %.2f dBTP", c.name, pk, c1, p.meters.tpMaxDb.load());
        }

        // meters do clipper e histórico de GR
        reset(); setParam (p, ids::mode, 1.f); setParam (p, ids::gain, 9.f); setParam (p, ids::ceiling, -3.f); p.prepareToPlay (fs, bs); run (p, prog, bs);
        CHECK (p.meters.clipDensityPct.load() > 0.f && p.meters.clipMaxDb.load() > 0.f && p.meters.clipEvents.load() > 0, "ClipStats: densidade %.1f%%, corte máx %.1f dB, %d eventos/bloco", p.meters.clipDensityPct.load(), p.meters.clipMaxDb.load(), p.meters.clipEvents.load());
        CHECK (p.grHistory.head.load() > 100, "Histórico de GR a gravar (%d entradas)", p.grHistory.head.load());
        { std::vector<float> snap (4096); p.snapshotOutput (snap.data(), 4096); double e = 0; for (float v : snap) e += (double) v * v; CHECK (e > 0.0, "Anel de saída para o monitor tem sinal"); }

        // delta: saída_delta == saída_normal - entrada alinhada
        {
            reset(); setParam (p, ids::mode, 0.f); setParam (p, ids::gain, 6.f); p.prepareToPlay (fs, bs); auto wet = run (p, prog, bs);
            setParam (p, ids::delta, 1.f); p.prepareToPlay (fs, bs); auto del = run (p, prog, bs); double err = 0;
            for (size_t i = (size_t) L0 + 100; i < prog.size(); ++i) err = std::max (err, (double) std::fabs (del[i] - (wet[i] - prog[i - (size_t) L0])));
            CHECK (err < 1e-4, "Delta total == saída - entrada alinhada (erro máx %.2e)", err);
            // delta por estágio: clipper sem corte devolve ~0; limitador com GR devolve sinal
            reset(); setParam (p, ids::mode, 2.f); setParam (p, ids::gain, 0.f); setParam (p, ids::ceiling, 0.f); setParam (p, ids::ratio, 100.f); setParam (p, ids::delta, 1.f); setParam (p, ids::deltasel, 1.f); p.prepareToPlay (fs, bs);
            std::vector<float> quiet (prog.size()); for (size_t i = 0; i < quiet.size(); ++i) quiet[i] = prog[i] * 0.2f; auto dq = run (p, quiet, bs);
            CHECK (peakOf (dq, 3000) < 1e-3, "Delta do clipper sem corte é ~0 (pico %.2e)", peakOf (dq, 3000));
            reset(); setParam (p, ids::mode, 2.f); setParam (p, ids::gain, 9.f); setParam (p, ids::ceiling, -3.f); setParam (p, ids::ratio, 100.f); setParam (p, ids::delta, 1.f); setParam (p, ids::deltasel, 1.f); p.prepareToPlay (fs, bs);
            auto dc = run (p, prog, bs); setParam (p, ids::deltasel, 2.f); p.prepareToPlay (fs, bs); auto dl = run (p, prog, bs);
            CHECK (peakOf (dc, 3000) > 0.01 && std::isfinite (peakOf (dl, 3000)), "Dual Delta: clipper %.3f, limiter %.3f (picos do sinal de diferença)", peakOf (dc, 3000), peakOf (dl, 3000));
        }

        // Aliasing AUTO sobe com densidade alta e HF; HQ offline força 32x sem mudar a latência
        {
            reset(); setParam (p, ids::mode, 1.f); setParam (p, ids::alias, 2.f); setParam (p, ids::gain, 18.f); setParam (p, ids::ceiling, -6.f); setParam (p, ids::shape, 2.f); p.prepareToPlay (fs, bs);
            std::vector<float> hf ((size_t) fs * 3); for (size_t i = 0; i < hf.size(); ++i) hf[i] = (float) (0.8 * std::sin (2 * M_PI * 5000.0 * (double) i / fs)); auto oh = run (p, hf, bs);
            const int stHi = p.meters.osStage.load();
            setParam (p, ids::gain, 0.f); setParam (p, ids::ceiling, 0.f); std::vector<float> lo ((size_t) fs * 3); for (size_t i = 0; i < lo.size(); ++i) lo[i] = (float) (0.05 * std::sin (2 * M_PI * 200.0 * (double) i / fs)); auto ol = run (p, lo, bs);
            CHECK (stHi == 4 && p.meters.osStage.load() == 0, "Aliasing AUTO: densidade alta -> estágio %d (16x), material suave depois -> estágio %d (1x)", stHi, p.meters.osStage.load());
            auto maxStep = [&] (const std::vector<float>& v) { double m = 0; for (size_t i = 3000; i < v.size(); ++i) m = std::max (m, (double) std::fabs (v[i] - v[i - 1])); return m; };
            auto fixedRun = [&] (int osIdx) { reset(); setParam (p, ids::mode, 1.f); setParam (p, ids::os, (float) osIdx); setParam (p, ids::gain, 18.f); setParam (p, ids::ceiling, -6.f); setParam (p, ids::shape, 2.f); p.prepareToPlay (fs, bs); return run (p, hf, bs); };
            const double stepAuto = maxStep (oh), stepRef = std::max (maxStep (fixedRun (0)), maxStep (fixedRun (2)));
            CHECK (stepAuto <= stepRef * 1.05, "Troca AUTO sem descontinuidade: maior passo %.3f <= %.3f (máx. de 1x e 16x fixos)", stepAuto, stepRef * 1.05);
            reset(); setParam (p, ids::mode, 1.f); setParam (p, ids::os, 0.f); p.setNonRealtime (true); p.prepareToPlay (fs, bs); run (p, prog, bs);
            CHECK (p.meters.osStage.load() == 5 && p.reportedLatency() == L0, "HQ offline: 32x (estágio %d) com latência inalterada (%d)", p.meters.osStage.load(), p.reportedLatency());
            p.setNonRealtime (false);
        }

        // A/B/C/D, Ceiling Lock, persistência dos slots
        {
            reset(); p.prepareToPlay (fs, bs);
            setParam (p, ids::gain, 5.f); setParam (p, ids::ceiling, -3.f); p.storeSlot (0); setParam (p, ids::gain, 10.f); setParam (p, ids::ceiling, -1.f);
            p.recallSlot (0); CHECK (std::fabs (p.apvts.getRawParameterValue (ids::gain)->load() - 5.f) < 0.01f && std::fabs (p.apvts.getRawParameterValue (ids::ceiling)->load() + 3.f) < 0.01f, "Slot A recupera ganho 5 dB e teto -3 dB");
            setParam (p, ids::ceiling, -1.f); setParam (p, ids::gain, 10.f); setParam (p, ids::ceilock, 1.f); p.recallSlot (0);
            CHECK (std::fabs (p.apvts.getRawParameterValue (ids::gain)->load() - 5.f) < 0.01f && std::fabs (p.apvts.getRawParameterValue (ids::ceiling)->load() + 1.f) < 0.01f, "Ceiling Lock: o slot muda o ganho mas o teto fica em -1 dB");
            p.storeSlot (2); juce::MemoryBlock mb; p.getStateInformation (mb); LoudnexProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q.slotUsed (0) && q.slotUsed (2) && ! q.slotUsed (1), "Slots A e C sobrevivem a guardar/restaurar o estado (B vazio)");
            setParam (p, ids::ceilock, 0.f);
        }

        // assistentes sobre áudio capturado (8 s com sinal) + Balance
        {
            reset(); setParam (p, ids::target, 5.f); p.prepareToPlay (fs, bs); p.startAutoDose();
            std::vector<float> long8; for (int k = 0; k < 6; ++k) long8.insert (long8.end(), prog.begin(), prog.end()); for (auto& v : long8) v *= 0.3f; run (p, long8, bs);
            const bool ready = p.dose.state.load() == 2; const bool ok = p.runAssistants(); const auto& a = p.assistInfo();
            std::printf ("        Auto-Dose: LUFS medido %.1f, ganho sugerido %.2f dB, alcançável %d\n", p.dose.measuredLufs.load(), p.dose.gainDb.load(), (int) p.dose.reachable.load());
            CHECK (ready && ok && a.valid && a.profile.crestDb > 6.0 && std::fabs (a.clipShareDb + a.limitShareDb - p.dose.gainDb.load()) < 1e-3 && p.dose.gainDb.load() > 3.f, "Assistentes: crest %.1f dB, clip amount %.1f dB, rota %s, distribuição clip %.1f / limit %.1f dB, perfil harmónico %d", a.profile.crestDb, a.clipGainDb, a.order == lx::dsp::HybridOrder::ClipThenLimit ? "Clip>Limit" : "Limit>Clip", a.clipShareDb, a.limitShareDb, a.qo.best + 1);
            p.applyAssistants(); CHECK (juce::roundToInt (p.apvts.getRawParameterValue (ids::mode)->load()) == 2 && juce::roundToInt (p.apvts.getRawParameterValue (ids::hchar)->load()) == a.qo.best + 1, "Aplicar assistentes: modo Hybrid e carácter harmónico escritos nos parâmetros");
            setParam (p, ids::balance, 0.f); p.applyBalance(); const float punch0 = p.apvts.getRawParameterValue (ids::punch)->load();
            setParam (p, ids::balance, 100.f); p.applyBalance(); const float punch1 = p.apvts.getRawParameterValue (ids::punch)->load();
            CHECK (punch0 > 90.f && punch1 < 10.f, "Balance Punch -> Loud: Punch Protect %.0f%% -> %.0f%%", punch0, punch1);
        }

        // outros sample rates: tetos e latência em ms razoável
        for (double rate : { 44100.0, 96000.0, 192000.0 })
        {
            reset(); setParam (p, ids::mode, 2.f); setParam (p, ids::gain, 12.f); setParam (p, ids::ceiling, -1.f); setParam (p, ids::alias, 2.f); p.setPlayConfigDetails (2, 2, rate, bs); p.prepareToPlay (rate, bs);
            std::vector<float> x ((size_t) rate * 2); for (size_t i = 0; i < x.size(); ++i) { const double tt = (double) i / rate; x[i] = (float) (0.7 * std::sin (2 * M_PI * 90.0 * tt) + 0.4 * std::sin (2 * M_PI * 7300.0 * tt)); }
            auto o = run (p, x, bs); const double pk = peakOf (o, (size_t) rate / 10);
            CHECK (pk <= std::pow (10.0, -1.0 / 20.0) * 1.0006 && p.meters.tpMaxDb.load() < -0.6f, "%.0f Hz Hybrid +12 dB: pico %.4f, TP %.2f dBTP, latência %.2f ms", rate, pk, p.meters.tpMaxDb.load(), 1000.0 * p.reportedLatency() / rate);
        }
        reset(); p.setPlayConfigDetails (2, 2, fs, bs); p.prepareToPlay (fs, bs);

        // fuzz de parâmetros: saída sempre finita e dentro do teto
        {
            std::mt19937 rng (2026); std::uniform_real_distribution<double> u (0, 1); int bad = 0, total = 0; double worstOver = 0;
            for (int k = 0; k < 40; ++k)
            {
                reset(); const double ceilDb = -u (rng) * 6; setParam (p, ids::mode, (float) (int) (u (rng) * 2.99)); setParam (p, ids::route, (float) (int) (u (rng) * 1.99)); setParam (p, ids::ms, u (rng) > 0.5); setParam (p, ids::mb, u (rng) > 0.5);
                setParam (p, ids::tp, u (rng) > 0.3); setParam (p, ids::gain, (float) (u (rng) * 24)); setParam (p, ids::ceiling, (float) ceilDb); setParam (p, ids::shape, (float) (int) (u (rng) * 3.99)); setParam (p, ids::os, (float) (int) (u (rng) * 3.99));
                setParam (p, ids::alias, (float) (int) (u (rng) * 3.99)); setParam (p, ids::hchar, (float) (int) (u (rng) * 4.99)); setParam (p, ids::dual, u (rng) > 0.5); setParam (p, ids::dcblock, u (rng) > 0.5); setParam (p, ids::bias, (float) (u (rng) * 100));
                setParam (p, ids::transel, (float) (u (rng) * 100)); setParam (p, ids::ratio, (float) (u (rng) * 100)); setParam (p, ids::relmode, (float) (int) (u (rng) * 2.99)); setParam (p, ids::punch, (float) (u (rng) * 100)); setParam (p, ids::distguard, (float) (int) (u (rng) * 3.99));
                setParam (p, ids::postgain, (float) (-u (rng) * 6)); setParam (p, ids::clippos, (float) (-u (rng) * 3)); setParam (p, ids::clipneg, (float) (-u (rng) * 3)); setParam (p, ids::mix, 100.f); setParam (p, ids::lookahead, (float) (0.2 + u (rng) * 4.8));
                p.prepareToPlay (fs, bs); auto o = run (p, prog, 64 + (int) (u (rng) * 900)); bool fin = true; for (float v : o) if (! std::isfinite (v)) fin = false;
                const double c1 = std::pow (10.0, ceilDb / 20.0); const double pk = peakOf (o, 4000); worstOver = std::max (worstOver, pk / c1); ++total; if (! fin || pk > c1 * 1.0006) { ++bad; std::printf ("        fuzz %d: pico %.4f vs teto %.4f finito %d | modo %.0f rota %.0f ms %.0f mb %.0f tp %.0f os %.0f alias %.0f hchar %.0f\n", k, pk, c1, fin, p.apvts.getRawParameterValue (ids::mode)->load(), p.apvts.getRawParameterValue (ids::route)->load(), p.apvts.getRawParameterValue (ids::ms)->load(), p.apvts.getRawParameterValue (ids::mb)->load(), p.apvts.getRawParameterValue (ids::tp)->load(), p.apvts.getRawParameterValue (ids::os)->load(), p.apvts.getRawParameterValue (ids::alias)->load(), p.apvts.getRawParameterValue (ids::hchar)->load()); }
            }
            CHECK (bad == 0, "%d combinações aleatórias de TODOS os modos/parâmetros: saída finita e nunca acima do teto (pior %.4f x teto)", total, worstOver);
        }
        reset(); p.prepareToPlay (fs, bs);
    }
    // 6d) segurança em tempo real (sem alocações) e custo de CPU
    {
        auto setP = [&] (const char* id, float v) { setParam (p, id, v); };
        struct Cfg { const char* name; int mode, route, os, alias, ms, mb, delta; };
        const Cfg cfgs[] = {
            { "Limiter", 0, 0, 1, 0, 0, 0, 0 }, { "Limiter M/S", 0, 0, 1, 0, 1, 0, 0 },
            { "Clipper 1x", 1, 0, 0, 0, 0, 0, 0 }, { "Clipper 4x", 1, 0, 1, 0, 0, 0, 0 }, { "Clipper 16x", 1, 0, 2, 0, 0, 0, 0 }, { "Clipper 32x", 1, 0, 3, 0, 0, 0, 0 },
            { "Clipper 16x multibanda", 1, 0, 2, 0, 0, 1, 0 }, { "Hybrid Clip>Limit 16x", 2, 0, 2, 0, 0, 0, 0 }, { "Hybrid Limit>Clip 16x", 2, 1, 2, 0, 0, 0, 0 },
            { "Hybrid AUTO + M/S + multibanda", 2, 0, 1, 2, 1, 1, 0 }, { "PIOR CASO: Hybrid Limit>Clip 32x multibanda M/S Delta", 2, 1, 3, 0, 1, 1, 1 } };
        std::printf ("        CPU de cada configuração (1 núcleo, estéreo 48 kHz, blocos de 512, compilação Release; este servidor, não o seu computador):\n");
        double worstCpu = 0; long allocTotal = 0; bool allocOk = true; struct mallinfo2 m0 {}, m1 {};
        for (const Cfg& c : cfgs)
        {
            setP (ids::mode, (float) c.mode); setP (ids::route, (float) c.route); setP (ids::os, (float) c.os); setP (ids::alias, (float) c.alias); setP (ids::ms, (float) c.ms); setP (ids::mb, (float) c.mb); setP (ids::delta, (float) c.delta);
            setP (ids::gain, 12.f); setP (ids::ceiling, -1.f); setP (ids::tp, 1.f); setP (ids::relmode, 2.f); setP (ids::distguard, 2.f); setP (ids::transel, 40.f); setP (ids::dual, 1.f); setP (ids::hchar, 0.f); setP (ids::mix, 100.f);
            p.prepareToPlay (fs, bs); run (p, prog, bs);                         // aquece (variáveis, caches)
            juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer midi; const int blocks = 400;
            m0 = mallinfo2(); g_allocs = 0; g_track = true; const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < blocks; ++k) { for (int ch = 0; ch < 2; ++ch) std::copy (prog.begin() + (k % 90) * bs, prog.begin() + (k % 90) * bs + bs, b.getWritePointer (ch)); p.processBlock (b, midi); }
            const auto t1 = std::chrono::steady_clock::now(); g_track = false; m1 = mallinfo2();
            const double sec = std::chrono::duration<double> (t1 - t0).count(), cpu = 100.0 * sec / ((double) blocks * bs / fs);
            worstCpu = std::max (worstCpu, cpu); allocTotal += g_allocs.load(); if (g_allocs.load() != 0 || (long) m1.uordblks > (long) m0.uordblks + 4096) allocOk = false;
            std::printf ("          %-62s %6.2f %% CPU   (operator new: %ld, variação do heap: %+ld bytes)\n", c.name, cpu, g_allocs.load(), (long) m1.uordblks - (long) m0.uordblks);
        }
        CHECK (allocOk, "Tempo real: 0 alocações (operator new) e sem crescimento do heap em processBlock nas %d configurações (total %ld)", (int) (sizeof (cfgs) / sizeof (cfgs[0])), allocTotal);
#if defined(__SANITIZE_ADDRESS__)
        std::printf ("        (build com sanitizers: o limiar de CPU não se aplica, pior caso %.0f %%)\n", worstCpu); CHECK (true, "CPU não avaliado com ASan/UBSan");
#else
        CHECK (worstCpu < 60.0, "CPU do pior caso (%.1f %% de um núcleo neste servidor) abaixo de 60 %%", worstCpu);
#endif
        setP (ids::mode, 0.f); setP (ids::route, 0.f); setP (ids::os, 1.f); setP (ids::alias, 0.f); setP (ids::ms, 0.f); setP (ids::mb, 0.f); setP (ids::delta, 0.f); setP (ids::dual, 0.f); setP (ids::transel, 0.f); setP (ids::relmode, 0.f); setP (ids::distguard, 0.f); setP (ids::gain, 0.f);
        p.prepareToPlay (fs, bs);
    }
    // 7) editor: render para PNG (verificação visual)
    {
        setParam (p, ids::mode, 0.f); setParam (p, ids::gain, 9.f); p.prepareToPlay (fs, bs);
        for (int i = 0; i < 80; ++i) run (p, std::vector<float> (prog.begin() + i * 1000, prog.begin() + i * 1000 + 1000), 500);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); ed->setSize (1280, 820);
        auto* le = dynamic_cast<LoudnexEditor*> (ed.get());
        struct Shot { int mode, page; bool adv, light; const char* name; };
        const Shot shots[] = { { 0,0,false,false,"lim_basic_dark" }, { 0,0,true,false,"lim_adv_dark" }, { 1,0,false,false,"clip_basic_dark" }, { 1,0,true,false,"clip_adv_dark" },
                               { 2,0,false,false,"hyb_basic_dark" }, { 2,0,true,false,"hyb_adv_dark" }, { 2,1,false,false,"multiband_dark" }, { 2,2,false,false,"assist_dark" },
                               { 0,0,false,true,"lim_basic_light" }, { 1,0,true,true,"clip_adv_light" }, { 2,1,false,true,"multiband_light" }, { 2,2,false,true,"assist_light" } };
        for (auto& s : shots)
        {
            setParam (p, ids::mode, (float) s.mode); setParam (p, ids::mb, s.page == 1 ? 1.f : 0.f);
            for (int i = 0; i < 60; ++i) run (p, std::vector<float> (prog.begin() + i * 1000, prog.begin() + i * 1000 + 1000), 500);
            if (le != nullptr) le->showForTest (s.page, s.adv, s.light);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String ("shot_") + s.name + ".png"));
            juce::FileOutputStream fo (f); fo.setPosition (0); fo.truncate(); juce::PNGImageFormat().writeImageToStream (img, fo);
        }
        if (le != nullptr) le->showForTest (0, false, false);
        CHECK (true, "editor criado e desenhado em 3 modos, 3 páginas e 2 skins");
        AccessManager::get().logout(); juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile ("editor_login.png"));
        juce::FileOutputStream fo (f); fo.setPosition (0); fo.truncate(); juce::PNGImageFormat().writeImageToStream (img, fo);
    }
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails);
    return fails ? 1 : 0;
}
