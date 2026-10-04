#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>
#include <vector>

class CommunityPackPage final : public juce::Component
{
public:
    explicit CommunityPackPage (MicrotonalAutotuneAudioProcessor& processor)
        : processorRef_ (processor),
          scaleList_ ("Community scales", &scaleModel_),
          presetList_ ("Community presets", &presetModel_),
          sceneList_ ("Community scenes", &sceneModel_)
    {
        setSize (760, 620);

        title_.setText ("Ergasterion Community Packs", juce::dontSendNotification);
        title_.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        title_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title_);

        folderLabel_.setText ("Pack folder", juce::dontSendNotification);
        folderLabel_.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (folderLabel_);

        folderPath_.setReadOnly (true);
        folderPath_.setTextToShowWhenEmpty (
            "No folder selected — Neumaton does not impose a pack directory.",
            juce::Colours::grey);
        addAndMakeVisible (folderPath_);

        chooseFolderButton_.onClick = [this] { choosePackFolder(); };
        rescanButton_.onClick = [this]
        {
            processorRef_.getCommunityPackLibrary().rescanPacks();
            refreshLoadedPackMenu();
        };
        addAndMakeVisible (chooseFolderButton_);
        addAndMakeVisible (rescanButton_);

        createTitle_.setText ("Create pack", juce::dontSendNotification);
        createTitle_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (createTitle_);

        packName_.setTextToShowWhenEmpty ("Pack name", juce::Colours::grey);
        author_.setTextToShowWhenEmpty ("Author / signature", juce::Colours::grey);
        addAndMakeVisible (packName_);
        addAndMakeVisible (author_);

        presetName_.setTextToShowWhenEmpty ("New preset name", juce::Colours::grey);
        savePresetButton_.onClick = [this] { saveCurrentPreset(); };
        addAndMakeVisible (presetName_);
        addAndMakeVisible (savePresetButton_);

        sceneName_.setTextToShowWhenEmpty ("New scene name", juce::Colours::grey);
        scenePresetSelector_.setTextWhenNothingSelected ("Preset for scene");
        saveSceneButton_.onClick = [this] { saveCurrentScene(); };
        addAndMakeVisible (sceneName_);
        addAndMakeVisible (scenePresetSelector_);
        addAndMakeVisible (saveSceneButton_);

        scaleList_.setMultipleSelectionEnabled (true);
        presetList_.setMultipleSelectionEnabled (true);
        sceneList_.setMultipleSelectionEnabled (true);
        addAndMakeVisible (scaleList_);
        addAndMakeVisible (presetList_);
        addAndMakeVisible (sceneList_);

        scaleListLabel_.setText ("Scales", juce::dontSendNotification);
        presetListLabel_.setText ("Presets", juce::dontSendNotification);
        sceneListLabel_.setText ("Scenes", juce::dontSendNotification);
        for (auto* label : { &scaleListLabel_, &presetListLabel_, &sceneListLabel_ })
        {
            label->setFont (juce::FontOptions (12.5f, juce::Font::bold));
            label->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*label);
        }

        exportButton_.onClick = [this] { exportSelectedPack(); };
        addAndMakeVisible (exportButton_);

        libraryTitle_.setText ("Library", juce::dontSendNotification);
        libraryTitle_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        addAndMakeVisible (libraryTitle_);

        packSelector_.setTextWhenNothingSelected ("Author / pack");
        contentType_.addItem ("Scales", 1);
        contentType_.addItem ("Presets", 2);
        contentType_.addItem ("Scenes", 3);
        contentType_.setSelectedId (1, juce::dontSendNotification);
        itemSelector_.setTextWhenNothingSelected ("Choose item");
        packSelector_.onChange = [this] { refreshLoadedItemMenu(); };
        contentType_.onChange = [this] { refreshLoadedItemMenu(); };
        applyButton_.onClick = [this] { applyLoadedItem(); };
        addAndMakeVisible (packSelector_);
        addAndMakeVisible (contentType_);
        addAndMakeVisible (itemSelector_);
        addAndMakeVisible (applyButton_);

        status_.setJustificationType (juce::Justification::centredLeft);
        status_.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (status_);

        refreshLocalContent();
        refreshFolderText();
        refreshLoadedPackMenu();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF10131E));

        const auto drawPanel = [&g] (juce::Rectangle<int> bounds)
        {
            g.setColour (juce::Colour (0xD0181D2C));
            g.fillRoundedRectangle (bounds.toFloat(), 9.0f);
            g.setColour (juce::Colour (0xFF38405F));
            g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 9.0f, 1.0f);
        };

        drawPanel (createPanel_);
        drawPanel (libraryPanel_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 14);
        title_.setBounds (area.removeFromTop (30));
        area.removeFromTop (8);

        auto folderRow = area.removeFromTop (30);
        folderLabel_.setBounds (folderRow.removeFromLeft (78));
        chooseFolderButton_.setBounds (folderRow.removeFromRight (104).reduced (3, 1));
        rescanButton_.setBounds (folderRow.removeFromRight (76).reduced (3, 1));
        folderPath_.setBounds (folderRow.reduced (3, 1));
        area.removeFromTop (10);

        createPanel_ = area.removeFromTop (348);
        auto create = createPanel_.reduced (12, 9);
        createTitle_.setBounds (create.removeFromTop (24));
        create.removeFromTop (4);

        auto identity = create.removeFromTop (28);
        packName_.setBounds (identity.removeFromLeft (identity.getWidth() / 2).reduced (2, 1));
        author_.setBounds (identity.reduced (2, 1));
        create.removeFromTop (5);

        auto saveRow = create.removeFromTop (28);
        presetName_.setBounds (saveRow.removeFromLeft (170).reduced (2, 1));
        savePresetButton_.setBounds (saveRow.removeFromLeft (118).reduced (2, 1));
        saveRow.removeFromLeft (8);
        sceneName_.setBounds (saveRow.removeFromLeft (150).reduced (2, 1));
        scenePresetSelector_.setBounds (saveRow.removeFromLeft (150).reduced (2, 1));
        saveSceneButton_.setBounds (saveRow.reduced (2, 1));
        create.removeFromTop (6);

        auto labels = create.removeFromTop (20);
        const int third = labels.getWidth() / 3;
        scaleListLabel_.setBounds (labels.removeFromLeft (third));
        presetListLabel_.setBounds (labels.removeFromLeft (third));
        sceneListLabel_.setBounds (labels);

        auto lists = create.removeFromTop (190);
        const int listThird = lists.getWidth() / 3;
        scaleList_.setBounds (lists.removeFromLeft (listThird).reduced (2));
        presetList_.setBounds (lists.removeFromLeft (listThird).reduced (2));
        sceneList_.setBounds (lists.reduced (2));

        create.removeFromTop (5);
        exportButton_.setBounds (create.removeFromBottom (30).removeFromRight (160));

        area.removeFromTop (10);
        libraryPanel_ = area.removeFromTop (112);
        auto library = libraryPanel_.reduced (12, 9);
        libraryTitle_.setBounds (library.removeFromTop (24));
        library.removeFromTop (5);
        auto browser = library.removeFromTop (30);
        const int packW = juce::jmax (190, browser.getWidth() * 38 / 100);
        packSelector_.setBounds (browser.removeFromLeft (packW).reduced (2, 1));
        contentType_.setBounds (browser.removeFromLeft (105).reduced (2, 1));
        applyButton_.setBounds (browser.removeFromRight (80).reduced (2, 1));
        itemSelector_.setBounds (browser.reduced (2, 1));

        area.removeFromTop (6);
        status_.setBounds (area.removeFromTop (24));
    }

private:
    struct SelectableItem
    {
        juce::String stableId;
        juce::String label;
    };

    class SelectionModel final : public juce::ListBoxModel
    {
    public:
        int getNumRows() override { return static_cast<int> (items.size()); }

        void paintListBoxItem (int row,
                               juce::Graphics& g,
                               int width,
                               int height,
                               bool selected) override
        {
            if (row < 0 || row >= static_cast<int> (items.size()))
                return;
            if (selected)
            {
                g.setColour (juce::Colour (0xFF384E75));
                g.fillRect (0, 0, width, height);
            }
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (items[static_cast<std::size_t> (row)].label,
                              6, 0, width - 10, height,
                              juce::Justification::centredLeft, 1);
        }

        std::vector<SelectableItem> items;
    };

    [[nodiscard]] static juce::StringArray selectedIds (
        const juce::ListBox& list,
        const SelectionModel& model)
    {
        juce::StringArray result;
        for (int row = 0; row < model.getNumRows(); ++row)
            if (list.isRowSelected (row))
                result.add (model.items[static_cast<std::size_t> (row)].stableId);
        return result;
    }

    void setStatus (const juce::String& text, bool error = false)
    {
        status_.setColour (juce::Label::textColourId,
                           error ? juce::Colour (0xFFFF8F8F)
                                 : juce::Colour (0xFFDCE4FF));
        status_.setText (text, juce::dontSendNotification);
    }

    void refreshFolderText()
    {
        const auto& directory = processorRef_.getCommunityPackLibrary().getPackDirectory();
        folderPath_.setText (directory.isDirectory() ? directory.getFullPathName()
                                                      : juce::String(),
                             juce::dontSendNotification);
    }

    void refreshLocalContent()
    {
        scaleModel_.items.clear();
        for (const auto& entry : processorRef_.getCommunityScaleEntries())
        {
            const auto id = entry.getProperty ("stableId").toString();
            const auto category = entry.getProperty ("category").toString();
            const auto name = entry.getProperty ("name").toString();
            scaleModel_.items.push_back ({ id, category + " — " + name });
        }
        scaleList_.updateContent();

        presetModel_.items.clear();
        scenePresetSelector_.clear (juce::dontSendNotification);
        const auto& presets = processorRef_.getCommunityPackLibrary().getUserPresets();
        for (int i = 0; i < static_cast<int> (presets.size()); ++i)
        {
            const auto& preset = presets[static_cast<std::size_t> (i)];
            presetModel_.items.push_back ({ preset.stableId, preset.name });
            scenePresetSelector_.addItem (preset.name, i + 1);
        }
        presetList_.updateContent();

        sceneModel_.items.clear();
        for (const auto& scene : processorRef_.getCommunityPackLibrary().getUserScenes())
            sceneModel_.items.push_back ({ scene.stableId, scene.name });
        sceneList_.updateContent();
    }

    void choosePackFolder()
    {
        chooser_ = std::make_unique<juce::FileChooser> (
            "Choose Community Pack folder",
            processorRef_.getCommunityPackLibrary().getPackDirectory());

        chooser_->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& chooser)
            {
                const auto folder = chooser.getResult();
                if (! folder.isDirectory())
                    return;
                processorRef_.getCommunityPackLibrary().setPackDirectory (folder);
                refreshFolderText();
                refreshLoadedPackMenu();
                setStatus ("Pack folder selected and scanned.");
            });
    }

    void saveCurrentPreset()
    {
        const auto name = presetName_.getText().trim();
        if (name.isEmpty())
        {
            setStatus ("Give the preset a name before saving it.", true);
            return;
        }

        const auto id = processorRef_.saveCurrentCommunityPreset (name);
        if (id.isEmpty())
        {
            setStatus ("Preset could not be saved.", true);
            return;
        }

        presetName_.clear();
        refreshLocalContent();
        setStatus ("Preset saved to the local Community library.");
    }

    void saveCurrentScene()
    {
        const auto name = sceneName_.getText().trim();
        const int presetIndex = scenePresetSelector_.getSelectedId() - 1;
        const auto& presets = processorRef_.getCommunityPackLibrary().getUserPresets();

        if (name.isEmpty() || presetIndex < 0
            || presetIndex >= static_cast<int> (presets.size()))
        {
            setStatus ("A scene needs a name and one saved preset.", true);
            return;
        }

        const auto id = processorRef_.saveCurrentCommunityScene (
            name, presets[static_cast<std::size_t> (presetIndex)].stableId);
        if (id.isEmpty())
        {
            setStatus ("Scene could not be saved.", true);
            return;
        }

        sceneName_.clear();
        refreshLocalContent();
        setStatus ("Scene saved: current scale + selected preset.");
    }

    void exportSelectedPack()
    {
        const auto name = packName_.getText().trim();
        const auto author = author_.getText().trim();
        if (name.isEmpty() || author.isEmpty())
        {
            setStatus ("A pack needs both a name and an author/signature.", true);
            return;
        }

        const auto scales = selectedIds (scaleList_, scaleModel_);
        const auto presets = selectedIds (presetList_, presetModel_);
        const auto scenes = selectedIds (sceneList_, sceneModel_);
        if (scales.isEmpty() && presets.isEmpty() && scenes.isEmpty())
        {
            setStatus ("Select at least one scale, preset or scene.", true);
            return;
        }

        const auto manifest =
            neumaton::community::CommunityPackLibrary::makeManifest (name, author);
        auto document = processorRef_.getCommunityPackLibrary().buildPack (
            manifest,
            processorRef_.getCommunityScaleEntries(),
            scales,
            presets,
            scenes);

        if (! document.isValid())
        {
            setStatus ("The pack manifest is incomplete.", true);
            return;
        }

        juce::File initial = processorRef_.getCommunityPackLibrary().getPackDirectory();
        if (! initial.isDirectory())
            initial = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
        initial = initial.getChildFile (juce::File::createLegalFileName (name) + ".ecpk");

        chooser_ = std::make_unique<juce::FileChooser> (
            "Export Ergasterion Community Pack", initial, "*.ecpk");
        chooser_->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, document = std::move (document)] (const juce::FileChooser& chooser) mutable
            {
                auto file = chooser.getResult();
                if (file == juce::File())
                    return;
                if (! document.writeToFile (file))
                {
                    setStatus ("Export failed.", true);
                    return;
                }
                processorRef_.getCommunityPackLibrary().rescanPacks();
                refreshLoadedPackMenu();
                setStatus ("Community pack exported as .ecpk.");
            });
    }

    void refreshLoadedPackMenu()
    {
        packSelector_.clear (juce::dontSendNotification);
        const auto& packs = processorRef_.getCommunityPackLibrary().getLoadedPacks();

        juce::String lastAuthor;
        for (int i = 0; i < static_cast<int> (packs.size()); ++i)
        {
            const auto& manifest = packs[static_cast<std::size_t> (i)].document.manifest;
            if (manifest.author != lastAuthor)
            {
                packSelector_.addSectionHeading (manifest.author);
                lastAuthor = manifest.author;
            }
            packSelector_.addItem (manifest.name, i + 1);
        }

        if (! packs.empty())
            packSelector_.setSelectedId (1, juce::dontSendNotification);
        refreshLoadedItemMenu();
    }

    void refreshLoadedItemMenu()
    {
        itemSelector_.clear (juce::dontSendNotification);
        const int packIndex = packSelector_.getSelectedId() - 1;
        const auto& packs = processorRef_.getCommunityPackLibrary().getLoadedPacks();
        if (packIndex < 0 || packIndex >= static_cast<int> (packs.size()))
            return;

        const auto& document = packs[static_cast<std::size_t> (packIndex)].document;
        const int type = contentType_.getSelectedId();
        const juce::ValueTree* collection = type == 1 ? &document.scales
            : type == 2 ? &document.presets
                        : &document.scenes;

        for (int i = 0; i < collection->getNumChildren(); ++i)
        {
            const auto node = collection->getChild (i);
            const auto name = node.getProperty ("name").toString();
            itemSelector_.addItem (name.isNotEmpty() ? name : "Unnamed", i + 1);
        }

        if (collection->getNumChildren() > 0)
            itemSelector_.setSelectedId (1, juce::dontSendNotification);
    }

    void applyLoadedItem()
    {
        const int packIndex = packSelector_.getSelectedId() - 1;
        const int itemIndex = itemSelector_.getSelectedId() - 1;
        const int type = contentType_.getSelectedId();
        const auto& packs = processorRef_.getCommunityPackLibrary().getLoadedPacks();
        if (packIndex < 0 || packIndex >= static_cast<int> (packs.size())
            || itemIndex < 0)
        {
            setStatus ("Choose a pack item first.", true);
            return;
        }

        const auto& document = packs[static_cast<std::size_t> (packIndex)].document;
        bool applied = false;

        if (type == 1 && itemIndex < document.scales.getNumChildren())
        {
            const auto node = document.scales.getChild (itemIndex);
            applied = processorRef_.activateCommunityScale (
                node.getProperty ("stableId").toString(), &document);
        }
        else if (type == 2 && itemIndex < document.presets.getNumChildren())
        {
            applied = processorRef_.applyCommunityPresetTree (
                document.presets.getChild (itemIndex));
        }
        else if (type == 3 && itemIndex < document.scenes.getNumChildren())
        {
            const auto scene = neumaton::community::CommunityScene::fromValueTree (
                document.scenes.getChild (itemIndex));
            applied = processorRef_.applyCommunityScene (scene, &document);
        }

        if (applied)
        {
            refreshLocalContent();
            setStatus ("Community item applied.");
        }
        else
        {
            setStatus ("The selected item could not be resolved.", true);
        }
    }

    MicrotonalAutotuneAudioProcessor& processorRef_;

    juce::Label title_;
    juce::Label folderLabel_;
    juce::TextEditor folderPath_;
    juce::TextButton chooseFolderButton_ { "Choose..." };
    juce::TextButton rescanButton_ { "Rescan" };

    juce::Label createTitle_;
    juce::TextEditor packName_;
    juce::TextEditor author_;
    juce::TextEditor presetName_;
    juce::TextButton savePresetButton_ { "Save preset" };
    juce::TextEditor sceneName_;
    juce::ComboBox scenePresetSelector_;
    juce::TextButton saveSceneButton_ { "Save scene" };

    SelectionModel scaleModel_;
    SelectionModel presetModel_;
    SelectionModel sceneModel_;
    juce::ListBox scaleList_;
    juce::ListBox presetList_;
    juce::ListBox sceneList_;
    juce::Label scaleListLabel_;
    juce::Label presetListLabel_;
    juce::Label sceneListLabel_;
    juce::TextButton exportButton_ { "Export .ecpk" };

    juce::Label libraryTitle_;
    juce::ComboBox packSelector_;
    juce::ComboBox contentType_;
    juce::ComboBox itemSelector_;
    juce::TextButton applyButton_ { "Apply" };
    juce::Label status_;

    juce::Rectangle<int> createPanel_;
    juce::Rectangle<int> libraryPanel_;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommunityPackPage)
};

inline void launchCommunityPackWindow (MicrotonalAutotuneAudioProcessor& processor)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Ergasterion Community Packs";
    options.dialogBackgroundColour = juce::Colour (0xFF10131E);
    options.content.setOwned (new CommunityPackPage (processor));
    options.componentToCentreAround = nullptr;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}
