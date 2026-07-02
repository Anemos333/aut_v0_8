#include "CustomScaleEditor.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

CustomScaleEditor::CustomScaleEditor (MicrotonalAutotuneAudioProcessor& processor,
                                      CustomScaleEditorListener& listener,
                                      juce::Image backgroundImage)
    : processorRef (processor),
      listenerRef (listener),
      bgImage (std::move (backgroundImage))
{
    // Title
    titleLabel.setText ("Crea Scala Personalizzata", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    // Octave label
    octaveLabel.setText ("Click: aggiungi divisione | doppio click: rimuovi | Ctrl/Cmd-click al centro: escludi intervallo", juce::dontSendNotification);
    octaveLabel.setFont (juce::FontOptions (13.0f));
    octaveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    octaveLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (octaveLabel);

    // Name label
    nameLabel.setText ("Nome della scala:", juce::dontSendNotification);
    nameLabel.setFont (juce::FontOptions (14.0f));
    nameLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    nameLabel.setJustificationType (juce::Justification::left);
    addAndMakeVisible (nameLabel);

    // Name editor
    nameEditor.setFont (juce::FontOptions (14.0f));
    nameEditor.setMultiLine (false);
    nameEditor.setTextToShowWhenEmpty ("Inserisci il nome...", juce::Colours::grey);
    nameEditor.onTextChange = [this]() { updateInfoLabel(); };
    addAndMakeVisible (nameEditor);

    // Generator labels and controls
    divisionCountLabel.setText ("Intervalli:", juce::dontSendNotification);
    divisionCountLabel.setFont (juce::FontOptions (13.0f));
    divisionCountLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (divisionCountLabel);

    spacingLabel.setText ("Spaziatura:", juce::dontSendNotification);
    spacingLabel.setFont (juce::FontOptions (13.0f));
    spacingLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (spacingLabel);

    exponentLabel.setText ("Curva a < 4:", juce::dontSendNotification);
    exponentLabel.setFont (juce::FontOptions (13.0f));
    exponentLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (exponentLabel);

    exponentEditor.setFont (juce::FontOptions (13.0f));
    exponentEditor.setMultiLine (false);
    exponentEditor.setText ("2.0", juce::dontSendNotification);
    exponentEditor.setInputRestrictions (6, "0123456789.");
    exponentEditor.onTextChange = [this]() { updateInfoLabel(); };
    addAndMakeVisible (exponentEditor);

    buildGeneratorMenus();

    // Allow this component to receive keyboard focus so clicking outside
    // the text editor unfocuses it.
    setWantsKeyboardFocus (true);

    // Save button
    saveButton.setEnabled (false);
    saveButton.onClick = [this]() { onSave(); };
    addAndMakeVisible (saveButton);

    // Back button
    backButton.onClick = [this]() { listenerRef.customScaleEditorClosed(); };
    addAndMakeVisible (backButton);

    generateButton.onClick = [this]() { generateScaleFromControls(); };
    addAndMakeVisible (generateButton);

    clearButton.onClick = [this]() { clearScale(); };
    addAndMakeVisible (clearButton);

    // Info label
    infoLabel.setFont (juce::FontOptions (13.0f));
    infoLabel.setColour (juce::Label::textColourId, juce::Colours::lightyellow);
    infoLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (infoLabel);

    // A useful default: 12 equally spaced intervals.
    generateScaleFromControls();
    updateExponentControlState();
    updateInfoLabel();
}

CustomScaleEditor::~CustomScaleEditor() {}

void CustomScaleEditor::buildGeneratorMenus()
{
    divisionCountSelector.clear (juce::dontSendNotification);
    for (int n = minScaleRatios; n <= maxScaleRatios; ++n)
        divisionCountSelector.addItem (juce::String (n), n);

    divisionCountSelector.setSelectedId (12, juce::dontSendNotification);
    divisionCountSelector.onChange = [this]() { generateScaleFromControls(); };
    addAndMakeVisible (divisionCountSelector);

    spacingSelector.clear (juce::dontSendNotification);
    spacingSelector.addItem ("Equilibrata",       static_cast<int> (SpacingMode::equal));
    spacingSelector.addItem ("Crescita lineare",  static_cast<int> (SpacingMode::linear));
    spacingSelector.addItem ("Spinta crescente",  static_cast<int> (SpacingMode::exponential));
    spacingSelector.addItem ("Crescita naturale", static_cast<int> (SpacingMode::logarithmic));
    spacingSelector.addItem ("Arco morbido",      static_cast<int> (SpacingMode::cosine));
    spacingSelector.addItem ("Curva ripida",      static_cast<int> (SpacingMode::power));
    spacingSelector.addItem ("Curva inversa",     static_cast<int> (SpacingMode::inversePower));

    spacingSelector.setSelectedId (static_cast<int> (SpacingMode::equal), juce::dontSendNotification);
    spacingSelector.onChange = [this]()
    {
        updateExponentControlState();
        generateScaleFromControls();
    };
    addAndMakeVisible (spacingSelector);
}

void CustomScaleEditor::paint (juce::Graphics& g)
{
    // Draw background image
    if (bgImage.isValid())
    {
        g.drawImage (bgImage, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.fillAll (juce::Colour (0xFF1A1A2E));
    }

    // Semi-transparent overlay for readability
    g.setColour (juce::Colour (0x99000000));
    g.fillRect (getLocalBounds());

    // Draw the octave rectangle
    g.setColour (juce::Colour (0xFF2A2A4A));
    g.fillRect (octaveRect);

    // Draw excluded intervals first, behind the division lines.
    auto rectF = octaveRect.toFloat();
    auto boundaries = buildIntervalBoundaries();
    for (int intervalIndex : excludedIntervalIndices)
    {
        if (intervalIndex < 0 || intervalIndex + 1 >= static_cast<int> (boundaries.size()))
            continue;

        const float x1 = rectF.getX() + static_cast<float> (boundaries[static_cast<size_t> (intervalIndex)]) * rectF.getWidth();
        const float x2 = rectF.getX() + static_cast<float> (boundaries[static_cast<size_t> (intervalIndex + 1)]) * rectF.getWidth();

        g.setColour (juce::Colour (0x66FFD166));
        g.fillRect (juce::Rectangle<float> (x1, rectF.getY(), juce::jmax (1.0f, x2 - x1), rectF.getHeight()));
    }

    g.setColour (juce::Colour (0xFFCCCCFF));
    g.drawRect (octaveRect, 2);

    // Draw frequency labels at start and end
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colours::white);
    g.drawText ("1x", octaveRect.getX(), octaveRect.getBottom() + 2, 30, 16,
                juce::Justification::centredLeft);
    g.drawText ("2x", octaveRect.getRight() - 30, octaveRect.getBottom() + 2, 30, 16,
                juce::Justification::centredRight);

    // Draw divisions as vertical lines
    for (int i = 0; i < static_cast<int> (divisions.size()); ++i)
    {
        const double logPos = divisions[static_cast<size_t> (i)]; // [0, 1] in log2 space
        const float xPos = rectF.getX() + static_cast<float> (logPos) * rectF.getWidth();
        const bool skipped = shouldSkipDivisionOnSave (i);

        g.setColour (skipped ? juce::Colour (0xFF888888) : juce::Colour (0xFFFF6B6B));
        g.drawLine (xPos, rectF.getY(), xPos, rectF.getBottom(), skipped ? 1.0f : 2.0f);

        // Draw cents value above the line
        const double cents = logPos * 1200.0;
        const juce::String centsStr = juce::String (static_cast<int> (std::round (cents))) + "c";
        g.setFont (juce::FontOptions (10.0f));
        g.drawText (centsStr, static_cast<int> (xPos) - 20, octaveRect.getY() - 16, 40, 14,
                    juce::Justification::centred);
    }
}

void CustomScaleEditor::resized()
{
    auto bounds = getLocalBounds().reduced (20);

    // Title at top
    titleLabel.setBounds (bounds.removeFromTop (30));
    bounds.removeFromTop (8);

    // Octave label
    octaveLabel.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (8);

    // Generator row 1
    auto generatorRow = bounds.removeFromTop (30);
    divisionCountLabel.setBounds (generatorRow.removeFromLeft (80));
    divisionCountSelector.setBounds (generatorRow.removeFromLeft (72));
    generatorRow.removeFromLeft (12);
    spacingLabel.setBounds (generatorRow.removeFromLeft (82));
    spacingSelector.setBounds (generatorRow);
    bounds.removeFromTop (6);

    // Generator row 2
    auto generatorRow2 = bounds.removeFromTop (30);
    exponentLabel.setBounds (generatorRow2.removeFromLeft (88));
    exponentEditor.setBounds (generatorRow2.removeFromLeft (70));
    generatorRow2.removeFromLeft (12);
    generateButton.setBounds (generatorRow2.removeFromLeft (100));
    generatorRow2.removeFromLeft (10);
    clearButton.setBounds (generatorRow2.removeFromLeft (100));
    bounds.removeFromTop (10);

    // Octave rectangle — central, good height
    const int rectHeight = juce::jmax (60, bounds.getHeight() / 4);
    const int rectMargin = bounds.getWidth() / 10;
    octaveRect = bounds.removeFromTop (rectHeight).reduced (rectMargin, 0);
    bounds.removeFromTop (25); // space for cents labels + gap

    // Info label
    infoLabel.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (8);

    // Name section
    auto nameRow = bounds.removeFromTop (30);
    nameLabel.setBounds (nameRow.removeFromLeft (130));
    nameEditor.setBounds (nameRow);
    bounds.removeFromTop (10);

    // Buttons
    auto buttonRow = bounds.removeFromTop (35);
    const int btnWidth = 120;
    const int gap = 20;
    const int totalBtnWidth = btnWidth * 2 + gap;
    const int startX = buttonRow.getCentreX() - totalBtnWidth / 2;

    backButton.setBounds (startX, buttonRow.getY(), btnWidth, 35);
    saveButton.setBounds (startX + btnWidth + gap, buttonRow.getY(), btnWidth, 35);
}

void CustomScaleEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto clickPos = event.getPosition();

    // Clicking anywhere outside the text/exponent editors unfocuses them.
    if (! nameEditor.getBounds().contains (clickPos) && ! exponentEditor.getBounds().contains (clickPos))
    {
        nameEditor.unfocusAllComponents();
        exponentEditor.unfocusAllComponents();
        grabKeyboardFocus();
    }

    if (! octaveRect.contains (clickPos))
        return;

    // Convert click position to log2 ratio position [0, 1].
    float relX = static_cast<float> (clickPos.getX() - octaveRect.getX())
                 / static_cast<float> (octaveRect.getWidth());
    relX = juce::jlimit (0.0f, 1.0f, relX);
    const double logPos = static_cast<double> (relX);

    const bool modifierClick = event.mods.isCommandDown() || event.mods.isCtrlDown();
    if (modifierClick)
    {
        toggleExcludedIntervalAt (logPos);
        updateInfoLabel();
        repaint();
        return;
    }

    const double pixelTolerance = octaveRect.getWidth() > 0
        ? 8.0 / static_cast<double> (octaveRect.getWidth())
        : manualMinDistance;
    const double removeTolerance = juce::jmax (manualMinDistance, pixelTolerance);

    if (event.getNumberOfClicks() >= 2)
    {
        const int closestIndex = findClosestDivisionIndex (logPos, removeTolerance);
        if (closestIndex >= 0)
            removeDivisionAtIndex (closestIndex);

        return;
    }

    if (static_cast<int> (divisions.size()) >= maxScaleRatios - 1)
        return;

    // Avoid exact boundaries when manually adding points.
    const double newLogPos = juce::jlimit (0.01, 0.99, logPos);

    // Check that we don't place too close to an existing division.
    if (findClosestDivisionIndex (newLogPos, manualMinDistance) >= 0)
        return;

    divisions.push_back (newLogPos);
    std::sort (divisions.begin(), divisions.end());

    // Manual topology edits change interval indices, so previous exclusions
    // would become ambiguous. Clear them rather than silently moving them.
    excludedIntervalIndices.clear();

    updateInfoLabel();
    repaint();
}

void CustomScaleEditor::generateScaleFromControls()
{
    const int intervalCount = getSelectedIntervalCount();
    const auto mode = getSelectedSpacingMode();
    const double exponent = getExponentFromEditor();

    std::vector<double> weights;
    weights.reserve (static_cast<size_t> (intervalCount));

    for (int i = 0; i < intervalCount; ++i)
    {
        const double n = static_cast<double> (i + 1);
        const double t = intervalCount > 1 ? static_cast<double> (i) / static_cast<double> (intervalCount - 1) : 0.0;

        double weight = 1.0;
        switch (mode)
        {
            case SpacingMode::equal:
                weight = 1.0;
                break;

            case SpacingMode::linear:
                weight = n;
                break;

            case SpacingMode::exponential:
                // Bounded exponential: musically usable, no enormous exp(33) range.
                weight = std::exp (0.5 * t);
                break;

            case SpacingMode::logarithmic:
                weight = std::log1p (n);
                break;

            case SpacingMode::cosine:
                // Positive cosine-derived envelope: small intervals first, wider later.
                weight = 0.15 + 0.85 * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * t));
                break;

            case SpacingMode::power:
                weight = std::pow (n, exponent);
                break;

            case SpacingMode::inversePower:
                weight = 1.0 / std::pow (n, exponent);
                break;
        }

        if (! std::isfinite (weight) || weight <= 0.0)
            weight = 1.0;

        weights.push_back (weight);
    }

    const double totalWeight = std::accumulate (weights.begin(), weights.end(), 0.0);
    if (totalWeight <= 0.0 || ! std::isfinite (totalWeight))
        return;

    divisions.clear();
    divisions.reserve (static_cast<size_t> (juce::jmax (0, intervalCount - 1)));

    double cumulative = 0.0;
    for (int i = 0; i < intervalCount - 1; ++i)
    {
        cumulative += weights[static_cast<size_t> (i)];
        const double logPos = juce::jlimit (1.0e-6, 1.0 - 1.0e-6, cumulative / totalWeight);
        divisions.push_back (logPos);
    }

    std::sort (divisions.begin(), divisions.end());
    excludedIntervalIndices.clear();

    updateInfoLabel();
    repaint();
}

void CustomScaleEditor::clearScale()
{
    divisions.clear();
    excludedIntervalIndices.clear();
    updateInfoLabel();
    repaint();
}

void CustomScaleEditor::updateExponentControlState()
{
    const auto mode = getSelectedSpacingMode();
    const bool needsExponent = mode == SpacingMode::power || mode == SpacingMode::inversePower;

    exponentLabel.setEnabled (needsExponent);
    exponentEditor.setEnabled (needsExponent);
    exponentLabel.setAlpha (needsExponent ? 1.0f : 0.45f);
    exponentEditor.setAlpha (needsExponent ? 1.0f : 0.45f);
}

double CustomScaleEditor::getExponentFromEditor() const
{
    double exponent = exponentEditor.getText().getDoubleValue();
    if (! std::isfinite (exponent) || exponent >= 4.0)
        exponent = 3.0;

    return juce::jlimit (1.2, 3.9, exponent);
}

int CustomScaleEditor::getSelectedIntervalCount() const
{
    const int selected = divisionCountSelector.getSelectedId();
    if (selected >= minScaleRatios && selected <= maxScaleRatios)
        return selected;

    return 12;
}

CustomScaleEditor::SpacingMode CustomScaleEditor::getSelectedSpacingMode() const
{
    const int selected = spacingSelector.getSelectedId();
    if (selected >= static_cast<int> (SpacingMode::equal)
        && selected <= static_cast<int> (SpacingMode::inversePower))
        return static_cast<SpacingMode> (selected);

    return SpacingMode::equal;
}

std::vector<double> CustomScaleEditor::buildIntervalBoundaries() const
{
    std::vector<double> boundaries;
    boundaries.reserve (divisions.size() + 2);
    boundaries.push_back (0.0);

    for (double d : divisions)
    {
        if (std::isfinite (d) && d > 0.0 && d < 1.0)
            boundaries.push_back (d);
    }

    std::sort (boundaries.begin(), boundaries.end());
    auto last = std::unique (boundaries.begin(), boundaries.end(),
        [] (double a, double b) { return std::abs (a - b) < 1.0e-9; });
    boundaries.erase (last, boundaries.end());

    boundaries.push_back (1.0);
    return boundaries;
}

int CustomScaleEditor::findClosestDivisionIndex (double logPos, double maxDistance) const
{
    int closestIndex = -1;
    double bestDistance = maxDistance;

    for (int i = 0; i < static_cast<int> (divisions.size()); ++i)
    {
        const double distance = std::abs (divisions[static_cast<size_t> (i)] - logPos);
        if (distance <= bestDistance)
        {
            bestDistance = distance;
            closestIndex = i;
        }
    }

    return closestIndex;
}

int CustomScaleEditor::findIntervalIndexAt (double logPos) const
{
    const auto boundaries = buildIntervalBoundaries();
    if (boundaries.size() < 2)
        return -1;

    for (int i = 0; i < static_cast<int> (boundaries.size()) - 1; ++i)
    {
        const double left = boundaries[static_cast<size_t> (i)];
        const double right = boundaries[static_cast<size_t> (i + 1)];

        if (logPos >= left && logPos <= right)
            return i;
    }

    return static_cast<int> (boundaries.size()) - 2;
}

int CustomScaleEditor::getEffectiveRatioCount() const
{
    int count = 1; // unison is always saved
    for (int i = 0; i < static_cast<int> (divisions.size()); ++i)
    {
        if (! shouldSkipDivisionOnSave (i))
            ++count;
    }

    return count;
}

bool CustomScaleEditor::shouldSkipDivisionOnSave (int divisionIndex) const
{
    if (divisionIndex < 0 || divisionIndex >= static_cast<int> (divisions.size()))
        return false;

    // A division is the right edge of interval `divisionIndex`.
    // Muting that interval removes its right-edge note from the saved scale.
    if (excludedIntervalIndices.count (divisionIndex) > 0)
        return true;

    // The final interval has no octave ratio in the saved preset. If the user
    // mutes it, remove the previous visible degree instead, which is the only
    // storable neighbour of that interval in the current preset format.
    const int finalIntervalIndex = static_cast<int> (divisions.size());
    const bool isLastDivision = divisionIndex == static_cast<int> (divisions.size()) - 1;
    if (isLastDivision && excludedIntervalIndices.count (finalIntervalIndex) > 0)
        return true;

    return false;
}

void CustomScaleEditor::removeDivisionAtIndex (int divisionIndex)
{
    if (divisionIndex < 0 || divisionIndex >= static_cast<int> (divisions.size()))
        return;

    divisions.erase (divisions.begin() + divisionIndex);
    excludedIntervalIndices.clear();

    updateInfoLabel();
    repaint();
}

void CustomScaleEditor::toggleExcludedIntervalAt (double logPos)
{
    if (divisions.empty())
        return;

    const int intervalIndex = findIntervalIndexAt (logPos);
    if (intervalIndex < 0)
        return;

    if (excludedIntervalIndices.count (intervalIndex) > 0)
        excludedIntervalIndices.erase (intervalIndex);
    else
        excludedIntervalIndices.insert (intervalIndex);

    sanitiseExcludedIntervals();
}

void CustomScaleEditor::sanitiseExcludedIntervals()
{
    const int intervalCount = static_cast<int> (divisions.size()) + 1;
    for (auto it = excludedIntervalIndices.begin(); it != excludedIntervalIndices.end();)
    {
        if (*it < 0 || *it >= intervalCount)
            it = excludedIntervalIndices.erase (it);
        else
            ++it;
    }
}

void CustomScaleEditor::updateInfoLabel()
{
    sanitiseExcludedIntervals();

    const int intervalCount = static_cast<int> (divisions.size()) + 1;
    const int effectiveCount = getEffectiveRatioCount();

    juce::String text = "Intervalli: " + juce::String (intervalCount)
                      + " | note salvate: " + juce::String (effectiveCount);

    if (! excludedIntervalIndices.empty())
        text += " | esclusi: " + juce::String (static_cast<int> (excludedIntervalIndices.size()));

    if (effectiveCount < minScaleRatios)
        text += " (servono almeno " + juce::String (minScaleRatios) + " note)";
    else if (effectiveCount >= maxScaleRatios)
        text += " (massimo raggiunto)";

    const auto mode = getSelectedSpacingMode();
    if ((mode == SpacingMode::power || mode == SpacingMode::inversePower)
        && exponentEditor.getText().getDoubleValue() <= 4.0)
    {
        text += " | a corretto a 5.0";
    }

    infoLabel.setText (text, juce::dontSendNotification);

    const bool canSave = effectiveCount >= minScaleRatios
                         && effectiveCount <= maxScaleRatios
                         && nameEditor.getText().trim().isNotEmpty();
    saveButton.setEnabled (canSave);
}

void CustomScaleEditor::onSave()
{
    const juce::String name = nameEditor.getText().trim();
    if (name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Errore", "Inserisci un nome per la scala.");
        return;
    }

    if (getEffectiveRatioCount() < minScaleRatios)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Errore", "Servono almeno 3 note salvabili. Riduci gli intervalli esclusi o aggiungi divisioni.");
        return;
    }

    // Convert log positions to frequency ratios.
    // Each division position d (in [0,1] log2 space) -> ratio = 2^d.
    std::vector<double> ratios;
    ratios.reserve (static_cast<size_t> (getEffectiveRatioCount()));
    ratios.push_back (1.0); // Always include unison

    for (int i = 0; i < static_cast<int> (divisions.size()); ++i)
    {
        if (shouldSkipDivisionOnSave (i))
            continue;

        ratios.push_back (std::pow (2.0, divisions[static_cast<size_t> (i)]));
    }

    std::sort (ratios.begin(), ratios.end());

    // Remove duplicates.
    auto last = std::unique (ratios.begin(), ratios.end(),
        [](double a, double b) { return std::abs (a - b) < 1e-8; });
    ratios.erase (last, ratios.end());

    if (static_cast<int> (ratios.size()) < minScaleRatios
        || static_cast<int> (ratios.size()) > maxScaleRatios)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Errore", "La scala deve contenere tra 3 e 33 note salvabili.");
        return;
    }

    const bool success = processorRef.getCustomPresets().addPreset (name, ratios);

    if (success)
    {
        processorRef.refreshScaleSnapshot();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "Scala salvata", "La scala \"" + name + "\" e' stata salvata con successo.");
        listenerRef.customScaleEditorClosed();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
            "Errore", "Impossibile salvare la scala. Verifica che ci siano meno di 7 preset.");
    }
}
