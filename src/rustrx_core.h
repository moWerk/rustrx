/* SPDX-License-Identifier: GPL-3.0-or-later
 * C ABI of librustrx (core/src/lib.rs). Kept by hand: six functions. */
#ifndef RUSTRX_CORE_H
#define RUSTRX_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rustrx_event_callback)(void *ctx, const char *json);

const char *rustrx_version(void);
/* the store's key from sailfish-secrets; before rustrx_start, once */
int rustrx_set_store_key(const char *key);
int rustrx_start(const char *data_dir, rustrx_event_callback cb, void *ctx);
int rustrx_command(const char *json);
/* no event reaches the callback after this; call before its receiver dies */
void rustrx_stop(void);
/* stop syncing and let go of the stores, at most timeout_ms; before the
 * process gives up its bus name (the store's lock) */
void rustrx_shutdown(unsigned int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
