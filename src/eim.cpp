/*
 * SPDX-FileCopyrightText: 2012~2012 Tai-Lin Chu <tailinchu@gmail.com>
 * SPDX-FileCopyrightText: 2012~2017 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */
#include "eim.h"
#include <algorithm>
#include <array>
#include <chewing.h>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fcitx-config/iniparser.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpaths.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/text.h>
#include <fcitx/userinterface.h>
#include <fcitx/userinterfacemanager.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

FCITX_DEFINE_LOG_CATEGORY(chewing_log, "chewing");
#define CHEWING_DEBUG() FCITX_LOGC(chewing_log, Debug)

namespace fcitx {

namespace {

constexpr int CHEWING_MAX_LEN = 18;

constexpr auto builtin_selectkeys = std::to_array<std::string_view>({
    "1234567890",
    "asdfghjkl;",
    "asdfzxcv89",
    "asdfjkl789",
    "aoeuhtn789",
    "1234qweras",
    "dstnaeo789",
});

static_assert(builtin_selectkeys.size() ==
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
        int page = (index_ / pageSize) + chewing_cand_CurrentPage(ctx);
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

class SmartSymbolCandidateWord : public CandidateWord {
public:
    SmartSymbolCandidateWord(ChewingEngine *engine, std::string symbol)
        : CandidateWord(Text(symbol)), engine_(engine),
          symbol_(std::move(symbol)) {}

    void select(InputContext *inputContext) const override {
        engine_->commitCurrentPreedit(inputContext);
        inputContext->commitString(symbol_);
        inputContext->inputPanel().reset();
        inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
    }

private:
    ChewingEngine *engine_;
    std::string symbol_;
};

class SmartSymbolCandidateList : public CandidateList,
                                 public CursorMovableCandidateList,
                                 public CursorModifiableCandidateList {
public:
    explicit SmartSymbolCandidateList(ChewingEngine *engine)
        : engine_(engine) {
        setCursorMovable(this);
        setCursorModifiable(this);
        constexpr auto symbols = std::to_array<std::string_view>({
            "，", "。", "、", "；", "：", "？", "！", "（", "）", "「",
            "」", "『", "』", "…", "—", "～", "《", "》", "〈", "〉",
        });
        for (size_t i = 0; i < symbols.size(); i++) {
            candidateWords_.emplace_back(
                std::make_unique<SmartSymbolCandidateWord>(
                    engine_, std::string(symbols[i])));
            if (i < 10) {
                labels_.emplace_back(std::to_string((i + 1) % 10) + ".");
            } else {
                labels_.emplace_back();
            }
        }
    }

    const Text &label(int idx) const override { return labels_[idx]; }
    const CandidateWord &candidate(int idx) const override {
        return *candidateWords_[idx];
    }
    int size() const override { return candidateWords_.size(); }
    int cursorIndex() const override { return cursor_; }
    CandidateLayoutHint layoutHint() const override {
        return CandidateLayoutHint::Horizontal;
    }
    void prevCandidate() override {
        cursor_ = cursor_ == 0 ? size() - 1 : cursor_ - 1;
    }
    void nextCandidate() override { cursor_ = (cursor_ + 1) % size(); }
    void setCursorIndex(int cursor) override {
        if (cursor >= 0 && cursor < size()) {
            cursor_ = cursor;
        }
    }

private:
    ChewingEngine *engine_;
    std::vector<std::unique_ptr<SmartSymbolCandidateWord>> candidateWords_;
    std::vector<Text> labels_;
    int cursor_ = 0;
};

bool isSmartSymbolHotkey(const Key &key) {
    return key.check(FcitxKey_grave) || key.check(FcitxKey_asciitilde);
}

bool isInputModeHotkey(const Key &key) {
    static const KeyList hotkeys{Key("Control+Shift+space"),
                                 Key("Control+Alt+space"),
                                 Key("Control+grave")};
    for (const auto &hotkey : hotkeys) {
        if (key.check(hotkey)) {
            return true;
        }
    }
    return false;
}

bool isPrintableKey(const Key &key) {
    return !key.hasModifier() && Key::keySymToUnicode(key.sym());
}

bool isAsciiAlphaKey(const Key &key) {
    if (key.hasModifier()) {
        return false;
    }
    const auto sym = key.sym();
    return (sym >= 'a' && sym <= 'z') || (sym >= 'A' && sym <= 'Z');
}

std::string asciiLower(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const auto c : text) {
        result.push_back(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

bool containsEnglishVowel(std::string_view text) {
    for (const auto c : text) {
        switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'a':
        case 'e':
        case 'i':
        case 'o':
        case 'u':
            return true;
        }
    }
    return false;
}

class SmartEnglishClassifier {
public:
    SmartEnglishClassifier() { load(); }

    bool likelyWord(std::string_view token, bool final) const {
        if (token.size() < 2 || token.size() > 32) {
            return false;
        }
        const auto lower = asciiLower(token);
        if (words_.contains(lower)) {
            return true;
        }
        if (!final && token.size() < 5) {
            return false;
        }

        int score = 0;
        if (prefixes_.contains(lower)) {
            score += final ? 2 : 4;
        }
        if (containsEnglishVowel(lower)) {
            score += 2;
        } else if (lower.size() >= 3) {
            score -= 4;
        }

        static constexpr auto strongNgrams = std::to_array<std::string_view>({
            "ing", "ion", "tion", "ght", "sch", "str", "ent", "est",
            "ous", "ive", "able", "ment", "ware", "load", "code",
        });
        static constexpr auto ngrams = std::to_array<std::string_view>({
            "al", "an", "ar", "as", "at", "ch", "ck", "co", "de", "ed",
            "el", "en", "er", "es", "et", "fi", "gh", "go", "gr", "he",
            "ht", "ic", "in", "io", "is", "it", "le", "ll", "lo", "ng",
            "oo", "op", "or", "ou", "ow", "re", "sh", "sp", "st", "te",
            "th", "ti", "to", "tt", "us", "wi", "wo",
        });
        for (const auto ngram : strongNgrams) {
            if (lower.find(ngram) != std::string::npos) {
                score += 3;
            }
        }
        for (const auto ngram : ngrams) {
            if (lower.find(ngram) != std::string::npos) {
                score += 1;
            }
        }

        if (lower.ends_with("ed") || lower.ends_with("er") ||
            lower.ends_with("es") || lower.ends_with("ly") ||
            lower.ends_with("s")) {
            score += 1;
        }
        if (lower.size() >= 4) {
            score += 1;
        }
        if (lower.size() >= 6) {
            score += 1;
        }

        const int consonantRun = longestConsonantRun(lower);
        if (consonantRun >= 4) {
            score -= 3;
        } else if (consonantRun >= 3) {
            score -= 1;
        }

        return score >= (final ? 5 : 8);
    }

private:
    void addWord(std::string_view word) {
        if (word.empty() || word.starts_with('#')) {
            return;
        }
        const auto lower = asciiLower(word);
        words_.insert(lower);
        for (size_t len = 2; len < lower.size(); len++) {
            prefixes_.insert(lower.substr(0, len));
        }
    }

    void loadFile(const std::filesystem::path &path) {
        std::ifstream file(path);
        if (!file) {
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            auto comment = line.find('#');
            if (comment != std::string::npos) {
                line.resize(comment);
            }
            auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                continue;
            }
            auto last = line.find_last_not_of(" \t\r\n");
            line = line.substr(first, last - first + 1);
            auto space = line.find_first_of(" \t");
            if (space != std::string::npos) {
                line.resize(space);
            }
            addWord(line);
        }
    }

    void load() {
        static constexpr auto builtinWords = std::to_array<std::string_view>({
            "about", "access", "account", "ai", "api", "app", "arch",
            "asus", "audio", "blog", "bot", "browser", "bug", "build",
            "cache", "chat", "chatgpt", "chrome", "code", "config", "cpu",
            "data", "debug", "desktop", "dev", "device", "display",
            "docker", "download", "drive", "email", "error", "fcitx",
            "file", "fix", "github", "git", "gnome", "good", "google",
            "gpt", "gpu", "hello", "help", "hotkey", "http", "https",
            "hyprland", "icon", "input", "install", "intel", "issue",
            "json", "key", "keyboard", "linux", "local", "login", "menu",
            "mode", "model", "monitor", "network", "nvidia", "openai",
            "package", "password", "pkg", "plugin", "proxy", "python",
            "reload", "restart", "script", "server", "shell", "space",
            "status", "super", "system", "terminal", "test", "this",
            "tool", "type", "update", "usb", "user", "version", "video",
            "wayland", "wifi", "window", "windows", "word", "work",
            "workspace", "world", "zoom",
        });
        for (const auto word : builtinWords) {
            addWord(word);
        }

        const auto &sp = StandardPaths::global();
        for (const auto &path : sp.directories(StandardPathsType::Data)) {
            loadFile(path / "fcitx5/chewing/smart-english-words.txt");
            loadFile(path / "fcitx5/chewing/smart-english-proper-nouns.txt");
        }
        const auto userDir = sp.userDirectory(StandardPathsType::Data);
        if (!userDir.empty()) {
            loadFile(userDir / "fcitx5/chewing/smart-english-user.txt");
        }
    }

    static int longestConsonantRun(std::string_view lower) {
        int longest = 0;
        int current = 0;
        for (const auto c : lower) {
            const bool vowel = c == 'a' || c == 'e' || c == 'i' ||
                               c == 'o' || c == 'u';
            if (vowel) {
                current = 0;
            } else {
                longest = std::max(longest, ++current);
            }
        }
        return longest;
    }

    std::unordered_set<std::string> words_;
    std::unordered_set<std::string> prefixes_;
};

const SmartEnglishClassifier &smartEnglishClassifier() {
    static const SmartEnglishClassifier classifier;
    return classifier;
}

bool smartEnglishLikelyWord(std::string_view token, bool final) {
    return smartEnglishClassifier().likelyWord(token, final);
}

bool isCtrlPrintableSymbol(const Key &key) {
    if (key.states() != KeyState::Ctrl) {
        return false;
    }
    const auto sym = key.sym();
    return sym >= 0x21 && sym <= 0x7e && !Key(sym).isDigit() &&
           !Key(sym).isLAZ() && !Key(sym).isUAZ();
}

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

std::string inputModeText(ChewingInputMode mode) {
    switch (mode) {
    case ChewingInputMode::Smart:
        return _("智");
    case ChewingInputMode::ForceChinese:
        return _("中");
    case ChewingInputMode::ForceEnglish:
        return _("英");
    }
    return _("智");
}

std::string inputModeIcon(ChewingInputMode mode) {
    switch (mode) {
    case ChewingInputMode::Smart:
        return "fcitx-chewing-smart";
    case ChewingInputMode::ForceChinese:
        return "fcitx-chewing-chinese";
    case ChewingInputMode::ForceEnglish:
        return "fcitx-chewing-english";
    }
    return "fcitx-chewing";
}

class ChewingInputModeAction : public Action {
public:
    explicit ChewingInputModeAction(ChewingEngine *engine) : engine_(engine) {
        setHotkey(KeyList{Key("Control+Shift+space"),
                          Key("Control+Alt+space"), Key("Control+grave")});
    }

    std::string shortText(InputContext *) const override {
        return inputModeText(engine_->inputMode());
    }

    std::string icon(InputContext *) const override {
        return inputModeIcon(engine_->inputMode());
    }

    std::string longText(InputContext *) const override {
        return _("Chewing input mode: Smart / Chinese / English");
    }

    void activate(InputContext *ic) override { engine_->toggleInputMode(ic); }

private:
    ChewingEngine *engine_;
};

class ChewingSetInputModeAction : public Action {
public:
    ChewingSetInputModeAction(ChewingEngine *engine, ChewingInputMode mode)
        : engine_(engine), mode_(mode) {
        setCheckable(true);
    }

    std::string shortText(InputContext *) const override {
        switch (mode_) {
        case ChewingInputMode::Smart:
            return _("Smart input mode");
        case ChewingInputMode::ForceChinese:
            return _("Chinese mode");
        case ChewingInputMode::ForceEnglish:
            return _("English mode");
        }
        return {};
    }

    std::string icon(InputContext *) const override { return inputModeIcon(mode_); }

    bool isChecked(InputContext *) const override {
        return engine_->inputMode() == mode_;
    }

    void activate(InputContext *ic) override { engine_->setInputMode(ic, mode_); }

private:
    ChewingEngine *engine_;
    ChewingInputMode mode_;
};

ChewingContext *getChewingContext() {
    const auto &sp = StandardPaths::global();
    std::filesystem::path dictData =
        sp.locate(StandardPathsType::Data, "libchewing/tsi.dat");
    if (!dictData.empty()) {
        std::string sysPath = dictData.parent_path().string();
        return chewing_new2(sysPath.c_str(), nullptr, nullptr, nullptr);
    }
    return chewing_new();
}

ChewingEngine::ChewingEngine(Instance *instance)
    : instance_(instance), context_(getChewingContext()),
      inputModeAction_(std::make_unique<ChewingInputModeAction>(this)),
      inputModeMenu_(std::make_unique<Menu>()) {
    inputModeAction_->registerAction("chewing-input-mode",
                                     &instance_->userInterfaceManager());
    inputModeAction_->setMenu(inputModeMenu_.get());
    int modeIndex = 0;
    for (auto mode : {ChewingInputMode::Smart, ChewingInputMode::ForceChinese,
                      ChewingInputMode::ForceEnglish}) {
        auto action = std::make_unique<ChewingSetInputModeAction>(this, mode);
        action->registerAction("chewing-set-input-mode-" +
                                   std::to_string(modeIndex++),
                               &instance_->userInterfaceManager());
        inputModeMenu_->addAction(action.get());
        inputModeMenuActions_.push_back(std::move(action));
    }
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
    inputMode_ = *config_.DefaultInputMode;

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
    smartEnglishPrefix_.clear();
    smartEnglishBuffer_.clear();
    smartEnglishPassthrough_ = false;
    ChewingContext *ctx = context_.get();
    chewing_cand_close(ctx);
    chewing_clean_preedit_buf(ctx);
    chewing_clean_bopomofo_buf(ctx);
    chewing_Reset(ctx);
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
    inputContext->statusArea().addAction(StatusGroup::BeforeInputMethod,
                                         inputModeAction_.get());
    updateInputModeAction(inputContext);
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

void ChewingEngine::updateInputModeAction(InputContext *ic) {
    if (inputModeAction_) {
        inputModeAction_->update(ic);
    }
    if (ic) {
        ic->updateUserInterface(UserInterfaceComponent::StatusArea);
    }
}

void ChewingEngine::setInputMode(InputContext *ic, ChewingInputMode mode) {
    if (mode == inputMode_) {
        return;
    }
    smartEnglishPrefix_.clear();
    smartEnglishBuffer_.clear();
    smartEnglishPassthrough_ = false;
    if (mode == ChewingInputMode::ForceEnglish) {
        commitCurrentPreedit(ic);
        chewing_set_ChiEngMode(context_.get(), SYMBOL_MODE);
    } else {
        chewing_set_ChiEngMode(context_.get(), CHINESE_MODE);
    }
    inputMode_ = mode;
    updateInputModeAction(ic);

}

void ChewingEngine::toggleInputMode(InputContext *ic) {
    switch (inputMode_) {
    case ChewingInputMode::Smart:
        setInputMode(ic, ChewingInputMode::ForceChinese);
        break;
    case ChewingInputMode::ForceChinese:
        setInputMode(ic, ChewingInputMode::ForceEnglish);
        break;
    case ChewingInputMode::ForceEnglish:
        setInputMode(ic, ChewingInputMode::Smart);
        break;
    }
}

bool ChewingEngine::handleInputModeHotkey(const KeyEvent &keyEvent) {
    if (!isInputModeHotkey(keyEvent.key())) {
        return false;
    }
    toggleInputMode(keyEvent.inputContext());
    return true;
}

std::string ChewingEngine::subMode(const InputMethodEntry & /*entry*/,
                                   InputContext & /*inputContext*/) {
    return inputModeText(inputMode_);
}

std::string ChewingEngine::subModeIconImpl(const InputMethodEntry & /*entry*/,
                                           InputContext & /*ic*/) {
    return inputModeIcon(inputMode_);
}

std::string ChewingEngine::subModeLabelImpl(const InputMethodEntry & /*entry*/,
                                            InputContext & /*ic*/) {
    return inputModeText(inputMode_);
}

void ChewingEngine::invokeActionImpl(const InputMethodEntry & /*entry*/,
                                     InvokeActionEvent &event) {
    if (event.action() == InvokeActionEvent::Action::LeftClick) {
        toggleInputMode(event.inputContext());
        event.filter();
    }
}

bool ChewingEngine::handleSmartSymbolCandidateKeyEvent(
    const KeyEvent &keyEvent) const {
    auto *ic = keyEvent.inputContext();
    auto candidateList = std::dynamic_pointer_cast<SmartSymbolCandidateList>(
        ic->inputPanel().candidateList());
    if (!candidateList) {
        return false;
    }

    const auto &key = keyEvent.key();
    int index = -1;
    if (key.check(FcitxKey_1)) {
        index = 0;
    } else if (key.check(FcitxKey_2)) {
        index = 1;
    } else if (key.check(FcitxKey_3)) {
        index = 2;
    } else if (key.check(FcitxKey_4)) {
        index = 3;
    } else if (key.check(FcitxKey_5)) {
        index = 4;
    } else if (key.check(FcitxKey_6)) {
        index = 5;
    } else if (key.check(FcitxKey_7)) {
        index = 6;
    } else if (key.check(FcitxKey_8)) {
        index = 7;
    } else if (key.check(FcitxKey_9)) {
        index = 8;
    } else if (key.check(FcitxKey_0)) {
        index = 9;
    }
    if (index >= 0 && index < candidateList->size()) {
        candidateList->candidate(index).select(ic);
        return true;
    }
    if (key.check(FcitxKey_Right) || key.check(FcitxKey_Down) ||
        key.check(FcitxKey_space) || key.check(FcitxKey_Tab)) {
        candidateList->nextCandidate();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }
    if (key.check(FcitxKey_Left) || key.check(FcitxKey_Up)) {
        candidateList->prevCandidate();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }
    if (key.check(FcitxKey_Return) || key.check(FcitxKey_KP_Enter)) {
        candidateList->candidate(candidateList->cursorIndex()).select(ic);
        return true;
    }
    if (key.check(FcitxKey_Escape)) {
        ic->inputPanel().reset();
        ic->updateUserInterface(UserInterfaceComponent::InputPanel);
        return true;
    }
    return false;
}

void ChewingEngine::showSmartSymbolCandidates(InputContext *ic) {
    ic->inputPanel().reset();
    ic->inputPanel().setAuxDown(Text(_("Smart Symbols")));
    ic->inputPanel().setCandidateList(
        std::make_unique<SmartSymbolCandidateList>(this));
    ic->updateUserInterface(UserInterfaceComponent::InputPanel);
}

bool ChewingEngine::handleCandidateKeyEvent(const KeyEvent &keyEvent) const {
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
    if ((keyEvent.key().check(FcitxKey_Return) ||
         keyEvent.key().check(FcitxKey_KP_Enter))) {
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

    if (handleInputModeHotkey(keyEvent) ||
        handleSmartSymbolCandidateKeyEvent(keyEvent) ||
        handleCandidateKeyEvent(keyEvent)) {
        keyEvent.filterAndAccept();
        return;
    }

    if (inputMode_ == ChewingInputMode::ForceEnglish &&
        isPrintableKey(keyEvent.key())) {
        commitCurrentPreedit(ic);
        return;
    }

    if (inputMode_ == ChewingInputMode::Smart && smartEnglishPassthrough_) {
        if (isAsciiAlphaKey(keyEvent.key())) {
            ic->commitString(std::string(1, static_cast<char>(keyEvent.key().sym())));
            keyEvent.filterAndAccept();
            return;
        }
        if (keyEvent.key().check(FcitxKey_space)) {
            ic->commitString(" ");
            smartEnglishPassthrough_ = false;
            keyEvent.filterAndAccept();
            return;
        }
        smartEnglishPassthrough_ = false;
    }

    if (inputMode_ == ChewingInputMode::Smart &&
        *config_.Layout != ChewingLayout::HanYuPinYin &&
        isAsciiAlphaKey(keyEvent.key()) &&
        (chewing_buffer_Check(ctx) || !smartEnglishBuffer_.empty())) {
        // Speculatively remember raw alphabetic keys after an existing Chinese
        // preedit.  libchewing will treat these keys as bopomofo, so checking
        // chewing_bopomofo_Check() would stop detection after the first letter.
        // Once we see a short alphabetic run, restore the original Chinese
        // preedit and commit the raw letters as English.
        if (smartEnglishBuffer_.empty()) {
            smartEnglishPrefix_ = safeChewing_buffer_String(ctx);
        }
        smartEnglishBuffer_.push_back(static_cast<char>(keyEvent.key().sym()));
        if (smartEnglishLikelyWord(smartEnglishBuffer_, false)) {
            chewing_cand_close(ctx);
            chewing_clean_preedit_buf(ctx);
            chewing_clean_bopomofo_buf(ctx);
            chewing_Reset(ctx);
            ic->commitString(smartEnglishPrefix_ + smartEnglishBuffer_);
            smartEnglishPrefix_.clear();
            smartEnglishBuffer_.clear();
            smartEnglishPassthrough_ = true;
            keyEvent.filterAndAccept();
            updateUI(ic);
            return;
        }
    } else if (!isAsciiAlphaKey(keyEvent.key())) {
        if (inputMode_ == ChewingInputMode::Smart && !smartEnglishBuffer_.empty() &&
            smartEnglishLikelyWord(smartEnglishBuffer_, true)) {
            chewing_cand_close(ctx);
            chewing_clean_preedit_buf(ctx);
            chewing_clean_bopomofo_buf(ctx);
            chewing_Reset(ctx);
            ic->commitString(smartEnglishPrefix_ + smartEnglishBuffer_);
            smartEnglishPrefix_.clear();
            smartEnglishBuffer_.clear();
            if (keyEvent.key().check(FcitxKey_space)) {
                ic->commitString(" ");
                keyEvent.filterAndAccept();
                updateUI(ic);
                return;
            }
            if (isPrintableKey(keyEvent.key())) {
                ic->commitString(std::string(
                    1, static_cast<char>(keyEvent.key().sym())));
                keyEvent.filterAndAccept();
                updateUI(ic);
                return;
            }
            updateUI(ic);
        }
        smartEnglishPrefix_.clear();
        smartEnglishBuffer_.clear();
    }

    if (*config_.AsusSmartSymbolInput && inputMode_ != ChewingInputMode::ForceEnglish &&
        isSmartSymbolHotkey(keyEvent.key())) {
        showSmartSymbolCandidates(ic);
        keyEvent.filterAndAccept();
        return;
    }

    if (*config_.AsusSmartSymbolInput && inputMode_ != ChewingInputMode::ForceEnglish &&
        isCtrlPrintableSymbol(keyEvent.key())) {
        commitCurrentPreedit(ic);
        ic->commitString(std::string(1, static_cast<char>(keyEvent.key().sym())));
        keyEvent.filterAndAccept();
        return;
    }

    int chewingReturnValue = 0;
    if (keyEvent.key().check(FcitxKey_space)) {
        chewingReturnValue = chewing_handle_Space(ctx);
    } else if (keyEvent.key().check(FcitxKey_Tab)) {
        chewingReturnValue = chewing_handle_Tab(ctx);
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
                keyEvent.filterAndAccept();
                return;
            }
        }
        chewingReturnValue = chewing_handle_Default(ctx, scan_code);
        chewing_set_easySymbolInput(ctx, 0);
    } else if (keyEvent.key().check(FcitxKey_BackSpace)) {
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            return;
        }
        chewingReturnValue = chewing_handle_Backspace(ctx);
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            keyEvent.filterAndAccept();
            reset(entry, keyEvent);
            return;
        }
    } else if (keyEvent.key().check(FcitxKey_Escape)) {
        chewingReturnValue = chewing_handle_Esc(ctx);
    } else if (keyEvent.key().check(FcitxKey_Delete)) {
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            return;
        }
        chewingReturnValue = chewing_handle_Del(ctx);
        if ((chewing_buffer_Check(ctx)) == 0 &&
            (chewing_bopomofo_Check(ctx) == 0)) {
            keyEvent.filterAndAccept();
            reset(entry, keyEvent);
            return;
        }
    } else if (keyEvent.key().check(FcitxKey_Up)) {
        chewingReturnValue = chewing_handle_Up(ctx);
    } else if (keyEvent.key().check(FcitxKey_Down)) {
        chewingReturnValue = chewing_handle_Down(ctx);
    } else if (keyEvent.key().check(FcitxKey_Page_Down)) {
        chewingReturnValue = chewing_handle_PageDown(ctx);
    } else if (keyEvent.key().check(FcitxKey_Page_Up)) {
        chewingReturnValue = chewing_handle_PageUp(ctx);
    } else if (keyEvent.key().check(FcitxKey_Right)) {
        chewingReturnValue = chewing_handle_Right(ctx);
    } else if (keyEvent.key().check(FcitxKey_Left)) {
        chewingReturnValue = chewing_handle_Left(ctx);
    } else if (keyEvent.key().check(FcitxKey_Home)) {
        chewingReturnValue = chewing_handle_Home(ctx);
    } else if (keyEvent.key().check(FcitxKey_End)) {
        chewingReturnValue = chewing_handle_End(ctx);
    } else if (keyEvent.key().check(FcitxKey_space, KeyState::Shift)) {
        chewingReturnValue = chewing_handle_ShiftSpace(ctx);
    } else if (keyEvent.key().check(FcitxKey_Left, KeyState::Shift)) {
        chewingReturnValue = chewing_handle_ShiftLeft(ctx);
    } else if (keyEvent.key().check(FcitxKey_Right, KeyState::Shift)) {
        chewingReturnValue = chewing_handle_ShiftRight(ctx);
    } else if ((keyEvent.key().check(FcitxKey_Return) ||
                keyEvent.key().check(FcitxKey_KP_Enter))) {
        chewingReturnValue = chewing_handle_Enter(ctx);
    } else if (keyEvent.key().states() == KeyState::Ctrl &&
               Key(keyEvent.key().sym()).isDigit()) {
        chewingReturnValue = chewing_handle_CtrlNum(ctx, keyEvent.key().sym());
    } else {
        // to do: more chewing_handle
        return;
    }

    CHEWING_DEBUG() << "Chewing return value: " << chewingReturnValue;
    if (chewing_keystroke_CheckIgnore(ctx)) {
        CHEWING_DEBUG() << "Keystroke ignored";
        return;
    }
    if (chewing_keystroke_CheckAbsorb(ctx)) {
        CHEWING_DEBUG() << "Keystroke absorbed";
        keyEvent.filterAndAccept();
    }
    if (chewing_commit_Check(ctx)) {
        keyEvent.filterAndAccept();
        ic->commitString(safeChewing_commit_String(ctx));
    }
    updateUI(ic);
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
        keyEvent.filterAndAccept();
        return;
    }

    if (!ic->inputPanel().candidateList()) {
        // Check if this key will produce something, if so, flush.
        if (isPrintableKey(keyEvent.key())) {
            if (inputMode_ == ChewingInputMode::ForceChinese) {
                return;
            }
            if (*config_.AsusSmartSymbolInput) {
                commitCurrentPreedit(ic);
            } else {
                flushBuffer(keyEvent);
            }
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

void ChewingEngine::commitCurrentPreedit(InputContext *ic) {
    auto *ctx = context_.get();
    std::string text;
    chewing_cand_close(ctx);
    if (chewing_buffer_Check(ctx)) {
        if (chewing_commit_preedit_buf(ctx) == 0) {
            text.append(safeChewing_commit_String(ctx));
        } else {
            text.append(safeChewing_buffer_String(ctx));
        }
    }
    if (text.empty() && chewing_bopomofo_Check(ctx)) {
        text.append(safeChewing_bopomofo_String(ctx));
    }
    if (!text.empty()) {
        ic->commitString(text);
    }
    chewing_cand_close(ctx);
    chewing_clean_preedit_buf(ctx);
    chewing_clean_bopomofo_buf(ctx);
    chewing_Reset(ctx);
    smartEnglishPrefix_.clear();
    smartEnglishBuffer_.clear();
    smartEnglishPassthrough_ = false;
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

FCITX_ADDON_FACTORY_V2(chewing, fcitx::ChewingEngineFactory);
