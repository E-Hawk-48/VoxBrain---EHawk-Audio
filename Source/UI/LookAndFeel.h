#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace vf
{
// ============================================================================
//  Theme — central colour palette (dark glass aesthetic)
// ============================================================================
namespace theme
{
    inline const juce::Colour bg          { 0xff0d1117 };
    inline const juce::Colour panel       { 0xff161b26 };
    inline const juce::Colour panelLight  { 0xff1e2533 };
    inline const juce::Colour outline     { 0xff2a3244 };
    inline const juce::Colour accent      { 0xff4f8cff };   // electric blue
    inline const juce::Colour accentWarm  { 0xffff7847 };   // learn/record orange
    inline const juce::Colour accentGreen { 0xff3ddc97 };
    inline const juce::Colour text        { 0xffd7dee9 };
    inline const juce::Colour textDim     { 0xff7c8798 };
}

// ============================================================================
//  VoxBrainLookAndFeel — modern rotary knobs, pill buttons, glass panels
// ============================================================================
class VoxBrainLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoxBrainLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    juce::Font getLabelFont (juce::Label&) override;
};
} // namespace vf
