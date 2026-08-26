#include "TestFramework.h"
#include "../Source/UI/LayoutMath.h"

// ============================================================================
//  Layout arithmetic.
//
//  The main window shipped with its two headline visualisations — the spectrum
//  and the Vocal DNA radar — sharing 54 pixels at the default size, and 5 at
//  the minimum. There was no bug you could point at: heights were carved off
//  the bottom in sequence and the scope silently received the residue. You only
//  see it by adding up the constants, which is exactly the sort of thing a test
//  does well and a person does badly.
//
//  The same shape of mistake was in the chain strip, where row height was the
//  list height divided by the row count with no floor, so a full chain rendered
//  at roughly eighteen pixels a row.
//
//  Both now go through vf::layout, and the properties that were violated are
//  asserted here directly.
// ============================================================================
namespace
{
using namespace vftest;

/** The main window's real region table, kept in step with PluginEditor::resized. */
std::vector<vf::layout::Region> mainWindowRegions()
{
    return { { 130, 3, 0 },    // spectrum + radar
             {  92, 1, 0 },    // pitch display
             { 112, 1, 0 },    // report + chat
             { 300, 4, 0 } };  // chain
}
constexpr int kPresetBarH = 36;
constexpr int kHeaderH    = 52;
} // namespace

void runLayoutTests()
{
    Suite s ("Window layout arithmetic");

    // ======================================================================
    //  1. THE ACTUAL REGRESSION. At every window height the plugin allows,
    //     no region may fall below its stated minimum.
    // ======================================================================
    {
        bool everyHeightOk = true;
        int worstHeight = 0, worstRegion = -1, worstValue = 0;

        for (int winH = 740; winH <= 1800; winH += 5)
        {
            auto regions = mainWindowRegions();
            vf::layout::distribute (regions, winH - kHeaderH - kPresetBarH);
            for (size_t i = 0; i < regions.size(); ++i)
                if (regions[i].height < regions[i].minHeight)
                {
                    everyHeightOk = false;
                    worstHeight = winH; worstRegion = (int) i;
                    worstValue = regions[i].height;
                }
        }
        s.check ("no region drops below its minimum at any allowed window height",
                 everyHeightOk,
                 everyHeightOk ? "checked 740..1800 px"
                               : "region " + juce::String (worstRegion) + " got "
                                 + juce::String (worstValue) + " px at "
                                 + juce::String (worstHeight) + " px tall");
    }

    // ======================================================================
    //  2. THE SPECIFIC NUMBER THAT WAS WRONG. At the shipping default size the
    //     scope must be a usable strip, not a sliver. 54 px was the old value.
    // ======================================================================
    {
        auto regions = mainWindowRegions();
        vf::layout::distribute (regions, 860 - kHeaderH - kPresetBarH);
        s.check ("the spectrum + radar row is usable at the default window size",
                 regions[0].height >= 130,
                 juce::String (regions[0].height) + " px (was 54 before this was fixed)");
        s.check ("the chain still gets the largest share at the default size",
                 regions[3].height > regions[0].height,
                 juce::String (regions[3].height) + " px chain vs "
                 + juce::String (regions[0].height) + " px scope");
    }

    // ======================================================================
    //  3. NOTHING OVERFLOWS. The regions must exactly fill what they are given
    //     — a layout that sums to more than the window silently clips the last
    //     component off the bottom.
    // ======================================================================
    {
        bool exact = true;
        int badAt = 0, got = 0, want = 0;
        for (int winH = 740; winH <= 1800; winH += 7)
        {
            auto regions = mainWindowRegions();
            const int avail = winH - kHeaderH - kPresetBarH;
            const int total = vf::layout::distribute (regions, avail);
            if (total != avail) { exact = false; badAt = winH; got = total; want = avail; }
        }
        s.check ("the regions exactly fill the available height", exact,
                 exact ? "checked 740..1800 px"
                       : "got " + juce::String (got) + " for " + juce::String (want)
                         + " at " + juce::String (badAt) + " px tall");
    }

    // ======================================================================
    //  4. UNDERSIZED WINDOWS degrade gracefully. A host can force a window
    //     smaller than we asked for; nothing may go to zero or negative.
    // ======================================================================
    {
        bool ok = true;
        int worst = 1 << 30;
        for (int avail : { 0, 50, 200, 400, 600 })
        {
            auto regions = mainWindowRegions();
            vf::layout::distribute (regions, avail);
            for (const auto& r : regions) { worst = juce::jmin (worst, r.height); if (r.height <= 0) ok = false; }
        }
        s.check ("an undersized window still gives every region positive height", ok,
                 "smallest region " + juce::String (worst) + " px");
    }

    // ======================================================================
    //  5. CHAIN STRIP ROWS. Short chains fill the strip; long chains stop
    //     shrinking and scroll instead of becoming unreadable.
    // ======================================================================
    {
        const int strip = 330, minRow = 30;

        s.check ("a short chain expands to fill the strip",
                 vf::layout::rowHeightFor (strip, 6, minRow) > minRow,
                 juce::String (vf::layout::rowHeightFor (strip, 6, minRow)) + " px rows for 6 modules");

        // 11 fixed stages + 5 rack modules is an ordinary chain after LEARN.
        const int longRow = vf::layout::rowHeightFor (strip, 16, minRow);
        s.check ("a long chain never shrinks rows below the readable minimum",
                 longRow >= minRow,
                 juce::String (longRow) + " px rows for 16 modules (was ~20 before)");
        s.check ("a long chain becomes scrollable rather than being crushed",
                 vf::layout::maxScrollFor (strip, 16, longRow) > 0,
                 juce::String (vf::layout::maxScrollFor (strip, 16, longRow)) + " px of scroll");
        s.check ("a chain that fits does not scroll",
                 vf::layout::maxScrollFor (strip, 6, vf::layout::rowHeightFor (strip, 6, minRow)) == 0);

        // The degenerate case that would divide by zero if it were inline.
        s.check ("an empty chain does not divide by zero",
                 vf::layout::rowHeightFor (strip, 0, minRow) == minRow
                 && vf::layout::maxScrollFor (strip, 0, minRow) == 0);
    }
}
