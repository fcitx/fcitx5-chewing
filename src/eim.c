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
    chewing->fc.iChewingPriority = 1;
    chewing->owner = instance;

    ConfigChewing(chewing);

    FcitxInstanceRegisterIM(
					instance,
                    chewing,
                    "chewing",
                    _("Chewing"),
                    "chewing",
                    FcitxChewingInit,
                    NULL,
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
    FcitxMessages *msgPreedit = FcitxInputStateGetPreedit(input);
    if (FcitxHotkeyIsHotKeySimple(sym, state)) {
        char *pho_buffer = (chewing->input_state).pho_buffer;
        int pho_count = (chewing->input_state).pho_count;
        unsigned int scan_code;

        pho_buffer[pho_count ++] = sym & 0xff;
        pho_buffer[pho_count] = '\0';
        (chewing->input_state).pho_count = strlen(pho_buffer); 

        FcitxLog(INFO,"input:%c\n",pho_buffer[pho_count-1]);
        
        FcitxHotkeyGetKey(sym, 0, NULL, &scan_code);
        
        chewing_handle_Default(chewing->context, scan_code);

        FcitxLog(INFO,"pho_buffer:%s count:%d\n",chewing->input_state.pho_buffer,chewing->input_state.pho_count);
        FcitxLog(INFO,"Input Display should be:%s%s\n",chewing->input_state.input_buffer,chewing->input_state.pho_buffer);
        FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", chewing->input_state.input_buffer);
        if(pho_buffer[0])
            FcitxMessagesAddMessageAtLast(msgPreedit, MSG_INPUT, "%s", chewing->input_state.pho_buffer);
    }
    return IRV_DISPLAY_MESSAGE;
}

boolean FcitxChewingInit(void* arg)
{
	chewing_Init("/usr/share/chewing", "~/.chewing");
	return true;
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
