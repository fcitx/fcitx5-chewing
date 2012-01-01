/***************************************************************************
 *   Copyright (C) 2010~2010 by CSSlayer                                   *
 *   wengxt@gmail.com                                                      *
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
#include <fcitx-config/hotkey.h>
#include <fcitx-config/xdg.h>
#include <fcitx-utils/log.h>
#include <fcitx-config/fcitx-config.h>
#include <fcitx-utils/utils.h>
#include <fcitx/instance.h>
#include <fcitx/keys.h>
#include <fcitx/ui.h>
#include <libintl.h>

#include <chewing.h>

#include "eim.h"

FCITX_EXPORT_API
FcitxIMClass ime = {
    FcitxChewingCreate,
    FcitxChewingDestroy
};
FCITX_EXPORT_API
int ABI_VERSION = FCITX_ABI_VERSION;

CONFIG_DESC_DEFINE(GetChewingConfigDesc, "fcitx-chewing.desc")

boolean LoadChewingConfig(FcitxChewingConfig* fs);
static void SaveChewingConfig(FcitxChewingConfig* fs);
static void ConfigChewing(FcitxChewing* chewing);

/**
 * @brief initialize the extra input method
 *
 * @param arg
 * @return successful or not
 **/
__EXPORT_API
void* FcitxChewingCreate(FcitxInstance* instance)
{
    FcitxChewing* chewing = (FcitxChewing*) fcitx_utils_malloc0(sizeof(FcitxChewing));
    bindtextdomain("fcitx-chewing", LOCALEDIR);

    if (!LoadChewingConfig(&chewing->fc)) {
        free(chewing);
        return NULL;
    }
    chewing->context = chewing_new();
    ChewingContext * c = chewing->context;
    chewing->fc.iChewingPriority = 1;
    chewing->owner = instance;
	chewing_set_ChiEngMode(c, CHINESE_MODE);
    ConfigChewing(chewing);
    chewing_set_maxChiSymbolLen(c, 16);
    chewing_set_candPerPage(c, 10);

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
                    chewing->fc.iChewingPriority,
                    "zh_CN"
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
    if (FcitxHotkeyIsHotKeySimple(sym, state)) {
		int scan_code= (int) sym & 0xff; 
		chewing_handle_Default(c, scan_code);
		FcitxLog(INFO, "scan code: %c %d", (char) scan_code, scan_code);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_BackSpace) {
		chewing_handle_Backspace(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Delete) {
		chewing_handle_Del(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_space) {
		chewing_handle_Space(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Tab) {
		
		return FcitxCandidateWordChooseByIndex(FcitxInputStateGetCandidateList(input), 0);
	}
	else if (sym == FcitxKey_Up) {
		chewing_handle_Up(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Down) {
		chewing_handle_Down(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Right) {
		chewing_handle_Right(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Left) {
		chewing_handle_Left(c);
		return IRV_DISPLAY_CANDWORDS;
	}
	else if (sym == FcitxKey_Return) {
		char * buf_str = chewing_buffer_String(c);
		strcpy(FcitxInputStateGetOutputString(input), buf_str);
		chewing_handle_Enter(c);
		chewing_free(buf_str);
		return IRV_COMMIT_STRING;
	}
	else {
		// to do: more chewing_handle
		return IRV_TO_PROCESS;
	}
}


boolean FcitxChewingInit(void* arg)
{
	if (0 == chewing_Init("/usr/share/chewing", NULL)) {
		FcitxLog(INFO, "chewing init ok");
		return true;
	}
	else {
		FcitxLog(INFO, "chewing init failed");
		return false;
	}
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
    chewing_cand_Enumerate(c);
    
    FcitxCandidateWordSetChoose(FcitxInputStateGetCandidateList(input), DIGIT_STR_CHOOSE);
    
	char * buf_str = chewing_buffer_String(c);
	char * zuin_str = chewing_zuin_String(c, NULL);

	FcitxLog(INFO, "%s %s", buf_str, zuin_str);
	
	while (chewing_cand_hasNext(c)) {
       char* str = chewing_cand_String(c);
       FcitxCandidateWord cw;
       cw.callback = FcitxChewingGetCandWord;
       cw.owner = chewing;
       cw.priv = NULL;
       cw.strExtra = NULL;
       cw.strWord = strdup(str);
       cw.wordType = MSG_OTHER;
       FcitxCandidateWordAppend(FcitxInputStateGetCandidateList(input), &cw);
       chewing_free(str);
	}
	
	FcitxInstanceCleanInputWindowUp(chewing->owner);
	FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s%s", buf_str, zuin_str);
	chewing_free(buf_str); chewing_free(zuin_str);
	return IRV_DISPLAY_CANDWORDS;
}

__EXPORT_API
INPUT_RETURN_VALUE FcitxChewingGetCandWord(void * arg, FcitxCandidateWord* cw)
{
	return IRV_DISPLAY_CANDWORDS;
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

/**
 * @brief Load the config file for fcitx-chewing
 *
 * @param Bool is reload or not
 **/
boolean LoadChewingConfig(FcitxChewingConfig* fs)
{
    FcitxConfigFileDesc *configDesc = GetChewingConfigDesc();
    if (!configDesc)
        return false;

    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-chewing.config", "rt", NULL);

    if (!fp) {
        if (errno == ENOENT)
            SaveChewingConfig(fs);
    }
    FcitxConfigFile *cfile = FcitxConfigParseConfigFileFp(fp, configDesc);


    if (fp)
        fclose(fp);
    return true;
}

void ConfigChewing(FcitxChewing* chewing)
{
	return;
}

/**
 * @brief Save the config
 *
 * @return void
 **/
void SaveChewingConfig(FcitxChewingConfig* fs)
{
    FcitxConfigFileDesc *configDesc = GetChewingConfigDesc();
    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-chewing.config", "wt", NULL);
    FcitxConfigSaveConfigFileFp(fp, &fs->gconfig, configDesc);
    if (fp)
        fclose(fp);
}
