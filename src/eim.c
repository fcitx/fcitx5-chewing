/***************************************************************************
 *   Copyright (C) 2012~2012 by Tai-Lin Chu                                *
 *   tailinchu@gmail.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcitx/ime.h>
#include <fcitx-config/fcitx-config.h>
#include <fcitx-config/xdg.h>
#include <fcitx-config/hotkey.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/utils.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/instance.h>
#include <fcitx/context.h>
#include <fcitx/keys.h>
#include <fcitx/ui.h>
#include <libintl.h>

#include <chewing.h>

#include "config.h"
#include "eim.h"

CONFIG_DESC_DEFINE(GetFcitxChewingConfigDesc, "fcitx-chewing.desc")
static int FcitxChewingGetRawCursorPos(char * str, int upos);
static INPUT_RETURN_VALUE FcitxChewingGetCandWord(void* arg, FcitxCandidateWord* candWord);
static void FcitxChewingReloadConfig(void* arg);
static boolean LoadChewingConfig(FcitxChewingConfig* fs);
static void SaveChewingConfig(FcitxChewingConfig* fs);
static void ConfigChewing(FcitxChewing* chewing);
static void FcitxChewingOnClose(void* arg, FcitxIMCloseEventType event);
static INPUT_RETURN_VALUE FcitxChewingKeyBlocker(void* arg, FcitxKeySym key, unsigned int state);

typedef struct _ChewingCandWord {
    int index;
} ChewingCandWord;

FCITX_DEFINE_PLUGIN(fcitx_chewing, ime2, FcitxIMClass2) = {
    FcitxChewingCreate,
    FcitxChewingDestroy,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

const FcitxHotkey FCITX_CHEWING_SHIFT_LEFT[2] = {{NULL, FcitxKey_Left, FcitxKeyState_Shift}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_SHIFT_RIGHT[2] = {{NULL, FcitxKey_Right, FcitxKeyState_Shift}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_UP[2] = {{NULL, FcitxKey_Up, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_DOWN[2] = {{NULL, FcitxKey_Down, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_PGUP[2] = {{NULL, FcitxKey_Page_Up, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_PGDN[2] = {{NULL, FcitxKey_Page_Down, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};

const char *builtin_keymaps[] = {
    "KB_DEFAULT",
    "KB_HSU",
    "KB_IBM",
    "KB_GIN_YEIH",
    "KB_ET",
    "KB_ET26",
    "KB_DVORAK",
    "KB_DVORAK_HSU",
    "KB_DACHEN_CP26",
    "KB_HANYU_PINYIN"
};

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
void* FcitxChewingCreate(FcitxInstance* instance)
{
    if (GetFcitxChewingConfigDesc() == NULL)
        return NULL;
    
    char* user_path = NULL;
    FILE* fp = FcitxXDGGetFileUserWithPrefix("chewing", ".place_holder", "w", NULL);
    if (fp)
        fclose(fp);
    FcitxXDGGetFileUserWithPrefix("chewing", "", NULL, &user_path);
    FcitxLog(INFO, "Chewing storage path %s", user_path);
    
    FcitxChewing* chewing = (FcitxChewing*) fcitx_utils_malloc0(sizeof(FcitxChewing));
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(instance);
    FcitxInputState *input = FcitxInstanceGetInputState(instance);
    
    bindtextdomain("fcitx-chewing", LOCALEDIR);
    bind_textdomain_codeset("fcitx-chewing", "UTF-8");

    chewing->context = chewing_new();
    ChewingContext * ctx = chewing->context;
    
    if (NULL == chewing->context) {
        FcitxLog(DEBUG, "chewing init failed");
        free(chewing);
        return NULL;
    } else {
        FcitxLog(DEBUG, "chewing init ok");
    }

    chewing->owner = instance;
    chewing_set_maxChiSymbolLen(ctx, CHEWING_MAX_LEN);
    // chewing will crash without set page
    chewing_set_candPerPage(ctx, config->iMaxCandWord);
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);
    LoadChewingConfig(&chewing->config);
    ConfigChewing(chewing);

    FcitxIMIFace iface;
    memset(&iface, 0, sizeof(FcitxIMIFace));

    iface.Init = FcitxChewingInit;
    iface.ResetIM = FcitxChewingReset;
    iface.DoInput = FcitxChewingDoInput;
    iface.GetCandWords = FcitxChewingGetCandWords;
    iface.Save = NULL;
    iface.ReloadConfig = FcitxChewingReloadConfig;
    iface.OnClose = FcitxChewingOnClose;
    iface.KeyBlocker = FcitxChewingKeyBlocker;

    FcitxInstanceRegisterIMv2(
        instance,
        chewing,
        "chewing",
        _("Chewing"),
        "chewing",
        iface,
        1,
        "zh_TW"
    );

    return chewing;
}

/**
 * @brief Process Key Input and return the status
 *
 * @param keycode keycode from XKeyEvent
 * @param state state from XKeyEvent
 * @param count count from XKeyEvent
 * @return INPUT_RETURN_VALUE
 **/
INPUT_RETURN_VALUE FcitxChewingDoInput(void* arg, FcitxKeySym sym, unsigned int state)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    ChewingContext* ctx = chewing->context;

    if (FcitxHotkeyIsHotKey(sym, state, FCITX_SPACE)) {
        chewing_handle_Space(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_TAB)) {
        chewing_handle_Tab(ctx);
    } else if (FcitxHotkeyIsHotKeySimple(sym, state)) {
        int scan_code = (int) sym & 0xff;
        chewing_handle_Default(ctx, scan_code);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
        const char* zuin_str = chewing_bopomofo_String_static(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0])
            return IRV_TO_PROCESS;
        chewing_handle_Backspace(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0])
            return IRV_CLEAN;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ESCAPE)) {
        chewing_handle_Esc(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
        const char* zuin_str = chewing_bopomofo_String_static(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0])
            return IRV_TO_PROCESS;
        chewing_handle_Del(ctx);
        if (chewing_buffer_Len(ctx) == 0 && !zuin_str[0])
            return IRV_CLEAN;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_UP)) {
        chewing_handle_Up(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_DOWN)) {
        chewing_handle_Down(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_PGUP)) {
        chewing_handle_PageDown(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_PGDN)) {
        chewing_handle_PageUp(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_RIGHT)) {
        chewing_handle_Right(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_LEFT)) {
        chewing_handle_Left(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_HOME)) {
        chewing_handle_Home(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_END)) {
        chewing_handle_End(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_SHIFT_SPACE)) {
        chewing_handle_ShiftSpace(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_SHIFT_LEFT)) {
        chewing_handle_ShiftLeft(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_SHIFT_RIGHT)) {
        chewing_handle_ShiftRight(ctx);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ENTER)) {
        chewing_handle_Enter(ctx);
    } else if (state == FcitxKeyState_Ctrl && FcitxHotkeyIsHotKeyDigit(sym, FcitxKeyState_None)) {
        chewing_handle_CtrlNum(ctx, sym);
    } else {
        // to do: more chewing_handle
        return IRV_TO_PROCESS;
    }

    if (chewing_keystroke_CheckAbsorb(ctx)) {
        return IRV_DISPLAY_CANDWORDS;
    } else if (chewing_keystroke_CheckIgnore(ctx)) {
        return IRV_TO_PROCESS;
    } else if (chewing_commit_Check(ctx)) {
        char* str = chewing_commit_String(ctx);
        FcitxInputContext* ic = FcitxInstanceGetCurrentIC(chewing->owner);
        FcitxInstanceCommitString(chewing->owner, ic, str);
        chewing_free(str);
        return IRV_DISPLAY_CANDWORDS;
    } else {
        return IRV_DISPLAY_CANDWORDS;
    }
}

boolean FcitxChewingInit(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInstanceSetContext(chewing->owner, CONTEXT_IM_KEYBOARD_LAYOUT, "us");
    FcitxInstanceSetContext(chewing->owner, CONTEXT_ALTERNATIVE_PREVPAGE_KEY, FCITX_LEFT);
    FcitxInstanceSetContext(chewing->owner, CONTEXT_ALTERNATIVE_NEXTPAGE_KEY, FCITX_RIGHT);
    return true;
}

void FcitxChewingReset(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    chewing_Reset(chewing->context);

    chewing_set_KBType( chewing->context, chewing_KBStr2Num(
                (char *) builtin_keymaps[chewing->config.layout]));

    chewing_set_ChiEngMode(chewing->context, CHINESE_MODE);
#if 0
    FcitxUIStatus* puncStatus = FcitxUIGetStatusByName(chewing->owner, "punc");
    if (puncStatus) {
        if (puncStatus->getCurrentStatus(puncStatus->arg))
            chewing_set_ShapeMode(chewing->context, FULLSHAPE_MODE);
        else
            chewing_set_ShapeMode(chewing->context, HALFSHAPE_MODE);
    }
#endif
}

static boolean FcitxChewingPaging(void* arg, boolean prev)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    FcitxCandidateWordList* candList = FcitxInputStateGetCandidateList(input);

    if (FcitxCandidateWordGetListSize(candList) == 0) {
        return false;
    }

    if (prev) {
        chewing_handle_Left(chewing->context);
    } else {
        chewing_handle_Right(chewing->context);
    }

    if (chewing_keystroke_CheckAbsorb(chewing->context)) {
        FcitxChewingGetCandWords(chewing);
        FcitxUIUpdateInputWindow(chewing->owner);
        return true;
    }
    return false;
}

static const char *builtin_selectkeys[] = {
DIGIT_STR_CHOOSE,
"asdfghjkl;",
"asdfzxcv89",
"asdfjkl789",
"aoeuhtn789",
"1234qweras",
};

/**
 * @brief function DoInput has done everything for us.
 *
 * @param searchMode
 * @return INPUT_RETURN_VALUE
 **/
INPUT_RETURN_VALUE FcitxChewingGetCandWords(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
    FcitxMessages *clientPreedit = FcitxInputStateGetClientPreedit(input);
    ChewingContext * ctx = chewing->context;
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(chewing->owner);
    FcitxCandidateWordList* candList = FcitxInputStateGetCandidateList(input);

    int selkey[10];
    int i = 0;
    for (i = 0; i < 10; i++) {
        selkey[i] = builtin_selectkeys[chewing->config.selkey][i];
    }
    
    chewing_set_selKey(ctx, selkey, 10);
    chewing_set_candPerPage(ctx, config->iMaxCandWord);
    FcitxCandidateWordSetPageSize(candList, config->iMaxCandWord);
    FcitxCandidateWordSetChoose(candList, builtin_selectkeys[chewing->config.selkey]);

    //clean up window asap
    FcitxInstanceCleanInputWindow(chewing->owner);

    char * buf_str = chewing_buffer_String(ctx);
    const char* zuin_str = chewing_bopomofo_String_static(ctx);
    ConfigChewing(chewing);

    FcitxLog(DEBUG, "%s %s", buf_str, zuin_str);

    int index = 0;
    /* if not check done, so there is candidate word */
    if (!chewing_cand_CheckDone(ctx)) {
        //get candidate word
        chewing_cand_Enumerate(ctx);
        while (chewing_cand_hasNext(ctx)) {
            char* str = chewing_cand_String(ctx);
            FcitxCandidateWord cw;
            ChewingCandWord* w = (ChewingCandWord*) fcitx_utils_malloc0(sizeof(ChewingCandWord));
            w->index = index;
            cw.callback = FcitxChewingGetCandWord;
            cw.owner = chewing;
            cw.priv = w;
            cw.strExtra = NULL;
            cw.strWord = strdup(str);
            cw.wordType = MSG_OTHER;
            FcitxCandidateWordAppend(candList, &cw);
            chewing_free(str);
            index ++;
        }

        if (FcitxCandidateWordGetListSize(candList) > 0) {
            FcitxCandidateWordSetOverridePaging(
                candList,
                chewing_cand_CurrentPage(ctx) > 0,
                chewing_cand_CurrentPage(ctx) + 1 < chewing_cand_TotalPage(ctx),
                FcitxChewingPaging,
                chewing,
                NULL);
        }
    }

    do {
        /* there is nothing */
        if (strlen(zuin_str) == 0 && strlen(buf_str) == 0 && index == 0)
            break;

        // setup cursor
        FcitxInputStateSetShowCursor(input, true);
        int cur = chewing_cursor_Current(ctx);
        FcitxLog(DEBUG, "cur: %d", cur);
        int rcur = FcitxChewingGetRawCursorPos(buf_str, cur);
        FcitxInputStateSetCursorPos(input, rcur);
        FcitxInputStateSetClientCursorPos(input, rcur);

        // insert zuin in the middle
        char * half1 = strndup(buf_str, rcur);
        char * half2 = strdup(buf_str + rcur);

        FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", half1);
        FcitxMessagesAddMessageAtLast(msgPreedit, MSG_CODE, "%s", zuin_str);
        FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", half2);

        FcitxMessagesAddMessageAtLast(clientPreedit, MSG_OTHER, "%s", half1);
        FcitxMessagesAddMessageAtLast(clientPreedit, MSG_HIGHLIGHT | MSG_DONOT_COMMIT_WHEN_UNFOCUS, "%s", zuin_str);
        FcitxMessagesAddMessageAtLast(clientPreedit, MSG_OTHER, "%s", half2);
        free(half1);
        free(half2);
    } while(0);

    chewing_free(buf_str);

    return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE FcitxChewingGetCandWord(void* arg, FcitxCandidateWord* candWord)
{
    FcitxChewing* chewing = (FcitxChewing*) candWord->owner;
    ChewingCandWord* w = (ChewingCandWord*) candWord->priv;
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(chewing->owner);
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    int page = w->index / config->iMaxCandWord + chewing_cand_CurrentPage(chewing->context);
    int off = w->index % config->iMaxCandWord;
    if (page < 0 || page >= chewing_cand_TotalPage(chewing->context))
        return IRV_TO_PROCESS;
    int lastPage = chewing_cand_CurrentPage(chewing->context);
    while (page != chewing_cand_CurrentPage(chewing->context)) {
        if (page < chewing_cand_CurrentPage(chewing->context)) {
            chewing_handle_Left(chewing->context);
        }
        if (page > chewing_cand_CurrentPage(chewing->context)) {
            chewing_handle_Right(chewing->context);
        }
        /* though useless, but take care if there is a bug cause freeze */
        if (lastPage == chewing_cand_CurrentPage(chewing->context)) {
            break;
        }
        lastPage = chewing_cand_CurrentPage(chewing->context);
    }
    chewing_handle_Default( chewing->context, builtin_selectkeys[chewing->config.selkey][off] );
    
    if (chewing_keystroke_CheckAbsorb(chewing->context)) {
        return IRV_DISPLAY_CANDWORDS;
    } else if (chewing_keystroke_CheckIgnore(chewing->context)) {
        return IRV_TO_PROCESS;
    } else if (chewing_commit_Check(chewing->context)) {
        char* str = chewing_commit_String(chewing->context);
        strcpy(FcitxInputStateGetOutputString(input), str);
        chewing_free(str);
        return IRV_COMMIT_STRING;
    } else
        return IRV_DISPLAY_CANDWORDS;
}

/**
 * @brief Get the non-utf8 cursor pos for fcitx
 *
 * @return int
 **/
static int FcitxChewingGetRawCursorPos(char * str, int upos)
{
    unsigned int i;
    int pos = 0;
    for (i = 0; i < upos; i++) {
        pos += fcitx_utf8_char_len(fcitx_utf8_get_nth_char(str, i));
    }
    return pos;
}

/**
 * @brief Destroy the input method while unload it.
 *
 * @return int
 **/
void FcitxChewingDestroy(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    chewing_delete(chewing->context);
    free(arg);
}

void FcitxChewingReloadConfig(void* arg) {
    FcitxChewing* chewing = (FcitxChewing*) arg;
    LoadChewingConfig(&chewing->config);
    ConfigChewing(chewing);
}

void FcitxChewingOnClose(void* arg, FcitxIMCloseEventType event)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    ChewingContext* ctx = chewing->context;
    if (event == CET_LostFocus || event == CET_ChangeByInactivate) {
        chewing_handle_Enter(ctx);
        if (event == CET_ChangeByInactivate) {
            if (chewing_commit_Check(ctx)) {
                char* str = chewing_commit_String(ctx);
                FcitxInputContext* ic = FcitxInstanceGetCurrentIC(chewing->owner);
                FcitxInstanceCommitString(chewing->owner, ic, str);
                chewing_free(str);
            } else {
                char * buf_str = chewing_buffer_String(ctx);
                do {
                    /* there is nothing */
                    if (strlen(buf_str) == 0)
                        break;
                    FcitxInputContext* ic = FcitxInstanceGetCurrentIC(chewing->owner);
                    FcitxInstanceCommitString(chewing->owner, ic, buf_str);
                } while(0);
                chewing_free(buf_str);
            }
        }
        else {
            FcitxInstanceResetInput(chewing->owner);
        }
    }

}

INPUT_RETURN_VALUE FcitxChewingKeyBlocker(void* arg, FcitxKeySym key, unsigned int state)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    FcitxCandidateWordList* candList = FcitxInputStateGetCandidateList(input);
    if (FcitxCandidateWordGetListSize(candList) > 0
        && (FcitxHotkeyIsHotKeySimple(key, state)
        || FcitxHotkeyIsHotkeyCursorMove(key, state)
        || FcitxHotkeyIsHotKey(key, state, FCITX_SHIFT_SPACE)
        || FcitxHotkeyIsHotKey(key, state, FCITX_TAB)
        || FcitxHotkeyIsHotKey(key, state, FCITX_SHIFT_ENTER)
        ))
        return IRV_DO_NOTHING;
    else
        return IRV_TO_PROCESS;
}

boolean LoadChewingConfig(FcitxChewingConfig* fs)
{
    FcitxConfigFileDesc *configDesc = GetFcitxChewingConfigDesc();
    if (!configDesc)
        return false;

    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-chewing.config", "r", NULL);

    if (!fp) {
        if (errno == ENOENT)
            SaveChewingConfig(fs);
    }
    FcitxConfigFile *cfile = FcitxConfigParseConfigFileFp(fp, configDesc);

    FcitxChewingConfigConfigBind(fs, cfile, configDesc);
    FcitxConfigBindSync(&fs->config);

    if (fp)
        fclose(fp);
    return true;
}

void SaveChewingConfig(FcitxChewingConfig* fc)
{
    FcitxConfigFileDesc *configDesc = GetFcitxChewingConfigDesc();
    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-chewing.config", "w", NULL);
    FcitxConfigSaveConfigFileFp(fp, &fc->config, configDesc);
    if (fp)
        fclose(fp);
}

void ConfigChewing(FcitxChewing* chewing)
{
    ChewingContext* ctx = chewing->context;
    chewing_set_addPhraseDirection( ctx, chewing->config.bAddPhraseForward ? 0 : 1 );
    chewing_set_phraseChoiceRearward( ctx, chewing->config.bChoiceBackward ? 1 : 0 );
    chewing_set_autoShiftCur( ctx, chewing->config.bAutoShiftCursor ? 1 : 0 );
    chewing_set_spaceAsSelection( ctx, chewing->config.bSpaceAsSelection ? 1 : 0 );
    chewing_set_escCleanAllBuf( ctx, 1 );
}
