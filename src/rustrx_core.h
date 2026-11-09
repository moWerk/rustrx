/* SPDX-License-Identifier: GPL-3.0-or-later
 * C ABI of librustrx (core/src/lib.rs). Kept by hand: three functions. */
#ifndef RUSTRX_CORE_H
#define RUSTRX_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*rustrx_event_callback)(void *ctx, const char *json);

const char *rustrx_version(void);
int rustrx_start(const char *data_dir, rustrx_event_callback cb, void *ctx);
int rustrx_command(const char *json);

#ifdef __cplusplus
}
#endif

#endif
