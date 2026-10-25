// =============================================================================
//  LABLOUD by Piradex — interface (JUCE). Desenhada em 1280 x 820 e escalada com proporção fixa.
//  Três modos (Limiter / Clipper / Hybrid), três páginas (Principal / Multibanda / Assistentes), duas skins (escura / clara),
//  camadas Básico / Avançado. Todos os controlos estão ligados aos 48 parâmetros do processador.
// =============================================================================
#pragma once
#include "PluginProcessor.h"
#include <vector>

namespace lxui
{
    // ---------------------------------------------------------------- skins
    inline bool& lightSkin() { static bool v = false; return v; }
    inline juce::Colour pick (juce::uint32 dark, juce::uint32 light) { return juce::Colour (lightSkin() ? light : dark); }
    inline juce::Colour cBg()     { return pick (0xff0f1114, 0xffeceae4); }
    inline juce::Colour cPanel()  { return pick (0xff171a1f, 0xfff7f6f2); }
    inline juce::Colour cPanel2() { return pick (0xff1d2127, 0xffffffff); }
    inline juce::Colour cLine()   { return pick (0xff2b3037, 0xffd4d1c8); }
    inline juce::Colour cTx()     { return pick (0xffecebe6, 0xff1c1d20); }
    inline juce::Colour cMu()     { return pick (0xffa3a9b2, 0xff565b63); }
    inline juce::Colour cAm()     { return pick (0xffffb224, 0xffa15c00); }
    inline juce::Colour cCy()     { return pick (0xff3fd6c6, 0xff00796f); }
    inline juce::Colour cGray()   { return pick (0xff6b7480, 0xff8a919b); }
    inline juce::Colour cOk()     { return pick (0xff8fe388, 0xff2e7d32); }
    inline juce::Colour cBad()    { return pick (0xffff6b5e, 0xffc0392b); }
    inline juce::Colour cOnAcc()  { return pick (0xff101214, 0xffffffff); }
    inline juce::Colour accent (int a) { return a == 0 ? cAm() : cCy(); }      // 0 = âmbar (limiter) · 1 = ciano (clipper)
    inline juce::Font fontOf (float h, bool bold = false) { return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain)); }
    inline juce::String T8 (const char* s) { return juce::String::fromUTF8 (s); }

    // cores fixas do ecrã de login
    constexpr juce::uint32 kBg = 0xff090b12, kPanel = 0xff12151f, kLine = 0xff262b3a, kText = 0xffe8ecf6, kDim = 0xff7b8398;

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look() { refresh(); }
        void refresh();
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float mn, float mx, juce::Slider::SliderStyle, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return fontOf (12.f, true); }
    };

    // botão rotativo com título (acc: 0 âmbar, 1 ciano)
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, const juce::String& suffix, int decimals, int acc);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::Slider slider;
    private:
        juce::String title;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    // escolha segmentada: ligada a um parâmetro de escolha, ou manual (interface apenas)
    class Seg : public juce::Component
    {
    public:
        Seg (juce::AudioProcessorValueTreeState&, const char* paramId, juce::StringArray labels, int acc);
        Seg (juce::StringArray labels, int acc, std::function<void (int)> onChange);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void setSelected (int i) { selected = i; repaint(); }
        int getSelected() const { return selected; }
    private:
        juce::StringArray labels; int acc; int selected = 0; std::function<void (int)> onChange;
        std::unique_ptr<juce::ParameterAttachment> attach;
    };

    // interruptor em pílula ligado a um parâmetro booleano
    class Pill : public juce::ToggleButton
    {
    public:
        Pill (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& text, int acc);
    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attach;
    };

    // regulador horizontal com título e valor
    class HSlider : public juce::Component
    {
    public:
        HSlider (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, const juce::String& suffix, int decimals, int acc);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::Slider s;
    private:
        juce::String title, suffix; int decimals;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    // histórico de nível (entrada / saída / redução de ganho) + linha do teto
    class GrView : public juce::Component
    {
    public:
        GrView (LoudnexProcessor& p, int acc) : proc (p), acc (acc) { hist.resize ((size_t) kN); }
        void tick();
        void paint (juce::Graphics&) override;
        int acc;
    private:
        static constexpr int kN = 300;
        struct Pt { float in = -120.f, out = -120.f, gr = 0.f; };
        LoudnexProcessor& proc; std::vector<Pt> hist; int pos = 0;
    };

    // curva de transferência real do clipper (mede o próprio Clipper com os parâmetros atuais) + harmónicos
    class CurveView : public juce::Component
    {
    public:
        explicit CurveView (LoudnexProcessor& p) : proc (p) {}
        void refresh();
        void paint (juce::Graphics&) override;
    private:
        LoudnexProcessor& proc; std::vector<juce::Point<float>> pts; double ceilLin = 0.89; double keyHash = -1;
    };
    class HarmView : public juce::Component
    {
    public:
        explicit HarmView (LoudnexProcessor& p) : proc (p) {}
        void refresh();
        void paint (juce::Graphics&) override;
    private:
        LoudnexProcessor& proc; float db[8] = {}; float thd = 0.f; double keyHash = -1;
    };

    // espectro da saída (64 bandas logarítmicas) com as 3 bandas e os cruzamentos
    class SpectrumView : public juce::Component
    {
    public:
        explicit SpectrumView (LoudnexProcessor& p) : proc (p) {}
        void refresh();
        void paint (juce::Graphics&) override;
    private:
        LoudnexProcessor& proc; float band[64] = {}; bool have = false;
    };

    // painel de medidores (coluna da direita das páginas Principal e Multibanda)
    class MeterView : public juce::Component
    {
    public:
        explicit MeterView (LoudnexProcessor& p) : proc (p) {}
        void tick();
        void paint (juce::Graphics&) override;
        int mode = 0;
    private:
        LoudnexProcessor& proc; float inD = -120.f, outD = -120.f;
    };

    // Auto-Dose (ouve 8 s e sugere)
    class DoseBar : public juce::Component
    {
    public:
        explicit DoseBar (LoudnexProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();
    private:
        LoudnexProcessor& proc; juce::TextButton start { "Auto-Dose · ouvir 8 s" }, apply { "Aplicar" };
    };

    // página dos assistentes: resultados das regras determinísticas
    class AssistView : public juce::Component
    {
    public:
        explicit AssistView (LoudnexProcessor& p);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();
    private:
        LoudnexProcessor& proc; juce::TextButton listen { "Auto-Dose · ouvir 8 s" }, analyse { "Analisar material" }, applyAll { "Aplicar tudo" };
        juce::String note;
    };

    // três números de eficiência de loudness
    class EffView : public juce::Component
    {
    public:
        explicit EffView (LoudnexProcessor& p) : proc (p) {}
        void paint (juce::Graphics&) override;
    private:
        LoudnexProcessor& proc;
    };

    class Login : public juce::Component
    {
    public:
        explicit Login (std::function<void()> onSuccess);
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        void submit();
        std::function<void()> onOk;
        juce::TextEditor pw; juce::ToggleButton remember { "Lembrar neste computador" };
        juce::TextButton enter { "ENTRAR" }; juce::Label msg; bool busy = false;
    };
}

class LoudnexEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LoudnexEditor (LoudnexProcessor&);
    ~LoudnexEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void showForTest (int pageIndex, bool advanced, bool light) { page = pageIndex; adv = advanced; pageSeg.setSelected (pageIndex); advSeg.setSelected (advanced ? 1 : 0); lxui::lightSkin() = light; applySkin(); layoutAll(); }

private:
    using K = lxui::Knob;
    void timerCallback() override;
    void layoutAll();
    void applySkin();
    void slotClicked (int i);

    LoudnexProcessor& proc;
    lxui::Look look;
    juce::Component content;

    // cabeçalho
    lxui::Seg modeSeg, pageSeg, advSeg;
    juce::TextButton slotBtn[4], saveBtn { "Guardar" }, undoBtn { "Anular" }, redoBtn { "Refazer" }, skinBtn { "Skin" }, logoutBtn { "Sair" };
    bool saveMode = false; int slotSel = -1;

    // botões rotativos (um por parâmetro)
    K gain, ceiling, release, lookahead, punch, mix, knee, cexp, bias, postgain, bassamt, rlink, link, bassfreq, clippos, clipneg, transel, dualoff, xo1, xo2, mbg1, mbg2, mbg3;
    lxui::Pill tp, bass, gainmatch, ceilock, dual, dcblock, mb, delta, ms, hq;
    lxui::Seg character, relmode, spectral, distguard, shape, os, alias, hchar, route, deltasel;
    lxui::HSlider ratio, balance;
    lxui::GrView grView;
    lxui::CurveView curve; lxui::HarmView harm; lxui::SpectrumView spectrum;
    lxui::MeterView meterView; lxui::DoseBar doseBar; lxui::AssistView assistView; lxui::EffView effView;
    lxui::Login login;

    struct CardR { juce::Rectangle<int> r; juce::String title; };
    std::vector<CardR> cards;
    std::vector<juce::Component*> everything;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;     // textos soltos pintados no editor
    int page = 0, shownMode = -1, tick = 0; bool adv = false, wasUnlocked = false;
};
