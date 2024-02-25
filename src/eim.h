/*
 * SPDX-FileCopyrightText: 2010~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#ifndef _FCITX5_CHEWING_EIM_H_
#define _FCITX5_CHEWING_EIM_H_

#include <chewing.h>
#include <fcitx-config/configuration.h>
#include <fcitx-config/enum.h>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/i18n.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <type_traits>

namespace fcitx {
FCITX_CONFIG_ENUM_NAME_WITH_I18N(CandidateLayoutHint, N_("Not Set"),
                                 N_("Vertical"), N_("Horizontal"));

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
    Carpalx,
    ColemakDH_ANSI
};
FCITX_CONFIG_ENUM_NAME(ChewingLayout, "Default Keyboard", "Hsu's Keyboard",
                       "IBM Keyboard", "Gin-Yieh Keyboard", "ETen Keyboard",
                       "ETen26 Keyboard", "Dvorak Keyboard",
                       "Dvorak Keyboard with Hsu's support",
                       "DACHEN_CP26 Keyboard", "Han-Yu PinYin Keyboard",
                       "Carpalx Keyboard", "Colemak-DH ANSI");
FCITX_CONFIG_ENUM_I18N_ANNOTATION(ChewingLayout, N_("Default Keyboard"),
                                  N_("Hsu's Keyboard"), N_("IBM Keyboard"),
                                  N_("Gin-Yieh Keyboard"), N_("ETen Keyboard"),
                                  N_("ETen26 Keyboard"), N_("Dvorak Keyboard"),
                                  N_("Dvorak Keyboard with Hsu's support"),
                                  N_("DACHEN_CP26 Keyboard"),
                                  N_("Han-Yu PinYin Keyboard"),
                                  N_("Carpalx Keyboard"),
                                  N_("Colemak-DH ANSI"));

FCITX_CONFIGURATION(
    ChewingConfig,
    OptionWithAnnotation<ChewingSelectionKey, ChewingSelectionKeyI18NAnnotation>
        SelectionKey{this, "SelectionKey", _("Selection Key"),
                     ChewingSelectionKey::CSK_Digit};
    Option<int, IntConstrain> PageSize{this, "PageSize", _("Page Size"), 10,
                                       IntConstrain(3, 10)};
    OptionWithAnnotation<CandidateLayoutHint, CandidateLayoutHintI18NAnnotation>
        CandidateLayout{this, "CandidateLayout", _("Candidate List Layout"),
                        fcitx::CandidateLayoutHint::NotSet};
    Option<bool> UseKeypadAsSelectionKey{
        this, "UseKeypadAsSelection", _("Use Keypad as Selection key"), false};
    Option<bool> AddPhraseForward{this, "AddPhraseForward",
                                  _("Add Phrase Forward"), true};
    Option<bool> ChoiceBackward{this, "ChoiceBackward",
                                _("Backward phrase choice"), true};
    Option<bool> AutoShiftCursor{this, "AutoShiftCursor",
                                 _("Automatically shift cursor"), false};
    Option<bool> EasySymbolInput{this, "EasySymbolInput",
                                 _("Enable easy symbol"), false};
    Option<bool> SpaceAsSelection{this, "SpaceAsSelection",
                                  _("Space as selection key"), true};
    OptionWithAnnotation<ChewingLayout, ChewingLayoutI18NAnnotation> Layout{
        this, "Layout", _("Keyboard Layout"), ChewingLayout::Default};);

class ChewingEngine final : public InputMethodEngine {
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
    void filterKey(const InputMethodEntry &, KeyEvent &) override;
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

    void flushBuffer(InputContextEvent &event);
    void doReset(InputContextEvent &event);

    ChewingContext *context() { return context_.get(); }

private:
    FCITX_ADDON_DEPENDENCY_LOADER(chttrans, instance_->addonManager());

    void populateConfig();
    Instance *instance_;
    ChewingConfig config_;
    UniqueCPtr<ChewingContext, chewing_delete> context_;
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
