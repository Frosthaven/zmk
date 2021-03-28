/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <device.h>
#include <init.h>
#include <kernel.h>
#include <settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <logging/log.h>

#include <drivers/led_strip.h>
#include <drivers/ext_power.h>

#include <zmk/rgb_underglow.h>

#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_LABEL DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)

#define HUE_MAX 360
#define SAT_MAX 100
#define BRT_MAX 100

enum rgb_underglow_effect {
    UNDERGLOW_EFFECT_CUSTOM,
    //UNDERGLOW_EFFECT_LAYER,
    UNDERGLOW_EFFECT_SOLID,
    UNDERGLOW_EFFECT_BREATHE,
    UNDERGLOW_EFFECT_SPECTRUM,
    UNDERGLOW_EFFECT_SWIRL,
    UNDERGLOW_EFFECT_NUMBER // Used to track number of underglow effects
};

struct rgb_underglow_state {
    struct zmk_led_hsb color;
    uint8_t animation_speed;
    uint8_t current_effect;
    uint16_t animation_step;
    uint16_t active_layer;
    bool on;
};

static const struct device *led_strip;

static struct led_rgb pixels[STRIP_NUM_PIXELS];

static struct rgb_underglow_state state;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
static const struct device *ext_power;
#endif

static struct led_rgb hsb_to_rgb(struct zmk_led_hsb hsb) {
    double r, g, b;

    uint8_t i = hsb.h / 60;
    double v = hsb.b / ((float)BRT_MAX);
    double s = hsb.s / ((float)SAT_MAX);
    double f = hsb.h / ((float)HUE_MAX) * 6 - i;
    double p = v * (1 - s);
    double q = v * (1 - f * s);
    double t = v * (1 - (1 - f) * s);

    switch (i % 6) {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    struct led_rgb rgb = {r : r * 255, g : g * 255, b : b * 255};

    return rgb;
}

static void zmk_rgb_underglow_effect_custom() {
    struct zmk_led_hsb hsb = state.color;
    hsb.h = 230; // azure
    hsb.s = 100;    
    //outer cluster
    pixels[10] = hsb_to_rgb(hsb);
    pixels[16] = hsb_to_rgb(hsb);
    pixels[17] = hsb_to_rgb(hsb);
    pixels[18] = hsb_to_rgb(hsb);
    pixels[19] = hsb_to_rgb(hsb);
    pixels[20] = hsb_to_rgb(hsb);
    pixels[21] = hsb_to_rgb(hsb);
    pixels[22] = hsb_to_rgb(hsb);
    pixels[23] = hsb_to_rgb(hsb);
    pixels[24] = hsb_to_rgb(hsb);
    pixels[25] = hsb_to_rgb(hsb);
    pixels[26] = hsb_to_rgb(hsb);

    hsb.h = 345; // rose

    //underglow
    pixels[0] = hsb_to_rgb(hsb);
    pixels[1] = hsb_to_rgb(hsb);
    pixels[2] = hsb_to_rgb(hsb);
    pixels[3] = hsb_to_rgb(hsb);
    pixels[4] = hsb_to_rgb(hsb);
    pixels[5] = hsb_to_rgb(hsb);

    //inner cluster
    pixels[6] = hsb_to_rgb(hsb);
    pixels[7] = hsb_to_rgb(hsb);
    pixels[8] = hsb_to_rgb(hsb);
    pixels[9] = hsb_to_rgb(hsb);
    pixels[11] = hsb_to_rgb(hsb);
    pixels[12] = hsb_to_rgb(hsb);
    pixels[13] = hsb_to_rgb(hsb);
    pixels[14] = hsb_to_rgb(hsb);
    pixels[15] = hsb_to_rgb(hsb);
}

/*
static void zmk_rgb_underglow_effect_layer() {
    if (state.current_effect == UNDERGLOW_EFFECT_LAYER) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = 1;
        switch(state.active_layer) {
            case 0:
                hsb.h = 220;
                break;
            case 1:
                hsb.h = 30;
                break;
            case 2:
                hsb.h = 60;
                break;
            case 3:
                hsb.h = 90;
                break;
            case 4:
                hsb.h = 120;
                break;
            case 5:
                hsb.h = 150;
                break;
            case 6:
                hsb.h = 180;
                break;
            case 7:
                hsb.h = 1;
                break;
        }


        for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
            pixels[i] = hsb_to_rgb(hsb);
        }
    }
}
*/

static void zmk_rgb_underglow_effect_solid() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = hsb_to_rgb(state.color);
    }
}

static void zmk_rgb_underglow_effect_breathe() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.b = abs(state.animation_step - 1200) / 12;

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed * 10;

    if (state.animation_step > 2400) {
        state.animation_step = 0;
    }
}

static void zmk_rgb_underglow_effect_spectrum() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = state.animation_step;

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed;
    state.animation_step = state.animation_step % HUE_MAX;
}

static void zmk_rgb_underglow_effect_swirl() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        struct zmk_led_hsb hsb = state.color;
        hsb.h = (HUE_MAX / STRIP_NUM_PIXELS * i + state.animation_step) % HUE_MAX;

        pixels[i] = hsb_to_rgb(hsb);
    }

    state.animation_step += state.animation_speed * 2;
    state.animation_step = state.animation_step % HUE_MAX;
}

static void zmk_rgb_underglow_tick(struct k_work *work) {
    switch (state.current_effect) {
    case UNDERGLOW_EFFECT_CUSTOM:
        zmk_rgb_underglow_effect_custom();
        break;
    /*
    case UNDERGLOW_EFFECT_LAYER:
        zmk_rgb_underglow_effect_layer(); // @todo need keymap.h for layer state but that breaks things from locality pr by forcing it in cmakelists.txt
        break;
    */
    case UNDERGLOW_EFFECT_SOLID:
        zmk_rgb_underglow_effect_solid();
        break;
    case UNDERGLOW_EFFECT_BREATHE:
        zmk_rgb_underglow_effect_breathe();
        break;
    case UNDERGLOW_EFFECT_SPECTRUM:
        zmk_rgb_underglow_effect_spectrum();
        break;
    case UNDERGLOW_EFFECT_SWIRL:
        zmk_rgb_underglow_effect_swirl();
        break;
    }

    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

K_WORK_DEFINE(underglow_work, zmk_rgb_underglow_tick);

static void zmk_rgb_underglow_tick_handler(struct k_timer *timer) {
    if (!state.on) {
        return;
    }

    k_work_submit(&underglow_work);
}

K_TIMER_DEFINE(underglow_tick, zmk_rgb_underglow_tick_handler, NULL);

#if IS_ENABLED(CONFIG_SETTINGS)
static int rgb_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(state)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &state, sizeof(state));
        if (rc >= 0) {
            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

struct settings_handler rgb_conf = {.name = "rgb/underglow", .h_set = rgb_settings_set};

static void zmk_rgb_underglow_save_state_work() {
    settings_save_one("rgb/underglow/state", &state, sizeof(state));
}

static struct k_delayed_work underglow_save_work;
#endif

static int zmk_rgb_underglow_init(const struct device *_arg) {
    led_strip = device_get_binding(STRIP_LABEL);
    if (led_strip) {
        LOG_INF("Found LED strip device %s", STRIP_LABEL);
    } else {
        LOG_ERR("LED strip device %s not found", STRIP_LABEL);
        return -EINVAL;
    }

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    ext_power = device_get_binding("EXT_POWER");
    if (ext_power == NULL) {
        LOG_ERR("Unable to retrieve ext_power device: EXT_POWER");
    }
#endif

    state = (struct rgb_underglow_state){
        color : {
            h : CONFIG_ZMK_RGB_UNDERGLOW_HUE_START,
            s : CONFIG_ZMK_RGB_UNDERGLOW_SAT_START,
            b : CONFIG_ZMK_RGB_UNDERGLOW_BRT_START,
        },
        animation_speed : CONFIG_ZMK_RGB_UNDERGLOW_SPD_START,
        current_effect : CONFIG_ZMK_RGB_UNDERGLOW_EFF_START,
        animation_step : 0,
        active_layer: 0,
        on : IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_ON_START)
    };

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    int err = settings_register(&rgb_conf);
    if (err) {
        LOG_ERR("Failed to register the ext_power settings handler (err %d)", err);
        return err;
    }

    k_delayed_work_init(&underglow_save_work, zmk_rgb_underglow_save_state_work);

    settings_load_subtree("rgb/underglow");
#endif

    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));

    return 0;
}

int zmk_rgb_underglow_save_state() {
#if IS_ENABLED(CONFIG_SETTINGS)
    k_delayed_work_cancel(&underglow_save_work);
    return k_delayed_work_submit(&underglow_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#else
    return 0;
#endif
}

int zmk_rgb_underglow_get_state(bool *on_off) {
    if (!led_strip)
        return -ENODEV;

    *on_off = state.on;
    return 0;
}

int zmk_rgb_underglow_on() {
    if (!led_strip)
        return -ENODEV;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (ext_power != NULL) {
        int rc = ext_power_enable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to enable EXT_POWER: %d", rc);
        }
    }
#endif

    state.on = true;
    state.animation_step = 0;
    k_timer_start(&underglow_tick, K_NO_WAIT, K_MSEC(50));

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_off() {
    if (!led_strip)
        return -ENODEV;

#if IS_ENABLED(CONFIG_ZMK_RGB_UNDERGLOW_EXT_POWER)
    if (ext_power != NULL) {
        int rc = ext_power_disable(ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to disable EXT_POWER: %d", rc);
        }
    }
#endif

    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
    }

    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);

    k_timer_stop(&underglow_tick);
    state.on = false;

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_cycle_effect(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.current_effect += UNDERGLOW_EFFECT_NUMBER + direction;
    state.current_effect %= UNDERGLOW_EFFECT_NUMBER;

    state.animation_step = 0;

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_toggle() {
    return state.on ? zmk_rgb_underglow_off() : zmk_rgb_underglow_on();
}

int zmk_rgb_underglow_set_hsb(struct zmk_led_hsb color) {
    if (color.h > HUE_MAX || color.s > SAT_MAX || color.b > BRT_MAX) {
        return -ENOTSUP;
    }

    state.color = color;

    return 0;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_hue(int direction) {
    struct zmk_led_hsb color = state.color;

    color.h += HUE_MAX + (direction * CONFIG_ZMK_RGB_UNDERGLOW_HUE_STEP);
    color.h %= HUE_MAX;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_sat(int direction) {
    struct zmk_led_hsb color = state.color;

    int s = color.s + (direction * CONFIG_ZMK_RGB_UNDERGLOW_SAT_STEP);
    if (s < 0) {
        s = 0;
    } else if (s > SAT_MAX) {
        s = SAT_MAX;
    }
    color.s = s;

    return color;
}

struct zmk_led_hsb zmk_rgb_underglow_calc_brt(int direction) {
    struct zmk_led_hsb color = state.color;

    int b = color.b + (direction * CONFIG_ZMK_RGB_UNDERGLOW_BRT_STEP);
    if (b < 0) {
        b = 0;
    } else if (b > BRT_MAX) {
        b = BRT_MAX;
    }
    color.b = b;

    return color;
}

int zmk_rgb_underglow_change_hue(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_hue(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_sat(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_sat(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_brt(int direction) {
    if (!led_strip)
        return -ENODEV;

    state.color = zmk_rgb_underglow_calc_brt(direction);

    return zmk_rgb_underglow_save_state();
}

int zmk_rgb_underglow_change_spd(int direction) {
    if (!led_strip)
        return -ENODEV;

    if (state.animation_speed == 1 && direction < 0) {
        return 0;
    }

    state.animation_speed += direction;

    if (state.animation_speed > 5) {
        state.animation_speed = 5;
    }

    return zmk_rgb_underglow_save_state();
}

int tokenize(const char **input, const char *delimmter, char buf[], int buf_size) {
    if(**input == 0)
        return 0;

    int i = strcspn(*input, delimmter);
    strncpy(buf, *input, i > buf_size ? buf_size : i);
    buf[(i > buf_size ? buf_size - 1: i)] = 0;

    *input += i + (i != strlen(*input));

    return 1;
}

//void set_layer_index() {
    //zmk_rgb_underglow_effect_layer(zmk_keymap_highest_layer_active());
    //led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);


    /*
    switch (active_layer_index) {
        case 1:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_1;
            break;
        case 2:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_2;
            break;
        case 3:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_3;
            break;
        case 4:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_4;
            break;
        case 5:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_5;
            hsb.h = 120;
            for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                pixels[i] = hsb_to_rgb(hsb);
            }
            break;
        case 6:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_6;
            break;
        case 7:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_7;
            hsb.h = 10;
            for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                pixels[i] = hsb_to_rgb(hsb);
            }
            break;
        case 8:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_8;
            break;
        case 9:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_9;
            break;
        default:
            selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_0;
            hsb.h = 60;
            for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                pixels[i] = hsb_to_rgb(hsb);
            }
            break;
    }

    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    */
//}
//void set_layer_color() {
    //int active_layer_index = zmk_keymap_highest_layer_active();
/*
    LOG_DBG("Layer changed to %i", active_layer_index);

    const char *layer_label = zmk_keymap_layer_label(active_layer_index);
    if (layer_label == NULL) {
        char text[6] = {};

        sprintf(text, LV_SYMBOL_KEYBOARD "%i", active_layer_index);

        lv_label_set_text(label, text);
    } else {
        char text[12] = {};

        snprintf(text, 12, LV_SYMBOL_KEYBOARD "%s", layer_label);

        lv_label_set_text(label, text);
    }
*/
//}

//int rgb_layer_status_listener(const zmk_event_t *eh) {
    //uint8_t active_layer = zmk_keymap_highest_layer_active();
//    return 0;
    //set_layer_index();

/*  
    if (state.current_effect == UNDERGLOW_EFFECT_LAYER) {
        const char * selected_layer;
        struct zmk_led_hsb hsb = state.color;
        hsb.h = 60;
        hsb.s = 100;
        hsb.b = 35;
        switch (5) {
            case 1:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_1;
                break;
            case 2:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_2;
                break;
            case 3:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_3;
                break;
            case 4:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_4;
                break;
            case 5:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_5;
                hsb.h = 120;
                for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                    pixels[i] = hsb_to_rgb(hsb);
                }
                break;
            case 6:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_6;
                break;
            case 7:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_7;
                hsb.h = 10;
                for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                    pixels[i] = hsb_to_rgb(hsb);
                }
                break;
            case 8:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_8;
                break;
            case 9:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_9;
                break;
            default:
                selected_layer = CONFIG_ZMK_RGB_LAYER_HSB_0;
                hsb.h = 60;
                for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
                    pixels[i] = hsb_to_rgb(hsb);
                }
                break;
        }

        char buf[STRIP_NUM_PIXELS];

        int led_step = 0;
        while (tokenize(&selected_layer, ",", buf, sizeof(buf)))
        {
            //each led sequence stored in %s as hhh.sss.bbb
            const char * hsb_str = "%s";
            char bufsub[2];
            struct zmk_led_hsb hsb = state.color;

            int stat_step = 0;
            while(tokenize(&hsb_str, ".", bufsub, sizeof(bufsub)))
            {
                if (stat_step == 0) {
                    hsb.h = (int) strtol("%s", (char **)NULL, 10);
                } else if (stat_step == 1) {
                    hsb.s = (int) strtol("%s", (char **)NULL, 10);
                } else if (stat_step == 2) {
                    hsb.b = (int) strtol("%s", (char **)NULL, 10);
                }
                stat_step++;
            }

            hsb.h = 60;
            hsb.s = 100;
            hsb.b = 35;
            pixels[led_step] = hsb_to_rgb(hsb);
            led_step++;
        }
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    }
*/
//}

//ZMK_LISTENER(rgb_layer_status, rgb_layer_status_listener);
//ZMK_SUBSCRIPTION(rgb_layer_status, zmk_layer_state_changed);

#if ((!CONFIG_ZMK_SPLIT) || CONFIG_ZMK_SPLIT_BLE_ROLE_CENTRAL)
int rgb_layer_change_listener(const zmk_event_t *eh) {
    if (!state.on) {
        return 0;
    }

    state.active_layer = zmk_keymap_highest_layer_active();

    #if CONFIG_ZMK_SPLIT
        // @todo sync state.active_layer to peripheral
    #endif

    /* access specific event data like this:
    const struct zmk_layer_state_changed *layer_ev;
    layer_ev = as_zmk_layer_state_changed(eh);
    active_layer = layer_ev->layer;
    */
    return 0;
}
ZMK_LISTENER(rgblayer, rgb_layer_change_listener)
ZMK_SUBSCRIPTION(rgblayer, zmk_layer_state_changed);
#endif

SYS_INIT(zmk_rgb_underglow_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);