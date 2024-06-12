/*
 * SPDX-FileCopyrightText: 2012~2012 Tai-Lin Chu <tailinchu@gmail.com>
 * SPDX-FileCopyrightText: 2012~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "eim.h"
#include <cstdarg>
#include <fcitx-utils/keysymgen.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <memory>
#include <utility>

FCITX_DEFINE_LOG_CATEGORY(chewing_log, "chewing");
#define CHEWING_DEBUG() FCITX_LOGC(chewing_log, Debug)

namespace fcitx {

namespace {

constexpr int CHEWING_MAX_LEN = 18;

const char *builtin_selectkeys[] = {
    "1234567890", "asdfghjkl;", "asdfzxcv89", "asdfjkl789",
    "aoeuhtn789", "1234qweras", "dstnaeo789",
};

static_assert(FCITX_ARRAY_SIZE(builtin_selectkeys) ==
                  ChewingSelectionKeyI18NAnnotation::enumLength,
              "Enum mismatch");

#define DEFINE_SAFE_CHEWING_STRING_GETTER(NAME)                                \
    static inline std::string safeChewing_##NAME##_String(                     \
        ChewingContext *ctx) {                                                 \
        if (chewing_##NAME##_Check(ctx)) {                                     \
            return chewing_##NAME##_String_static(ctx);                        \
        }                                                                      \
        return "";                                                             \
    }

DEFINE_SAFE_CHEWING_STRING_GETTER(aux);
DEFINE_SAFE_CHEWING_STRING_GETTER(buffer);
DEFINE_SAFE_CHEWING_STRING_GETTER(bopomofo);
DEFINE_SAFE_CHEWING_STRING_GETTER(commit);

class ChewingCandidateWord : public CandidateWord {
public:
    ChewingCandidateWord(ChewingEngine *engine, std::string str, int index)
        : CandidateWord(Text(std::move(str))), engine_(engine), index_(index) {}

    void select(InputContext *inputContext) const override {
        auto *ctx = engine_->context();
        auto pageSize = chewing_get_candPerPage(ctx);
        int page = index_ / pageSize + chewing_cand_CurrentPage(ctx);
        int off = index_ % pageSize;
        if (page < 0 || page >= chewing_cand_TotalPage(ctx)) {
            return;
        }
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

        if (chewing_keystroke_CheckIgnore(ctx)) {
            return;
        }

        if (chewing_commit_Check(ctx)) {
            inputContext->commitString(safeChewing_commit_String(ctx));
        }
        engine_->updateUI(inputContext);
    }

private:
    ChewingEngine *engine_;
    int index_;
};

class ChewingCandidateList : public CandidateList,
                             public PageableCandidateList,
                             public CursorMovableCandidateList,
                             public CursorModifiableCandidateList {
public:
    ChewingCandidateList(ChewingEngine *engine, InputContext *ic)
        : engine_(engine), ic_(ic) {
        setPageable(this);
        setCursorMovable(this);
        setCursorModifiable(this);

        fillCandidate();
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

    void fillCandidate() {
        auto *ctx = engine_->context();
        candidateWords_.clear();
        labels_.clear();
        cursor_ = 0;

        int index = 0;
        // get candidate word
        int pageSize = chewing_cand_ChoicePerPage(ctx);
        if (pageSize <= 0) {
            return;
        }
        chewing_cand_Enumerate(ctx);
        while (chewing_cand_hasNext(ctx) && index < pageSize) {
            candidateWords_.emplace_back(std::make_unique<ChewingCandidateWord>(
                engine_, chewing_cand_String_static(ctx), index));
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
    }

    int size() const override { return candidateWords_.size(); }
    int cursorIndex() const override {
        if (empty() || !*engine_->config().selectCandidateWithArrowKey) {
            return -1;
        }
        return cursor_;
    }
    CandidateLayoutHint layoutHint() const override {
        switch (*engine_->config().CandidateLayout) {
        case ChewingCandidateLayout::Horizontal:
            return CandidateLayoutHint::Horizontal;
        case ChewingCandidateLayout::Vertical:
            return CandidateLayoutHint::Vertical;
        }
        return CandidateLayoutHint::Horizontal;
    }

    // Need for paging, allow rotating pages.
    bool hasPrev() const override { return true; }
    bool hasNext() const override { return true; }
    void prev() override { paging(true); }
    void next() override { paging(false); }

    bool usedNextBefore() const override { return true; }

    void prevCandidate() override {
        if (cursor_ == 0) {
            prev();
            // We do not reset cursor to the last on purpose.
            // This way it could be easily to change the range of current word.
        } else {
            cursor_ -= 1;
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

    void nextCandidate() override {
        if (cursor_ + 1 == size()) {
            next();
            cursor_ = 0;
        } else {
            cursor_ += 1;
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

    void setCursorIndex(int cursor) override {
        if (cursor < 0 || cursor >= size()) {
            return;
        }
        cursor_ = cursor;
    }

private:
    void paging(bool prev) {
        if (candidateWords_.empty()) {
            return;
        }

        auto *ctx = engine_->context();
        const int currentPage = chewing_cand_CurrentPage(ctx);
        if (prev) {
            const int hasNext = chewing_cand_list_has_next(ctx);
            const int hasPrev = chewing_cand_list_has_prev(ctx);
            if ((currentPage == 0) && (hasNext == 1 || hasPrev == 1)) {
                chewing_handle_Down(ctx);
            } else {
                chewing_handle_PageUp(ctx);
            }
        } else {
            const int totalPage = chewing_cand_TotalPage(ctx);
            if (currentPage == totalPage - 1) {
                chewing_handle_Down(ctx);
            } else {
                chewing_handle_PageDown(ctx);
            }
        }

        if (chewing_keystroke_CheckAbsorb(ctx)) {
            fillCandidate();
            engine_->updatePreedit(ic_);
            ic_->updatePreedit();
            ic_->updateUserInterface(UserInterfaceComponent::InputPanel);
        }
    }

    ChewingEngine *engine_;
    InputContext *ic_;
    std::vector<std::unique_ptr<ChewingCandidateWord>> candidateWords_;
    std::vector<Text> labels_;
    int cursor_ = 0;
};

void logger(void * /*context*/, int /*level*/, const char *fmt, ...) {
    if (!chewing_log().checkLogLevel(Debug)) {
        return;
    }
    std::va_list argp;
    va_start(argp, fmt);
    char onechar[1];
    int len = std::vsnprintf(onechar, 1, fmt, argp);
    va_end(argp);
    if (len < 1) {
        return;
    }
    std::vector<char> buf;
    buf.resize(len + 1);
    buf.back() = 0;
    va_start(argp, fmt);
    std::vsnprintf(buf.data(), len, fmt, argp);
    va_end(argp);
    CHEWING_DEBUG() << buf.data();
}

} // namespace

ChewingContext *getChewingContext() {
    const auto &sp = StandardPath::global();
    std::string dictData =
        sp.locate(StandardPath::Type::Data, "libchewing/dictionary.dat");
    if (!dictData.empty()) {
        std::string sysPath = fs::dirName(dictData);
        return chewing_new2(sysPath.c_str(), nullptr, nullptr, nullptr);
    }
    return chewing_new();
}

ChewingEngine::ChewingEngine(Instance *instance)
    : instance_(instance), context_(getChewingContext()) {
    chewing_set_maxChiSymbolLen(context_.get(), CHEWING_MAX_LEN);
    chewing_set_logger(context_.get(), logger, nullptr);
    reloadConfig();
}

ChewingEngine::~ChewingEngine() = default;

void ChewingEngine::reloadConfig() {
    readAsIni(config_, "conf/chewing.conf");
    populateConfig();
}

void ChewingEngine::populateConfig() {
    ChewingContext *ctx = context_.get();

    CHEWING_DEBUG() << "Set layout to: "
                    << builtin_keymaps[static_cast<int>(*config_.Layout)];
    chewing_set_KBType(
        ctx,
        chewing_KBStr2Num(builtin_keymaps[static_cast<int>(*config_.Layout)]));

    chewing_set_ChiEngMode(ctx, CHINESE_MODE);

    int selkey[10];
    for (size_t i = 0; i < 10; i++) {
        selkey[i] = static_cast<unsigned char>(
            builtin_selectkeys[static_cast<int>(*config_.SelectionKey)][i]);
    }

    chewing_set_selKey(ctx, selkey, 10);
    chewing_set_candPerPage(ctx, *config_.PageSize);
    chewing_set_addPhraseDirection(ctx, *config_.AddPhraseForward ? 0 : 1);
    chewing_set_phraseChoiceRearward(ctx, *config_.ChoiceBackward ? 1 : 0);
    chewing_set_autoShiftCur(ctx, *config_.AutoShiftCursor ? 1 : 0);
    chewing_set_spaceAsSelection(ctx, *config_.SpaceAsSelection ? 1 : 0);
    chewing_set_escCleanAllBuf(ctx, 1);
}

void ChewingEngine::reset(const InputMethodEntry & /*entry*/,
                          InputContextEvent &event) {
    doReset(event);
}

void ChewingEngine::doReset(InputContextEvent &event) {
    ChewingContext *ctx = context_.get();
    chewing_cand_close(ctx);
    chewing_clean_preedit_buf(ctx);
    chewing_clean_bopomofo_buf(ctx);
    updateUI(event.inputContext());
}

void ChewingEngine::save() {}

void ChewingEngine::activate(const InputMethodEntry & /*entry*/,
                             InputContextEvent &event) {
    // Request chttrans.
    // Fullwidth is not required for chewing.
    chttrans();
    auto *inputContext = event.inputContext();
    if (auto *action =
            instance_->userInterfaceManager().lookupAction("chttrans")) {
        inputContext->statusArea().addAction(StatusGroup::InputMethod, action);
    }
    auto *ic = event.inputContext();
    if (!ic_.isNull() && ic_.get() != ic) {
        doReset(event);
    }
    ic_ = ic->watch();
}

void ChewingEngine::deactivate(const InputMethodEntry &entry,
                               InputContextEvent &event) {
    if (event.type() == EventType::InputContextSwitchInputMethod) {
        flushBuffer(event);
    } else {
        reset(entry, event);
    }
}

bool ChewingEngine::handleCandidateKeyEvent(const KeyEvent &keyEvent) {
    auto *ic = keyEvent.inputContext();
    auto candidateList = std::dynamic_pointer_cast<ChewingCandidateList>(
        ic->inputPanel().candidateList());
    if (!candidateList) {
        return false;
    }

    const KeyList keypadKeys{Key{FcitxKey_KP_1}, Key{FcitxKey_KP_2},
                             Key{FcitxKey_KP_3}, Key{FcitxKey_KP_4},
                             Key{FcitxKey_KP_5}, Key{FcitxKey_KP_6},
                             Key{FcitxKey_KP_7}, Key{FcitxKey_KP_8},
                             Key{FcitxKey_KP_9}, Key{FcitxKey_KP_0}};
    if (*config_.UseKeypadAsSelectionKey) {
        if (int index = keyEvent.key().keyListIndex(keypadKeys);
            index >= 0 && index < candidateList->size()) {
            candidateList->candidate(index).select(ic);
            return true;
        }
    }

    if (!*config_.selectCandidateWithArrowKey) {
        return false;
    }

    if (keyEvent.key().check(FcitxKey_Right)) {
        if (*config_.CandidateLayout == ChewingCandidateLayout::Horizontal) {
            candidateList->nextCandidate();
        } else {
            candidateList->next();
        }
        return true;
    }
    if (keyEvent.key().check(FcitxKey_Left)) {
        if (*config_.CandidateLayout == ChewingCandidateLayout::Horizontal) {
            candidateList->prevCandidate();
        } else {
            candidateList->prev();
        }
        return true;
    }
    if (keyEvent.key().check(FcitxKey_Up)) {
        if (*config_.CandidateLayout == ChewingCandidateLayout::Horizontal) {
            candidateList->prev();
        } else {
            candidateList->prevCandidate();
        }
        return true;
    }
    if (keyEvent.key().check(FcitxKey_Down)) {
        if (*config_.CandidateLayout == ChewingCandidateLayout::Horizontal) {
            candidateList->next();
        } else {
            candidateList->nextCandidate();
        }
        return true;
    }
    if (keyEvent.key().check(FcitxKey_Return)) {
        if (int index = candidateList->cursorIndex();
            index >= 0 && index < candidateList->size()) {
            candidateList->candidate(index).select(ic);
        }
        return true;
    }
    if (keyEvent.key().check(FcitxKey_space)) {
        candidateList->next();
        return true;
    }
    return false;
}

void ChewingEngine::keyEvent(const InputMethodEntry &entry,
                             KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }
    auto *ctx = context_.get();
    auto *ic = keyEvent.inputContext();

    chewing_set_easySymbolInput(ctx, 0);
    CHEWING_DEBUG() << "KeyEvent: " << keyEvent.key().toString();

    if (handleCandidateKeyEvent(keyEvent)) {
        return keyEvent.filterAndAccept();
    }

    if (keyEvent.key().check(FcitxKey_space)) {
        chewing_handle_Space(ctx);
    } else if (keyEvent.key().check(FcitxKey_Tab)) {
        chewing_handle_Tab(ctx);
    } else if (keyEvent.key().isSimple()) {
        if (keyEvent.rawKey().states().test(KeyState::Shift)) {
            chewing_set_easySymbolInput(ctx, *config_.EasySymbolInput ? 1 : 0);
        }
        int scan_code = keyEvent.key().sym() & 0xff;
        if (*config_.Layout == ChewingLayout::HanYuPinYin) {
            auto zuin = safeChewing_bopomofo_String(ctx);
            // Workaround a bug in libchewing fixed in 2017 but never has
            // stable release.
            if (zuin.size() >= 9) {
                return keyEvent.filterAndAccept();
            }
        }
        chewing_handle_Default(ctx, scan_code);
        chewing_set_easySymbolInput(ctx, 0);
    } else if (keyEvent.key().check(FcitxKey_BackSpace)) {
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            return;
        }
        chewing_handle_Backspace(ctx);
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            keyEvent.filterAndAccept();
            return reset(entry, keyEvent);
        }
    } else if (keyEvent.key().check(FcitxKey_Escape)) {
        chewing_handle_Esc(ctx);
    } else if (keyEvent.key().check(FcitxKey_Delete)) {
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            return;
        }
        chewing_handle_Del(ctx);
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
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

    if (chewing_keystroke_CheckIgnore(ctx)) {
        return;
    }
    if (chewing_keystroke_CheckAbsorb(ctx)) {
        keyEvent.filterAndAccept();
    }
    if (chewing_commit_Check(ctx)) {
        keyEvent.filterAndAccept();
        ic->commitString(safeChewing_commit_String(ctx));
    }
    return updateUI(ic);
}

void ChewingEngine::filterKey(const InputMethodEntry & /*entry*/,
                              KeyEvent &keyEvent) {
    if (keyEvent.isRelease()) {
        return;
    }
    auto *ic = keyEvent.inputContext();
    if (ic->inputPanel().candidateList() &&
        (keyEvent.key().isSimple() || keyEvent.key().isCursorMove() ||
         keyEvent.key().check(FcitxKey_space, KeyState::Shift) ||
         keyEvent.key().check(FcitxKey_Tab) ||
         keyEvent.key().check(FcitxKey_Return, KeyState::Shift))) {
        return keyEvent.filterAndAccept();
    }

    if (!ic->inputPanel().candidateList()) {
        // Check if this key will produce something, if so, flush
        if (!keyEvent.key().hasModifier() &&
            Key::keySymToUnicode(keyEvent.key().sym())) {
            flushBuffer(keyEvent);
        }
    }
}

void ChewingEngine::updatePreeditImpl(InputContext *ic) {
    ic->inputPanel().setClientPreedit(Text());
    ic->inputPanel().setPreedit(Text());
    ic->inputPanel().setAuxDown(Text());

    ChewingContext *ctx = context_.get();
    std::string buffer = safeChewing_buffer_String(ctx);
    std::string_view text = buffer;
    std::string zuin = safeChewing_bopomofo_String(ctx);

    CHEWING_DEBUG() << "Text: " << text << " Zuin: " << zuin;

    /* there is nothing */
    if (zuin.empty() && text.empty()) {
        return;
    }

    auto len = utf8::lengthValidated(text);
    if (len == utf8::INVALID_LENGTH) {
        return;
    }
    const auto useClientPreedit =
        ic->capabilityFlags().test(CapabilityFlag::Preedit);
    const auto format =
        useClientPreedit ? TextFormatFlag::Underline : TextFormatFlag::NoFlag;
    Text preedit;

    int cur = chewing_cursor_Current(ctx);
    int rcur = text.size();
    if (cur >= 0 && static_cast<size_t>(cur) < len) {
        rcur = utf8::ncharByteLength(text.begin(), cur);
    }
    preedit.setCursor(rcur);

    // insert zuin in the middle
    preedit.append(std::string(text.substr(0, rcur)), format);
    preedit.append(std::move(zuin), {TextFormatFlag::HighLight, format});
    preedit.append(std::string(text.substr(rcur)), format);

    if (auto aux = safeChewing_aux_String(ctx); !aux.empty()) {
        ic->inputPanel().setAuxDown(Text(std::move(aux)));
    }

    if (useClientPreedit) {
        ic->inputPanel().setClientPreedit(preedit);
    } else {
        ic->inputPanel().setPreedit(preedit);
    }
}

void ChewingEngine::updatePreedit(InputContext *ic) {
    updatePreeditImpl(ic);
    ic->updatePreedit();
}

void ChewingEngine::updateUI(InputContext *ic) {
    CHEWING_DEBUG() << "updateUI";
    // clean up window asap
    ic->inputPanel().reset();
    ic->inputPanel().setCandidateList(
        std::make_unique<ChewingCandidateList>(this, ic));
    if (ic->inputPanel().candidateList()->empty()) {
        ic->inputPanel().setCandidateList(nullptr);
    }

    updatePreedit(ic);
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void ChewingEngine::flushBuffer(InputContextEvent &event) {
    auto *ctx = context_.get();
    std::string text;
    if (*config_.switchInputMethodBehavior ==
            SwitchInputMethodBehavior::CommitPreedit ||
        *config_.switchInputMethodBehavior ==
            SwitchInputMethodBehavior::CommitDefault) {
        chewing_cand_close(ctx);
        if (chewing_buffer_Check(ctx)) {
            // When not success, chewing_commit_preedit_buf will not change the
            // output value. while chewing_handle_* will always update button
            // result.
            if (chewing_commit_preedit_buf(ctx) == 0) {
                text.append(safeChewing_commit_String(ctx));
            }
        }
    }

    if (*config_.switchInputMethodBehavior ==
        SwitchInputMethodBehavior::CommitPreedit) {
        text.append(safeChewing_buffer_String(ctx));
        text.append(safeChewing_bopomofo_String(ctx));
    }
    if (!text.empty()) {
        event.inputContext()->commitString(text);
    }
    doReset(event);
}

} // namespace fcitx

FCITX_ADDON_FACTORY(fcitx::ChewingEngineFactory);
