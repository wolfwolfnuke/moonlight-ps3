#ifndef UI_UI_H
#define UI_UI_H

/* Runs the top-level UI state machine: host discovery/list, pairing, and
 * streaming. Rendering is currently a solid-color placeholder (TODO: real
 * on-screen text via the PSL1GHT font lib); menu state is also logged. */
int ui_run(void);

#endif /* UI_UI_H */
