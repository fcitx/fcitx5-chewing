/***************************************************************************
 *   Copyright (C) 2012~2012 by Tai-Lin Chu                                *
 *   tailinchugmail.com                                                    *
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

FCITX_EXPORT_API
FcitxIMClass ime = {
    FcitxChewingCreate,
    FcitxChewingDestroy
};
FCITX_EXPORT_API
int ABI_VERSION = FCITX_ABI_VERSION;

static int FcitxChewingGetRawCursorPos(char * str, int upos);
static INPUT_RETURN_VALUE FcitxChewingGetCandWord(void* arg, FcitxCandidateWord* candWord);

typedef struct _ChewingCandWord {
    int index;
} ChewingCandWord;

const FcitxHotkey FCITX_CHEWING_UP[2] = {{NULL, FcitxKey_Up, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
const FcitxHotkey FCITX_CHEWING_DOWN[2] = {{NULL, FcitxKey_Down, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
FcitxHotkey FCITX_CHEWING_PGUP[2] = {{NULL, FcitxKey_Page_Up, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
FcitxHotkey FCITX_CHEWING_PGDN[2] = {{NULL, FcitxKey_Page_Down, FcitxKeyState_None}, {NULL, FcitxKey_None, FcitxKeyState_None}};
int selKey[10] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
__EXPORT_API
void* FcitxChewingCreate(FcitxInstance* instance)
{
    char* user_path = NULL;
    FILE* fp = FcitxXDGGetFileUserWithPrefix("chewing", ".place_holder", "w", NULL);
    if (fp)
        fclose(fp);
    FcitxXDGGetFileUserWithPrefix("/", "chewing", NULL, &user_path);
    FcitxLog(INFO, "Chewing storage path %s", user_path);
    if (0 == chewing_Init(CHEWING_DATADIR, user_path)) {
        FcitxLog(DEBUG, "chewing init ok");
    } else {
        FcitxLog(DEBUG, "chewing init failed");
        return NULL;
    }
    
    FcitxChewing* chewing = (FcitxChewing*) fcitx_utils_malloc0(sizeof(FcitxChewing));
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(instance);
    FcitxInputState *input = FcitxInstanceGetInputState(instance);
    FcitxCandidateWordSetChoose(FcitxInputStateGetCandidateList(input), DIGIT_STR_CHOOSE);
    
    bindtextdomain("fcitx-chewing", LOCALEDIR);

    chewing->context = chewing_new();
    ChewingContext * c = chewing->context;
    chewing->owner = instance;
    chewing_set_ChiEngMode(c, CHINESE_MODE);
    chewing_set_maxChiSymbolLen(c, 16);
    // chewing will crash without set page
    chewing_set_candPerPage(c, config->iMaxCandWord);
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);
    chewing_set_selKey(c, selKey, 10);

    FcitxInstanceRegisterIM(
        instance,
        chewing,
        "chewing",
        _("Chewing"),
        "chewing",
        FcitxChewingInit,
        FcitxChewingReset,
        FcitxChewingDoInput,
        FcitxChewingGetCandWords,
        NULL,
        NULL,
        NULL,
        NULL,
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
__EXPORT_API
INPUT_RETURN_VALUE FcitxChewingDoInput(void* arg, FcitxKeySym sym, unsigned int state)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    ChewingContext * c = chewing->context;
    
    if (FcitxHotkeyIsHotKeyDigit(sym, state) && !chewing_cand_CheckDone(c))
        return IRV_TO_PROCESS;

    if (FcitxHotkeyIsHotKeySimple(sym, state)) {
        int scan_code = (int) sym & 0xff;
        chewing_handle_Default(c, scan_code);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_BACKSPACE)) {
        chewing_handle_Backspace(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_DELETE)) {
        chewing_handle_Del(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_SPACE)) {
        chewing_handle_Space(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_UP)) {
        chewing_handle_Up(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_DOWN)) {
        chewing_handle_Down(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_PGUP)) {
        chewing_handle_PageDown(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_CHEWING_PGDN)) {
        chewing_handle_PageUp(c);
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_RIGHT)) {
        return IRV_TO_PROCESS;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_LEFT)) {
        return IRV_TO_PROCESS;
    } else if (FcitxHotkeyIsHotKey(sym, state, FCITX_ENTER)) {
        chewing_handle_Enter(c);
    } else if (state == FcitxKeyState_Ctrl && FcitxHotkeyIsHotKeyDigit(sym, FcitxKeyState_None)) {
        chewing_handle_CtrlNum(c, sym);
    } else {
        // to do: more chewing_handle
        return IRV_TO_PROCESS;
    }
    if (chewing_keystroke_CheckAbsorb(c)) {
        return IRV_DISPLAY_CANDWORDS;
    } else if (chewing_keystroke_CheckIgnore(c)) {
        return IRV_TO_PROCESS;
    } else if (chewing_commit_Check(c)) {
        char* str = chewing_commit_String(c);
        strcpy(FcitxInputStateGetOutputString(input), str);
        chewing_free(str);
        return IRV_COMMIT_STRING;
    } else
        return IRV_DISPLAY_CANDWORDS;
}

__EXPORT_API
boolean FcitxChewingInit(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInstanceSetContext(chewing->owner, CONTEXT_ALTERNATIVE_PREVPAGE_KEY, FCITX_LEFT);
    FcitxInstanceSetContext(chewing->owner, CONTEXT_ALTERNATIVE_NEXTPAGE_KEY, FCITX_RIGHT);
    return true;
}

__EXPORT_API
void FcitxChewingReset(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    chewing_Reset(chewing->context);
}


/**
 * @brief function DoInput has done everything for us.
 *
 * @param searchMode
 * @return INPUT_RETURN_VALUE
 **/
__EXPORT_API
INPUT_RETURN_VALUE FcitxChewingGetCandWords(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
    ChewingContext * c = chewing->context;
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(chewing->owner);
    
    chewing_set_candPerPage(c, config->iMaxCandWord);
    FcitxCandidateWordSetPageSize(FcitxInputStateGetCandidateList(input), config->iMaxCandWord);

    //clean up window asap
    FcitxInstanceCleanInputWindowUp(chewing->owner);

    char * buf_str = chewing_buffer_String(c);
    char * zuin_str = chewing_zuin_String(c, NULL);

    FcitxLog(INFO, "%s %s", buf_str, zuin_str);

    if (!chewing_cand_CheckDone(c)) {
        //get candidate word
        chewing_cand_Enumerate(c);
        int index = 0;
        while (chewing_cand_hasNext(c)) {
            char* str = chewing_cand_String(c);
            FcitxCandidateWord cw;
            ChewingCandWord* w = (ChewingCandWord*) fcitx_utils_malloc0(sizeof(ChewingCandWord));
            w->index = index;
            cw.callback = FcitxChewingGetCandWord;
            cw.owner = chewing;
            cw.priv = w;
            cw.strExtra = NULL;
            cw.strWord = strdup(str);
            cw.wordType = MSG_OTHER;
            FcitxCandidateWordAppend(FcitxInputStateGetCandidateList(input), &cw);
            chewing_free(str);
            index ++;
        }
    }

    // setup cursor
    FcitxInputStateSetShowCursor(input, true);
    int buf_len = chewing_buffer_Len(c);
    int cur = chewing_cursor_Current(c);
    FcitxLog(INFO, "buf len: %d, cur: %d", buf_len, cur);
    int rcur = FcitxChewingGetRawCursorPos(buf_str, cur);
    FcitxInputStateSetCursorPos(input, rcur);

    // insert zuin in the middle
    char * half1 = strndup(buf_str, rcur);
    char * half2 = strdup(buf_str + rcur);
    FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s%s%s", half1, zuin_str, half2);
    chewing_free(buf_str); chewing_free(zuin_str);

    return IRV_DISPLAY_CANDWORDS;
}

INPUT_RETURN_VALUE FcitxChewingGetCandWord(void* arg, FcitxCandidateWord* candWord)
{
    FcitxChewing* chewing = (FcitxChewing*) candWord->owner;
    ChewingCandWord* w = (ChewingCandWord*) candWord->priv;
    FcitxGlobalConfig* config = FcitxInstanceGetGlobalConfig(chewing->owner);
    FcitxInputState *input = FcitxInstanceGetInputState(chewing->owner);
    int page = w->index / config->iMaxCandWord;
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
    chewing_handle_Default( chewing->context, selKey[off] );
    
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
__EXPORT_API
void FcitxChewingDestroy(void* arg)
{
    FcitxChewing* chewing = (FcitxChewing*) arg;
    chewing_delete(chewing->context);
    chewing_Terminate();
    free(arg);
}
