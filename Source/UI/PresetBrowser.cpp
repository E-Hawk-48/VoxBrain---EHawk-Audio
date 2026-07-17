#include "PresetBrowser.h"
#include <set>

namespace vf
{
PresetBrowser::PresetBrowser (PresetLibrary& lib,
                              std::function<void (const Preset&)> onLoad,
                              std::function<Preset()> onCaptureCurrent,
                              std::function<Preset()> onGenerateAi)
    : library (lib), loadFn (std::move (onLoad)),
      captureFn (std::move (onCaptureCurrent)), genAiFn (std::move (onGenerateAi))
{
    titleLabel.setText ("PRESET BROWSER", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (titleLabel);

    statsLabel.setJustificationType (juce::Justification::centredRight);
    statsLabel.setColour (juce::Label::textColourId, theme::textDim);
    statsLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (statsLabel);

    closeBtn.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeBtn);

    searchBox.setTextToShowWhenEmpty ("Search presets: name, genre, mood, tags…", theme::textDim);
    searchBox.setColour (juce::TextEditor::backgroundColourId, theme::panel);
    searchBox.setColour (juce::TextEditor::outlineColourId, theme::outline);
    searchBox.setColour (juce::TextEditor::textColourId, theme::text);
    searchBox.onTextChange = [this] { view = View::Browse; runQuery(); };
    addAndMakeVisible (searchBox);

    // Genre filter — populated from the library.
    genreBox.addItem ("All genres", 1);
    {
        std::set<juce::String> genres;
        PresetLibrary::Query all; all.limit = 0;
        for (auto* p : library.query (all)) if (p->meta.genre.isNotEmpty()) genres.insert (p->meta.genre);
        int id = 2;
        for (const auto& gname : genres) genreBox.addItem (gname, id++);
    }
    genreBox.setSelectedId (1, juce::dontSendNotification);
    genreBox.onChange = [this] { runQuery(); };
    addAndMakeVisible (genreBox);

    sortBox.addItemList ({ "Relevance", "Name A-Z", "Newest", "Highest Rated", "Most Downloaded", "Trending" }, 1);
    sortBox.setSelectedId (1, juce::dontSendNotification);
    sortBox.onChange = [this] { runQuery(); };
    addAndMakeVisible (sortBox);

    auto tab = [this] (juce::TextButton& b, View v)
    {
        b.setColour (juce::TextButton::buttonColourId, theme::panel);
        b.onClick = [this, v] { view = v; runQuery(); };
        addAndMakeVisible (b);
    };
    tab (browseTab,   View::Browse);
    tab (trendingTab, View::Trending);
    tab (newTab,      View::New);
    tab (aiTab,       View::AiRecommended);
    tab (favTab,      View::Favorites);

    list.setRowHeight (54);
    list.setColour (juce::ListBox::backgroundColourId, theme::bg);
    addAndMakeVisible (list);

    loadBtn.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    loadBtn.onClick = [this] { loadSelected(); };
    captureBtn.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    captureBtn.onClick = [this]
    {
        if (! captureFn) return;
        Preset p = captureFn();
        p.meta.name = "My Chain " + juce::Time::getCurrentTime().formatted ("%H:%M");
        p.meta.creatorName = "You";
        p.seal();
        library.add (std::move (p));
        runQuery();
    };
    genAiBtn.setColour (juce::TextButton::buttonColourId, theme::accentWarm.withAlpha (0.85f));
    genAiBtn.onClick = [this]
    {
        if (! genAiFn) return;
        Preset p = genAiFn();
        library.add (p);
        if (loadFn) loadFn (p);
        view = View::AiRecommended;
        runQuery();
    };
    favBtn.setColour (juce::TextButton::buttonColourId, theme::panelLight);
    favBtn.onClick = [this]
    {
        if (auto* p = selectedPreset())
        {
            library.setFavorite (p->meta.presetId, ! library.isFavorite (p->meta.presetId));
            list.repaint();
        }
    };
    for (auto* b : { &loadBtn, &captureBtn, &genAiBtn, &favBtn }) addAndMakeVisible (*b);

    runQuery();
}

const Preset* PresetBrowser::selectedPreset() const
{
    const int r = list.getSelectedRow();
    return (r >= 0 && r < (int) results.size()) ? results[(size_t) r] : nullptr;
}

void PresetBrowser::loadSelected()
{
    if (auto* p = selectedPreset())
        if (loadFn) loadFn (*p);
}

void PresetBrowser::runQuery()
{
    PresetLibrary::Query q;
    q.text = searchBox.getText();
    if (genreBox.getSelectedId() > 1) q.genre = genreBox.getText();
    switch (sortBox.getSelectedId())
    {
        case 2: q.sort = PresetLibrary::Sort::NameAsc; break;
        case 3: q.sort = PresetLibrary::Sort::Newest; break;
        case 4: q.sort = PresetLibrary::Sort::HighestRated; break;
        case 5: q.sort = PresetLibrary::Sort::MostDownloaded; break;
        case 6: q.sort = PresetLibrary::Sort::Trending; break;
        default: q.sort = PresetLibrary::Sort::Relevance; break;
    }

    switch (view)
    {
        case View::Trending:      results = library.trending (300); break;
        case View::New:           results = library.newArrivals (300); break;
        case View::AiRecommended: results = library.recommendedFor (q.genre.isNotEmpty() ? q.genre : "Pop", 300); break;
        case View::Favorites:     q.favoritesOnly = true; results = library.query (q); break;
        default:                  results = library.query (q); break;
    }

    // reflect active tab
    for (auto* b : { &browseTab, &trendingTab, &newTab, &aiTab, &favTab })
        b->setColour (juce::TextButton::buttonColourId, theme::panel);
    juce::TextButton* active = view == View::Trending ? &trendingTab
                             : view == View::New ? &newTab
                             : view == View::AiRecommended ? &aiTab
                             : view == View::Favorites ? &favTab : &browseTab;
    active->setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.7f));

    statsLabel.setText (juce::String (results.size()) + " of " + juce::String (library.size()) + " presets",
                        juce::dontSendNotification);
    list.updateContent();
    list.repaint();
    repaint();
}

void PresetBrowser::refresh() { runQuery(); }

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) results.size()) return;
    const auto& m = results[(size_t) row]->meta;
    auto r = juce::Rectangle<int> (0, 0, w, h);

    g.setColour (selected ? theme::accent.withAlpha (0.18f) : (row % 2 ? theme::panel.withAlpha (0.4f) : theme::bg));
    g.fillRect (r.reduced (2, 1));

    // favourite star (left gutter, clickable)
    auto star = r.removeFromLeft (30);
    const bool fav = library.isFavorite (m.presetId);
    g.setColour (fav ? theme::accentWarm : theme::outline);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText (fav ? juce::String::fromUTF8 ("★") : juce::String::fromUTF8 ("☆"), star, juce::Justification::centred);

    auto txt = r.reduced (6, 5);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (m.name, txt.removeFromTop (18), juce::Justification::topLeft);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    juce::String sub;
    sub << m.creatorName;
    if (m.genre.isNotEmpty()) sub << "  •  " << m.genre;
    if (m.mood.isNotEmpty())  sub << "  •  " << m.mood;
    g.drawText (sub, txt, juce::Justification::topLeft);

    // right column: badge + rating
    auto right = juce::Rectangle<int> (w - 118, 4, 112, h - 8);
    juce::String badge = m.aiGenerated ? "AI" : (m.official ? "OFFICIAL" : (m.source == "community" ? "COMMUNITY" : "USER"));
    g.setColour (m.aiGenerated ? theme::accentWarm : (m.official ? theme::accent : theme::textDim));
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText (badge, right.removeFromTop (14), juce::Justification::topRight);
    if (m.rating > 0.0f)
    {
        g.setColour (theme::accentGreen);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("★ ") + juce::String (m.rating, 1) + "  " + juce::String (m.downloads) + juce::String::fromUTF8 (" ⤓"),
                    right.removeFromTop (14), juce::Justification::topRight);
    }
    if (m.verifiedCreator)
    {
        g.setColour (theme::accent);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (juce::String::fromUTF8 ("✔ verified"), right, juce::Justification::bottomRight);
    }
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    list.selectRow (row);
    if (e.x < 30 && row >= 0 && row < (int) results.size())   // star gutter → toggle favourite
    {
        const auto id = results[(size_t) row]->meta.presetId;
        library.setFavorite (id, ! library.isFavorite (id));
        list.repaint();
    }
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    loadSelected();
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg.withAlpha (0.985f));
    g.setColour (theme::outline);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (theme::accent);
    g.fillRect (0, 44, getWidth(), 2);
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds().reduced (16);
    auto header = r.removeFromTop (34);
    titleLabel.setBounds (header.removeFromLeft (240));
    closeBtn.setBounds (header.removeFromRight (90));
    statsLabel.setBounds (header);
    r.removeFromTop (10);

    // filter row
    auto filters = r.removeFromTop (30);
    searchBox.setBounds (filters.removeFromLeft (filters.getWidth() / 2));
    filters.removeFromLeft (8);
    sortBox.setBounds (filters.removeFromRight (150));
    filters.removeFromRight (8);
    genreBox.setBounds (filters);
    r.removeFromTop (8);

    // discovery tabs
    auto tabs = r.removeFromTop (28);
    const int tw = tabs.getWidth() / 5;
    browseTab.setBounds   (tabs.removeFromLeft (tw).reduced (2, 0));
    trendingTab.setBounds (tabs.removeFromLeft (tw).reduced (2, 0));
    newTab.setBounds      (tabs.removeFromLeft (tw).reduced (2, 0));
    aiTab.setBounds       (tabs.removeFromLeft (tw).reduced (2, 0));
    favTab.setBounds      (tabs.reduced (2, 0));
    r.removeFromTop (8);

    // action bar at the bottom
    auto actions = r.removeFromBottom (34);
    loadBtn.setBounds    (actions.removeFromLeft (110).reduced (2));
    favBtn.setBounds     (actions.removeFromLeft (110).reduced (2));
    genAiBtn.setBounds   (actions.removeFromRight (130).reduced (2));
    captureBtn.setBounds (actions.removeFromRight (140).reduced (2));
    r.removeFromBottom (8);

    list.setBounds (r);
}
} // namespace vf
