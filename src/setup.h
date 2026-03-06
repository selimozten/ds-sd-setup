#ifndef SETUP_H
#define SETUP_H

#include <stdbool.h>
#include <pthread.h>

#define MAX_PATH_LEN     1024
#define MAX_LOG_LINES    512
#define MAX_LOG_LINE_LEN 256
#define MAX_URL_LEN      512

typedef enum {
    PROFILE_DSI = 0,
    PROFILE_FLASHCARD,
    PROFILE_3DS,
    PROFILE_COUNT
} ConsoleProfile;

typedef enum {
    SETUP_IDLE = 0,
    SETUP_FETCHING,
    SETUP_RUNNING,
    SETUP_STOPPING,
    SETUP_DONE,
} SetupState;

typedef struct {
    char lines[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
    int  count;
    int  scroll_offset;
    pthread_mutex_t mutex;
} LogBuffer;

typedef struct {
    char version[64];
    char download_url[MAX_URL_LEN];
    char filename[128];
    bool fetched;
} ComponentInfo;

typedef struct {
    char sd_root[MAX_PATH_LEN];
    ConsoleProfile profile;
    bool install_twilight;
    bool install_bootstrap;
    bool backup_first;
} SetupConfig;

typedef struct {
    SetupConfig    config;
    SetupState     state;
    LogBuffer      log;
    ComponentInfo  twilight;
    ComponentInfo  bootstrap;
    int            dl_current;   // download progress (KB)
    int            dl_total;     // download total (KB)
    char           status_label[128];
    pthread_mutex_t status_mutex;
} SetupContext;

void setup_init(SetupContext *ctx);
void setup_fetch_versions(SetupContext *ctx);
void setup_start(SetupContext *ctx);
void setup_stop(SetupContext *ctx);
void setup_log(SetupContext *ctx, const char *fmt, ...);
char *setup_get_log_text(SetupContext *ctx);

const char *profile_label(ConsoleProfile p);
const char *profile_asset_suffix(ConsoleProfile p);

#endif
