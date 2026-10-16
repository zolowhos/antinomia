// re_log.h - stderr only logging, compiled down to nothing when disabled.
// Module: util (C11).
// Owns: log level state, mute for mcp mode, the single stderr writer in the tree.
// Depends: re_err codes. Never writes stdout, so it cannot corrupt MCP framing.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

typedef enum {
    RE_LOG_OFF = 0,
    RE_LOG_ERROR,
    RE_LOG_WARN,
    RE_LOG_INFO,
    RE_LOG_DEBUG
} re_log_level_t;

const char *re_log_level_name(re_log_level_t lvl);
re_log_level_t re_log_level(void);
void re_log_set_level(re_log_level_t lvl);

// Mute silences everything regardless of level. The mcp entry point calls this
// unconditionally, because stdout is the protocol stream and stderr noise from
// a parser would look like a protocol violation to the client.
void re_log_set_mute(bool mute);
bool re_log_muted(void);

static inline bool re_log_enabled(re_log_level_t lvl) {
    if (lvl == RE_LOG_OFF)
        return false;
    return !re_log_muted() && lvl <= re_log_level();
}

void re_log_emit(re_log_level_t lvl, const char *file, int line, const char *fmt, ...);

// Read RE_LOG from the environment on first use. Values: off, error, warn, info, debug.
void re_log_init_from_env(void);

#define RE_LOGE(...)                                                    \
    do {                                                                \
        if (re_log_enabled(RE_LOG_ERROR))                               \
            re_log_emit(RE_LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)
#define RE_LOGW(...)                                                   \
    do {                                                               \
        if (re_log_enabled(RE_LOG_WARN))                               \
            re_log_emit(RE_LOG_WARN, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)
#define RE_LOGI(...)                                                   \
    do {                                                               \
        if (re_log_enabled(RE_LOG_INFO))                               \
            re_log_emit(RE_LOG_INFO, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)
#define RE_LOGD(...)                                                    \
    do {                                                                \
        if (re_log_enabled(RE_LOG_DEBUG))                               \
            re_log_emit(RE_LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#ifdef __cplusplus
}
#endif
