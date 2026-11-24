/* SPDX-License-Identifier: GPL-3.0-or-later
 * C ABI of librustrx (core/src/lib.rs). Kept by hand: eight functions. */
#ifndef RUSTRX_CORE_H
#define RUSTRX_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rustrx_event_callback)(void *ctx, const char *json);

const char *rustrx_version(void);
/* the store's key from sailfish-secrets; before rustrx_start, once */
int rustrx_set_store_key(const char *key);
/* a core for one account's data folder; its number (0, 1, ...), below 0 on failure */
int rustrx_start(const char *data_dir, rustrx_event_callback cb, void *ctx);
/* to the first account's core */
int rustrx_command(const char *json);
/* to the core rustrx_start numbered `index` */
int rustrx_command_to(int index, const char *json);
/* a QR code in a greyscale picture into out: its length, 0 for none (also for a frame the decoder panics on), <0 for errors */
int rustrx_decode_qr(const unsigned char *grey, int width, int height, int stride, unsigned char *out, int cap);
/* no event reaches the callback after this; call before its receiver dies */
void rustrx_stop(void);
/* stop syncing and let go of every core's store, at most timeout_ms; before the
 * process gives up its bus name (the store's lock) */
void rustrx_shutdown(unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
