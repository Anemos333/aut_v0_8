#include "CustomScaleEditor.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr auto panelColour = 0xB0121724;
constexpr auto railColour = 0xE0182032;
constexpr auto railOutline = 0xFF56617A;
constexpr auto activeDegree = 0xFFFF6B6B;
constexpr auto lockedDegree = 0xFFFFC857;
constexpr auto excludedDegree = 0xFF7A8398;
constexpr auto selectedGlow = 0x5587A7FF;
}

CustomScaleEditor::CustomScaleEditor (MicrotonalAutotuneAudioProcessor& processor,
                                      CustomScaleEditorListener& listener,
                                      juce::Image backgroundImage)
    : processorRef (processor),
      listenerRef (listener),
      bgImage (std::move (backgroundImage))
{
    setWantsKeyboardFocus (true);
    configureUi();
    refreshSelectedControls();
    refreshInfo();
}

void CustomScaleEditor::configureUi()
{
    const auto configureLabel = [this] (juce::Label& label,
                                         const juce::String& text,
                                         float size = 13.0f,
                                         juce::Justification justification = juce::Justification::centredLeft)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (size));
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setJustificationType (justification);
        addAndMakeVisible (label);
    };

    configureLabel (titleLabel_, "Custom Scale Editor", 22.0f, juce::Justification::centred);
    titleLabel_.setFont (juce::FontOptions (22.0f, juce::Font::bold));

    configureLabel (
        helpLabel_,
        "Drag a degree  |  Shift-drag: warp its unlocked span  |  Double-click empty: add  |  Double-click degree: remove  |  Cmd/Ctrl-click: include/exclude",
        11.5f,
        juce::Justification::centred);
    helpLabel_.setColour (juce::Label::textColourId, juce::Colour (0xFFD7DCEC));

    configureLabel (nameLabel_, "Scale name");
    nameEditor_.setMultiLine (false);
    nameEditor_.setTextToShowWhenEmpty ("Name your scale...", juce::Colours::grey);
    nameEditor_.onTextChange = [this] { refreshInfo(); };
    addAndMakeVisible (nameEditor_);

    configureLabel (equaveLabel_, "Period / equave");
    equaveEditor_.setMultiLine (false);
    equaveEditor_.setInputRestrictions (18, "0123456789.:/");
    equaveEditor_.setText ("2:1", juce::dontSendNotification);
    equaveEditor_.setTooltip ("Free period ratio. Examples: 2:1, 3:1, 1.5");
    equaveEditor_.onReturnKey = [this] { applyEquaveEditor(); };
    equaveEditor_.onFocusLost = [this] { applyEquaveEditor(); };
    addAndMakeVisible (equaveEditor_);

    configureLabel (
        equaveHintLabel_,
        "If you're not sure what this does, leave it at 2:1.",
        11.0f,
        juce::Justification::centredRight);
    equaveHintLabel_.setColour (juce::Label::textColourId, juce::Colour (0xFFFFD58A));

    configureLabel (stepsLabel_, "Steps");
    stepsEditor_.setMultiLine (false);
    stepsEditor_.setInputRestrictions (3, "0123456789");
    stepsEditor_.setText ("12", juce::dontSendNotification);
    stepsEditor_.setTooltip ("Equal divisions of the current period, 3 to 96 pitch classes.");
    stepsEditor_.onReturnKey = [this] { applyEqualDivision(); };
    addAndMakeVisible (stepsEditor_);

    divideButton_.setTooltip (
        "Divide the current period equally. If the topology count is unchanged, locked degrees stay fixed and only free degrees are redistributed.");
    divideButton_.onClick = [this] { applyEqualDivision(); };
    addAndMakeVisible (divideButton_);

    redistributeButton_.setTooltip (
        "Keep every locked degree fixed and distribute only the unlocked degrees evenly between locked anchors.");
    redistributeButton_.onClick = [this] { redistributeFreeDegrees(); };
    addAndMakeVisible (redistributeButton_);

    clearButton_.onClick = [this] { clearScale(); };
    addAndMakeVisible (clearButton_);

    configureLabel (spacingLabel_, "Spacing");
    spacingSelector_.addItem ("Even", static_cast<int> (neumaton::scaleeditor::SpacingShape::even));
    spacingSelector_.addItem ("Gentle Opening", static_cast<int> (neumaton::scaleeditor::SpacingShape::gentleOpening));
    spacingSelector_.addItem ("Natural Opening", static_cast<int> (neumaton::scaleeditor::SpacingShape::naturalOpening));
    spacingSelector_.addItem ("Steady Opening", static_cast<int> (neumaton::scaleeditor::SpacingShape::steadyOpening));
    spacingSelector_.addItem ("Soft Arc", static_cast<int> (neumaton::scaleeditor::SpacingShape::softArc));
    spacingSelector_.addItem ("Tight -> Wide", static_cast<int> (neumaton::scaleeditor::SpacingShape::tightToWide));
    spacingSelector_.addItem ("Wide -> Tight", static_cast<int> (neumaton::scaleeditor::SpacingShape::wideToTight));
    spacingSelector_.setSelectedId (static_cast<int> (neumaton::scaleeditor::SpacingShape::even),
                                    juce::dontSendNotification);
    spacingSelector_.setTooltip (
        "Mathematical spacing curves with musical names. They reshape only unlocked degrees and keep locked anchors fixed.");
    addAndMakeVisible (spacingSelector_);

    applySpacingButton_.setTooltip (
        "Apply the selected spacing shape inside each region delimited by locked degrees. The result stays fully editable.");
    applySpacingButton_.onClick = [this] { applySpacingShape(); };
    addAndMakeVisible (applySpacingButton_);

    configureLabel (valueLabel_, "Selected degree");
    valueModeSelector_.addItem ("Cents", static_cast<int> (ValueMode::cents));
    valueModeSelector_.addItem ("Ratio", static_cast<int> (ValueMode::ratio));
    valueModeSelector_.setSelectedId (static_cast<int> (ValueMode::cents), juce::dontSendNotification);
    valueModeSelector_.onChange = [this] { refreshSelectedControls(); };
    addAndMakeVisible (valueModeSelector_);

    valueEditor_.setMultiLine (false);
    valueEditor_.setInputRestrictions (24, "0123456789.:-/");
    valueEditor_.onReturnKey = [this] { applySelectedValueEditor(); };
    valueEditor_.onFocusLost = [this] { applySelectedValueEditor(); };
    addAndMakeVisible (valueEditor_);

    lockToggle_.setTooltip ("Locked degrees become fixed anchors for warp, redistribution and spacing shapes.");
    lockToggle_.onClick = [this]
    {
        if (selectedDegree_ >= 0)
        {
            geometry_.setLocked (selectedDegree_, lockToggle_.getToggleState());
            statusMessage_ = lockToggle_.getToggleState()
                ? "Degree locked."
                : "Degree unlocked.";
            refreshSelectedControls();
            refreshInfo();
            repaint();
        }
    };
    addAndMakeVisible (lockToggle_);

    includeToggle_.setTooltip ("Excluded degrees stay in the editor geometry but are omitted from the saved scale.");
    includeToggle_.onClick = [this]
    {
        if (selectedDegree_ >= 0)
        {
            geometry_.setIncluded (selectedDegree_, includeToggle_.getToggleState());
            statusMessage_ = includeToggle_.getToggleState()
                ? "Degree included in the saved scale."
                : "Degree excluded from the saved scale.";
            refreshSelectedControls();
            refreshInfo();
            repaint();
        }
    };
    addAndMakeVisible (includeToggle_);

    snapToggle_.setTooltip ("Round mouse-drag positions to the nearest cent. Numeric entry remains exact.");
    addAndMakeVisible (snapToggle_);

    infoLabel_.setFont (juce::FontOptions (12.5f));
    infoLabel_.setColour (juce::Label::textColourId, juce::Colour (0xFFFFE7A8));
    infoLabel_.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (infoLabel_);

    saveButton_.onClick = [this] { onSave(); };
    addAndMakeVisible (saveButton_);

    backButton_.onClick = [this] { listenerRef.customScaleEditorClosed(); };
    addAndMakeVisible (backButton_);
}

void CustomScaleEditor::paint (juce::Graphics& g)
{
    if (bgImage.isValid())
        g.drawImage (bgImage, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
    else
        g.fillAll (juce::Colour (0xFF111521));

    g.setColour (juce::Colour (0x88000000));
    g.fillRect (getLocalBounds());

    auto panel = scaleRail_.expanded (16, 34).toFloat();
    g.setColour (juce::Colour (panelColour));
    g.fillRoundedRectangle (panel, 12.0f);
    g.setColour (juce::Colour (0x556F7E9A));
    g.drawRoundedRectangle (panel.reduced (0.5f), 12.0f, 1.0f);

    const auto rail = scaleRail_.toFloat();
    g.setColour (juce::Colour (railColour));
    g.fillRoundedRectangle (rail, 8.0f);
    g.setColour (juce::Colour (railOutline));
    g.drawRoundedRectangle (rail.reduced (0.5f), 8.0f, 1.5f);

    std::vector<double> boundaries;
    boundaries.reserve (geometry_.degrees().size() + 2);
    boundaries.push_back (0.0);
    for (const auto& degree : geometry_.degrees())
        boundaries.push_back (degree.phase);
    boundaries.push_back (1.0);

    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i)
    {
        const float x1 = xFromPhase (boundaries[i]);
        const float x2 = xFromPhase (boundaries[i + 1]);
        if ((i % 2u) == 0u)
        {
            g.setColour (juce::Colour (0x1419A5C8));
            g.fillRect (juce::Rectangle<float> (
                x1, rail.getY(), juce::jmax (1.0f, x2 - x1), rail.getHeight()));
        }
    }

    const float centreY = rail.getCentreY();
    g.setColour (juce::Colour (0xFF8995AC));
    g.drawLine (rail.getX() + 4.0f, centreY,
                rail.getRight() - 4.0f, centreY, 1.5f);

    g.setFont (juce::FontOptions (11.0f));
    g.setColour (juce::Colours::white);
    g.drawText ("1:1", scaleRail_.getX() - 8, scaleRail_.getBottom() + 5, 48, 18,
                juce::Justification::centredLeft);
    g.drawText (formatRatio (equaveRatio_), scaleRail_.getRight() - 76,
                scaleRail_.getBottom() + 5, 84, 18,
                juce::Justification::centredRight);

    const auto& degrees = geometry_.degrees();
    const bool drawAllValues = degrees.size() <= 24;
    const double periodCents = equaveCents();

    for (int i = 0; i < static_cast<int> (degrees.size()); ++i)
    {
        const auto& degree = degrees[static_cast<std::size_t> (i)];
        const float x = xFromPhase (degree.phase);
        const bool selected = i == selectedDegree_;

        if (selected)
        {
            g.setColour (juce::Colour (selectedGlow));
            g.fillEllipse (x - 12.0f, centreY - 12.0f, 24.0f, 24.0f);
        }

        const auto colour = ! degree.included
            ? juce::Colour (excludedDegree)
            : degree.locked ? juce::Colour (lockedDegree)
                            : juce::Colour (activeDegree);

        g.setColour (colour.withAlpha (0.70f));
        g.drawLine (x, rail.getY() + 5.0f, x, rail.getBottom() - 5.0f,
                    selected ? 2.4f : 1.4f);

        const float radius = selected ? 6.5f : 5.0f;
        if (degree.included)
        {
            g.setColour (colour);
            g.fillEllipse (x - radius, centreY - radius, radius * 2.0f, radius * 2.0f);
        }
        else
        {
            g.setColour (colour);
            g.drawEllipse (x - radius, centreY - radius, radius * 2.0f, radius * 2.0f, 1.8f);
        }

        if (degree.locked)
        {
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            g.setColour (juce::Colour (lockedDegree));
            g.drawText ("L", static_cast<int> (x) - 7, scaleRail_.getY() + 4, 14, 12,
                        juce::Justification::centred);
        }

        if (drawAllValues || selected)
        {
            const auto text = juce::String (degree.phase * periodCents, selected ? 2 : 0) + "c";
            g.setFont (juce::FontOptions (selected ? 10.5f : 9.0f));
            g.setColour (degree.included ? juce::Colours::white
                                         : juce::Colour (excludedDegree));
            g.drawText (text, static_cast<int> (x) - 31, scaleRail_.getY() - 22, 62, 18,
                        juce::Justification::centred);
        }
    }

    g.setColour (juce::Colour (0xFFB9C3D6));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("period " + juce::String (periodCents, 3) + " cents",
                scaleRail_.getX(), scaleRail_.getBottom() + 5,
                scaleRail_.getWidth(), 18, juce::Justification::centred);
}

void CustomScaleEditor::resized()
{
    auto bounds = getLocalBounds().reduced (24, 18);

    titleLabel_.setBounds (bounds.removeFromTop (30));
    bounds.removeFromTop (4);
    helpLabel_.setBounds (bounds.removeFromTop (22));
    bounds.removeFromTop (8);

    auto identityRow = bounds.removeFromTop (30);
    auto nameArea = identityRow.removeFromLeft (juce::roundToInt (identityRow.getWidth() * 0.56f));
    identityRow.removeFromLeft (12);
    auto equaveArea = identityRow;

    nameLabel_.setBounds (nameArea.removeFromLeft (78));
    nameEditor_.setBounds (nameArea);
    equaveLabel_.setBounds (equaveArea.removeFromLeft (108));
    equaveEditor_.setBounds (equaveArea.removeFromLeft (98));
    equaveHintLabel_.setBounds (equaveArea);

    bounds.removeFromTop (8);
    auto generatorRow = bounds.removeFromTop (30);
    stepsLabel_.setBounds (generatorRow.removeFromLeft (42));
    stepsEditor_.setBounds (generatorRow.removeFromLeft (54));
    generatorRow.removeFromLeft (8);
    divideButton_.setBounds (generatorRow.removeFromLeft (82));
    generatorRow.removeFromLeft (8);
    redistributeButton_.setBounds (generatorRow.removeFromLeft (142));
    generatorRow.removeFromLeft (8);
    clearButton_.setBounds (generatorRow.removeFromLeft (72));

    bounds.removeFromTop (6);
    auto spacingRow = bounds.removeFromTop (30);
    spacingLabel_.setBounds (spacingRow.removeFromLeft (58));
    spacingSelector_.setBounds (spacingRow.removeFromLeft (164));
    spacingRow.removeFromLeft (8);
    applySpacingButton_.setBounds (spacingRow.removeFromLeft (112));

    bounds.removeFromTop (10);
    const int railHeight = juce::jmax (90, juce::jmin (190, bounds.getHeight() - 138));
    scaleRail_ = bounds.removeFromTop (railHeight).reduced (34, 12);
    bounds.removeFromTop (34);

    auto selectedRow = bounds.removeFromTop (30);
    valueLabel_.setBounds (selectedRow.removeFromLeft (104));
    valueModeSelector_.setBounds (selectedRow.removeFromLeft (78));
    selectedRow.removeFromLeft (6);
    valueEditor_.setBounds (selectedRow.removeFromLeft (126));
    selectedRow.removeFromLeft (12);
    lockToggle_.setBounds (selectedRow.removeFromLeft (82));
    includeToggle_.setBounds (selectedRow.removeFromLeft (92));
    snapToggle_.setBounds (selectedRow.removeFromLeft (84));

    bounds.removeFromTop (7);
    infoLabel_.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (7);

    auto buttonRow = bounds.removeFromTop (36);
    const int width = 126;
    const int gap = 16;
    const int total = width * 2 + gap;
    const int x = buttonRow.getCentreX() - total / 2;
    backButton_.setBounds (x, buttonRow.getY(), width, 34);
    saveButton_.setBounds (x + width + gap, buttonRow.getY(), width, 34);
}

void CustomScaleEditor::mouseDown (const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    if (! scaleRail_.contains (point))
        return;

    const int degree = findDegreeAt (point);
    if (degree < 0)
    {
        selectDegree (-1);
        return;
    }

    selectDegree (degree);

    if (event.mods.isCommandDown() || event.mods.isCtrlDown())
    {
        const auto& current = geometry_.degrees()[static_cast<std::size_t> (degree)];
        geometry_.setIncluded (degree, ! current.included);
        statusMessage_ = current.included ? "Degree excluded." : "Degree included.";
        refreshSelectedControls();
        refreshInfo();
        repaint();
        return;
    }

    if (! geometry_.degrees()[static_cast<std::size_t> (degree)].locked)
    {
        draggingDegree_ = degree;
        draggingWarp_ = event.mods.isShiftDown();
        dragStartGeometry_ = geometry_;
    }
}

void CustomScaleEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingDegree_ < 0 || scaleRail_.isEmpty())
        return;

    geometry_ = dragStartGeometry_;
    const double target = snappedPhase (phaseFromX (event.position.x));

    const bool moved = draggingWarp_
        ? geometry_.warpAroundDegree (draggingDegree_, target)
        : geometry_.setDegreePhase (draggingDegree_, target);

    if (! moved)
        return;

    statusMessage_ = draggingWarp_
        ? "Warping unlocked degrees between the nearest locked anchors."
        : "Degree moved.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::mouseUp (const juce::MouseEvent&)
{
    draggingDegree_ = -1;
    draggingWarp_ = false;
}

void CustomScaleEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    const auto point = event.getPosition();
    if (! scaleRail_.contains (point))
        return;

    const int degree = findDegreeAt (point);
    if (degree >= 0)
    {
        if (geometry_.removeDegree (degree))
        {
            statusMessage_ = "Degree removed.";
            selectDegree (-1);
            updateStepsEditorFromGeometry();
            refreshInfo();
            repaint();
        }
        else
        {
            statusMessage_ = "Unlock this degree before removing it.";
            refreshInfo();
        }
        return;
    }

    if (geometry_.addDegree (snappedPhase (phaseFromX (event.position.x))))
    {
        const int inserted = findDegreeAt (point, 14.0f);
        selectDegree (inserted);
        updateStepsEditorFromGeometry();
        statusMessage_ = "Degree added.";
        refreshInfo();
        repaint();
    }
}

void CustomScaleEditor::applyEqualDivision()
{
    const int steps = stepsEditor_.getText().getIntValue();
    if (steps < neumaton::scaleeditor::Geometry::minPitchClasses
        || steps > neumaton::scaleeditor::Geometry::maxPitchClasses)
    {
        statusMessage_ = "Steps must be between 3 and 96.";
        refreshInfo();
        return;
    }

    const bool sameTopology = steps == geometry_.intervalCount();
    geometry_.setEqualDivision (steps, true);
    selectedDegree_ = -1;
    statusMessage_ = sameTopology
        ? "Free degrees redistributed; locked degrees kept fixed."
        : "Equal division created; topology changed, so degree locks were reset.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::redistributeFreeDegrees()
{
    geometry_.redistributeFreeDegrees();
    statusMessage_ = "Only unlocked degrees were redistributed evenly.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::applySpacingShape()
{
    geometry_.applySpacingShape (spacingShape());
    statusMessage_ = "Spacing shape applied to unlocked degrees; locked anchors were preserved.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::clearScale()
{
    geometry_.clear();
    selectedDegree_ = -1;
    statusMessage_ = "Scale cleared. Double-click the rail to add degrees, or use Divide.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::applyEquaveEditor()
{
    double parsed = 0.0;
    if (! parsePositiveRatio (equaveEditor_.getText(), parsed)
        || ! std::isfinite (parsed)
        || parsed <= 1.0)
    {
        equaveEditor_.setText (formatRatio (equaveRatio_), juce::dontSendNotification);
        statusMessage_ = "Period must be a finite ratio greater than 1:1.";
        refreshInfo();
        return;
    }

    equaveRatio_ = parsed;
    equaveEditor_.setText (formatRatio (equaveRatio_), juce::dontSendNotification);
    statusMessage_ = "Period changed; degree geometry was preserved proportionally.";
    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::applySelectedValueEditor()
{
    if (selectedDegree_ < 0
        || selectedDegree_ >= static_cast<int> (geometry_.degrees().size()))
        return;

    const auto& selected = geometry_.degrees()[static_cast<std::size_t> (selectedDegree_)];
    if (selected.locked)
    {
        statusMessage_ = "Unlock this degree before editing its value.";
        refreshSelectedControls();
        refreshInfo();
        return;
    }

    double phase = selected.phase;
    if (valueMode() == ValueMode::cents)
    {
        const double cents = valueEditor_.getText().getDoubleValue();
        const double period = equaveCents();
        if (! std::isfinite (cents) || cents <= 0.0 || cents >= period)
        {
            statusMessage_ = "Degree cents must lie strictly inside the current period.";
            refreshSelectedControls();
            refreshInfo();
            return;
        }
        phase = cents / period;
    }
    else
    {
        double ratio = 0.0;
        if (! parsePositiveRatio (valueEditor_.getText(), ratio)
            || ratio <= 1.0 || ratio >= equaveRatio_)
        {
            statusMessage_ = "Degree ratio must lie strictly between 1:1 and the current period.";
            refreshSelectedControls();
            refreshInfo();
            return;
        }
        phase = std::log2 (ratio) / std::log2 (equaveRatio_);
    }

    if (geometry_.setDegreePhase (selectedDegree_, phase))
        statusMessage_ = "Degree value updated.";

    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::onSave()
{
    const auto name = nameEditor_.getText().trim();
    const auto ratios = geometry_.toRatios (equaveRatio_);

    if (name.isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Scale name required",
            "Enter a name before saving the scale.");
        return;
    }

    if (ratios.size() < static_cast<std::size_t> (neumaton::scaleeditor::Geometry::minPitchClasses)
        || ratios.size() > static_cast<std::size_t> (neumaton::scaleeditor::Geometry::maxPitchClasses))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Invalid scale",
            "The saved scale must contain between 3 and 96 included pitch classes.");
        return;
    }

    const bool success = processorRef.getCustomPresets().addPreset (
        name, ratios, equaveRatio_);

    if (! success)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Could not save scale",
            "The scale data was rejected by the custom-scale library.");
        return;
    }

    processorRef.refreshScaleSnapshot();
    juce::AlertWindow::showMessageBoxAsync (
        juce::MessageBoxIconType::InfoIcon,
        "Scale saved",
        "The scale \"" + name + "\" was added to your custom library.");
    listenerRef.customScaleEditorClosed();
}

void CustomScaleEditor::selectDegree (int index)
{
    if (index < 0 || index >= static_cast<int> (geometry_.degrees().size()))
        selectedDegree_ = -1;
    else
        selectedDegree_ = index;

    refreshSelectedControls();
    refreshInfo();
    repaint();
}

void CustomScaleEditor::refreshSelectedControls()
{
    const bool valid = selectedDegree_ >= 0
        && selectedDegree_ < static_cast<int> (geometry_.degrees().size());

    valueModeSelector_.setEnabled (valid);
    valueEditor_.setEnabled (valid);
    lockToggle_.setEnabled (valid);
    includeToggle_.setEnabled (valid);

    if (! valid)
    {
        valueEditor_.setText ({}, juce::dontSendNotification);
        lockToggle_.setToggleState (false, juce::dontSendNotification);
        includeToggle_.setToggleState (false, juce::dontSendNotification);
        return;
    }

    const auto& degree = geometry_.degrees()[static_cast<std::size_t> (selectedDegree_)];
    lockToggle_.setToggleState (degree.locked, juce::dontSendNotification);
    includeToggle_.setToggleState (degree.included, juce::dontSendNotification);

    if (valueMode() == ValueMode::cents)
        valueEditor_.setText (juce::String (selectedCents(), 4), juce::dontSendNotification);
    else
        valueEditor_.setText (formatRatio (selectedRatio()), juce::dontSendNotification);
}

void CustomScaleEditor::refreshInfo()
{
    const int topologyPitchClasses = geometry_.intervalCount();
    const int included = geometry_.includedPitchClassCount();

    int locked = 0;
    for (const auto& degree : geometry_.degrees())
        if (degree.locked)
            ++locked;

    juce::String text = "Topology: " + juce::String (topologyPitchClasses)
        + " pitch classes  |  saved: " + juce::String (included)
        + "  |  locked: " + juce::String (locked)
        + "  |  period: " + formatRatio (equaveRatio_)
        + " (" + juce::String (equaveCents(), 3) + "c)";

    if (statusMessage_.isNotEmpty())
        text += "  —  " + statusMessage_;

    infoLabel_.setText (text, juce::dontSendNotification);

    const bool validCount = included >= neumaton::scaleeditor::Geometry::minPitchClasses
        && included <= neumaton::scaleeditor::Geometry::maxPitchClasses;
    saveButton_.setEnabled (validCount
                            && geometry_.isStrictlyOrdered()
                            && nameEditor_.getText().trim().isNotEmpty()
                            && std::isfinite (equaveRatio_)
                            && equaveRatio_ > 1.0);
}

void CustomScaleEditor::updateStepsEditorFromGeometry()
{
    stepsEditor_.setText (juce::String (geometry_.intervalCount()), juce::dontSendNotification);
}

int CustomScaleEditor::findDegreeAt (juce::Point<int> point, float tolerancePixels) const
{
    if (! scaleRail_.expanded (static_cast<int> (std::ceil (tolerancePixels))).contains (point))
        return -1;

    int closest = -1;
    float best = tolerancePixels;
    for (int i = 0; i < static_cast<int> (geometry_.degrees().size()); ++i)
    {
        const float distance = std::abs (xFromPhase (
            geometry_.degrees()[static_cast<std::size_t> (i)].phase)
            - static_cast<float> (point.x));
        if (distance <= best)
        {
            best = distance;
            closest = i;
        }
    }
    return closest;
}

double CustomScaleEditor::phaseFromX (float x) const noexcept
{
    if (scaleRail_.getWidth() <= 0)
        return 0.0;
    return juce::jlimit (
        0.0, 1.0,
        static_cast<double> ((x - static_cast<float> (scaleRail_.getX()))
            / static_cast<float> (scaleRail_.getWidth())));
}

float CustomScaleEditor::xFromPhase (double phase) const noexcept
{
    return static_cast<float> (scaleRail_.getX())
        + static_cast<float> (juce::jlimit (0.0, 1.0, phase))
            * static_cast<float> (scaleRail_.getWidth());
}

double CustomScaleEditor::equaveCents() const noexcept
{
    return 1200.0 * std::log2 (equaveRatio_);
}

double CustomScaleEditor::selectedRatio() const noexcept
{
    if (selectedDegree_ < 0
        || selectedDegree_ >= static_cast<int> (geometry_.degrees().size()))
        return 1.0;

    return std::exp2 (
        geometry_.degrees()[static_cast<std::size_t> (selectedDegree_)].phase
        * std::log2 (equaveRatio_));
}

double CustomScaleEditor::selectedCents() const noexcept
{
    if (selectedDegree_ < 0
        || selectedDegree_ >= static_cast<int> (geometry_.degrees().size()))
        return 0.0;

    return geometry_.degrees()[static_cast<std::size_t> (selectedDegree_)].phase
        * equaveCents();
}

CustomScaleEditor::ValueMode CustomScaleEditor::valueMode() const noexcept
{
    return valueModeSelector_.getSelectedId() == static_cast<int> (ValueMode::ratio)
        ? ValueMode::ratio
        : ValueMode::cents;
}

neumaton::scaleeditor::SpacingShape CustomScaleEditor::spacingShape() const noexcept
{
    const int selected = spacingSelector_.getSelectedId();
    const int first = static_cast<int> (neumaton::scaleeditor::SpacingShape::even);
    const int last = static_cast<int> (neumaton::scaleeditor::SpacingShape::wideToTight);
    if (selected >= first && selected <= last)
        return static_cast<neumaton::scaleeditor::SpacingShape> (selected);
    return neumaton::scaleeditor::SpacingShape::even;
}

double CustomScaleEditor::snappedPhase (double phase) const noexcept
{
    phase = juce::jlimit (0.0, 1.0, phase);
    if (! snapToggle_.getToggleState())
        return phase;

    const double period = equaveCents();
    if (! std::isfinite (period) || period <= 0.0)
        return phase;

    const double cents = std::round (phase * period);
    return juce::jlimit (0.0, 1.0, cents / period);
}

bool CustomScaleEditor::parsePositiveRatio (juce::String text, double& result)
{
    text = text.trim();
    if (text.isEmpty())
        return false;

    int separator = text.indexOfChar (':');
    if (separator < 0)
        separator = text.indexOfChar ('/');

    double numerator = 0.0;
    double denominator = 1.0;
    if (separator >= 0)
    {
        numerator = text.substring (0, separator).trim().getDoubleValue();
        denominator = text.substring (separator + 1).trim().getDoubleValue();
    }
    else
    {
        numerator = text.getDoubleValue();
    }

    if (! std::isfinite (numerator)
        || ! std::isfinite (denominator)
        || numerator <= 0.0
        || denominator <= 0.0)
        return false;

    result = numerator / denominator;
    return std::isfinite (result) && result > 0.0;
}

juce::String CustomScaleEditor::formatRatio (double ratio)
{
    if (! std::isfinite (ratio) || ratio <= 0.0)
        return "-";

    int bestNumerator = 1;
    int bestDenominator = 1;
    double bestErrorCents = std::numeric_limits<double>::infinity();

    for (int denominator = 1; denominator <= 64; ++denominator)
    {
        const int numerator = juce::jmax (1, juce::roundToInt (ratio * denominator));
        const double candidate = static_cast<double> (numerator)
            / static_cast<double> (denominator);
        const double error = std::abs (1200.0 * std::log2 (ratio / candidate));
        if (error < bestErrorCents)
        {
            bestErrorCents = error;
            bestNumerator = numerator;
            bestDenominator = denominator;
        }
    }

    if (bestErrorCents <= 0.15)
        return juce::String (bestNumerator) + ":" + juce::String (bestDenominator);

    return juce::String (ratio, 7);
}
