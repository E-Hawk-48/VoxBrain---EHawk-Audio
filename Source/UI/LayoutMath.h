#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace vf::layout
{
// ============================================================================
//  LayoutMath — the arithmetic behind the main window's vertical layout,
//  separated from the components so it can be tested.
//
//  WHY THIS IS ITS OWN FILE. The editor used to carve fixed heights off the
//  bottom of the window in sequence and hand whatever survived to the spectrum
//  and the Vocal DNA radar. At the shipped window size that residue worked out
//  to 54 pixels for both of them together, and at the minimum window height it
//  was 5. Nothing about the code looked wrong — you only see it if you add up
//  the constants — and because layout lives inside a GUI component it was never
//  going to be caught by a test.
//
//  Distribution is a pure function of (available height, region minimums and
//  weights), so pulling it out here makes the property testable directly:
//  every region always gets at least its minimum, the total never overflows,
//  and spare height is shared out by weight.
// ============================================================================
struct Region
{
    int minHeight = 0;
    int weight    = 0;      // share of any leftover height
    int height    = 0;      // filled in by distribute()
};

/** Give every region its minimum, then share the remainder out by weight.

    If `available` is smaller than the sum of the minimums — a host can force a
    window smaller than we asked for — every region is scaled down
    proportionally instead of the last one being driven to zero or negative.
    `floorHeight` is the hard lower bound in that case.

    Returns the total height actually assigned. */
inline int distribute (std::vector<Region>& regions, int available, int floorHeight = 24)
{
    if (regions.empty()) return 0;

    int totalMin = 0, totalWeight = 0;
    for (auto& r : regions)
    {
        r.height    = r.minHeight;
        totalMin   += r.minHeight;
        totalWeight += r.weight;
    }

    const int spare = available - totalMin;

    if (spare > 0 && totalWeight > 0)
    {
        int given = 0;
        for (auto& r : regions)
        {
            const int add = spare * r.weight / totalWeight;
            r.height += add;
            given    += add;
        }
        // Rounding remainder goes to the heaviest region, so the total is exact.
        int heaviest = 0;
        for (size_t i = 1; i < regions.size(); ++i)
            if (regions[i].weight > regions[heaviest].weight) heaviest = (int) i;
        regions[(size_t) heaviest].height += spare - given;
    }
    else if (spare < 0 && totalMin > 0)
    {
        const double k = (double) juce::jmax (0, available) / (double) totalMin;
        for (auto& r : regions)
            r.height = juce::jmax (floorHeight, (int) ((double) r.minHeight * k));
    }

    int total = 0;
    for (const auto& r : regions) total += r.height;
    return total;
}

/** Row height for a fixed-height list that fills its area when short and stops
    shrinking when long. The companion to distribute(): the chain strip used to
    divide its height by the row count with no floor, so a full chain rendered
    at around eighteen pixels a row. */
inline int rowHeightFor (int areaHeight, int rowCount, int minRowHeight)
{
    if (rowCount <= 0) return minRowHeight;
    return juce::jmax (minRowHeight, areaHeight / rowCount);
}

/** How far a list of `rowCount` rows can scroll inside `areaHeight`. */
inline int maxScrollFor (int areaHeight, int rowCount, int rowHeight)
{
    return juce::jmax (0, rowCount * rowHeight - areaHeight);
}
} // namespace vf::layout
