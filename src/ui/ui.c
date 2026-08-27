#include "ui/ui.h"

#include <string.h>

#include "common/log.h"
#include "common/crypto.h"
#include "common/hoststore.h"
#include "net/sock.h"
#include "net/mdns.h"
#include "proto/pairing.h"
#include "proto/connection.h"
#include "input/pad.h"
#include "render/rsx_renderer.h"

#define PAIR_PIN_PLACEHOLDER "0000"

enum { ST_HOSTS, ST_PAIR, ST_PIN, ST_APPS, ST_STREAM, ST_QUIT };

/* PIN is a placeholder; the real client shows a PIN the user enters on the
 * host. TODO: drive this from on-screen UI input. */
static int run_stream(paired_host_t *h)
{
    session_t s;
    if (session_start(h, &s) != 0) {
        LOGE("stream: session_start failed for %s\n", h->name);
        return -1;
    }
    rsx_renderer_t *rr = rsx_renderer_init(854, 480);
    LOGI("streaming from %s (Triangle = quit)\n", h->name);
    for (;;) {
        session_pump(&s);
        gamepad_state_t st;
        if (pad_read(0, &st) == 0 && (st.buttons & BUTTON_Y))
            break;
        rsx_renderer_clear(rr, 20, 50, 90); /* TODO: present decoded frame */
    }
    rsx_renderer_shutdown(rr);
    session_stop(&s);
    return 0;
}

int ui_run(void)
{
    if (crypto_init() != 0) {
        LOGE("crypto init failed\n");
        return 1;
    }
    if (hoststore_load() != 0)
        LOGW("no persisted hosts (first run)\n");
    LOGI("ui: calling net_init\n");
    net_init();
    LOGI("ui: net_init done\n");

    discovered_host_t found[MAX_DISCOVERED];
    LOGI("ui: calling mdns_discover\n");
    int n = mdns_discover(found, MAX_DISCOVERED);
    LOGI("ui: mdns_discover found %d hosts\n", n);
    for (int i = 0; i < n; i++)
        hoststore_add(found[i].ip, found[i].name);

    LOGI("ui: calling pad_init\n");
    pad_init();
    LOGI("ui: pad_init done\n");
    LOGI("ui: calling rsx_renderer_init\n");
    rsx_renderer_t *rr = rsx_renderer_init(854, 480);
    LOGI("ui: rsx_renderer_init done\n");

    int state = ST_HOSTS;
    int sel = 0;
    paired_host_t *cur = NULL;

    app_entry_t apps[MAX_APPS];
    int app_n = 0, app_sel = 0, apps_loaded = 0;
    char pin[8] = PAIR_PIN_PLACEHOLDER;
    int  pin_pos = 0;

    for (;;) {
        int cnt = hoststore_count();
        if (sel >= cnt) sel = 0;

        if (state == ST_HOSTS) {
            rsx_renderer_clear(rr, 12, 12, 32);
            rsx_renderer_draw_text(rr, 40, 20, 3, "MOONLIGHT PS3", 200, 220, 255);
            LOGI("MENU hosts n=%d sel=%d [%s] paired=%d\n", cnt,
                 sel, cnt ? hoststore_get(sel)->name : "-",
                 cnt ? hoststore_get(sel)->paired : 0);
            gamepad_state_t st;
            if (pad_read(0, &st) == 0) {
                if (st.buttons & BUTTON_DOWN)      sel = (sel + 1) % (cnt ? cnt : 1);
                else if (st.buttons & BUTTON_UP)   sel = (sel + (cnt ? cnt : 1) - 1) % (cnt ? cnt : 1);
                else if (st.buttons & BUTTON_A && cnt > 0) {
                    cur = hoststore_get(sel);
                    state = cur->paired ? ST_APPS : ST_PAIR;
                    apps_loaded = 0;
                } else if (st.buttons & BUTTON_START) {
                    state = ST_QUIT;
                }
            }
            /* TODO: throttle the menu loop (e.g. usleep). */
        } else if (state == ST_PAIR) {
            /* Move to PIN entry instead of a hardcoded placeholder. */
            memset(pin, '0', 4);
            pin[4] = 0;
            pin_pos = 0;
            state = ST_PIN;
        } else if (state == ST_PIN) {
            rsx_renderer_clear(rr, 10, 10, 30);
            rsx_renderer_draw_text(rr, 120, 40, 4, "ENTER PIN", 255, 255, 255);
            char d[2] = { 0, 0 };
            for (int i = 0; i < 4; i++) {
                d[0] = pin[i];
                int on = (i == pin_pos);
                rsx_renderer_draw_text(rr, 140 + i * 50, 120, 6, d,
                                       on ? 255 : 170, on ? 220 : 170, on ? 0 : 170);
            }
            rsx_renderer_draw_text(rr, 90, 220, 2, "CROSS OK  CIRCLE BACK", 180, 180, 180);
            gamepad_state_t st;
            if (pad_read(0, &st) == 0) {
                if (st.buttons & BUTTON_UP)
                    pin[pin_pos] = (char)(((pin[pin_pos] - '0' + 1) % 10) + '0');
                else if (st.buttons & BUTTON_DOWN)
                    pin[pin_pos] = (char)(((pin[pin_pos] - '0' + 9) % 10) + '0');
                else if (st.buttons & BUTTON_LEFT)
                    pin_pos = (pin_pos + 3) % 4;
                else if (st.buttons & BUTTON_RIGHT)
                    pin_pos = (pin_pos + 1) % 4;
                else if (st.buttons & BUTTON_A) {
                    LOGI("pairing with %s PIN=%s\n", cur->name, pin);
                    if (pair_with_host(cur, pin) == 0) {
                        hoststore_save();
                        LOGI("paired with %s\n", cur->name);
                        state = ST_APPS;
                        apps_loaded = 0;
                    } else {
                        LOGW("pairing failed (check PIN / host)\n");
                        state = ST_HOSTS;
                    }
                } else if (st.buttons & BUTTON_B) {
                    state = ST_HOSTS;
                }
            }
        } else if (state == ST_APPS) {
            rsx_renderer_clear(rr, 20, 20, 40);
            if (!apps_loaded) {
                app_n = rtsp_applist(cur, apps, MAX_APPS);
                apps_loaded = 1;
                if (app_n <= 0) {
                    LOGW("applist failed or empty (need real host)\n");
                    state = ST_HOSTS;
                    apps_loaded = 0;
                }
            }
            if (apps_loaded) {
                LOGI("APPS n=%d sel=%d [%s]\n", app_n, app_sel,
                     app_n ? apps[app_sel].name : "-");
                gamepad_state_t st;
                if (pad_read(0, &st) == 0) {
                    if (st.buttons & BUTTON_DOWN) app_sel = (app_sel + 1) % (app_n ? app_n : 1);
                    else if (st.buttons & BUTTON_UP) app_sel = (app_sel + (app_n ? app_n : 1) - 1) % (app_n ? app_n : 1);
                    else if (st.buttons & BUTTON_A && app_n > 0) {
                        LOGI("launching %s\n", apps[app_sel].name);
                        if (rtsp_launch(cur, apps[app_sel].id) == 0)
                            state = ST_STREAM;
                        else
                            LOGW("launch failed\n");
                    } else if (st.buttons & BUTTON_B) {
                        state = ST_HOSTS;
                        apps_loaded = 0;
                    }
                }
            }
        } else if (state == ST_STREAM) {
            run_stream(cur);
            state = ST_HOSTS;
            apps_loaded = 0;
        } else if (state == ST_QUIT) {
            break;
        }
    }

    rsx_renderer_shutdown(rr);
    pad_finalize();
    LOGI("ui: quit\n");
    return 0;
}
