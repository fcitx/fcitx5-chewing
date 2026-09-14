/*
 * SPDX-FileCopyrightText: 2010~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _FCITX5_CHEWING_EIM_H_
#define _FCITX5_CHEWING_EIM_H_

#include <chewing.h>
#include <cstddef>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-config/option.h>
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/i18n.h>
#include <fcitx-utils/misc.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/action.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/text.h>
#include <memory>
#include <string>
#include <vector>

namespace fcitx {

enum class ChewingCandidateLayout {
    Vertical,
    Horizontal,
};

FCITX_CONFIG_ENUM_NAME_WITH_I18N(ChewingCandidateLayout, N_("Vertical"),
                                 N_("Horizontal"));

enum class ChewingSelectionKey {
    CSK_Digit,
    CSK_asdfghjkl,
    CSK_asdfzxcv89,
    CSK_asdfjkl789,
    CSK_aoeuhtn789,
    CSK_1234qweras,
    CSK_dstnaeo789
};

FCITX_CONFIG_ENUM_NAME(ChewingSelectionKey, "1234567890", "asdfghjkl;",
                       "asdfzxcv89", "asdfjkl789", "aoeuhtn789", "1234qweras",
                       "dstnaeo789");
FCITX_CONFIG_ENUM_I18N_ANNOTATION(ChewingSelectionKey, N_("1234567890"),
                                  N_("asdfghjkl;"), N_("asdfzxcv89"),
                                  N_("asdfjkl789"), N_("aoeuhtn789"),
                                  N_("1234qweras"), N_("dstnaeo789"));

enum class ChewingLayout {
    Default,
    Hsu,
    IBM,
    GinYieh,
    ETen,
    ETen26,
    Dvorak,
    DvorakHsu,
    DACHEN_CP26,
    HanYuPinYin,
    ThlPinYin,
    Mps2PinYin,
    Carpalx,
    Colemak,
    ColemakDH_ANSI,
    ColemakDH_ORTH
};

static constexpr const char *builtin_keymaps[] = {
    "KB_DEFAULT",        "KB_HSU",        "KB_IBM",
    "KB_GIN_YIEH",       "KB_ET",         "KB_ET26",
    "KB_DVORAK",         "KB_DVORAK_HSU", "KB_DACHEN_CP26",
    "KB_HANYU_PINYIN",   "KB_THL_PINYIN", "KB_MPS2_PINYIN",
    "KB_CARPALX",        "KB_COLEMAK",    "KB_COLEMAK_DH_ANSI",
    "KB_COLEMAK_DH_ORTH"};

FCITX_CONFIG_ENUM_NAME(ChewingLayout, "Default Keyboard", "Hsu's Keyboard",
                       "IBM Keyboard", "Gin-Yieh Keyboard", "ETen Keyboard",
                       "ETen26 Keyboard", "Dvorak Keyboard",
                       "Dvorak Keyboard with Hsu's support",
                       "DACHEN_CP26 Keyboard", "Han-Yu PinYin Keyboard",
                       "THL PinYin Keyboard", "MPS2 PinYin Keyboard",
                       "Carpalx Keyboard", "Colemak Keyboard",
                       "Colemak-DH ANSI Keyboard", "Colemak-DH Orth Keyboard");
FCITX_CONFIG_ENUM_I18N_ANNOTATION(
    ChewingLayout, N_("Default Keyboard"), N_("Hsu's Keyboard"),
    N_("IBM Keyboard"), N_("Gin-Yieh Keyboard"), N_("ETen Keyboard"),
    N_("ETen26 Keyboard"), N_("Dvorak Keyboard"),
    N_("Dvorak Keyboard with Hsu's support"), N_("DACHEN_CP26 Keyboard"),
    N_("Han-Yu PinYin Keyboard"), N_("THL PinYin Keyboard"),
    N_("MPS2 PinYin Keyboard"), N_("Carpalx Keyboard"), N_("Colemak Keyboard"),
    N_("Colemak-DH ANSI Keyboard"), N_("Colemak-DH Orth Keyboard"));

class ChewingLayoutOption : public Option<ChewingLayout> {
    using Base = Option<ChewingLayout>;

public:
    using Base::Base;

    void dumpDescription(RawConfig &config) const override {
        Base::dumpDescription(config);
        config.remove("Enum");
        for (size_t i = 0; i < supportedLayouts_.size(); i++) {
            config.setValueByPath("Enum/" + std::to_string(i),
                                  _ChewingLayout_Names[static_cast<size_t>(
                                      supportedLayouts_[i])]);
            config.setValueByPath(
                "EnumI18n/" + std::to_string(i),
                ChewingLayoutI18NAnnotation::toString(supportedLayouts_[i]));
        }
    }

private:
    static std::vector<ChewingLayout> supportedLayouts() {
        std::vector<ChewingLayout> supported = {ChewingLayout::Default};
        auto defaultNum = chewing_KBStr2Num(builtin_keymaps[0]);
        for (size_t i = 1; i < ChewingLayoutI18NAnnotation::enumLength; i++) {
            auto num = chewing_KBStr2Num(builtin_keymaps[i]);
            if (num == defaultNum) {
                continue;
            }
            supported.push_back(static_cast<ChewingLayout>(i));
        }

        return supported;
    }
    std::vector<ChewingLayout> supportedLayouts_ = supportedLayouts();
};

enum class SwitchInputMethodBehavior { Clear, CommitPreedit, CommitDefault };

enum class ChewingInputMode { Smart, ForceChinese, ForceEnglish };

FCITX_CONFIG_ENUM_NAME_WITH_I18N(ChewingInputMode, N_("Smart"),
                                 N_("Chinese"), N_("English"))

FCITX_CONFIG_ENUM_NAME_WITH_I18N(SwitchInputMethodBehavior, N_("Clear"),
                                 N_("Commit current preedit"),
                                 N_("Commit default selection"))

FCITX_CONFIGURATION(
    ChewingConfig,
    OptionWithAnnotation<ChewingSelectionKey, ChewingSelectionKeyI18NAnnotation>
        SelectionKey{this, "SelectionKey", _("Selection Key"),
                     ChewingSelectionKey::CSK_Digit};
    Option<bool> selectCandidateWithArrowKey{
        this, "SelectCandidateWithArrowKey",
        _("Select candidate with arrow key"), true};
    Option<int, IntConstrain> PageSize{this, "PageSize", _("Page Size"), 10,
                                       IntConstrain(3, 10)};
    OptionWithAnnotation<ChewingCandidateLayout,
                         ChewingCandidateLayoutI18NAnnotation>
        CandidateLayout{this, "CandidateLayout", _("Candidate List Layout"),
                        ChewingCandidateLayout::Horizontal};
    Option<bool> UseKeypadAsSelectionKey{
        this, "UseKeypadAsSelection", _("Use Keypad as Selection key"), false};
    OptionWithAnnotation<SwitchInputMethodBehavior,
                         SwitchInputMethodBehaviorI18NAnnotation>
        switchInputMethodBehavior{this, "SwitchInputMethodBehavior",
                                  _("Action when switching input method"),
                                  SwitchInputMethodBehavior::CommitDefault};
    Option<bool> AddPhraseForward{this, "AddPhraseForward",
                                  _("Add Phrase Forward"), true};
    Option<bool> ChoiceBackward{this, "ChoiceBackward",
                                _("Backward phrase choice"), true};
    Option<bool> AutoShiftCursor{this, "AutoShiftCursor",
                                 _("Automatically shift cursor"), false};
    Option<bool> EasySymbolInput{this, "EasySymbolInput",
                                 _("Enable easy symbol"), false};
    Option<bool> AsusSmartSymbolInput{
        this, "AsusSmartSymbolInput",
        _("Enable ASUS-like smart symbol input"), true};
    OptionWithAnnotation<ChewingInputMode, ChewingInputModeI18NAnnotation>
        DefaultInputMode{this, "DefaultInputMode", _("Default input mode"),
                         ChewingInputMode::Smart};
    Option<bool> SpaceAsSelection{this, "SpaceAsSelection",
                                  _("Space as selection key"), true};
    ChewingLayoutOption Layout{this, "Layout", _("Keyboard Layout"),
                               ChewingLayout::Default};);

class ChewingInputModeAction;
class ChewingSetInputModeAction;
class Menu;

class ChewingEngine final : public InputMethodEngineV3 {
public:
    ChewingEngine(Instance *instance);
    ~ChewingEngine();
    Instance *instance() { return instance_; }
    const ChewingConfig &config() { return config_; }
    void activate(const InputMethodEntry &entry,
                  InputContextEvent &event) override;
    void deactivate(const InputMethodEntry &entry,
                    InputContextEvent &event) override;
    void keyEvent(const InputMethodEntry &entry, KeyEvent &keyEvent) override;
    std::string subMode(const InputMethodEntry &entry,
                        InputContext &inputContext) override;
    std::string subModeIconImpl(const InputMethodEntry &entry,
                                InputContext &ic) override;
    std::string subModeLabelImpl(const InputMethodEntry &entry,
                                 InputContext &ic) override;
    void invokeActionImpl(const InputMethodEntry &entry,
                          InvokeActionEvent &event) override;
    void filterKey(const InputMethodEntry & /*entry*/,
                   KeyEvent & /*event*/) override;
    void reloadConfig() override;
    void reset(const InputMethodEntry &entry,
               InputContextEvent &event) override;
    void save() override;

    const Configuration *getConfig() const override { return &config_; }
    void setConfig(const RawConfig &config) override {
        config_.load(config, true);
        populateConfig();
        safeSaveAsIni(config_, "conf/chewing.conf");
    }

    void updateUI(InputContext *ic);
    void updatePreedit(InputContext *ic);
    Text getPreedit(InputContext *ic);

    void flushBuffer(InputContextEvent &event);
    void doReset(InputContextEvent &event);

    ChewingContext *context() { return context_.get(); }
    ChewingInputMode inputMode() const { return inputMode_; }
    void commitCurrentPreedit(InputContext *ic);
    void setInputMode(InputContext *ic, ChewingInputMode mode);
    void toggleInputMode(InputContext *ic);
    void updateInputModeAction(InputContext *ic);

private:
    bool handleInputModeHotkey(const KeyEvent &keyEvent);
    bool handleCandidateKeyEvent(const KeyEvent &keyEvent) const;
    bool handleSmartSymbolCandidateKeyEvent(const KeyEvent &keyEvent) const;
    void showSmartSymbolCandidates(InputContext *ic);
    void updatePreeditImpl(InputContext *ic);

    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());

    void populateConfig();
    Instance *instance_;
    ChewingConfig config_;
    UniqueCPtr<ChewingContext, chewing_delete> context_;
    ChewingInputMode inputMode_ = ChewingInputMode::Smart;
    std::unique_ptr<ChewingInputModeAction> inputModeAction_;
    std::unique_ptr<Menu> inputModeMenu_;
    std::vector<std::unique_ptr<ChewingSetInputModeAction>> inputModeMenuActions_;
    std::string smartEnglishPrefix_;
    std::string smartEnglishBuffer_;
    bool smartEnglishPassthrough_ = false;
    TrackableObjectReference<InputContext> ic_;
};

class ChewingEngineFactory : public AddonFactory {
public:
    AddonInstance *create(AddonManager *manager) override {
        registerDomain("fcitx5-chewing", FCITX_INSTALL_LOCALEDIR);
        return new ChewingEngine(manager->instance());
    }
};
} // namespace fcitx

#endif // _FCITX5_CHEWING_EIM_H_
