#ifndef UI_MENU_H
#define UI_MENU_H

/* Top-level UI state machine: host discovery/list, pairing flow, app
 * selection, launch, settings (resolution/fps/bitrate), and the
 * in-stream overlay + quit. Drives the other modules. */

typedef enum {
    UI_STATE_DISCOVER,
    UI_STATE_HOST_LIST,
    UI_STATE_PAIRING,
    UI_STATE_APP_LIST,
    UI_STATE_STREAMING,
    UI_STATE_SETTINGS
} ui_state_t;

int  ui_init(void);
void ui_run(void);          /* main loop; blocks until exit */
void ui_shutdown(void);

#endif /* UI_MENU_H */
