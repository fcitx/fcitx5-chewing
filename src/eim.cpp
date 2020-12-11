/*
 * SPDX-FileCopyrightText: 2012~2012 Tai-Lin Chu <tailinchu@gmail.com>
 * SPDX-FileCopyrightText: 2012~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "eim.h"
#include <fcitx-utils/utf8.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterfacemanager.h>

FCITX_DEFINE_LOG_CATEGORY(chewing_log, "chewing");
#define CHEWING_DEBUG() FCITX_LOGC(chewing_log, Debug)

namespace fcitx {

namespace {

constexpr int CHEWING_MAX_LEN = 18;

std::string builtin_keymaps[] = {
    "KB_DEFAULT",     "KB_HSU",          "KB_IBM",    "KB_GIN_YEIH",
    "KB_ET",          "KB_ET26",         "KB_DVORAK", "KB_DVORAK_HSU",
    "KB_DACHEN_CP26", "KB_HANYU_PINYIN", "KB_CARPALX"};

static const char *builtin_selectkeys[] = {
    "1234567890", "asdfghjkl;", "asdfzxcv89", "asdfjkl789",
    "aoeuhtn789", "1234qweras", "dstnaeo789",
};

static_assert(FCITX_ARRAY_SIZE(builtin_selectkeys) ==
                  ChewingSelectionKeyI18NAnnotation::enumLength,
              "Enum mismatch");

class ChewingCandidateWord : public CandidateWord {
public:
    ChewingCandidateWord(ChewingEngine *engine, std::string str, int index)
        : CandidateWord(Text(str)), engine_(engine), index_(index) {}

    void select(InputContext *inputContext) const override {
        auto pageSize = engine_->instance()->globalConfig().defaultPageSize();
        auto ctx = engine_->context();
        int page = index_ / pageSize + chewing_cand_CurrentPage(ctx);
        int off = index_ % pageSize;
        if (page < 0 || page >= chewing_cand_TotalPage(ctx))
            return;
        int lastPage = chewing_cand_CurrentPage(ctx);
        while (page != chewing_cand_CurrentPage(ctx)) {
            if (page < chewing_cand_CurrentPage(ctx)) {
                chewing_handle_Left(ctx);
            }
            if (page > chewing_cand_CurrentPage(ctx)) {
                chewing_handle_Right(ctx);
            }
            /* though useless, but take care if there is a bug cause freeze */
            if (lastPage == chewing_cand_CurrentPage(ctx)) {
                break;
            }
            lastPage = chewing_cand_CurrentPage(ctx);
        }
        chewing_handle_Default(ctx, builtin_selectkeys[static_cast<int>(
                                        *engine_->config().SelectionKey)][off]);

        if (chewing_keystroke_CheckAbsorb(ctx)) {
            engine_->updateUI(inputContext);
        } else if (chewing_keystroke_CheckIgnore(ctx)) {
            return;
        } else if (chewing_commit_Check(ctx)) {
            UniqueCPtr<char, chewing_free> str(chewing_commit_String(ctx));
            inputContext->commitString(str.get());
            engine_->updateUI(inputContext);
        } else {
            engine_->updateUI(inputContext);
        }
    }

private:
    ChewingEngine *engine_;
    int index_;
};

class ChewingCandidateList : public CandidateList,
                             public PageableCandidateList {
public:
    ChewingCandidateList(ChewingEngine *engine, InputContext *ic)
        : engine_(engine), ic_(ic) {
        auto ctx = engine_->context();
        setPageable(this);
        int index = 0;
        // get candidate word
        int pageSize = chewing_get_candPerPage(ctx);
        chewing_cand_Enumerate(ctx);
        while (chewing_cand_hasNext(ctx) && index < pageSize) {
            UniqueCPtr<char, chewing_free> str(chewing_cand_String(ctx));
            candidateWords_.emplace_back(std::make_unique<ChewingCandidateWord>(
                engine_, str.get(), index));
            if (index < 10) {
                const char label[] = {
                    builtin_selectkeys[static_cast<int>(
                        *engine_->config().SelectionKey)][index],
                    '.', '\0'};
                labels_.emplace_back(label);
            } else {
                labels_.emplace_back();
            }
            index++;
        }

        hasPrev_ = chewing_cand_CurrentPage(ctx) > 0;
        hasNext_ =
            chewing_cand_CurrentPage(ctx) + 1 < chewing_cand_TotalPage(ctx);
    }

    const Text &label(int idx) const override {
        if (idx < 0 || idx >= size()) {
            throw std::invalid_argument("Invalid index");
        }
        return labels_[idx];
    }
    const CandidateWord &candidate(int idx) const override {
        if (idx < 0 || idx >= size()) {
            throw std::invalid_argument("Invalid index");
        }
        return *candidateWords_[idx];
    }

    int size() const override { return candidateWords_.size(); }
    int cursorIndex() const override { return -1; }
    CandidateLayoutHint layoutHint() const override {
        return CandidateLayoutHint::NotSet;
    }

    // Need for paging
    bool hasPrev() const override { return hasPrev_; }
    bool hasNext() const override { return hasNext_; }
    void prev() override { paging(true); }
    void next() override { paging(false); }

    bool usedNextBefore() const override { return true; }

private:
    void paging(bool prev) {
        if (candidateWords_.empty()) {
            return;
        }

        auto ctx = engine_->context();
        if (prev) {
            chewing_handle_Left(ctx);
        } else {
            chewing_handle_Right(ctx);
        }

        if (chewing_keystroke_CheckAbsorb(ctx)) {
            engine_->updateUI(ic_);
        }
    }

    bool hasPrev_ = false;
    bool hasNext_ = false;
    ChewingEngine *engine_;
    InputContext *ic_;
    std::vector<std::unique_ptr<ChewingCandidateWord>> candidateWords_;
    std::vector<Text> labels_;
};

} // namespace

ChewingEngine::ChewingEngine(Instance *instance)
    : instance_(instance), context_(chewing_new()) {
    chewing_set_maxChiSymbolLen(context_.get(), CHEWING_MAX_LEN);
    chewing_set_candPerPage(context_.get(),
                            instance_->globalConfig().defaultPageSize());
    reloadConfig();
}

ChewingEngine::~ChewingEngine() = default;

void ChewingEngine::reloadConfig() {
    readAsIni(config_, "conf/chewing.conf");
    populateConfig();
}

void ChewingEngine::populateConfig() {
    ChewingContext *ctx = context_.get();
    chewing_set_addPhraseDirection(ctx, *config_.AddPhraseForward ? 0 : 1);
    chewing_set_phraseChoiceRearward(ctx, *config_.ChoiceBackward ? 1 : 0);
    chewing_set_autoShiftCur(ctx, *config_.AutoShiftCursor ? 1 : 0);
    chewing_set_spaceAsSelection(ctx, *config_.SpaceAsSelection ? 1 : 0);
    chewing_set_escCleanAllBuf(ctx, 1);
}

void ChewingEngine::reset(const InputMethodEntry &, InputContextEvent &event) {
    doReset(event);
}

void ChewingEngine::doReset(InputContextEvent &event) {
    ChewingContext *ctx = context_.get();
    chewing_Reset(ctx);

    chewing_set_KBType(
        ctx, chewing_KBStr2Num(
                 builtin_keymaps[static_cast<int>(*config_.Layout)].data()));

    chewing_set_ChiEngMode(ctx, CHINESE_MODE);
    updateUI(event.inputContext());
}

void ChewingEngine::save() {}

void ChewingEngine::activate(const InputMethodEntry &,
                             InputContextEvent &event) {
    // Request chttrans.
    // Fullwidth is not required for chewing.
    chttrans();
    auto *inputContext = event.inputContext();
    if (auto *action =
            instance_->userInterfaceManager().lookupAction("chttrans")) {
        inputContext->statusArea().addAction(StatusGroup::InputMethod, action);
    }
}

void ChewingEngine::deactivate(const InputMethodEntry &entry,
                               InputContextEvent &event) {
    if (event.type() == EventType::InputContextFocusOut ||
        event.type() == EventType::InputContextSwitchInputMethod) {
        flushBuffer(event);
    } else {
        reset(entry, event);
    }
}

void ChewingEngine::keyEvent(const InputMethodEntry &entry,
                             KeyEvent &keyEvent) {
    auto ctx = context_.get();
    if (keyEvent.isRelease()) {
        return;
    }

    CHEWING_DEBUG() << "KeyEvent: " << keyEvent.key().toString();
    auto ic = keyEvent.inputContext();
    const KeyList keypadKeys{Key{FcitxKey_KP_1}, Key{FcitxKey_KP_2},
                             Key{FcitxKey_KP_3}, Key{FcitxKey_KP_4},
                             Key{FcitxKey_KP_5}, Key{FcitxKey_KP_6},
                             Key{FcitxKey_KP_7}, Key{FcitxKey_KP_8},
                             Key{FcitxKey_KP_9}, Key{FcitxKey_KP_0}};
    if (*config_.UseKeypadAsSelectionKey && ic->inputPanel().candidateList()) {
        if (int index = keyEvent.key().keyListIndex(keypadKeys);
            index >= 0 && index < ic->inputPanel().candidateList()->size()) {
            FCITX_INFO() << index;
            ic->inputPanel().candidateList()->candidate(index).select(ic);
            return keyEvent.filterAndAccept();
        }
    }

    if (keyEvent.key().check(FcitxKey_space)) {
        chewing_handle_Space(ctx);
    } else if (keyEvent.key().check(FcitxKey_Tab)) {
        chewing_handle_Tab(ctx);
    } else if (keyEvent.key().isSimple()) {
        int scan_code = keyEvent.key().sym() & 0xff;
        chewing_handle_Default(ctx, scan_code);
    } else if (keyEvent.key().check(FcitxKey_BackSpace)) {
        const char *zuin_str = chewing_bopomofo_String_static(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0]) {
            return;
        }
        chewing_handle_Backspace(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0]) {
            keyEvent.filterAndAccept();
            return reset(entry, keyEvent);
        }
    } else if (keyEvent.key().check(FcitxKey_Escape)) {
        chewing_handle_Esc(ctx);
    } else if (keyEvent.key().check(FcitxKey_Delete)) {
        const char *zuin_str = chewing_bopomofo_String_static(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0]) {
            return;
        }
        chewing_handle_Del(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0]) {
            keyEvent.filterAndAccept();
            return reset(entry, keyEvent);
        }
    } else if (keyEvent.key().check(FcitxKey_Up)) {
        chewing_handle_Up(ctx);
    } else if (keyEvent.key().check(FcitxKey_Down)) {
        chewing_handle_Down(ctx);
    } else if (keyEvent.key().check(FcitxKey_Page_Down)) {
        chewing_handle_PageDown(ctx);
    } else if (keyEvent.key().check(FcitxKey_Page_Up)) {
        chewing_handle_PageUp(ctx);
    } else if (keyEvent.key().check(FcitxKey_Right)) {
        chewing_handle_Right(ctx);
    } else if (keyEvent.key().check(FcitxKey_Left)) {
        chewing_handle_Left(ctx);
    } else if (keyEvent.key().check(FcitxKey_Home)) {
        chewing_handle_Home(ctx);
    } else if (keyEvent.key().check(FcitxKey_End)) {
        chewing_handle_End(ctx);
    } else if (keyEvent.key().check(FcitxKey_space, KeyState::Shift)) {
        chewing_handle_ShiftSpace(ctx);
    } else if (keyEvent.key().check(FcitxKey_Left, KeyState::Shift)) {
        chewing_handle_ShiftLeft(ctx);
    } else if (keyEvent.key().check(FcitxKey_Right, KeyState::Shift)) {
        chewing_handle_ShiftRight(ctx);
    } else if (keyEvent.key().check(FcitxKey_Return)) {
        chewing_handle_Enter(ctx);
    } else if (keyEvent.key().states() == KeyState::Ctrl &&
               Key(keyEvent.key().sym()).isDigit()) {
        chewing_handle_CtrlNum(ctx, keyEvent.key().sym());
    } else {
        // to do: more chewing_handle
        return;
    }

    if (chewing_keystroke_CheckAbsorb(ctx)) {
        keyEvent.filterAndAccept();
        return updateUI(ic);
    } else if (chewing_keystroke_CheckIgnore(ctx)) {
        return;
    } else if (chewing_commit_Check(ctx)) {
        keyEvent.filterAndAccept();
        UniqueCPtr<char, chewing_free> str(chewing_commit_String(ctx));
        ic->commitString(str.get());
        return updateUI(ic);
    } else {
        keyEvent.filterAndAccept();
        return updateUI(ic);
    }
}

void ChewingEngine::filterKey(const InputMethodEntry &, KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }
    auto ic = keyEvent.inputContext();
    if (ic->inputPanel().candidateList() &&
        (keyEvent.key().isSimple() || keyEvent.key().isCursorMove() ||
         keyEvent.key().check(FcitxKey_space, KeyState::Shift) ||
         keyEvent.key().check(FcitxKey_Tab) ||
         keyEvent.key().check(FcitxKey_Return, KeyState::Shift))) {
        return keyEvent.filterAndAccept();
    }

    if (!ic->inputPanel().candidateList()) {
        FCITX_INFO() << "Flush";
        // Check if this key will produce something, if so, flush
        if (!keyEvent.key().hasModifier() &&
            Key::keySymToUnicode(keyEvent.key().sym())) {
            flushBuffer(keyEvent);
        }
    }
}

void ChewingEngine::updateUI(InputContext *ic) {
    CHEWING_DEBUG() << "updateUI";
    ChewingContext *ctx = context_.get();

    int selkey[10];
    int i = 0;
    for (i = 0; i < 10; i++) {
        selkey[i] =
            builtin_selectkeys[static_cast<int>(*config_.SelectionKey)][i];
    }

    chewing_set_selKey(ctx, selkey, 10);
    chewing_set_candPerPage(ctx, instance_->globalConfig().defaultPageSize());
    populateConfig();

    // clean up window asap
    ic->inputPanel().reset();
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);

    UniqueCPtr<char, chewing_free> buf_str(chewing_buffer_String(ctx));
    const char *zuin_str = chewing_bopomofo_String_static(ctx);

    std::string text = buf_str.get();
    std::string zuin = zuin_str;
    CHEWING_DEBUG() << "Text: " << text << " Zuin: " << zuin;
    /* if not check done, so there is candidate word */
    if (!chewing_cand_CheckDone(ctx)) {
        ic->inputPanel().setCandidateList(
            std::make_unique<ChewingCandidateList>(this, ic));
        if (!ic->inputPanel().candidateList()->size()) {
            ic->inputPanel().setCandidateList(nullptr);
        }
    }

    /* there is nothing */
    if (zuin.empty() && text.empty() && !ic->inputPanel().candidateList()) {
        ic->updatePreedit();
        return;
    }

    auto len = utf8::lengthValidated(text);
    if (len == utf8::INVALID_LENGTH) {
        return;
    }
    Text preedit;

    int cur = chewing_cursor_Current(ctx);
    int rcur = text.size();
    if (cur >= 0 && static_cast<size_t>(cur) < len) {
        rcur = utf8::ncharByteLength(text.begin(), cur);
    }
    preedit.setCursor(rcur);

    // insert zuin in the middle
    preedit.append(text.substr(0, rcur), TextFormatFlag::Underline);
    preedit.append(zuin,
                   {TextFormatFlag::HighLight, TextFormatFlag::Underline});
    preedit.append(text.substr(rcur), TextFormatFlag::Underline);

    if (!ic->capabilityFlags().test(CapabilityFlag::Preedit)) {
        ic->inputPanel().setPreedit(preedit);
    }
    ic->inputPanel().setClientPreedit(preedit);
    ic->updatePreedit();
}

void ChewingEngine::flushBuffer(InputContextEvent &event) {
    auto ctx = context_.get();
    // This check is because we ask the client to do the focus out commit.
    if (event.type() != EventType::InputContextFocusOut) {
        chewing_handle_Enter(ctx);
        if (chewing_commit_Check(ctx)) {
            UniqueCPtr<char, chewing_free> str(chewing_commit_String(ctx));
            event.inputContext()->commitString(str.get());
        }
        UniqueCPtr<char, chewing_free> buf_str(chewing_buffer_String(ctx));
        const char *zuin_str = chewing_bopomofo_String_static(ctx);
        std::string text = buf_str.get();
        std::string zuin = zuin_str;
        text += zuin;
        if (!text.empty()) {
            event.inputContext()->commitString(text);
        }
    }
    doReset(event);
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::ChewingEngineFactory);
