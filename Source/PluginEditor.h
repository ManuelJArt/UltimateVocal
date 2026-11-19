#pragma once
#include "PluginProcessor.h"

// ============================================================================
//  Estética HUD futurista: azul marino, trazos finos blancos, acento cian
// ============================================================================
namespace hud
{
    const juce::Colour bgTop    { 0xff04101f };
    const juce::Colour bgBottom { 0xff0b2a47 };
    const juce::Colour line     { 0xffd8ecff };
    const juce::Colour dim      { 0xff5d7a99 };
    const juce::Colour accent   { 0xff1fc8ff };
    const juce::Colour accentHi { 0xff8be4ff };
    const juce::Colour warn     { 0xffffb347 };

    juce::Font mono (float size, bool bold = false);
}

class HudLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HudLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return hud::mono (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return hud::mono (11.0f); }
    juce::Font getPopupMenuFont() override { return hud::mono (13.0f); }
};

// --- potenciómetro rotatorio con etiqueta y valor --------------------------------
class HudKnob : public juce::Component
{
public:
    HudKnob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
             const juce::String& title, const juce::String& suffix, int decimals);
    void resized() override;
    juce::Slider slider;
private:
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// --- anillo central con la nota actual -----------------------------------------------
class RingComponent : public juce::Component, private juce::Timer
{
public:
    explicit RingComponent (UltimateVocalProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    UltimateVocalProcessor& proc;
    float smoothedCents = 0.0f;
};

// --- editor de tono: traza detectada, corregida y bloques de nota ---------------------
class PitchGraph : public juce::Component, private juce::Timer
{
public:
    explicit PitchGraph (UltimateVocalProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override { repaint(); }
    UltimateVocalProcessor& proc;
    float centre = 60.0f;
    static constexpr int kVisible = 1100;
    static constexpr float kRangeSt = 26.0f;
    float rowTop (float midi, juce::Rectangle<float> r, float lo) const;
};

// --- teclado de escala --------------------------------------------------------------
class ScaleKeyboard : public juce::Component, private juce::Timer
{
public:
    explicit ScaleKeyboard (UltimateVocalProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void timerCallback() override { repaint(); }
    int keyAt (juce::Point<float>) const;
    juce::Rectangle<float> keyRect (int pc) const;
    UltimateVocalProcessor& proc;
};

// --- medidores -----------------------------------------------------------------------
class MetersComponent : public juce::Component, private juce::Timer
{
public:
    explicit MetersComponent (UltimateVocalProcessor& p) : proc (p) { startTimerHz (30); }
    void paint (juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    void drawBar (juce::Graphics&, juce::Rectangle<float>, float norm, const juce::String&, bool fromTop);
    UltimateVocalProcessor& proc;
};

// ============================================================================
class UltimateVocalEditor : public juce::AudioProcessorEditor
{
public:
    explicit UltimateVocalEditor (UltimateVocalProcessor&);
    ~UltimateVocalEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String& id, const juce::String& title);

    UltimateVocalProcessor& proc;
    HudLookAndFeel laf;

    RingComponent ring;
    PitchGraph graph;
    ScaleKeyboard keyboard;
    MetersComponent meters;

    juce::ComboBox keyBox, scaleBox;
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAtt;

    HudKnob retune, amount, transpose, fine, formant, mix, inGain, hpf, gate, compThr, compRatio, outGain;

    juce::Rectangle<int> pRing, pGraph, pTuning, pKeys, pInput, pVoice, pDynamics, pMeters;
};
