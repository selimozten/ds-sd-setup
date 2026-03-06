#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#endif

#include "setup.h"
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#define TWILIGHT_REPO  "DS-Homebrew/TWiLightMenu"
#define BOOTSTRAP_REPO "DS-Homebrew/nds-bootstrap"
#define GITHUB_API     "https://api.github.com/repos"

// ---------- Profile helpers ----------

const char *profile_label(ConsoleProfile p) {
    switch (p) {
        case PROFILE_DSI:       return "DSi / DSi XL";
        case PROFILE_FLASHCARD: return "Flashcard";
        case PROFILE_3DS:       return "3DS / 2DS";
        default:                return "Unknown";
    }
}

const char *profile_asset_suffix(ConsoleProfile p) {
    switch (p) {
        case PROFILE_DSI:       return "DSi";
        case PROFILE_FLASHCARD: return "Flashcard";
        case PROFILE_3DS:       return "3DS";
        default:                return "";
    }
}

// ---------- Logging ----------

void setup_init(SetupContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = SETUP_IDLE;
    ctx->config.install_twilight = true;
    ctx->config.install_bootstrap = true;
    ctx->config.backup_first = true;
    pthread_mutex_init(&ctx->log.mutex, NULL);
    pthread_mutex_init(&ctx->status_mutex, NULL);
}

void setup_log(SetupContext *ctx, const char *fmt, ...) {
    pthread_mutex_lock(&ctx->log.mutex);
    if (ctx->log.count >= MAX_LOG_LINES) {
        memmove(ctx->log.lines[0], ctx->log.lines[1],
                (MAX_LOG_LINES - 1) * MAX_LOG_LINE_LEN);
        ctx->log.count = MAX_LOG_LINES - 1;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->log.lines[ctx->log.count], MAX_LOG_LINE_LEN, fmt, args);
    va_end(args);
    ctx->log.count++;
    ctx->log.scroll_offset = 0;
    pthread_mutex_unlock(&ctx->log.mutex);
}

char *setup_get_log_text(SetupContext *ctx) {
    pthread_mutex_lock(&ctx->log.mutex);
    size_t total = 0;
    for (int i = 0; i < ctx->log.count; i++)
        total += strlen(ctx->log.lines[i]) + 1;
    char *out = malloc(total + 1);
    if (out) {
        out[0] = '\0';
        for (int i = 0; i < ctx->log.count; i++) {
            strcat(out, ctx->log.lines[i]);
            strcat(out, "\n");
        }
    }
    pthread_mutex_unlock(&ctx->log.mutex);
    return out;
}

// ---------- curl helpers ----------

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MemBuffer;

static size_t write_mem_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    MemBuffer *buf = (MemBuffer *)userdata;
    size_t total = size * nmemb;
    if (buf->size + total > buf->capacity) {
        size_t new_cap = (buf->capacity + total) * 2;
        unsigned char *tmp = realloc(buf->data, new_cap);
        if (!tmp) return 0;
        buf->data = tmp;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    return total;
}

static CURL *create_curl_handle(void) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DS-SD-Setup/1.0");
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    return curl;
}

static long download_to_mem(CURL *curl, const char *url, MemBuffer *buf) {
    buf->data = malloc(65536);
    buf->size = 0;
    buf->capacity = 65536;
    if (!buf->data) return 0;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK) {
        free(buf->data);
        buf->data = NULL;
        buf->size = 0;
        return 0;
    }
    return http_code;
}

// Download progress callback
typedef struct {
    SetupContext *ctx;
    const char *label;
} DlProgress;

static int xfer_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                    curl_off_t ultotal, curl_off_t ulnow) {
    (void)ultotal; (void)ulnow;
    DlProgress *pd = (DlProgress *)clientp;
    if (dltotal > 0) {
        pthread_mutex_lock(&pd->ctx->status_mutex);
        pd->ctx->dl_current = (int)(dlnow / 1024);
        pd->ctx->dl_total = (int)(dltotal / 1024);
        snprintf(pd->ctx->status_label, sizeof(pd->ctx->status_label),
                 "%s (%.1f / %.1f MB)", pd->label,
                 dlnow / 1048576.0, dltotal / 1048576.0);
        pthread_mutex_unlock(&pd->ctx->status_mutex);
    }
    return pd->ctx->state == SETUP_STOPPING ? 1 : 0;
}

static bool download_to_file(CURL *curl, const char *url, const char *filepath,
                              SetupContext *ctx, const char *label) {
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        setup_log(ctx, "Error: Cannot create temp file %s", filepath);
        return false;
    }

    DlProgress pd = { ctx, label };

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // default fwrite
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfer_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pd);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    CURLcode res = curl_easy_perform(curl);
    fclose(f);

    // Restore memory writer for subsequent API calls
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_mem_cb);

    if (res != CURLE_OK) {
        setup_log(ctx, "Error: Download failed: %s", curl_easy_strerror(res));
        remove(filepath);
        return false;
    }

    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        setup_log(ctx, "Error: HTTP %ld downloading %s", http_code, label);
        remove(filepath);
        return false;
    }

    return true;
}

// ---------- JSON helpers (simple strstr-based) ----------

static bool json_extract_string(const char *json, const char *key, char *out, size_t out_len) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *pos = strstr(json, needle);
    if (!pos) return false;
    pos += strlen(needle);
    // Skip whitespace and colon
    while (*pos && (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
    if (*pos != '"') return false;
    pos++;
    const char *end = strchr(pos, '"');
    if (!end) return false;
    size_t len = end - pos;
    if (len >= out_len) len = out_len - 1;
    memcpy(out, pos, len);
    out[len] = '\0';
    return true;
}

// Find an asset by name in the GitHub releases JSON and get its download URL
static bool json_find_asset(const char *json, const char *asset_name,
                             char *url_out, size_t url_len) {
    // Search for the asset name string in the response
    char name_needle[256];
    snprintf(name_needle, sizeof(name_needle), "\"%s\"", asset_name);
    const char *pos = strstr(json, name_needle);
    if (!pos) return false;

    // Find browser_download_url after this position
    const char *url_key = strstr(pos, "\"browser_download_url\"");
    if (!url_key) return false;
    url_key += strlen("\"browser_download_url\"");
    while (*url_key && (*url_key == ' ' || *url_key == ':' || *url_key == '\t')) url_key++;
    if (*url_key != '"') return false;
    url_key++;
    const char *url_end = strchr(url_key, '"');
    if (!url_end) return false;
    size_t len = url_end - url_key;
    if (len >= url_len) len = url_len - 1;
    memcpy(url_out, url_key, len);
    url_out[len] = '\0';
    return true;
}

// ---------- Version fetch (background, lightweight) ----------

static void *fetch_thread(void *arg) {
    SetupContext *ctx = (SetupContext *)arg;
    CURL *curl = create_curl_handle();
    if (!curl) {
        setup_log(ctx, "Error: Failed to initialize curl");
        ctx->state = SETUP_IDLE;
        return NULL;
    }

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Fetch TwilightMenu++ latest version
    {
        char url[256];
        snprintf(url, sizeof(url), "%s/%s/releases/latest", GITHUB_API, TWILIGHT_REPO);
        MemBuffer buf = {0};
        long code = download_to_mem(curl, url, &buf);
        if (code == 200 && buf.data) {
            buf.data = realloc(buf.data, buf.size + 1);
            buf.data[buf.size] = '\0';
            json_extract_string((char *)buf.data, "tag_name",
                                ctx->twilight.version, sizeof(ctx->twilight.version));
            ctx->twilight.fetched = true;
            setup_log(ctx, "TwilightMenu++ latest: %s", ctx->twilight.version);
        } else {
            setup_log(ctx, "Error: Failed to fetch TwilightMenu++ version (HTTP %ld)", code);
        }
        free(buf.data);
    }

    // Fetch nds-bootstrap latest version
    {
        char url[256];
        snprintf(url, sizeof(url), "%s/%s/releases/latest", GITHUB_API, BOOTSTRAP_REPO);
        MemBuffer buf = {0};
        long code = download_to_mem(curl, url, &buf);
        if (code == 200 && buf.data) {
            buf.data = realloc(buf.data, buf.size + 1);
            buf.data[buf.size] = '\0';
            json_extract_string((char *)buf.data, "tag_name",
                                ctx->bootstrap.version, sizeof(ctx->bootstrap.version));
            ctx->bootstrap.fetched = true;
            setup_log(ctx, "nds-bootstrap latest: %s", ctx->bootstrap.version);
        } else {
            setup_log(ctx, "Error: Failed to fetch nds-bootstrap version (HTTP %ld)", code);
        }
        free(buf.data);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    ctx->state = SETUP_IDLE;
    return NULL;
}

void setup_fetch_versions(SetupContext *ctx) {
    if (ctx->state != SETUP_IDLE) return;
    ctx->state = SETUP_FETCHING;
    ctx->twilight.fetched = false;
    ctx->bootstrap.fetched = false;
    setup_log(ctx, "Fetching latest versions from GitHub...");

    pthread_t thread;
    pthread_create(&thread, NULL, fetch_thread, ctx);
    pthread_detach(thread);
}

// ---------- Archive extraction ----------

static bool extract_archive(const char *archive_path, const char *dest_dir, SetupContext *ctx) {
    struct archive *a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    struct archive *ext = archive_write_disk_new();
    archive_write_disk_set_options(ext,
        ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM);
    archive_write_disk_set_standard_lookup(ext);

    if (archive_read_open_filename(a, archive_path, 10240) != ARCHIVE_OK) {
        setup_log(ctx, "Error: Cannot open archive: %s", archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    int file_count = 0;
    struct archive_entry *entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (ctx->state == SETUP_STOPPING) break;

        // Prepend destination directory to entry path
        const char *entry_path = archive_entry_pathname(entry);
        char full_path[MAX_PATH_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%s", dest_dir, entry_path);
        archive_entry_set_pathname(entry, full_path);

        int r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK) continue;

        if (archive_entry_size(entry) > 0) {
            const void *buff;
            size_t size;
            la_int64_t offset;
            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK) {
                archive_write_data_block(ext, buff, size, offset);
            }
        }
        archive_write_finish_entry(ext);
        file_count++;
    }

    archive_read_free(a);
    archive_write_free(ext);
    setup_log(ctx, "Extracted %d entries to %s", file_count, dest_dir);
    return file_count > 0;
}

// ---------- Setup helpers ----------

static void get_temp_dir(char *out, size_t len) {
#ifdef _WIN32
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = ".";
    snprintf(out, len, "%s\\ds-sd-setup", tmp);
    _mkdir(out);
#else
    snprintf(out, len, "/tmp/ds-sd-setup");
    mkdir(out, 0755);
#endif
}

static bool backup_nds_dir(SetupContext *ctx) {
    char nds_path[MAX_PATH_LEN];
    snprintf(nds_path, sizeof(nds_path), "%s/_nds", ctx->config.sd_root);

    struct stat st;
    if (stat(nds_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        setup_log(ctx, "No existing _nds folder to backup");
        return true;
    }

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char backup_path[MAX_PATH_LEN];
    snprintf(backup_path, sizeof(backup_path),
             "%s/_nds_backup_%04d%02d%02d_%02d%02d%02d",
             ctx->config.sd_root,
             tm_info->tm_year + 1900, tm_info->tm_mon + 1, tm_info->tm_mday,
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);

    setup_log(ctx, "Backing up _nds -> %s", backup_path);
    if (rename(nds_path, backup_path) != 0) {
        setup_log(ctx, "Error: Failed to rename _nds for backup");
        return false;
    }
    setup_log(ctx, "OK: Backup complete");
    return true;
}

// ---------- Main setup thread ----------

static void *setup_thread(void *arg) {
    SetupContext *ctx = (SetupContext *)arg;

    setup_log(ctx, "---");
    setup_log(ctx, "Starting setup: %s profile", profile_label(ctx->config.profile));
    setup_log(ctx, "SD card: %s", ctx->config.sd_root);
    setup_log(ctx, "Components: %s%s%s",
              ctx->config.install_twilight ? "TwilightMenu++ " : "",
              (ctx->config.install_twilight && ctx->config.install_bootstrap) ? "+ " : "",
              ctx->config.install_bootstrap ? "nds-bootstrap" : "");
    setup_log(ctx, "---");

    // Backup existing _nds if requested
    if (ctx->config.backup_first) {
        if (!backup_nds_dir(ctx)) {
            setup_log(ctx, "Error: Backup failed, aborting setup");
            ctx->state = SETUP_DONE;
            return NULL;
        }
    }

    CURL *curl = create_curl_handle();
    if (!curl) {
        setup_log(ctx, "Error: Failed to initialize curl");
        ctx->state = SETUP_DONE;
        return NULL;
    }

    struct curl_slist *api_headers = NULL;
    api_headers = curl_slist_append(api_headers, "Accept: application/vnd.github+json");

    char temp_dir[MAX_PATH_LEN];
    get_temp_dir(temp_dir, sizeof(temp_dir));

    // --- Download & extract TwilightMenu++ ---
    if (ctx->config.install_twilight && ctx->state == SETUP_RUNNING) {
        // Resolve asset URL
        char asset_name[128];
        snprintf(asset_name, sizeof(asset_name), "TWiLightMenu-%s.7z",
                 profile_asset_suffix(ctx->config.profile));
        char download_url[MAX_URL_LEN] = {0};

        setup_log(ctx, "Fetching TwilightMenu++ release info...");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api_headers);

        char api_url[256];
        snprintf(api_url, sizeof(api_url), "%s/%s/releases/latest",
                 GITHUB_API, TWILIGHT_REPO);
        MemBuffer buf = {0};
        long code = download_to_mem(curl, api_url, &buf);
        if (code == 200 && buf.data) {
            buf.data = realloc(buf.data, buf.size + 1);
            buf.data[buf.size] = '\0';
            if (!json_find_asset((char *)buf.data, asset_name,
                                 download_url, sizeof(download_url))) {
                setup_log(ctx, "Error: Asset %s not found in release", asset_name);
            }
        } else {
            setup_log(ctx, "Error: GitHub API failed (HTTP %ld)", code);
        }
        free(buf.data);

        if (download_url[0] && ctx->state == SETUP_RUNNING) {
            // Remove API headers for actual download (redirects to CDN)
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);

            char dl_path[MAX_PATH_LEN];
            snprintf(dl_path, sizeof(dl_path), "%s/%s", temp_dir, asset_name);

            setup_log(ctx, "Downloading %s...", asset_name);
            if (download_to_file(curl, download_url, dl_path, ctx, "TwilightMenu++")) {
                setup_log(ctx, "OK: Downloaded %s", asset_name);

                pthread_mutex_lock(&ctx->status_mutex);
                snprintf(ctx->status_label, sizeof(ctx->status_label),
                         "Extracting TwilightMenu++...");
                ctx->dl_current = 0;
                ctx->dl_total = 0;
                pthread_mutex_unlock(&ctx->status_mutex);

                setup_log(ctx, "Extracting to %s...", ctx->config.sd_root);
                if (extract_archive(dl_path, ctx->config.sd_root, ctx)) {
                    setup_log(ctx, "OK: TwilightMenu++ installed");
                } else {
                    setup_log(ctx, "Error: Failed to extract TwilightMenu++");
                }
                remove(dl_path);
            }
        } else if (!download_url[0]) {
            setup_log(ctx, "Error: Could not resolve download URL for TwilightMenu++");
        }
    }

    // --- Download & extract nds-bootstrap ---
    if (ctx->config.install_bootstrap && ctx->state == SETUP_RUNNING) {
        char download_url[MAX_URL_LEN] = {0};

        setup_log(ctx, "Fetching nds-bootstrap release info...");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, api_headers);

        char api_url[256];
        snprintf(api_url, sizeof(api_url), "%s/%s/releases/latest",
                 GITHUB_API, BOOTSTRAP_REPO);
        MemBuffer buf = {0};
        long code = download_to_mem(curl, api_url, &buf);
        if (code == 200 && buf.data) {
            buf.data = realloc(buf.data, buf.size + 1);
            buf.data[buf.size] = '\0';
            if (!json_find_asset((char *)buf.data, "nds-bootstrap.7z",
                                 download_url, sizeof(download_url))) {
                setup_log(ctx, "Error: nds-bootstrap.7z not found in release");
            }
        } else {
            setup_log(ctx, "Error: GitHub API failed (HTTP %ld)", code);
        }
        free(buf.data);

        if (download_url[0] && ctx->state == SETUP_RUNNING) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, NULL);

            char dl_path[MAX_PATH_LEN];
            snprintf(dl_path, sizeof(dl_path), "%s/nds-bootstrap.7z", temp_dir);

            setup_log(ctx, "Downloading nds-bootstrap.7z...");
            if (download_to_file(curl, download_url, dl_path, ctx, "nds-bootstrap")) {
                setup_log(ctx, "OK: Downloaded nds-bootstrap.7z");

                pthread_mutex_lock(&ctx->status_mutex);
                snprintf(ctx->status_label, sizeof(ctx->status_label),
                         "Extracting nds-bootstrap...");
                ctx->dl_current = 0;
                ctx->dl_total = 0;
                pthread_mutex_unlock(&ctx->status_mutex);

                setup_log(ctx, "Extracting to %s...", ctx->config.sd_root);
                if (extract_archive(dl_path, ctx->config.sd_root, ctx)) {
                    setup_log(ctx, "OK: nds-bootstrap installed");
                } else {
                    setup_log(ctx, "Error: Failed to extract nds-bootstrap");
                }
                remove(dl_path);
            }
        } else if (!download_url[0]) {
            setup_log(ctx, "Error: Could not resolve download URL for nds-bootstrap");
        }
    }

    curl_slist_free_all(api_headers);
    curl_easy_cleanup(curl);

    setup_log(ctx, "---");
    if (ctx->state == SETUP_STOPPING) {
        setup_log(ctx, "Setup cancelled by user");
    } else {
        setup_log(ctx, "Done! SD card setup complete for %s",
                  profile_label(ctx->config.profile));
        setup_log(ctx, "Safe to eject the SD card and insert into your console");
    }

    ctx->state = SETUP_DONE;
    return NULL;
}

// ---------- Format thread ----------

static void *format_thread(void *arg) {
    SetupContext *ctx = (SetupContext *)arg;

    setup_log(ctx, "---");
    setup_log(ctx, "Formatting SD card: %s", ctx->config.sd_root);

    pthread_mutex_lock(&ctx->status_mutex);
    snprintf(ctx->status_label, sizeof(ctx->status_label), "Formatting SD card...");
    pthread_mutex_unlock(&ctx->status_mutex);

    char new_path[MAX_PATH_LEN] = {0};
    char error[256] = {0};

    bool ok = platform_format_sd(ctx->config.sd_root, "DSSD",
                                  new_path, sizeof(new_path),
                                  error, sizeof(error));

    if (ok) {
        setup_log(ctx, "OK: SD card formatted as FAT32 (label: DSSD)");
        if (new_path[0]) {
            strncpy(ctx->format_new_path, new_path, MAX_PATH_LEN - 1);
            setup_log(ctx, "New volume path: %s", new_path);
        }
    } else {
        setup_log(ctx, "Error: Format failed: %s", error);
    }

    setup_log(ctx, "---");
    ctx->state = SETUP_DONE;
    return NULL;
}

void setup_format_sd(SetupContext *ctx) {
    if (ctx->state != SETUP_IDLE && ctx->state != SETUP_DONE) return;

    ctx->state = SETUP_FORMATTING;
    ctx->format_new_path[0] = '\0';
    ctx->dl_current = 0;
    ctx->dl_total = 0;
    ctx->status_label[0] = '\0';

    pthread_t thread;
    pthread_create(&thread, NULL, format_thread, ctx);
    pthread_detach(thread);
}

void setup_start(SetupContext *ctx) {
    if (ctx->state == SETUP_RUNNING || ctx->state == SETUP_FETCHING) return;

    struct stat st;
    if (stat(ctx->config.sd_root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        setup_log(ctx, "Error: '%s' is not a valid directory", ctx->config.sd_root);
        return;
    }

    if (!ctx->config.install_twilight && !ctx->config.install_bootstrap) {
        setup_log(ctx, "Error: No components selected to install");
        return;
    }

    ctx->state = SETUP_RUNNING;
    ctx->dl_current = 0;
    ctx->dl_total = 0;
    ctx->status_label[0] = '\0';

    pthread_t thread;
    pthread_create(&thread, NULL, setup_thread, ctx);
    pthread_detach(thread);
}

void setup_stop(SetupContext *ctx) {
    if (ctx->state == SETUP_RUNNING) {
        ctx->state = SETUP_STOPPING;
        setup_log(ctx, "Stopping...");
    }
}
