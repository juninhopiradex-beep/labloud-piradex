// =============================================================================
//  LABLOUD by Piradex — interface (JUCE). Estilo "Pulse": vidro escuro, neon menta/violeta (Limiter) e laranja/magenta (Clipper).
//  Desenhada em 1000 x 600 e escalada para qualquer tamanho com proporção fixa.
// =============================================================================
#pragma once
#include "PluginProcessor.h"
#include <vector>

namespace lxui
{
    struct Palette { juce::Colour a1, a2; };
    inline Palette paletteFor (int mode)
    {
        return mode == 0 ? Palette { juce::Colour (0xff3ef2c0), juce::Colour (0xff8b6bff) }
                         : Palette { juce::Colour (0xffff8a3d), juce::Colour (0xffff3d8b) };
    }
    constexpr juce::uint32 kBg = 0xff090b12, kPanel = 0xff12151f, kLine = 0xff262b3a, kText = 0xffe8ecf6, kDim = 0xff7b8398;

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();
        void setPalette (Palette p) { pal = p; }
        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
        Palette pal { paletteFor (0) };
    };

    // botão rotativo com título
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState&, const char* paramId, const juce::String& title, const juce::String& suffix, int decimals);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::Slider slider;
    private:
        juce::String title;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    // escolha segmentada ligada a um parâmetro de escolha
    class Segmented : public juce::Component
    {
    public:
        Segmented (juce::AudioProcessorValueTreeState&, const char* paramId, juce::StringArray labels, Look&);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
    private:
        juce::RangedAudioParameter* param = nullptr;
        juce::StringArray labels; Look& look; int selected = 0;
        std::unique_ptr<juce::ParameterAttachment> attach;
    };

    // histórico de nível (entrada / saída / redução de ganho) + linha do teto
    class Display : public juce::Component
    {
    public:
        explicit Display (LoudnexProcessor& p, Look& l) : proc (p), look (l) { hist.resize ((size_t) kN); }
        void tick();
        void paint (juce::Graphics&) override;
    private:
        static constexpr int kN = 300;
        struct Pt { float in = -120.f, out = -120.f, gr = 0.f; };
        LoudnexProcessor& proc; Look& look; std::vector<Pt> hist; int pos = 0;
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
private:
    void timerCallback() override;
    void applyMode (int mode);
    void layoutControls();

    LoudnexProcessor& proc;
    lxui::Look look;
    juce::Component content;
    lxui::Display display;
    lxui::Segmented modeSeg, characterSeg, shapeSeg, osSeg;
    lxui::Knob gain, ceiling, release, lookahead, assist, link, mixL, kneeC, mixC;
    juce::ToggleButton tpBtn { "True Peak" }, bassBtn { "Bass Guard" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tpAtt, bassAtt;
    lxui::Login login;
    juce::TextButton logoutBtn { "SAIR" };
    int shownMode = -1; bool wasUnlocked = false;
    float inDisp = -120.f, outDisp = -120.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoudnexEditor)
};
