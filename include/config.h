#ifndef _CONFIG_H
#define _CONFIG_H
#ifdef __cplusplus
extern "C" {
#endif

// These must only be called from the main thread.
int config_init (const char *b64ConfigStr);
int config_deinit (void);

#ifdef __cplusplus
}
#endif
#endif
