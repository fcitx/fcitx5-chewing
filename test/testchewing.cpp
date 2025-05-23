/*
 * SPDX-FileCopyrightText: 2021-2021 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include "testdir.h"
#include "testfrontend_public.h"
#include <fcitx-config/rawconfig.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/macros.h>
#include <fcitx-utils/testing.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <string>
#include <string_view>

using namespace fcitx;

void testBasic(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *chewing = instance->addonManager().addon("chewing", true);
        FCITX_ASSERT(chewing);
        auto defaultGroup = instance->inputMethodManager().currentGroup();
        defaultGroup.inputMethodList().clear();
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("keyboard-us"));
        defaultGroup.inputMethodList().push_back(
            InputMethodGroupItem("chewing"));
        defaultGroup.setDefaultInputMethod("");
        instance->inputMethodManager().setGroup(defaultGroup);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "chewing");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(ic->inputPanel().preedit().toString() == "ㄈㄣ");
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Down"), false));
        FCITX_ASSERT(ic->inputPanel().candidateList());
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        // This should be the case.
        FCITX_ASSERT(ic->inputPanel().candidateList()->size() >= 7);
        std::string text =
            ic->inputPanel().candidateList()->candidate(6).text().toString();

        testfrontend->call<ITestFrontend::pushCommitExpectation>(text);
        ic->inputPanel().candidateList()->toCursorModifiable()->setCursorIndex(
            6);
        FCITX_ASSERT(ic->inputPanel().candidateList()->cursorIndex() == 6);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        RawConfig config;
        config.setValueByPath("Layout", "Han-Yu PinYin Keyboard");
        chewing->setConfig(config);
        for (char c : std::string_view("hu2jia3hu3wei1")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        text = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(text == "狐假虎威");
        testfrontend->call<ITestFrontend::pushCommitExpectation>(text);
        bool found = false;
        for (int i = 0; i < 10; i++) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key("space"), false));

            FCITX_ASSERT(ic->inputPanel().candidateList());
            FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
            for (int j = 0; j < ic->inputPanel().candidateList()->size(); j++) {
                if (ic->inputPanel()
                        .candidateList()
                        ->candidate(j)
                        .text()
                        .toString() == "威") {
                    found = true;
                    break;
                }
            }
            if (found) {
                break;
            }
        }
        FCITX_ASSERT(found);

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));

        for (char c : std::string_view("fen1")) {
            FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
                uuid, Key(static_cast<KeySym>(c)), false));
        }
        text = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(ic->inputPanel().candidateList());
        FCITX_ASSERT(!ic->inputPanel().candidateList()->empty());
        testfrontend->call<ITestFrontend::pushCommitExpectation>(text);

        instance->deactivate();
    });
}

void testBackspaceWithBuffer(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *chewing = instance->addonManager().addon("chewing", true);
        FCITX_ASSERT(chewing);
        RawConfig config;
        config.setValueByPath("Layout", "Default");
        chewing->setConfig(config);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "chewing");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        auto text = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(text.empty());

        instance->deactivate();
    });
}

void testBackspaceWhenBufferEmpty(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *chewing = instance->addonManager().addon("chewing", true);
        FCITX_ASSERT(chewing);
        RawConfig config;
        config.setValueByPath("Layout", "Default");
        chewing->setConfig(config);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "chewing");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        auto text = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(text);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Return"), false));
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));

        instance->deactivate();
    });
}

void testBackspaceWithBopomofo(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *chewing = instance->addonManager().addon("chewing", true);
        FCITX_ASSERT(chewing);
        RawConfig config;
        config.setValueByPath("Layout", "Default");
        chewing->setConfig(config);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "chewing");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));
        auto text = ic->inputPanel().preedit().toString();
        FCITX_ASSERT(text.empty());
        FCITX_ASSERT(!testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key(FcitxKey_BackSpace), false));

        instance->deactivate();
    });
}

void testCommitPreedit(Instance *instance) {
    instance->eventDispatcher().schedule([instance]() {
        auto *chewing = instance->addonManager().addon("chewing", true);
        FCITX_ASSERT(chewing);
        RawConfig config;
        config.setValueByPath("Layout", "Default Keyboard");
        config.setValueByPath("SwitchInputMethodBehavior",
                              "Commit current preedit");
        chewing->setConfig(config);
        auto *testfrontend = instance->addonManager().addon("testfrontend");
        auto uuid =
            testfrontend->call<ITestFrontend::createInputContext>("testapp");
        auto *ic = instance->inputContextManager().findByUUID(uuid);
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("Control+space"), false));
        FCITX_ASSERT(instance->inputMethod(ic) == "chewing");

        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("space"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("z"), false));
        FCITX_ASSERT(testfrontend->call<ITestFrontend::sendKeyEvent>(
            uuid, Key("p"), false));
        auto text = ic->inputPanel().preedit().toString();
        testfrontend->call<ITestFrontend::pushCommitExpectation>(text);

        instance->deactivate();
    });
}

int main() {
    setupTestingEnvironment(TESTING_BINARY_DIR, {TESTING_BINARY_DIR "/src"},
                            {TESTING_BINARY_DIR "/test"});
    // fcitx::Log::setLogRule("default=5,table=5,libime-table=5");
    char arg0[] = "testchewing";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,testfrontend,chewing";
    char *argv[] = {arg0, arg1, arg2};
    fcitx::Log::setLogRule("default=5,chewing=5");
    Instance instance(FCITX_ARRAY_SIZE(argv), argv);
    instance.addonManager().registerDefaultLoader(nullptr);

    testBasic(&instance);
    testBackspaceWhenBufferEmpty(&instance);
    testBackspaceWithBuffer(&instance);
    testBackspaceWithBopomofo(&instance);
    testCommitPreedit(&instance);

    instance.eventDispatcher().schedule([&instance]() { instance.exit(); });
    instance.exec();

    return 0;
}
