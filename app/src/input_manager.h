#ifndef SC_INPUTMANAGER_H
#define SC_INPUTMANAGER_H

#include "common.h"

#include <stdbool.h>

#include <SDL2/SDL.h>

#include "controller.h"
#include "file_pusher.h"
#include "fps_counter.h"
#include "options.h"
#include "trait/key_processor.h"
#include "trait/mouse_processor.h"
#include "keymap/fpsgame_keys.h"

struct sc_input_manager
{
    struct sc_controller *controller;
    struct sc_file_pusher *fp;
    struct sc_screen *screen;

    struct sc_key_processor *kp;
    struct sc_mouse_processor *mp;

    struct sc_fpsgame_keys *fpsgame_keys;

    bool forward_all_clicks;
    bool legacy_paste;
    bool clipboard_autosync;

    struct
    {
        unsigned data[SC_MAX_SHORTCUT_MODS];
        unsigned count;
    } sdl_shortcut_mods;

    bool vfinger_down;

    // 跟踪相同的连续快捷键按下事件的数量。
    // 不要与event->repeat混淆，后者统计系统生成的重复按键次数。
    unsigned key_repeat;
    SDL_Keycode last_keycode;
    uint16_t last_mod;

    uint64_t next_sequence; // 用于请求确认
};

struct sc_input_manager_params
{
    struct sc_controller *controller;
    struct sc_file_pusher *fp;
    struct sc_screen *screen;
    struct sc_key_processor *kp;
    struct sc_mouse_processor *mp;
    struct sc_fpsgame_keys *fpsgame_keys;

    bool forward_all_clicks;
    bool legacy_paste;
    bool clipboard_autosync;
    const struct sc_shortcut_mods *shortcut_mods;
};

void sc_input_manager_init(struct sc_input_manager *im,
                           const struct sc_input_manager_params *params);

void sc_input_manager_handle_event(struct sc_input_manager *im,
                                   const SDL_Event *event,
                                   bool mouse_capture);
void sc_input_manager_send_touch_event(
    struct sc_input_manager *im,
    float x, float y,
    Uint32 type,
    SDL_FingerID fingerId);

#endif
