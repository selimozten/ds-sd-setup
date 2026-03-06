#include "setup.h"
#include "platform.h"
#include "raylib.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define WINDOW_W   780
#define WINDOW_H   620
#define PAD        20
#define SMALL_FONT 14
#define FIELD_H    30
#define BTN_H      34
#define BROWSE_W   80
#define SECTION_GAP 14

// Modifier key: Cmd on macOS, Ctrl on Windows/Linux
#ifdef __APPLE__
#define MOD_KEY_LEFT  KEY_LEFT_SUPER
#define MOD_KEY_RIGHT KEY_RIGHT_SUPER
#else
#define MOD_KEY_LEFT  KEY_LEFT_CONTROL
#define MOD_KEY_RIGHT KEY_RIGHT_CONTROL
#endif

// Color scheme (matches TwilightBoxart)
#define BG_COLOR        CLITERAL(Color){30, 30, 38, 255}
#define ACCENT_COLOR    CLITERAL(Color){155, 107, 234, 255}
#define ACCENT_HOVER    CLITERAL(Color){180, 138, 255, 255}
#define ACCENT_DIM      CLITERAL(Color){112, 68, 181, 255}
#define TEXT_COLOR       WHITE
#define DIM_TEXT        CLITERAL(Color){140, 140, 160, 255}
#define FIELD_BG        CLITERAL(Color){22, 22, 30, 255}
#define FIELD_BORDER    CLITERAL(Color){60, 60, 80, 255}
#define LOG_BG          CLITERAL(Color){16, 16, 22, 255}
#define GREEN_OK        CLITERAL(Color){80, 200, 120, 255}
#define RED_ERR         CLITERAL(Color){220, 80, 80, 255}
#define ORANGE_WARN     CLITERAL(Color){230, 160, 50, 255}
#define DISABLED_BG     CLITERAL(Color){50, 50, 60, 255}
#define BTN_DISABLED    CLITERAL(Color){55, 55, 65, 255}

static const char *PROFILE_LABELS[] = {
    "DSi / DSi XL",
    "Flashcard",
    "3DS / 2DS",
};

// ---------- Text field ----------

typedef struct {
    char text[MAX_PATH_LEN];
    bool active;
    int  cursor;
    int  scroll_x;
} TextField;

static void textfield_set(TextField *tf, const char *text) {
    strncpy(tf->text, text, MAX_PATH_LEN - 1);
    tf->text[MAX_PATH_LEN - 1] = '\0';
    tf->cursor = (int)strlen(tf->text);
    tf->scroll_x = 0;
}

static void textfield_handle(TextField *tf, Rectangle bounds) {
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        tf->active = CheckCollisionPointRec(GetMousePosition(), bounds);
        if (tf->active) {
            int mx = (int)(GetMousePosition().x - bounds.x - 6) + tf->scroll_x;
            int best = 0, best_dist = 99999;
            int len = (int)strlen(tf->text);
            for (int i = 0; i <= len; i++) {
                char tmp[MAX_PATH_LEN];
                memcpy(tmp, tf->text, i);
                tmp[i] = '\0';
                int tw = MeasureText(tmp, SMALL_FONT);
                int dist = mx > tw ? mx - tw : tw - mx;
                if (dist < best_dist) { best_dist = dist; best = i; }
            }
            tf->cursor = best;
        }
    }
    if (!tf->active) return;

    int key;
    while ((key = GetCharPressed()) > 0) {
        int len = (int)strlen(tf->text);
        if (key >= 32 && key < 127 && len < MAX_PATH_LEN - 1) {
            memmove(tf->text + tf->cursor + 1, tf->text + tf->cursor,
                    len - tf->cursor + 1);
            tf->text[tf->cursor++] = (char)key;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        if (tf->cursor > 0) {
            int len = (int)strlen(tf->text);
            memmove(tf->text + tf->cursor - 1, tf->text + tf->cursor,
                    len - tf->cursor + 1);
            tf->cursor--;
        }
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
        int len = (int)strlen(tf->text);
        if (tf->cursor < len)
            memmove(tf->text + tf->cursor, tf->text + tf->cursor + 1, len - tf->cursor);
    }
    if ((IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) && tf->cursor > 0)
        tf->cursor--;
    if ((IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) && tf->cursor < (int)strlen(tf->text))
        tf->cursor++;
    if (IsKeyPressed(KEY_HOME)) tf->cursor = 0;
    if (IsKeyPressed(KEY_END))  tf->cursor = (int)strlen(tf->text);

    if ((IsKeyDown(MOD_KEY_LEFT) || IsKeyDown(MOD_KEY_RIGHT)) && IsKeyPressed(KEY_A))
        tf->cursor = (int)strlen(tf->text);

    if ((IsKeyDown(MOD_KEY_LEFT) || IsKeyDown(MOD_KEY_RIGHT)) && IsKeyPressed(KEY_V)) {
        const char *clip = GetClipboardText();
        if (clip) {
            int clip_len = (int)strlen(clip);
            int cur_len = (int)strlen(tf->text);
            int space = MAX_PATH_LEN - 1 - cur_len;
            if (clip_len > space) clip_len = space;
            if (clip_len > 0) {
                memmove(tf->text + tf->cursor + clip_len,
                        tf->text + tf->cursor, cur_len - tf->cursor + 1);
                memcpy(tf->text + tf->cursor, clip, clip_len);
                tf->cursor += clip_len;
            }
        }
    }
}

static void textfield_draw(TextField *tf, Rectangle bounds, const char *placeholder) {
    DrawRectangleRec(bounds, FIELD_BG);
    DrawRectangleLinesEx(bounds, 1, tf->active ? ACCENT_COLOR : FIELD_BORDER);

    bool has_text = strlen(tf->text) > 0;
    const char *display = has_text ? tf->text : placeholder;
    Color col = has_text ? TEXT_COLOR : DIM_TEXT;

    int field_inner_w = (int)bounds.width - 12;
    if (has_text && tf->active) {
        char tmp[MAX_PATH_LEN];
        int n = tf->cursor < (int)sizeof(tmp)-1 ? tf->cursor : (int)sizeof(tmp)-1;
        memcpy(tmp, tf->text, n); tmp[n] = '\0';
        int cursor_px = MeasureText(tmp, SMALL_FONT);
        if (cursor_px - tf->scroll_x > field_inner_w - 10)
            tf->scroll_x = cursor_px - field_inner_w + 20;
        if (cursor_px - tf->scroll_x < 0)
            tf->scroll_x = cursor_px > 10 ? cursor_px - 10 : 0;
    }

    int text_x = (int)bounds.x + 6 - (has_text ? tf->scroll_x : 0);
    int text_y = (int)bounds.y + (FIELD_H - SMALL_FONT) / 2;

    BeginScissorMode((int)bounds.x + 2, (int)bounds.y, (int)bounds.width - 4, (int)bounds.height);
    DrawText(display, text_x, text_y, SMALL_FONT, col);

    if (tf->active && ((int)(GetTime() * 2.5) % 2 == 0)) {
        char tmp[MAX_PATH_LEN];
        int n = tf->cursor < (int)sizeof(tmp)-1 ? tf->cursor : (int)sizeof(tmp)-1;
        memcpy(tmp, tf->text, n); tmp[n] = '\0';
        int cx = text_x + MeasureText(tmp, SMALL_FONT);
        DrawRectangle(cx, (int)bounds.y + 5, 2, FIELD_H - 10, ACCENT_COLOR);
    }
    EndScissorMode();
}

// ---------- UI widgets ----------

static bool draw_button(Rectangle bounds, const char *label, Color bg_color, bool enabled) {
    bool hovered = enabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    Color bg = enabled ? (hovered ? ACCENT_HOVER : bg_color) : BTN_DISABLED;
    Color fg = enabled ? WHITE : DIM_TEXT;

    DrawRectangleRounded(bounds, 0.3f, 8, bg);
    int tw = MeasureText(label, SMALL_FONT);
    DrawText(label, (int)(bounds.x + (bounds.width - tw) / 2),
             (int)(bounds.y + (bounds.height - SMALL_FONT) / 2), SMALL_FONT, fg);

    return enabled && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool draw_checkbox(int x, int y, const char *label, bool *value) {
    Rectangle box = {(float)x, (float)y, 18, 18};
    Rectangle hit = {(float)x, (float)y, (float)(22 + MeasureText(label, SMALL_FONT)), 18};
    bool hovered = CheckCollisionPointRec(GetMousePosition(), hit);

    DrawRectangleRec(box, FIELD_BG);
    DrawRectangleLinesEx(box, 1, hovered ? ACCENT_COLOR : FIELD_BORDER);
    if (*value) {
        DrawRectangle(x + 3, y + 3, 12, 12, ACCENT_COLOR);
        DrawLineEx((Vector2){x + 5, y + 9}, (Vector2){x + 8, y + 13}, 2, WHITE);
        DrawLineEx((Vector2){x + 8, y + 13}, (Vector2){x + 14, y + 5}, 2, WHITE);
    }
    DrawText(label, x + 24, y + 1, SMALL_FONT, TEXT_COLOR);

    if (hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        *value = !(*value);
        return true;
    }
    return false;
}

static bool draw_radio(int x, int y, const char *label, bool selected) {
    Rectangle hit = {(float)x, (float)y, (float)(22 + MeasureText(label, SMALL_FONT)), 18};
    bool hovered = CheckCollisionPointRec(GetMousePosition(), hit);

    DrawCircleLines(x + 9, y + 9, 8, hovered ? ACCENT_COLOR : FIELD_BORDER);
    if (selected) DrawCircle(x + 9, y + 9, 5, ACCENT_COLOR);
    DrawText(label, x + 22, y + 1, SMALL_FONT, TEXT_COLOR);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static int draw_section(int y, const char *label) {
    DrawText(label, PAD, y, SMALL_FONT, ACCENT_DIM);
    return y + 18;
}

// ---------- Config persistence ----------

static void get_config_path(char *out, size_t len) {
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata)
        snprintf(out, len, "%s\\DS-SD-Setup.ini", appdata);
    else
        snprintf(out, len, "DS-SD-Setup.ini");
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (home)
        snprintf(out, len, "%s/Library/Preferences/DS-SD-Setup.ini", home);
    else
        snprintf(out, len, "DS-SD-Setup.ini");
#else
    const char *config = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (config)
        snprintf(out, len, "%s/DS-SD-Setup.ini", config);
    else if (home)
        snprintf(out, len, "%s/.config/DS-SD-Setup.ini", home);
    else
        snprintf(out, len, "DS-SD-Setup.ini");
#endif
}

typedef struct {
    char sd_root[MAX_PATH_LEN];
    int  profile;
    bool backup_first;
} SavedConfig;

static void load_config(SavedConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->backup_first = true;

    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1280];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        size_t vlen = strlen(val);
        while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r')) val[--vlen] = '\0';

        if (strcmp(key, "sd_root") == 0)
            strncpy(cfg->sd_root, val, MAX_PATH_LEN - 1);
        else if (strcmp(key, "profile") == 0)
            cfg->profile = atoi(val);
        else if (strcmp(key, "backup_first") == 0)
            cfg->backup_first = atoi(val) != 0;
    }
    fclose(f);
}

static void save_config(const char *sd_root, int profile, bool backup_first) {
    char path[MAX_PATH_LEN];
    get_config_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "sd_root=%s\n", sd_root);
    fprintf(f, "profile=%d\n", profile);
    fprintf(f, "backup_first=%d\n", backup_first ? 1 : 0);
    fclose(f);
}

// ---------- Main ----------

int main(void) {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W, WINDOW_H, "DS SD Setup");
    SetWindowMinSize(600, 480);
    SetTargetFPS(60);

    SetupContext ctx;
    setup_init(&ctx);

    TextField tf_sdroot = {0};

    // Load saved settings
    SavedConfig saved;
    load_config(&saved);
    if (saved.sd_root[0]) textfield_set(&tf_sdroot, saved.sd_root);
    if (saved.profile >= 0 && saved.profile < PROFILE_COUNT)
        ctx.config.profile = (ConsoleProfile)saved.profile;
    ctx.config.backup_first = saved.backup_first;

    SetupState prev_state = SETUP_IDLE;
    int log_filter = 0; // 0=All, 1=OK, 2=Err
    bool show_format_modal = false;

    // Auto-detect SD cards on launch
    DetectedDrive detected[8];
    int num_detected = platform_detect_sd(detected, 8);
    bool show_drive_picker = false;

    for (int i = 0; i < num_detected; i++) {
        if (detected[i].has_twilight) {
            textfield_set(&tf_sdroot, detected[i].path);
            setup_log(&ctx, "Auto-detected TwilightMenu SD: %s", detected[i].path);
            break;
        }
    }

    // Fetch versions on startup
    setup_fetch_versions(&ctx);

    while (!WindowShouldClose()) {
        int win_w = GetScreenWidth();
        int win_h = GetScreenHeight();

        // Completion notification
        if (prev_state == SETUP_RUNNING && ctx.state == SETUP_DONE) {
            platform_notify_done();
        }
        prev_state = ctx.state;

        // After format completes, update the SD path
        if (ctx.format_new_path[0] && ctx.state == SETUP_DONE) {
            textfield_set(&tf_sdroot, ctx.format_new_path);
            ctx.format_new_path[0] = '\0';
        }

        // Drag & drop a folder
        if (IsFileDropped()) {
            FilePathList dropped = LoadDroppedFiles();
            if (dropped.count > 0) {
                textfield_set(&tf_sdroot, dropped.paths[0]);
                size_t len = strlen(tf_sdroot.text);
                if (len > 1 && tf_sdroot.text[len-1] == '/') tf_sdroot.text[len-1] = '\0';
            }
            UnloadDroppedFiles(dropped);
        }

        // Keyboard shortcuts
        if (show_format_modal) {
            if (IsKeyPressed(KEY_ESCAPE)) show_format_modal = false;
        } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
            bool can_start = strlen(tf_sdroot.text) > 0;
            bool is_idle = ctx.state == SETUP_IDLE || ctx.state == SETUP_DONE;
            if (can_start && is_idle) {
                strncpy(ctx.config.sd_root, tf_sdroot.text, MAX_PATH_LEN - 1);
                setup_start(&ctx);
            }
        }
        if (!show_format_modal && IsKeyPressed(KEY_ESCAPE) && ctx.state == SETUP_RUNNING) {
            setup_stop(&ctx);
        }

        BeginDrawing();
        ClearBackground(BG_COLOR);

        int y = PAD;

        // ===== Title bar =====
        DrawText("DS SD Setup", PAD, y, 22, ACCENT_COLOR);
        DrawText("v1.0", PAD + MeasureText("DS SD Setup", 22) + 8, y + 5, SMALL_FONT, DIM_TEXT);
        y += 32;
        DrawLine(PAD, y, win_w - PAD, y, FIELD_BORDER);
        y += SECTION_GAP;

        // ===== SD Card Directory =====
        y = draw_section(y, "SD Card Directory");
        {
            float field_w = win_w - PAD * 2 - BROWSE_W - 8;
            Rectangle field = {PAD, (float)y, field_w, FIELD_H};
            if (!show_format_modal) textfield_handle(&tf_sdroot, field);
            textfield_draw(&tf_sdroot, field, "Select SD card folder or drag & drop...");

            Rectangle browse_btn = {PAD + field_w + 8, (float)y, BROWSE_W, FIELD_H};
            if (draw_button(browse_btn, "Browse", ACCENT_COLOR, true)) {
                char path[MAX_PATH_LEN];
                if (platform_folder_dialog("Select your SD card", path, sizeof(path))) {
                    textfield_set(&tf_sdroot, path);
                }
            }
        }
        y += FIELD_H + 6;

        // Drive detection
        {
            int bx = PAD;
            Rectangle detect_btn = {(float)bx, (float)y, 110, 26};
            if (draw_button(detect_btn, "Detect SD", ACCENT_DIM, true)) {
                num_detected = platform_detect_sd(detected, 8);
                if (num_detected > 0) {
                    show_drive_picker = true;
                    int nds_count = 0, nds_idx = -1;
                    for (int i = 0; i < num_detected; i++) {
                        if (detected[i].has_twilight) { nds_count++; nds_idx = i; }
                    }
                    if (nds_count == 1) {
                        textfield_set(&tf_sdroot, detected[nds_idx].path);
                        show_drive_picker = false;
                        setup_log(&ctx, "Auto-selected: %s", detected[nds_idx].path);
                    }
                } else {
                    setup_log(&ctx, "No removable drives found");
                    show_drive_picker = false;
                }
            }

            // Format SD button
            {
                bool can_fmt = strlen(tf_sdroot.text) > 0 &&
                               !show_format_modal &&
                               (ctx.state == SETUP_IDLE || ctx.state == SETUP_DONE);
                Rectangle fmt_btn = {(float)(bx + 118), (float)y, 100, 26};
                if (draw_button(fmt_btn, "Format SD", RED_ERR, can_fmt))
                    show_format_modal = true;
            }

            if (show_drive_picker && num_detected > 0) {
                bx += 226;
                for (int i = 0; i < num_detected; i++) {
                    char label[80];
                    snprintf(label, sizeof(label), "%s%s",
                             detected[i].label,
                             detected[i].has_twilight ? " *" : "");
                    int lw = MeasureText(label, SMALL_FONT) + 16;
                    Rectangle dbtn = {(float)bx, (float)y, (float)lw, 26};
                    Color dc = detected[i].has_twilight ? GREEN_OK : DISABLED_BG;
                    if (draw_button(dbtn, label, dc, true)) {
                        textfield_set(&tf_sdroot, detected[i].path);
                        show_drive_picker = false;
                    }
                    bx += lw + 6;
                    if (bx > win_w - PAD - 50) break;
                }
            }
        }
        y += 26 + SECTION_GAP;

        // ===== Console Profile =====
        y = draw_section(y, "Console Profile");
        {
            int rx = PAD;
            for (int i = 0; i < PROFILE_COUNT; i++) {
                if (draw_radio(rx, y, PROFILE_LABELS[i],
                               ctx.config.profile == (ConsoleProfile)i))
                    ctx.config.profile = (ConsoleProfile)i;
                rx += MeasureText(PROFILE_LABELS[i], SMALL_FONT) + 40;
            }
        }
        y += 26 + SECTION_GAP;

        // ===== Components =====
        y = draw_section(y, "Components");
        {
            // TwilightMenu++ checkbox + version
            draw_checkbox(PAD, y, "TwilightMenu++", &ctx.config.install_twilight);
            if (ctx.twilight.fetched) {
                int vx = PAD + 24 + MeasureText("TwilightMenu++", SMALL_FONT) + 12;
                DrawText(ctx.twilight.version, vx, y + 1, SMALL_FONT, GREEN_OK);
            } else if (ctx.state == SETUP_FETCHING) {
                int vx = PAD + 24 + MeasureText("TwilightMenu++", SMALL_FONT) + 12;
                DrawText("fetching...", vx, y + 1, SMALL_FONT, DIM_TEXT);
            }
            y += 24;

            // nds-bootstrap checkbox + version
            draw_checkbox(PAD, y, "nds-bootstrap", &ctx.config.install_bootstrap);
            if (ctx.bootstrap.fetched) {
                int vx = PAD + 24 + MeasureText("nds-bootstrap", SMALL_FONT) + 12;
                DrawText(ctx.bootstrap.version, vx, y + 1, SMALL_FONT, GREEN_OK);
            } else if (ctx.state == SETUP_FETCHING) {
                int vx = PAD + 24 + MeasureText("nds-bootstrap", SMALL_FONT) + 12;
                DrawText("fetching...", vx, y + 1, SMALL_FONT, DIM_TEXT);
            }
        }
        y += 26 + SECTION_GAP;

        // ===== Options =====
        y = draw_section(y, "Options");
        {
            draw_checkbox(PAD, y, "Backup existing _nds folder before setup",
                          &ctx.config.backup_first);
        }
        y += 26;

        // ===== Start / Stop =====
        DrawLine(PAD, y, win_w - PAD, y, FIELD_BORDER);
        y += 10;
        {
            bool can_start = strlen(tf_sdroot.text) > 0 &&
                             (ctx.config.install_twilight || ctx.config.install_bootstrap);
            bool is_running = ctx.state == SETUP_RUNNING;
            bool is_formatting = ctx.state == SETUP_FORMATTING;
            bool is_busy = is_running || is_formatting;
            bool is_idle = ctx.state == SETUP_IDLE || ctx.state == SETUP_DONE;

            Rectangle btn = {PAD, (float)y, 130, BTN_H};
            if (is_running) {
                if (draw_button(btn, "Stop", RED_ERR, true))
                    setup_stop(&ctx);
            } else if (is_formatting) {
                draw_button(btn, "Formatting...", BTN_DISABLED, false);
            } else {
                if (draw_button(btn, "Start Setup", ACCENT_COLOR, can_start && is_idle)) {
                    strncpy(ctx.config.sd_root, tf_sdroot.text, MAX_PATH_LEN - 1);
                    setup_start(&ctx);
                }
            }

            // Refresh button (re-fetch versions)
            Rectangle refresh_btn = {PAD + 138, (float)y, 80, BTN_H};
            if (draw_button(refresh_btn, "Refresh", ACCENT_DIM, is_idle && !is_busy)) {
                setup_fetch_versions(&ctx);
            }

            // Status text
            int sx = PAD + 228;
            if (is_formatting) {
                pthread_mutex_lock(&ctx.status_mutex);
                if (ctx.status_label[0]) {
                    DrawText(ctx.status_label, sx, y + 10, SMALL_FONT, ORANGE_WARN);
                } else {
                    DrawText("Formatting...", sx, y + 10, SMALL_FONT, ORANGE_WARN);
                }
                pthread_mutex_unlock(&ctx.status_mutex);
                DrawText("Do not remove the SD card!", sx, y - 6, SMALL_FONT - 2, RED_ERR);
            } else if (is_running) {
                pthread_mutex_lock(&ctx.status_mutex);
                if (ctx.status_label[0]) {
                    DrawText(ctx.status_label, sx, y + 10, SMALL_FONT, GREEN_OK);
                } else {
                    int dots = ((int)(GetTime() * 3)) % 4;
                    char status[64];
                    snprintf(status, sizeof(status), "Setting up%.*s", dots, "...");
                    DrawText(status, sx, y + 10, SMALL_FONT, GREEN_OK);
                }
                pthread_mutex_unlock(&ctx.status_mutex);
                DrawText("Do not remove the SD card!", sx, y - 6, SMALL_FONT - 2, RED_ERR);
            } else if (ctx.state == SETUP_DONE) {
                DrawText("Done! Safe to eject SD card.", sx, y + 10, SMALL_FONT, GREEN_OK);
            } else if (ctx.state == SETUP_FETCHING) {
                int dots = ((int)(GetTime() * 3)) % 4;
                char status[64];
                snprintf(status, sizeof(status), "Checking versions%.*s", dots, "...");
                DrawText(status, sx, y + 10, SMALL_FONT, DIM_TEXT);
            } else if (!can_start) {
                DrawText("Select an SD card to begin", sx, y + 10, SMALL_FONT, ORANGE_WARN);
            }
        }
        y += BTN_H + 10;

        // ===== Progress bar =====
        {
            pthread_mutex_lock(&ctx.status_mutex);
            int cur = ctx.dl_current;
            int tot = ctx.dl_total;
            pthread_mutex_unlock(&ctx.status_mutex);

            bool show_bar = ctx.state == SETUP_RUNNING && tot > 0;
            if (show_bar) {
                float progress = (float)cur / tot;
                if (progress > 1.0f) progress = 1.0f;
                Rectangle bar_bg = {PAD, (float)y, win_w - PAD * 2, 8};
                DrawRectangleRounded(bar_bg, 0.5f, 4, FIELD_BG);
                Rectangle bar_fill = {PAD, (float)y, (win_w - PAD * 2) * progress, 8};
                if (bar_fill.width > 0)
                    DrawRectangleRounded(bar_fill, 0.5f, 4, ACCENT_COLOR);
                char pct[16];
                snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100));
                DrawText(pct, win_w - PAD - MeasureText(pct, SMALL_FONT - 2),
                         y - 12, SMALL_FONT - 2, DIM_TEXT);
                y += 14;
            } else if (ctx.state == SETUP_RUNNING || ctx.state == SETUP_FORMATTING) {
                // Indeterminate bar (extraction/format phase)
                Rectangle bar_bg = {PAD, (float)y, win_w - PAD * 2, 8};
                DrawRectangleRounded(bar_bg, 0.5f, 4, FIELD_BG);
                float t = (float)GetTime();
                float pos = (float)(0.5 + 0.5 * sin(t * 3.0));
                float bar_w = (win_w - PAD * 2) * 0.2f;
                Rectangle bar_fill = {PAD + (win_w - PAD * 2 - bar_w) * pos, (float)y, bar_w, 8};
                DrawRectangleRounded(bar_fill, 0.5f, 4, ACCENT_COLOR);
                y += 14;
            }
        }

        // ===== Log panel =====
        y = draw_section(y, "Log");
        {
            // Filter buttons
            int fx = PAD + MeasureText("Log", SMALL_FONT) + 12;
            int fy = y - 18;
            const char *filters[] = {"All", "OK", "Err"};
            for (int i = 0; i < 3; i++) {
                int fw = MeasureText(filters[i], SMALL_FONT - 2) + 12;
                Rectangle fb = {(float)fx, (float)fy, (float)fw, 18};
                Color fc = (log_filter == i) ? ACCENT_COLOR : ACCENT_DIM;
                if (draw_button(fb, filters[i], fc, true))
                    log_filter = i;
                fx += fw + 4;
            }
        }
        {
            // Copy Logs button
            Rectangle copy_btn = {win_w - PAD - 90, (float)(y - 18), 90, 20};
            bool has_logs = ctx.log.count > 0;
            static float copy_flash = 0;
            if (copy_flash > 0) {
                DrawText("Copied!", (int)copy_btn.x, (int)copy_btn.y + 2, SMALL_FONT, GREEN_OK);
                copy_flash -= GetFrameTime();
            } else if (draw_button(copy_btn, "Copy Logs", ACCENT_DIM, has_logs)) {
                char *text = setup_get_log_text(&ctx);
                if (text) {
                    SetClipboardText(text);
                    free(text);
                    copy_flash = 1.5f;
                }
            }
        }
        {
            int log_h = win_h - y - PAD;
            Rectangle log_area = {PAD, (float)y, win_w - PAD * 2, (float)log_h};
            DrawRectangleRounded(log_area, 0.015f, 8, LOG_BG);
            DrawRectangleRoundedLinesEx(log_area, 0.015f, 8, 1, FIELD_BORDER);

            int line_h = SMALL_FONT + 3;
            int visible_lines = (log_h - 10) / line_h;

            // Mouse wheel scroll
            if (CheckCollisionPointRec(GetMousePosition(), log_area)) {
                int wheel = (int)GetMouseWheelMove();
                if (wheel != 0) {
                    pthread_mutex_lock(&ctx.log.mutex);
                    ctx.log.scroll_offset += wheel * 3;
                    int max_off = ctx.log.count > visible_lines ? ctx.log.count - visible_lines : 0;
                    if (ctx.log.scroll_offset < 0) ctx.log.scroll_offset = 0;
                    if (ctx.log.scroll_offset > max_off) ctx.log.scroll_offset = max_off;
                    pthread_mutex_unlock(&ctx.log.mutex);
                }
            }

            pthread_mutex_lock(&ctx.log.mutex);

            // Build filtered list
            int filtered[MAX_LOG_LINES];
            int filtered_count = 0;
            for (int i = 0; i < ctx.log.count; i++) {
                const char *line = ctx.log.lines[i];
                if (log_filter == 1 && strncmp(line, "OK:", 3) != 0) continue;
                if (log_filter == 2 && strstr(line, "Error") == NULL) continue;
                filtered[filtered_count++] = i;
            }

            int start = filtered_count > visible_lines ? filtered_count - visible_lines : 0;
            start -= ctx.log.scroll_offset;
            if (start < 0) start = 0;

            BeginScissorMode((int)log_area.x + 1, (int)log_area.y + 1,
                             (int)log_area.width - 2, (int)log_area.height - 2);

            int ly = y + 6;
            for (int fi = start; fi < filtered_count && (ly - y) < log_h - 6; fi++) {
                int i = filtered[fi];
                Color lc = DIM_TEXT;
                const char *line = ctx.log.lines[i];
                if (strncmp(line, "OK:", 3) == 0)
                    lc = GREEN_OK;
                else if (strstr(line, "Error"))
                    lc = RED_ERR;
                else if (strstr(line, "Done!") || strstr(line, "Auto-detected") ||
                         strstr(line, "Safe to eject"))
                    lc = GREEN_OK;
                else if (strstr(line, "latest:") || strstr(line, "Starting") ||
                         strstr(line, "Downloading") || strstr(line, "Extracting"))
                    lc = TEXT_COLOR;
                else if (strstr(line, "---"))
                    lc = FIELD_BORDER;

                DrawText(ctx.log.lines[i], (int)log_area.x + 10, ly, SMALL_FONT, lc);
                ly += line_h;
            }
            EndScissorMode();

            // Scrollbar
            if (filtered_count > visible_lines) {
                float ratio = (float)visible_lines / filtered_count;
                float sb_h = log_h * ratio;
                if (sb_h < 20) sb_h = 20;
                int max_off = filtered_count - visible_lines;
                float pos = max_off > 0 ? 1.0f - (float)ctx.log.scroll_offset / max_off : 0;
                float sb_y = y + pos * (log_h - sb_h);
                DrawRectangleRounded(
                    (Rectangle){log_area.x + log_area.width - 8, sb_y, 5, sb_h},
                    0.5f, 4, CLITERAL(Color){80, 80, 100, 180});
            }

            // Empty state
            if (ctx.log.count == 0) {
                const char *hint = "Drag & drop an SD card folder, use Browse, or click Detect SD";
                int hw = MeasureText(hint, SMALL_FONT);
                DrawText(hint, (int)(log_area.x + (log_area.width - hw) / 2),
                         (int)(log_area.y + log_h / 2 - 7), SMALL_FONT, DIM_TEXT);
            }

            pthread_mutex_unlock(&ctx.log.mutex);
        }

        // ===== Format confirmation modal =====
        if (show_format_modal) {
            DrawRectangle(0, 0, win_w, win_h, (Color){0, 0, 0, 160});

            int modal_w = 440, modal_h = 190;
            int mx = (win_w - modal_w) / 2;
            int my = (win_h - modal_h) / 2;
            Rectangle modal = {(float)mx, (float)my, (float)modal_w, (float)modal_h};
            DrawRectangleRounded(modal, 0.05f, 8, BG_COLOR);
            DrawRectangleRoundedLinesEx(modal, 0.05f, 8, 2, RED_ERR);

            DrawText("Format SD Card", mx + 20, my + 16, 18, RED_ERR);
            DrawText("This will ERASE ALL DATA on:", mx + 20, my + 50, SMALL_FONT, TEXT_COLOR);

            // Truncate path if too long for modal
            const char *disp_path = tf_sdroot.text;
            char trunc_path[64];
            if (MeasureText(disp_path, SMALL_FONT) > modal_w - 40) {
                snprintf(trunc_path, sizeof(trunc_path), "...%s",
                         tf_sdroot.text + strlen(tf_sdroot.text) - 40);
                disp_path = trunc_path;
            }
            DrawText(disp_path, mx + 20, my + 72, SMALL_FONT, ACCENT_COLOR);
            DrawText("Format as FAT32 with label DSSD (MBR)",
                     mx + 20, my + 98, SMALL_FONT, DIM_TEXT);

            Rectangle cancel_btn = {(float)(mx + modal_w - 230), (float)(my + modal_h - 50),
                                    100, BTN_H};
            Rectangle format_btn = {(float)(mx + modal_w - 120), (float)(my + modal_h - 50),
                                    100, BTN_H};

            if (draw_button(cancel_btn, "Cancel", ACCENT_DIM, true))
                show_format_modal = false;
            if (draw_button(format_btn, "Format", RED_ERR, true)) {
                show_format_modal = false;
                strncpy(ctx.config.sd_root, tf_sdroot.text, MAX_PATH_LEN - 1);
                setup_format_sd(&ctx);
            }
        }

        EndDrawing();
    }

    // Save settings before exit
    save_config(tf_sdroot.text, (int)ctx.config.profile, ctx.config.backup_first);

    CloseWindow();
    return 0;
}
