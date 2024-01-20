#include "input_manager.h"

#include <assert.h>
#include <SDL2/SDL_keycode.h>

#include "input_events.h"
#include "screen.h"
#include "util/log.h"

#define SC_SDL_SHORTCUT_MODS_MASK (KMOD_CTRL | KMOD_ALT | KMOD_GUI)

static void
sc_input_manager_process_touch(struct sc_input_manager *im,
                               const SDL_TouchFingerEvent *event);

static inline uint16_t
to_sdl_mod(unsigned shortcut_mod)
{
    uint16_t sdl_mod = 0;
    if (shortcut_mod & SC_SHORTCUT_MOD_LCTRL)
    {
        sdl_mod |= KMOD_LCTRL;
    }
    if (shortcut_mod & SC_SHORTCUT_MOD_RCTRL)
    {
        sdl_mod |= KMOD_RCTRL;
    }
    if (shortcut_mod & SC_SHORTCUT_MOD_LALT)
    {
        sdl_mod |= KMOD_LALT;
    }
    if (shortcut_mod & SC_SHORTCUT_MOD_RALT)
    {
        sdl_mod |= KMOD_RALT;
    }
    if (shortcut_mod & SC_SHORTCUT_MOD_LSUPER)
    {
        sdl_mod |= KMOD_LGUI;
    }
    if (shortcut_mod & SC_SHORTCUT_MOD_RSUPER)
    {
        sdl_mod |= KMOD_RGUI;
    }
    return sdl_mod;
}

static bool
is_shortcut_mod(struct sc_input_manager *im, uint16_t sdl_mod)
{
    // 只保留相关的修饰符键
    sdl_mod &= SC_SDL_SHORTCUT_MODS_MASK;

    assert(im->sdl_shortcut_mods.count);
    assert(im->sdl_shortcut_mods.count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < im->sdl_shortcut_mods.count; ++i)
    {
        if (im->sdl_shortcut_mods.data[i] == sdl_mod)
        {
            return true;
        }
    }

    return false;
}

void sc_input_manager_init(struct sc_input_manager *im,
                           const struct sc_input_manager_params *params)
{
    assert(!params->controller || (params->kp && params->kp->ops));
    assert(!params->controller || (params->mp && params->mp->ops));

    im->controller = params->controller;
    im->fp = params->fp;
    im->screen = params->screen;
    im->kp = params->kp;
    im->mp = params->mp;
    im->fpsgame_keys = params->fpsgame_keys;

    im->forward_all_clicks = params->forward_all_clicks;
    im->legacy_paste = params->legacy_paste;
    im->clipboard_autosync = params->clipboard_autosync;

    const struct sc_shortcut_mods *shortcut_mods = params->shortcut_mods;
    assert(shortcut_mods->count);
    assert(shortcut_mods->count < SC_MAX_SHORTCUT_MODS);
    for (unsigned i = 0; i < shortcut_mods->count; ++i)
    {
        uint16_t sdl_mod = to_sdl_mod(shortcut_mods->data[i]);
        assert(sdl_mod);
        im->sdl_shortcut_mods.data[i] = sdl_mod;
    }
    im->sdl_shortcut_mods.count = shortcut_mods->count;

    im->vfinger_down = false;

    im->last_keycode = SDLK_UNKNOWN;
    im->last_mod = 0;
    im->key_repeat = 0;

    im->next_sequence = 1; // 0 is reserved for SC_SEQUENCE_INVALID
}

static void
send_keycode(struct sc_controller *controller, enum android_keycode keycode,
             enum sc_action action, const char *name)
{
    // send DOWN event
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_INJECT_KEYCODE;
    msg.inject_keycode.action = action == SC_ACTION_DOWN
                                    ? AKEY_EVENT_ACTION_DOWN
                                    : AKEY_EVENT_ACTION_UP;
    msg.inject_keycode.keycode = keycode;
    msg.inject_keycode.metastate = 0;
    msg.inject_keycode.repeat = 0;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'inject %s'", name);
    }
}

static inline void
action_home(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_HOME, action, "HOME");
}

static inline void
action_back(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_BACK, action, "BACK");
}

static inline void
action_app_switch(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_APP_SWITCH, action, "APP_SWITCH");
}

static inline void
action_power(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_POWER, action, "POWER");
}

static inline void
action_volume_up(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_VOLUME_UP, action, "VOLUME_UP");
}

static inline void
action_volume_down(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_VOLUME_DOWN, action, "VOLUME_DOWN");
}

static inline void
action_menu(struct sc_controller *controller, enum sc_action action)
{
    send_keycode(controller, AKEYCODE_MENU, action, "MENU");
}

// turn the screen on if it was off, press BACK otherwise
// If the screen is off, it is turned on only on ACTION_DOWN
static void
press_back_or_turn_screen_on(struct sc_controller *controller,
                             enum sc_action action)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON;
    msg.back_or_screen_on.action = action == SC_ACTION_DOWN
                                       ? AKEY_EVENT_ACTION_DOWN
                                       : AKEY_EVENT_ACTION_UP;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'press back or turn screen on'");
    }
}

static void
expand_notification_panel(struct sc_controller *controller)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'expand notification panel'");
    }
}

static void
expand_settings_panel(struct sc_controller *controller)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'expand settings panel'");
    }
}

static void
collapse_panels(struct sc_controller *controller)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'collapse notification panel'");
    }
}

static bool
get_device_clipboard(struct sc_controller *controller,
                     enum sc_copy_key copy_key)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_GET_CLIPBOARD;
    msg.get_clipboard.copy_key = copy_key;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'get device clipboard'");
        return false;
    }

    return true;
}

static bool
set_device_clipboard(struct sc_controller *controller, bool paste,
                     uint64_t sequence)
{
    char *text = SDL_GetClipboardText();
    if (!text)
    {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return false;
    }

    char *text_dup = strdup(text);
    SDL_free(text);
    if (!text_dup)
    {
        LOGW("Could not strdup input text");
        return false;
    }

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_SET_CLIPBOARD;
    msg.set_clipboard.sequence = sequence;
    msg.set_clipboard.text = text_dup;
    msg.set_clipboard.paste = paste;

    if (!sc_controller_push_msg(controller, &msg))
    {
        free(text_dup);
        LOGW("Could not request 'set device clipboard'");
        return false;
    }

    return true;
}

static void
set_screen_power_mode(struct sc_controller *controller,
                      enum sc_screen_power_mode mode)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_SET_SCREEN_POWER_MODE;
    msg.set_screen_power_mode.mode = mode;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request 'set screen power mode'");
    }
}

static void
switch_fps_counter_state(struct sc_fps_counter *fps_counter)
{
    // the started state can only be written from the current thread, so there
    // is no ToCToU issue
    if (sc_fps_counter_is_started(fps_counter))
    {
        sc_fps_counter_stop(fps_counter);
    }
    else
    {
        sc_fps_counter_start(fps_counter);
        // Any error is already logged
    }
}

static void
clipboard_paste(struct sc_controller *controller)
{
    char *text = SDL_GetClipboardText();
    if (!text)
    {
        LOGW("Could not get clipboard text: %s", SDL_GetError());
        return;
    }
    if (!*text)
    {
        // empty text
        SDL_free(text);
        return;
    }

    char *text_dup = strdup(text);
    SDL_free(text);
    if (!text_dup)
    {
        LOGW("Could not strdup input text");
        return;
    }

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_INJECT_TEXT;
    msg.inject_text.text = text_dup;
    if (!sc_controller_push_msg(controller, &msg))
    {
        free(text_dup);
        LOGW("Could not request 'paste clipboard'");
    }
}

static void
rotate_device(struct sc_controller *controller)
{
    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_ROTATE_DEVICE;

    if (!sc_controller_push_msg(controller, &msg))
    {
        LOGW("Could not request device rotation");
    }
}

static void
apply_orientation_transform(struct sc_screen *screen,
                            enum sc_orientation transform)
{
    enum sc_orientation new_orientation =
        sc_orientation_apply(screen->orientation, transform);
    sc_screen_set_orientation(screen, new_orientation);
}

static void
sc_input_manager_process_text_input(struct sc_input_manager *im,
                                    const SDL_TextInputEvent *event)
{
    if (!im->kp->ops->process_text)
    {
        // The key processor does not support text input
        return;
    }

    if (is_shortcut_mod(im, SDL_GetModState()))
    {
        // A shortcut must never generate text events
        return;
    }

    struct sc_text_event evt = {
        .text = event->text,
    };

    im->kp->ops->process_text(im->kp, &evt);
}

// 模拟虚拟手指
static bool
simulate_virtual_finger(struct sc_input_manager *im,
                        enum android_motionevent_action action,
                        struct sc_point point)
{
    bool up = action == AMOTION_EVENT_ACTION_UP;

    struct sc_control_msg msg;
    msg.type = SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT;
    msg.inject_touch_event.action = action;
    msg.inject_touch_event.position.screen_size = im->screen->frame_size;
    msg.inject_touch_event.position.point = point;
    msg.inject_touch_event.pointer_id =
        im->forward_all_clicks ? POINTER_ID_VIRTUAL_MOUSE
                               : POINTER_ID_VIRTUAL_FINGER;
    msg.inject_touch_event.pressure = up ? 0.0f : 1.0f; // 是否是抬起
    msg.inject_touch_event.action_button = 0;           // 按钮
    msg.inject_touch_event.buttons = 0;

    if (!sc_controller_push_msg(im->controller, &msg))
    {
        LOGW("Could not request 'inject virtual finger event'");
        return false;
    }

    return true;
}

static struct sc_point
inverse_point(struct sc_point point, struct sc_size size)
{
    point.x = size.width - point.x;
    point.y = size.height - point.y;
    return point;
}

void
sc_input_manager_send_touch_event(struct sc_input_manager *im,
                 float ix, float iy,
                 Uint32 type,
                 SDL_FingerID fingerId)
{
    int32_t w = im->screen->content_size.width;
    int32_t h = im->screen->content_size.height;
    enum sc_orientation orientation = im->screen->orientation;
    struct sc_point result;

    int32_t x = ix * w;
    int32_t y = iy * h;

    switch (orientation) {
        case SC_ORIENTATION_0:
            result.x = x;
            result.y = y;
            break;
        case SC_ORIENTATION_90:
            result.x = y;
            result.y = w - x;
            break;
        case SC_ORIENTATION_180:
            result.x = w - x;
            result.y = h - y;
            break;
        case SC_ORIENTATION_270:
            result.x = h - y;
            result.y = x;
            break;
        case SC_ORIENTATION_FLIP_0:
            result.x = w - x;
            result.y = y;
            break;
        case SC_ORIENTATION_FLIP_90:
            result.x = h - y;
            result.y = w - x;
            break;
        case SC_ORIENTATION_FLIP_180:
            result.x = x;
            result.y = h - y;
            break;
        default:
            assert(orientation == SC_ORIENTATION_FLIP_270);
            result.x = y;
            result.y = x;
            break;
    }

    struct sc_touch_event evt = {
        .position = {
            .screen_size = im->screen->frame_size,
            .point = result,
        },
        .action = sc_touch_action_from_sdl(type),
        .pointer_id = fingerId,
        .pressure = (rand() % 300 + 700) / 1000.0,
    };

    im->mp->ops->process_touch(im->mp, &evt);
}

static void
sc_input_manager_process_key(struct sc_input_manager *im,
                             const SDL_KeyboardEvent *event,
                             bool mouse_capture)
{
    // 如果--no-control，则controller为NULL
    struct sc_controller *controller = im->controller;

    SDL_Keycode keycode = event->keysym.sym;
    uint16_t mod = event->keysym.mod;
    bool down = event->type == SDL_KEYDOWN;
    bool ctrl = event->keysym.mod & KMOD_CTRL;
    bool shift = event->keysym.mod & KMOD_SHIFT;
    bool repeat = event->repeat; // 是否是长按的重复事件

    // bool smod = is_shortcut_mod(im, mod);
    // LOGI("keycode: %d, smod: %d", keycode, smod);

    if (down && !repeat)
    {
        if (keycode == im->last_keycode && mod == im->last_mod)
        {
            ++im->key_repeat;
        }
        else
        {
            im->key_repeat = 0;
            im->last_keycode = keycode;
            im->last_mod = mod;
        }
    }

    if (mouse_capture)
    {
        // 如果鼠标在手机里
        SDL_EventType action = down ? SDL_FINGERDOWN : SDL_FINGERUP;
        struct sc_fpsgame_keys *sfk = im->fpsgame_keys;
        switch (keycode)
        {
        case SDLK_w: // 前进
            if (!repeat)
            {
                int rx = sfk->rouletteX;
                if (rx < 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX - sfk->wheelLeftOffset,
                        sfk->wheelCenterposY - (down ? sfk->wheelUpOffset : 0),
                        SDL_FINGERMOTION,
                        1);
                }
                else if (rx > 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX + sfk->wheelRightOffset,
                        sfk->wheelCenterposY - (down ? sfk->wheelUpOffset : 0),
                        SDL_FINGERMOTION,
                        1);
                }
                else
                {
                    if (down)
                    {
                        sc_input_manager_send_touch_event(
                            im,
                            sfk->wheelCenterposX,
                            sfk->wheelCenterposY,
                            SDL_FINGERDOWN,
                            (SDL_FingerID)1);
                    }
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX,
                        sfk->wheelCenterposY - (down ? sfk->wheelUpOffset : 0),
                        down ? SDL_FINGERMOTION : SDL_FINGERUP,
                        1);
                }
                sfk->rouletteY += down ? 1 : -1;
            }
            return;
        case SDLK_s: // 后退
            if (!repeat)
            {
                int rx = sfk->rouletteX;
                if (rx < 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX - sfk->wheelLeftOffset,
                        sfk->wheelCenterposY + (down ? sfk->wheeldownOffset : 0),
                        SDL_FINGERMOTION,
                        1);
                }
                else if (rx > 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX + sfk->wheelRightOffset,
                        sfk->wheelCenterposY + (down ? sfk->wheeldownOffset : 0),
                        SDL_FINGERMOTION,
                        1);
                }
                else
                {
                    if (down)
                    {
                        sc_input_manager_send_touch_event(
                            im,
                            sfk->wheelCenterposX,
                            sfk->wheelCenterposY,
                            SDL_FINGERDOWN,
                            1);
                    }
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX,
                        sfk->wheelCenterposY + (down ? sfk->wheeldownOffset : 0),
                        down ? SDL_FINGERMOTION : SDL_FINGERUP,
                        1);
                }
                sfk->rouletteY -= down ? 1 : -1;
            }
            return;
        case SDLK_a: // 左
            if (!repeat)
            {
                int ry = sfk->rouletteY;
                if (ry < 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX - (down ? sfk->wheelLeftOffset : 0),
                        sfk->wheelCenterposY + sfk->wheeldownOffset,
                        SDL_FINGERMOTION,
                        1);
                }
                else if (ry > 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX - (down ? sfk->wheelLeftOffset : 0),
                        sfk->wheelCenterposY - sfk->wheelUpOffset,
                        SDL_FINGERMOTION,
                        1);
                }
                else
                {
                    if (down)
                    {
                        sc_input_manager_send_touch_event(
                            im,
                            sfk->wheelCenterposX,
                            sfk->wheelCenterposY,
                            SDL_FINGERDOWN,
                            1);
                    }
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX - (down ? sfk->wheelLeftOffset : 0),
                        sfk->wheelCenterposY,
                        down ? SDL_FINGERMOTION : SDL_FINGERUP,
                        1);
                }
                sfk->rouletteX -= down ? 1 : -1;
            }
            return;
        case SDLK_d: // 右
            if (!repeat)
            {
                int ry = sfk->rouletteY;
                if (ry < 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX + (down ? sfk->wheelRightOffset : 0),
                        sfk->wheelCenterposY + sfk->wheeldownOffset,
                        SDL_FINGERMOTION,
                        1);
                }
                else if (ry > 0)
                {
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX + (down ? sfk->wheelRightOffset : 0),
                        sfk->wheelCenterposY - sfk->wheelUpOffset,
                        SDL_FINGERMOTION,
                        1);
                }
                else
                {
                    if (down)
                    {
                        sc_input_manager_send_touch_event(
                            im,
                            sfk->wheelCenterposX,
                            sfk->wheelCenterposY,
                            SDL_FINGERDOWN,
                            1);
                    }
                    sc_input_manager_send_touch_event(
                        im,
                        sfk->wheelCenterposX + (down ? sfk->wheelRightOffset : 0),
                        sfk->wheelCenterposY,
                        down ? SDL_FINGERMOTION : SDL_FINGERUP,
                        1);
                }
                sfk->rouletteX += down ? 1 : -1;
            }
            return;
        case SDLK_q: // 左探头
            if (!repeat)
            {
                sc_input_manager_send_touch_event(
                    im,
                    sfk->leftProbeX,
                    sfk->leftProbeY,
                    action,
                    3);
            }
            return;
        case SDLK_e: // 右探头
            if (!repeat)
            {
                sc_input_manager_send_touch_event(
                    im,
                    sfk->rightProbeX,
                    sfk->rightProbeY,
                    action,
                    3);
            }
            return;
        case SDLK_EQUALS: // 自动跑
            if (!repeat)
            {
                sc_input_manager_send_touch_event(
                    im,
                    sfk->autoRunX,
                    sfk->autoRunY,
                    action,
                    3);
            }
            return;
        case SDLK_SPACE: // 跳
            if (!repeat)
            {
                sc_input_manager_send_touch_event(
                    im,
                    sfk->jumpX,
                    sfk->jumpY,
                    action,
                    11);
            }
            return;
        case SDLK_m: // 地图
            if (!repeat)
            {
                sc_input_manager_send_touch_event(
                    im,
                    sfk->mapX,
                    sfk->mapY,
                    action,
                    10);
            }
            return;
        case SDLK_TAB: // 背包
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->knapsackX, sfk->knapsackY, action, 9);
            }
            return;
        case SDLK_z: // 趴
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->dropX, sfk->dropY, action, 8);
            }
            return;
        case SDLK_c: // 蹲
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->squatX, sfk->squatY, action, 7);
            }
            return;
        case SDLK_r: // 装弹
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->reloadX, sfk->reloadY, action, 6);
            }
            return;
        case SDLK_f: // 拾取1
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->pickup1X, sfk->pickup1Y, action, 3);
            }
            return;
        case SDLK_g: // 拾取2
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->pickup2X, sfk->pickup2Y, action, 3);
            }
            return;
        case SDLK_h: // 拾取3
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->pickup3X, sfk->pickup3Y, action, 3);
            }
            return;
        case SDLK_1: // 换枪1
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->switchGun1X, sfk->switchGun1Y, action, 4);
            }
            return;
        case SDLK_2: // 换枪2
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->switchGun2X, sfk->switchGun2Y, action, 5);
            }
            return;
        case SDLK_3: // 打药
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->medicineX, sfk->medicineY, action, 3);
            }
            return;
        case SDLK_4: // 手雷
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->fragX, sfk->fragY, action, 3);
            }
            return;
        case SDLK_5: // 下车
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->getOffCarX, sfk->getOffCarY, action, 3);
            }
            return;
        case SDLK_6: // 救人
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->helpX, sfk->helpY, action, 3);
            }
            return;
        case SDLK_7: // 上车
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->getOnCarX, sfk->getOnCarY, action, 3);
            }
            return;
        case SDLK_x: // 开门
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->openDoorX, sfk->openDoorY, action, 3);
            }
            return;
        case SDLK_t: // 舔包
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->lickBagX, sfk->lickBagY, action, 3);
            }
            return;
        case SDLK_b: // 标点
            if (!repeat)
            {
                sc_input_manager_send_touch_event(im, sfk->punctuationX, sfk->punctuationY, action, 3);
            }
            return;
        }
        return;
    }
    else
    {

        // The shortcut modifier is pressed
        // if (smod || true) {
        enum sc_action action = down ? SC_ACTION_DOWN : SC_ACTION_UP;
        switch (keycode)
        {
        case SDLK_h: // home
            if (controller && !shift && !repeat)
            {
                action_home(controller, action);
            }
            return;
        case SDLK_b: // fall-through
        case SDLK_BACKSPACE:
            if (controller && !shift && !repeat)
            {
                action_back(controller, action);
            }
            return;
        case SDLK_s: // 任务视图
            if (controller && !shift && !repeat)
            {
                action_app_switch(controller, action);
            }
            return;
        case SDLK_m: // 菜单 任务视图
            if (controller && !shift && !repeat)
            {
                action_menu(controller, action);
            }
            return;
        case SDLK_p: // 息屏
            if (controller && !shift && !repeat)
            {
                action_power(controller, action);
            }
            return;
        case SDLK_o: // 手机黑屏
            if (controller && !repeat && down)
            {
                enum sc_screen_power_mode mode = shift
                                                     ? SC_SCREEN_POWER_MODE_NORMAL
                                                     : SC_SCREEN_POWER_MODE_OFF;
                set_screen_power_mode(controller, mode);
            }
            return;
        case SDLK_DOWN:
            if (shift)
            { // 上下翻转
                if (!repeat & down)
                {
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_FLIP_180);
                }
            }
            else if (controller)
            { // 音量减
                // forward repeated events
                action_volume_down(controller, action);
            }
            return;
        case SDLK_UP:
            if (shift)
            { // 上下翻转
                if (!repeat & down)
                {
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_FLIP_180);
                }
            }
            else if (controller)
            { // 音量加
                // forward repeated events
                action_volume_up(controller, action);
            }
            return;
        case SDLK_LEFT:
            if (!repeat && down)
            {
                if (shift)
                { // 左右翻转
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_FLIP_0);
                }
                else
                { // 逆时针旋转
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_270);
                }
            }
            return;
        case SDLK_RIGHT:
            if (!repeat && down)
            {
                if (shift)
                { // 左右翻转
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_FLIP_0);
                }
                else
                { // 顺时针旋转
                    apply_orientation_transform(im->screen,
                                                SC_ORIENTATION_90);
                }
            }
            return;
        case SDLK_c: // 获取设备剪切板
            if (controller && !shift && !repeat && down)
            {
                get_device_clipboard(controller, SC_COPY_KEY_COPY);
            }
            return;
        case SDLK_x: // 获取设备剪切板
            if (controller && !shift && !repeat && down)
            {
                get_device_clipboard(controller, SC_COPY_KEY_CUT);
            }
            return;
        case SDLK_v:
            if (controller && !repeat && down)
            {
                if (shift || im->legacy_paste)
                { // 剪贴板粘贴
                    // inject the text as input events
                    clipboard_paste(controller);
                }
                else
                {
                    // store the text in the device clipboard and paste,
                    // without requesting an acknowledgment
                    set_device_clipboard(controller, true,
                                         SC_SEQUENCE_INVALID);
                }
            }
            return;
        case SDLK_f: // 全屏
            if (!shift && !repeat && down)
            {
                sc_screen_switch_fullscreen(im->screen);
            }
            return;
        case SDLK_w:
            if (!shift && !repeat && down)
            {
                sc_screen_resize_to_fit(im->screen);
            }
            return;
        case SDLK_g:
            if (!shift && !repeat && down)
            {
                sc_screen_resize_to_pixel_perfect(im->screen);
            }
            return;
        case SDLK_i: // 获取fps
            if (!shift && !repeat && down)
            {
                switch_fps_counter_state(&im->screen->fps_counter);
            }
            return;
        case SDLK_n:
            if (controller && !repeat && down)
            {
                if (shift)
                { // 取消下拉菜单
                    collapse_panels(controller);
                }
                else if (im->key_repeat == 0)
                {
                    expand_notification_panel(controller);
                }
                else
                { // 下拉菜单
                    expand_settings_panel(controller);
                }
            }
            return;
        case SDLK_r:
            if (controller && !shift && !repeat && down)
            {
                rotate_device(controller);
            }
            return;
        }

        return;
    }

    if (!controller)
    {
        return;
    }

    uint64_t ack_to_wait = SC_SEQUENCE_INVALID;
    bool is_ctrl_v = ctrl && !shift && keycode == SDLK_v && down && !repeat;
    if (im->clipboard_autosync && is_ctrl_v)
    {
        if (im->legacy_paste)
        {
            // 将文本作为输入事件注入
            clipboard_paste(controller);
            return;
        }

        // 仅在必要时请求确认
        uint64_t sequence = im->kp->async_paste ? im->next_sequence
                                                : SC_SEQUENCE_INVALID;

        // 在发送Ctrl+v之前，将计算机剪贴板同步到设备剪贴板，以允许无缝复制粘贴。
        bool ok = set_device_clipboard(controller, false, sequence);
        if (!ok)
        {
            LOGW("Clipboard could not be synchronized, Ctrl+v not injected");
            return;
        }

        if (im->kp->async_paste)
        {
            // 按键处理器必须等待此ack，然后才能注入Ctrl+v
            ack_to_wait = sequence;
            // 仅当请求成功时递增
            ++im->next_sequence;
        }
    }

    struct sc_key_event evt = {
        .action = sc_action_from_sdl_keyboard_type(event->type),
        .keycode = sc_keycode_from_sdl(event->keysym.sym),
        .scancode = sc_scancode_from_sdl(event->keysym.scancode),
        .repeat = event->repeat,
        .mods_state = sc_mods_state_from_sdl(event->keysym.mod),
    };

    assert(im->kp->ops->process_key);
    im->kp->ops->process_key(im->kp, &evt, ack_to_wait);
}

static void
sc_input_manager_process_mouse_motion(struct sc_input_manager *im,
                                      const SDL_MouseMotionEvent *event,
                                      bool mouse_capture)
{
    if (mouse_capture)
    {
        struct sc_fpsgame_keys *sfk = im->fpsgame_keys;
        float px = sfk->pointX + sfk->speedRatioX * (float)event->xrel;
        float py = sfk->pointY + sfk->speedRatioY * (float)event->yrel;
        bool flag = px < 0 || px > 1 || py < 0 || py > 1;
        if (flag)
        {
            sc_input_manager_send_touch_event(im, sfk->pointX, sfk->pointY, SDL_FINGERUP, 2);
            sfk->pointX = 0.55, sfk->pointY = 0.4;
        } else {
            sfk->pointX = px, sfk->pointY = py;
        }
        sc_input_manager_send_touch_event(im, sfk->pointX, sfk->pointY, flag ? SDL_FINGERDOWN : SDL_FINGERMOTION, 2);
        return;
    }
    struct sc_mouse_motion_event evt = {
        .position = {
            .screen_size = im->screen->frame_size,
            .point = sc_screen_convert_window_to_frame_coords(im->screen,
                                                              event->x,
                                                              event->y),
        },
        .pointer_id = im->forward_all_clicks ? POINTER_ID_MOUSE : POINTER_ID_GENERIC_FINGER,
        .xrel = event->xrel,
        .yrel = event->yrel,
        .buttons_state = sc_mouse_buttons_state_from_sdl(event->state, im->forward_all_clicks),
    };

    assert(im->mp->ops->process_mouse_motion);
    im->mp->ops->process_mouse_motion(im->mp, &evt);

    // vfinger决不能在相对模式中使用
    assert(!im->mp->relative_mode || !im->vfinger_down);

    if (im->vfinger_down)
    {
        assert(!im->mp->relative_mode); // assert one more time
        // 转换坐标
        struct sc_point mouse =
            sc_screen_convert_window_to_frame_coords(im->screen, event->x,
                                                     event->y);
        struct sc_point vfinger = inverse_point(mouse, im->screen->frame_size);
        simulate_virtual_finger(im, AMOTION_EVENT_ACTION_MOVE, vfinger);
    }
}

static void
sc_input_manager_process_touch(struct sc_input_manager *im,
                               const SDL_TouchFingerEvent *event)
{
    int dw;
    int dh;
    SDL_GL_GetDrawableSize(im->screen->window, &dw, &dh);

    // SDL触摸事件坐标在[0:1]范围内标准化
    int32_t x = event->x * dw;
    int32_t y = event->y * dh;

    struct sc_touch_event evt = {
        .position = {
            .screen_size = im->screen->frame_size,
            .point =
                sc_screen_convert_drawable_to_frame_coords(im->screen, x, y),
        },
        .action = sc_touch_action_from_sdl(event->type),
        .pointer_id = event->fingerId,
        .pressure = event->pressure,
    };

    im->mp->ops->process_touch(im->mp, &evt);
}

static void
sc_input_manager_process_mouse_button(struct sc_input_manager *im,
                                      const SDL_MouseButtonEvent *event,
                                      bool mouse_capture)
{
    struct sc_controller *controller = im->controller;

    bool down = event->type == SDL_MOUSEBUTTONDOWN;

    if (mouse_capture) {
        struct sc_fpsgame_keys *sfk = im->fpsgame_keys;
        SDL_EventType action = down ? SDL_FINGERDOWN : SDL_FINGERUP;
        if (event->button == SDL_BUTTON_LEFT) {
            sc_input_manager_send_touch_event(im, sfk->fireX, sfk->fireY, action, 12);
        } else if (event->button == SDL_BUTTON_RIGHT)
        {
            sc_input_manager_send_touch_event(im, sfk->openMirrorX, sfk->openMirrorY, action, 13);
        } else if (event->button == SDL_BUTTON_MIDDLE)
        {
            sc_input_manager_send_touch_event(im, sfk->punctuationX, sfk->punctuationY, action, 14);
        }
        return;
    }

    if (!im->forward_all_clicks)
    {
        if (controller)
        {
            enum sc_action action = down ? SC_ACTION_DOWN : SC_ACTION_UP;

            if (event->button == SDL_BUTTON_X1)
            {
                action_app_switch(controller, action);
                return;
            }
            if (event->button == SDL_BUTTON_X2 && down)
            {
                if (event->clicks < 2)
                {
                    expand_notification_panel(controller);
                }
                else
                {
                    expand_settings_panel(controller);
                }
                return;
            }
            if (event->button == SDL_BUTTON_RIGHT)
            {
                press_back_or_turn_screen_on(controller, action);
                return;
            }
            if (event->button == SDL_BUTTON_MIDDLE)
            {
                action_home(controller, action);
                return;
            }
        }

        // 双击黑色边框调整大小以适应设备屏幕
        if (event->button == SDL_BUTTON_LEFT && event->clicks == 2)
        {
            int32_t x = event->x;
            int32_t y = event->y;
            sc_screen_hidpi_scale_coords(im->screen, &x, &y);
            SDL_Rect *r = &im->screen->rect;
            bool outside = x < r->x || x >= r->x + r->w || y < r->y || y >= r->y + r->h;
            if (outside)
            {
                if (down)
                {
                    sc_screen_resize_to_fit(im->screen);
                }
                return;
            }
        }
        // 否则，将单击事件发送到设备
    }

    if (!controller)
    {
        return;
    }

    uint32_t sdl_buttons_state = SDL_GetMouseState(NULL, NULL);

    struct sc_mouse_click_event evt = {
        .position = {
            .screen_size = im->screen->frame_size,
            .point = sc_screen_convert_window_to_frame_coords(im->screen,
                                                              event->x,
                                                              event->y),
        },
        .action = sc_action_from_sdl_mousebutton_type(event->type),
        .button = sc_mouse_button_from_sdl(event->button),
        .pointer_id = im->forward_all_clicks ? POINTER_ID_MOUSE : POINTER_ID_GENERIC_FINGER,
        .buttons_state = sc_mouse_buttons_state_from_sdl(sdl_buttons_state, im->forward_all_clicks),
    };

    assert(im->mp->ops->process_mouse_click);
    im->mp->ops->process_mouse_click(im->mp, &evt);

    if (im->mp->relative_mode)
    {
        assert(!im->vfinger_down); // vfinger不能在相对模式中使用
        // 无缩放模拟
        return;
    }

    // 双指缩放 模拟.
    //
    // 如果在按下左点击按钮时按住Ctrl，则启用缩放模式：
    // 在每次鼠标事件上，直到释放左点击按钮为止，
    // 生成一个额外的“虚拟手指”事件，其位置通过屏幕中心反转。
    //
    // 换句话说，旋转/缩放的中心就是屏幕的中心。
#define CTRL_PRESSED (SDL_GetModState() & (KMOD_LCTRL | KMOD_RCTRL))
    if (event->button == SDL_BUTTON_LEFT &&
        ((down && !im->vfinger_down && CTRL_PRESSED) ||
         (!down && im->vfinger_down)))
    {
        struct sc_point mouse =
            sc_screen_convert_window_to_frame_coords(im->screen, event->x,
                                                     event->y);
        struct sc_point vfinger = inverse_point(mouse, im->screen->frame_size);
        enum android_motionevent_action action = down
                                                     ? AMOTION_EVENT_ACTION_DOWN
                                                     : AMOTION_EVENT_ACTION_UP;
        if (!simulate_virtual_finger(im, action, vfinger))
        {
            return;
        }
        im->vfinger_down = down;
    }
}

static void
sc_input_manager_process_mouse_wheel(struct sc_input_manager *im,
                                     const SDL_MouseWheelEvent *event)
{
    if (!im->mp->ops->process_mouse_scroll)
    {
        // The mouse processor does not support scroll events
        return;
    }

    // mouse_x and mouse_y are expressed in pixels relative to the window
    int mouse_x;
    int mouse_y;
    uint32_t buttons = SDL_GetMouseState(&mouse_x, &mouse_y);

    struct sc_mouse_scroll_event evt =
    {
        .position = {
            .screen_size = im->screen->frame_size,
            .point = sc_screen_convert_window_to_frame_coords(im->screen,
                                                              mouse_x, mouse_y),
        },
#if SDL_VERSION_ATLEAST(2, 0, 18)
        .hscroll = CLAMP(event->preciseX, -1.0f, 1.0f),
        .vscroll = CLAMP(event->preciseY, -1.0f, 1.0f),
#else
        .hscroll = CLAMP(event->x, -1, 1),
        .vscroll = CLAMP(event->y, -1, 1),
#endif
        .buttons_state = sc_mouse_buttons_state_from_sdl(buttons, im->forward_all_clicks),
    };

    im->mp->ops->process_mouse_scroll(im->mp, &evt);
}

static bool
is_apk(const char *file)
{
    const char *ext = strrchr(file, '.');
    return ext && !strcmp(ext, ".apk");
}

static void
sc_input_manager_process_file(struct sc_input_manager *im,
                              const SDL_DropEvent *event)
{
    char *file = strdup(event->file);
    SDL_free(event->file);
    if (!file)
    {
        LOG_OOM();
        return;
    }

    enum sc_file_pusher_action action;
    if (is_apk(file))
    {
        action = SC_FILE_PUSHER_ACTION_INSTALL_APK;
    }
    else
    {
        action = SC_FILE_PUSHER_ACTION_PUSH_FILE;
    }
    bool ok = sc_file_pusher_request(im->fp, action, file);
    if (!ok)
    {
        free(file);
    }
}

void sc_input_manager_handle_event(struct sc_input_manager *im,
                                   const SDL_Event *event,
                                   bool mouse_capture)
{
    bool control = im->controller;
    switch (event->type)
    {
    case SDL_TEXTINPUT: // 文本输入
        if (!control)
        {
            break;
        }
        sc_input_manager_process_text_input(im, &event->text);
        break;
    case SDL_KEYDOWN: // 键盘按键
    case SDL_KEYUP:
        sc_input_manager_process_key(im, &event->key, mouse_capture);
        break;
    case SDL_MOUSEMOTION: // 鼠标移动
        sc_input_manager_process_mouse_motion(im, &event->motion, mouse_capture);
        break;
    case SDL_MOUSEWHEEL: // 鼠标滚轮
        if (!control)
        {
            break;
        }
        sc_input_manager_process_mouse_wheel(im, &event->wheel);
        break;
    case SDL_MOUSEBUTTONDOWN: // 鼠标按下抬起
    case SDL_MOUSEBUTTONUP:
        // some mouse events do not interact with the device, so process
        // the event even if control is disabled
        sc_input_manager_process_mouse_button(im, &event->button, mouse_capture);
        break;
    case SDL_FINGERMOTION: // 手指
    case SDL_FINGERDOWN:
    case SDL_FINGERUP:
        if (!control)
        {
            break;
        }
        sc_input_manager_process_touch(im, &event->tfinger);
        break;
    case SDL_DROPFILE:
    { // 拖放文件
        if (!control)
        {
            break;
        }
        sc_input_manager_process_file(im, &event->drop);
    }
    }
}
