#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include <memory>

namespace vf
{
// ============================================================================
//  Theme — the live, user-customizable colour palette. Colours are MUTABLE
//  globals (inline vars = one shared definition), so changing one recolours
//  the whole plugin on the next repaint. Default look: Neon Red + Neon Purple
//  on near-black violet glass.
// ============================================================================
namespace theme
{
    inline juce::Colour bg          { 0xff0a0612 };   // near-black, violet-tinted
    inline juce::Colour panel       { 0xff150d24 };
    inline juce::Colour panelLight  { 0xff231640 };
    inline juce::Colour outline     { 0xff3d2668 };
    inline juce::Colour accent      { 0xffb14dff };   // NEON PURPLE (primary)
    inline juce::Colour accentWarm  { 0xffff1e56 };   // NEON RED   (secondary)
    inline juce::Colour accentGreen { 0xff2be0a6 };   // positive / success
    inline juce::Colour text        { 0xfff3ecff };
    inline juce::Colour textDim     { 0xff9182b3 };
    inline float        glow        { 1.0f };          // 0..1 neon glow intensity

    // A named colour scheme (packed ARGB hex per role).
    struct Scheme
    {
        juce::String name;
        juce::uint32 bg, panel, panelLight, outline, accent, accentWarm, accentGreen, text, textDim;
    };

    const std::vector<Scheme>& schemes();
    void         applyScheme (const Scheme& s);
    bool         applyScheme (const juce::String& name);   // false if unknown
    juce::String currentSchemeName();

    // Fine-grained customization (marks the scheme as "Custom").
    void setPrimary    (juce::Colour c);      // accent
    void setSecondary  (juce::Colour c);      // accentWarm
    void setBackground (juce::Colour c);      // derives panel/panelLight/outline
    void setGlow       (float amount01);

    // Persistence — <userAppData>/VoxBrain/theme.xml
    std::unique_ptr<juce::XmlElement> toXml();
    void          fromXml (const juce::XmlElement* e);
    juce::File    settingsFile();
    void          load();
    void          save();

    // Convenience: a purple→red neon gradient for arcs/fills.
    juce::ColourGradient neonGradient (juce::Rectangle<float> area);
}

// ============================================================================
//  VoxBrainLookAndFeel — neon-glass knobs, glowing pill buttons, dark panels
// ============================================================================
class VoxBrainLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VoxBrainLookAndFeel();
    void refreshColours();   // re-pull theme colours into LookAndFeel_V4 roles

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
