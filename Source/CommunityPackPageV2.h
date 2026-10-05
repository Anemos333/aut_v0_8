#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

#include <memory>
#include <vector>

class CommunityPackPageV2 final : public juce::Component
{
public:
    explicit CommunityPackPageV2 (MicrotonalAutotuneAudioProcessor& processor)
        : processorRef_ (processor),
          scaleList_ ("Pack scales", &scaleModel_),
          presetList_ ("Pack presets", &presetModel_),
          sceneList_ ("Pack scenes", &sceneModel_)
    {
        setSize (780, 630);

        processorRef_.getCommunityPackLibrary().reloadFromDisk();

        configureTitle (title_, "Ergasterion Community Packs", 22.0f);
        configureTitle (createTitle_, "Create pack", 16.0f);
        configureTitle (libraryTitle_, "Library", 16.0f);

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
            processorRef_.getCommunityPackLibrary().reloadFromDisk();
            refreshFolderText();
            refreshLocalContent();
            refreshLoadedPackMenu();
            setStatus ("Community library reloaded and pack folder rescanned.");
        };
        addAndMakeVisible (chooseFolderButton_);
        addAndMakeVisible (rescanButton_);

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

        addAndMakeVisible (scaleList_);
        addAndMakeVisible (presetList_);
        addAndMakeVisible (sceneList_);

        configureListLabel (scaleListLabel_, "Scales");
        configureListLabel (presetListLabel_, "Presets");
        configureListLabel (sceneListLabel_, "Scenes");

        selectAllScales_.onClick = [this]
        {
            scaleModel_.setAllChecked (selectAllScales_.getToggleState());
            scaleList_.updateContent();
        };
        selectAllPresets_.onClick = [this]
        {
            presetModel_.setAllChecked (selectAllPresets_.getToggleState());
            presetList_.updateContent();
        };
        selectAllScenes_.onClick = [this]
        {
            sceneModel_.setAllChecked (selectAllScenes_.getToggleState());
            sceneList_.updateContent();
        };
        addAndMakeVisible (selectAllScales_);
        addAndMakeVisible (selectAllPresets_);
        addAndMakeVisible (selectAllScenes_);

        exportButton_.onClick = [this] { exportSelectedPack(); };
        addAndMakeVisible (exportButton_);

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

        status_.setFont (juce::FontOptions (12.0f));
        status_.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (status_);

        refreshLocalContent();
        refreshFolderText();
        refreshLoadedPackMenu();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF10131E));
        drawPanel (g, createPanel_);
        drawPanel (g, libraryPanel_);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18, 14);
        title_.setBounds (area.removeFromTop (30));
        area.removeFromTop (8);

        auto folder = area.removeFromTop (30);
        folderLabel_.setBounds (folder.removeFromLeft (80));
        chooseFolderButton_.setBounds (folder.removeFromRight (100).reduced (2, 1));
        rescanButton_.setBounds (folder.removeFromRight (84).reduced (2, 1));
        folderPath_.setBounds (folder.reduced (2, 1));
        area.removeFromTop (10);

        createPanel_ = area.removeFromTop (364);
        auto create = createPanel_.reduced (12, 9);
        createTitle_.setBounds (create.removeFromTop (24));
        create.removeFromTop (4);

        auto identity = create.removeFromTop (28);
        const int identityHalf = identity.getWidth() / 2;
        packName_.setBounds (identity.removeFromLeft (identityHalf).reduced (2, 1));
        author_.setBounds (identity.reduced (2, 1));
        create.removeFromTop (6);

        auto save = create.removeFromTop (28);
        presetName_.setBounds (save.removeFromLeft (160).reduced (2, 1));
        savePresetButton_.setBounds (save.removeFromLeft (112).reduced (2, 1));
        save.removeFromLeft (8);
        sceneName_.setBounds (save.removeFromLeft (136).reduced (2, 1));
        scenePresetSelector_.setBounds (save.removeFromLeft (160).reduced (2, 1));
        saveSceneButton_.setBounds (save.reduced (2, 1));
        create.removeFromTop (7);

        auto labels = create.removeFromTop (22);
        const int third = labels.getWidth() / 3;
        auto scaleLabelArea = labels.removeFromLeft (third);
        auto presetLabelArea = labels.removeFromLeft (third);
        auto sceneLabelArea = labels;

        scaleListLabel_.setBounds (scaleLabelArea.removeFromLeft (90));
        selectAllScales_.setBounds (scaleLabelArea.reduced (2, 0));
        presetListLabel_.setBounds (presetLabelArea.removeFromLeft (90));
        selectAllPresets_.setBounds (presetLabelArea.reduced (2, 0));
        sceneListLabel_.setBounds (sceneLabelArea.removeFromLeft (90));
        selectAllScenes_.setBounds (sceneLabelArea.reduced (2, 0));

        auto lists = create.removeFromTop (204);
        const int listThird = lists.getWidth() / 3;
        scaleList_.setBounds (lists.removeFromLeft (listThird).reduced (2));
        presetList_.setBounds (lists.removeFromLeft (listThird).reduced (2));
        sceneList_.setBounds (lists.reduced (2));

        create.removeFromTop (6);
        exportButton_.setBounds (create.removeFromBottom (30).removeFromRight (156));

        area.removeFromTop (10);
        libraryPanel_ = area.removeFromTop (112);
        auto library = libraryPanel_.reduced (12, 9);
        libraryTitle_.setBounds (library.removeFromTop (24));
        library.removeFromTop (5);

        auto browser = library.removeFromTop (30);
        packSelector_.setBounds (browser.removeFromLeft (230).reduced (2, 1));
        contentType_.setBounds (browser.removeFromLeft (104).reduced (2, 1));
        applyButton_.setBounds (browser.removeFromRight (78).reduced (2, 1));
        itemSelector_.setBounds (browser.reduced (2, 1));

        area.removeFromTop (6);
        status_.setBounds (area.removeFromTop (24));
    }

private:
    struct SelectableItem
    {
        juce::String stableId;
        juce::String label;
        bool checked = false;
    };

    class SelectionModel final : public juce::ListBoxModel
    {
    public:
        class CheckRow final : public juce::Component
        {
        public:
            explicit CheckRow (SelectionModel& owner) : owner_ (owner)
            {
                checkbox_.setClickingTogglesState (true);
                checkbox_.onClick = [this]
                {
                    if (row_ >= 0 && row_ < static_cast<int> (owner_.items.size()))
                        owner_.items[static_cast<std::size_t> (row_)].checked =
                            checkbox_.getToggleState();
                };
                addAndMakeVisible (checkbox_);
            }

            void setRow (int row)
            {
                row_ = row;
                if (row_ < 0 || row_ >= static_cast<int> (owner_.items.size()))
                    return;

                const auto& item = owner_.items[static_cast<std::size_t> (row_)];
                checkbox_.setButtonText (item.label);
                checkbox_.setToggleState (item.checked, juce::dontSendNotification);
            }

            void resized() override
            {
                checkbox_.setBounds (getLocalBounds().reduced (4, 0));
            }

        private:
            SelectionModel& owner_;
            juce::ToggleButton checkbox_;
            int row_ = -1;
        };

        int getNumRows() override
        {
            return static_cast<int> (items.size());
        }

        void paintListBoxItem (int,
                               juce::Graphics&,
                               int,
                               int,
                               bool) override
        {
        }

        juce::Component* refreshComponentForRow (
            int rowNumber,
            bool,
            juce::Component* existingComponentToUpdate) override
        {
            auto* row = dynamic_cast<CheckRow*> (existingComponentToUpdate);
            if (row == nullptr)
                row = new CheckRow (*this);
            row->setRow (rowNumber);
            return row;
        }

        [[nodiscard]] juce::StringArray checkedIds() const
        {
            juce::StringArray ids;
            for (const auto& item : items)
                if (item.checked)
                    ids.add (item.stableId);
            return ids;
        }

        void setAllChecked (bool checked)
        {
            for (auto& item : items)
                item.checked = checked;
        }

        std::vector<SelectableItem> items;
    };

    void configureTitle (juce::Label& label,
                         const juce::String& text,
                         float size)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (size, juce::Font::bold));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    void configureListLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        label.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (label);
    }

    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        if (bounds.isEmpty())
            return;
        g.setColour (juce::Colour (0xD0181D2C));
        g.fillRoundedRectangle (bounds.toFloat(), 9.0f);
        g.setColour (juce::Colour (0xFF38405F));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 9.0f, 1.0f);
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
        const auto& directory =
            processorRef_.getCommunityPackLibrary().getPackDirectory();
        folderPath_.setText (directory.isDirectory()
                                 ? directory.getFullPathName()
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
            const auto label = category.isNotEmpty()
                ? category + " — " + name
                : name;
            scaleModel_.items.push_back ({ id, label, false });
        }
        scaleList_.updateContent();

        presetModel_.items.clear();
        scenePresetSelector_.clear (juce::dontSendNotification);
        const auto& presets =
            processorRef_.getCommunityPackLibrary().getUserPresets();
        for (int i = 0; i < static_cast<int> (presets.size()); ++i)
        {
            const auto& preset = presets[static_cast<std::size_t> (i)];
            presetModel_.items.push_back ({ preset.stableId, preset.name, false });
            scenePresetSelector_.addItem (preset.name, i + 1);
        }
        presetList_.updateContent();

        sceneModel_.items.clear();
        for (const auto& scene : processorRef_.getCommunityPackLibrary().getUserScenes())
            sceneModel_.items.push_back ({ scene.stableId, scene.name, false });
        sceneList_.updateContent();

        selectAllScales_.setToggleState (false, juce::dontSendNotification);
        selectAllPresets_.setToggleState (false, juce::dontSendNotification);
        selectAllScenes_.setToggleState (false, juce::dontSendNotification);
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

        if (processorRef_.saveCurrentCommunityPreset (name).isEmpty())
        {
            setStatus ("Preset could not be saved.", true);
            return;
        }

        presetName_.clear();
        refreshLocalContent();
        setStatus ("Preset saved with Center + Tuning context.");
    }

    void saveCurrentScene()
    {
        const auto name = sceneName_.getText().trim();
        const int presetIndex = scenePresetSelector_.getSelectedId() - 1;
        const auto& presets =
            processorRef_.getCommunityPackLibrary().getUserPresets();

        if (name.isEmpty()
            || presetIndex < 0
            || presetIndex >= static_cast<int> (presets.size()))
        {
            setStatus ("A scene needs a name and one saved preset.", true);
            return;
        }

        const auto& preset = presets[static_cast<std::size_t> (presetIndex)];
        if (processorRef_.saveCurrentCommunityScene (
                name, preset.stableId).isEmpty())
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

        const auto scales = scaleModel_.checkedIds();
        const auto presets = presetModel_.checkedIds();
        const auto scenes = sceneModel_.checkedIds();
        if (scales.isEmpty() && presets.isEmpty() && scenes.isEmpty())
        {
            setStatus ("Tick at least one scale, preset or scene.", true);
            return;
        }

        const auto manifest =
            neumaton::community::CommunityPackLibrary::makeManifest (
                name, author);
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

        auto initial = processorRef_.getCommunityPackLibrary().getPackDirectory();
        if (! initial.isDirectory())
            initial = juce::File::getSpecialLocation (
                juce::File::userDocumentsDirectory);
        initial = initial.getChildFile (
            juce::File::createLegalFileName (name) + ".ecpk");

        chooser_ = std::make_unique<juce::FileChooser> (
            "Export Ergasterion Community Pack", initial, "*.ecpk");
        chooser_->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, document = std::move (document)] (
                const juce::FileChooser& chooser) mutable
            {
                const auto file = chooser.getResult();
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
        const auto& packs =
            processorRef_.getCommunityPackLibrary().getLoadedPacks();

        juce::String lastAuthor;
        for (int i = 0; i < static_cast<int> (packs.size()); ++i)
        {
            const auto& manifest =
                packs[static_cast<std::size_t> (i)].document.manifest;
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
        const auto& packs =
            processorRef_.getCommunityPackLibrary().getLoadedPacks();
        if (packIndex < 0 || packIndex >= static_cast<int> (packs.size()))
            return;

        const auto& document =
            packs[static_cast<std::size_t> (packIndex)].document;
        const int type = contentType_.getSelectedId();
        const auto& collection = type == 1 ? document.scales
            : type == 2 ? document.presets
                        : document.scenes;

        for (int i = 0; i < collection.getNumChildren(); ++i)
        {
            const auto node = collection.getChild (i);
            const auto name = node.getProperty ("name").toString();
            itemSelector_.addItem (
                name.isNotEmpty() ? name : juce::String ("Unnamed"), i + 1);
        }

        if (collection.getNumChildren() > 0)
            itemSelector_.setSelectedId (1, juce::dontSendNotification);
    }

    void applyLoadedItem()
    {
        const int packIndex = packSelector_.getSelectedId() - 1;
        const int itemIndex = itemSelector_.getSelectedId() - 1;
        const int type = contentType_.getSelectedId();
        const auto& packs =
            processorRef_.getCommunityPackLibrary().getLoadedPacks();

        if (packIndex < 0 || packIndex >= static_cast<int> (packs.size())
            || itemIndex < 0)
        {
            setStatus ("Choose a pack item first.", true);
            return;
        }

        const auto& document =
            packs[static_cast<std::size_t> (packIndex)].document;
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
            const auto scene =
                neumaton::community::CommunityScene::fromValueTree (
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
    juce::TextButton rescanButton_ { "Reload" };

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
    juce::ToggleButton selectAllScales_ { "All" };
    juce::ToggleButton selectAllPresets_ { "All" };
    juce::ToggleButton selectAllScenes_ { "All" };
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommunityPackPageV2)
};

inline void launchCommunityPackWindowV2 (MicrotonalAutotuneAudioProcessor& processor)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Ergasterion Community Packs";
    options.dialogBackgroundColour = juce::Colour (0xFF10131E);
    options.content.setOwned (new CommunityPackPageV2 (processor));
    options.componentToCentreAround = nullptr;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}
