// Testes dos assistentes (determinismo, monotonia, histerese)
#include "DSP/Assist.h"
#include "test_util.h"
int main()
{
    const double fs = 48000.0;
    auto kick = kickTrain (fs, 4.0, 1.4, 0.5);
    auto pad = sineV (fs, 220, 0.4, (int) (fs * 4)); { auto h = sineV (fs, 1500, 0.2, (int) (fs * 4)); for (size_t i = 0; i < pad.size(); ++i) pad[i] += h[i]; }
    std::printf ("[Perfil do material, Smart Clip Amount, Auto Routing, Smart Distribution]\n");
    auto mk = profileMaterial (kick.data(), (int) kick.size(), fs), mp = profileMaterial (pad.data(), (int) pad.size(), fs);
    CHECK (mk.crestDb > mp.crestDb + 3.0 && mk.transientness > mp.transientness, "Bumbos: crest %.1f dB, transiência %.2f; pad: crest %.1f dB, transiência %.2f", mk.crestDb, mk.transientness, mp.crestDb, mp.transientness);
    CHECK (autoRouting (mk) == HybridOrder::ClipThenLimit && autoRouting (mp) == HybridOrder::LimitThenClip, "Auto Routing: bumbos -> Clip>Limit, pad -> Limit>Clip");
    { auto a = profileMaterial (kick.data(), (int) kick.size(), fs); CHECK (a.p98Db == mk.p98Db && a.transientness == mk.transientness, "Perfil determinístico"); }
    { const double g = smartClipAmountDb (mp, -1.0); CHECK (g >= 0 && g <= 8, "Smart Clip Amount dentro de 0..8 dB (%.2f dB)", g); }
    { auto d1 = smartDistribution (6.0, mk), d2 = smartDistribution (6.0, mp); CHECK (d1.clipDb > d2.clipDb && std::fabs (d1.clipDb + d1.limitDb - 6.0) < 1e-9, "Distribution: percussivo dá %.2f dB ao clipper vs %.2f dB; soma = 6 dB", d1.clipDb, d2.clipDb); }
    CHECK (std::fabs (hybridClipThresholdDb (-1.0, 1.0) + 1.0) < 1e-12 && std::fabs (hybridClipThresholdDb (-1.0, 0.0) - 5.0) < 1e-12, "Ratio Clip/Limit: r=1 -> teto, r=0 -> +6 dB");
    { auto b0 = mapBalance (0), b1 = mapBalance (1), bm = mapBalance (0.5);
      CHECK (b0.punchProtect > bm.punchProtect && bm.punchProtect > b1.punchProtect && b0.clipRatio < b1.clipRatio && b0.clipAssist < b1.clipAssist, "Balance Punch->Loud monótono"); }

    std::printf ("\n[Aliasing Guard com histerese]\n");
    {
        AliasingGuard g; int flips = 0, last = 0;
        for (int i = 0; i < 400; ++i) { const double d = (i % 2) ? 0.55 : 0.45; const int s = g.update (d, 0.0, AliasMode::Auto); if (s != last) { ++flips; last = s; } }
        CHECK (flips <= 1, "Densidade a oscilar à volta do limiar: %d mudança(s) de estágio (sem flapping)", flips);
        g.reset(); for (int i = 0; i < 5; ++i) g.update (10.0, 0.5, AliasMode::Auto); CHECK (g.current() == 4, "Densidade alta -> 16x");
        int s = 4; int blocks = 0; for (; blocks < 100 && s == 4; ++blocks) s = g.update (0.0, 0.0, AliasMode::Auto); CHECK (s != 4 && blocks >= g.holdBlocks, "Desce só depois de %d blocos calmos (desceu ao fim de %d)", g.holdBlocks, blocks);
        AliasingGuard e; for (int i = 0; i < 5; ++i) e.update (50.0, 1.0, AliasMode::Eco); CHECK (e.current() == 2, "ECO limita a 4x");
        AliasingGuard m; CHECK (m.update (0.0, 0.0, AliasMode::Max) == 4, "MAX fixa 16x");
    }

    std::printf ("\n[Loudness Efficiency]\n");
    { auto e = loudnessEfficiency (-14.0, -9.0, 3.0, -35.0); CHECK (std::fabs (e.deltaLufs - 5.0) < 1e-12 && std::fabs (e.lufsPerGrDb - 5.0 / 3.0) < 1e-12, "dLUFS %.1f, GR %.1f dB, %.2f LU por dB de GR (sem nota única)", e.deltaLufs, e.grAvgDb, e.lufsPerGrDb); }

    std::printf ("\n[Quality Optimizer]\n");
    {
        ClipperParams b; b.gainDb = 6; b.ceilingDb = -2; b.mix = 1;
        auto r1 = qualityOptimize (kick.data(), (int) kick.size(), fs, b, 1.0), r2 = qualityOptimize (kick.data(), (int) kick.size(), fs, b, 1.0);
        bool same = r1.best == r2.best; for (int c = 0; c < 4; ++c) same = same && r1.cand[c].score == r2.cand[c].score;
        CHECK (same, "Determinístico: melhor = %d em ambas as corridas", r1.best);
        for (int c = 0; c < 4; ++c) std::printf ("        perfil %d: ganho de loudness %+.2f dB, DSR %.1f dB, pontuação %.2f\n", c, r1.cand[c].loudGainDb, r1.cand[c].dsrDb, r1.cand[c].score);
        auto free = qualityOptimize (kick.data(), (int) kick.size(), fs, b, 0.0), strict = qualityOptimize (kick.data(), (int) kick.size(), fs, b, 50.0);
        CHECK (free.cand[free.best].loudGainDb >= strict.cand[strict.best].loudGainDb - 1e-9 && strict.cand[strict.best].dsrDb <= free.cand[free.best].dsrDb + 1e-9, "Sem penalização escolhe o mais forte (%+.2f dB), com penalização alta escolhe o mais limpo (DSR %.1f dB)", free.cand[free.best].loudGainDb, strict.cand[strict.best].dsrDb);
    }
    std::printf ("\n%s (%d falhas)\n", fails ? "FALHOU" : "TUDO OK", fails); return fails ? 1 : 0;
}
