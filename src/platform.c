#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#endif

#if !defined(_WIN32)
#include <dirent.h>
#endif

// ---------- Folder dialog ----------

#if defined(_WIN32)

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
    BROWSEINFOA bi = {0};
    bi.lpszTitle = prompt;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (!pidl) return false;
    char path[MAX_PATH];
    bool ok = SHGetPathFromIDListA(pidl, path);
    CoTaskMemFree(pidl);
    if (ok && strlen(path) > 0) {
        strncpy(out, path, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

#elif defined(__APPLE__)

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "osascript -e 'set f to POSIX path of (choose folder with prompt \"%s\")' 2>/dev/null",
        prompt);

    FILE *fp = popen(cmd, "r");
    if (!fp) return false;

    char buf[MAX_PATH_LEN] = {0};
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        pclose(fp);
        return false;
    }
    int status = pclose(fp);
    if (status != 0) return false;

    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
    while (len > 1 && buf[len-1] == '/') buf[--len] = '\0';

    if (len == 0) return false;
    strncpy(out, buf, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

#else // Linux

bool platform_folder_dialog(const char *prompt, char *out, size_t out_len) {
    (void)prompt;
    const char *cmds[] = {
        "zenity --file-selection --directory 2>/dev/null",
        "kdialog --getexistingdirectory ~ 2>/dev/null",
        NULL
    };
    for (int i = 0; cmds[i]; i++) {
        FILE *fp = popen(cmds[i], "r");
        if (!fp) continue;
        char buf[MAX_PATH_LEN] = {0};
        if (fgets(buf, sizeof(buf), fp) != NULL) {
            int status = pclose(fp);
            if (status == 0) {
                size_t len = strlen(buf);
                while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = '\0';
                while (len > 1 && buf[len-1] == '/') buf[--len] = '\0';
                if (len > 0) {
                    strncpy(out, buf, out_len - 1);
                    out[out_len - 1] = '\0';
                    return true;
                }
            }
        } else {
            pclose(fp);
        }
    }
    return false;
}

#endif

// ---------- SD card detection ----------

#if defined(_WIN32)

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
    int count = 0;
    DWORD mask = GetLogicalDrives();
    for (int i = 3; i < 26 && count < max_drives; i++) {
        if (!(mask & (1 << i))) continue;
        char drive_root[] = { 'A' + i, ':', '\\', '\0' };
        UINT type = GetDriveTypeA(drive_root);
        if (type != DRIVE_REMOVABLE) continue;

        char vol_path[MAX_PATH_LEN];
        snprintf(vol_path, sizeof(vol_path), "%c:", 'A' + i);

        char nds_path[MAX_PATH_LEN];
        snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
        struct stat st;
        bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

        char vol_name[64] = {0};
        GetVolumeInformationA(drive_root, vol_name, sizeof(vol_name),
                              NULL, NULL, NULL, NULL, 0);

        strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
        if (vol_name[0])
            strncpy(drives[count].label, vol_name, 63);
        else
            snprintf(drives[count].label, 64, "Drive %c:", 'A' + i);
        drives[count].has_twilight = has_nds;
        count++;
    }
    return count;
}

#elif defined(__APPLE__)

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
    int count = 0;
    DIR *volumes = opendir("/Volumes");
    if (!volumes) return 0;

    struct dirent *entry;
    while ((entry = readdir(volumes)) != NULL && count < max_drives) {
        if (entry->d_name[0] == '.') continue;

        char vol_path[MAX_PATH_LEN];
        snprintf(vol_path, sizeof(vol_path), "/Volumes/%s", entry->d_name);

        struct stat st;
        if (stat(vol_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char nds_path[MAX_PATH_LEN];
        snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
        bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

        if (strcmp(entry->d_name, "Macintosh HD") == 0 ||
            strcmp(entry->d_name, "Macintosh HD - Data") == 0) continue;

        strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
        strncpy(drives[count].label, entry->d_name, 63);
        drives[count].has_twilight = has_nds;
        count++;
    }
    closedir(volumes);
    return count;
}

#else // Linux

int platform_detect_sd(DetectedDrive *drives, int max_drives) {
    int count = 0;
    const char *user = getenv("USER");
    if (!user) user = "root";

    char media_paths[2][MAX_PATH_LEN];
    snprintf(media_paths[0], MAX_PATH_LEN, "/media/%s", user);
    snprintf(media_paths[1], MAX_PATH_LEN, "/run/media/%s", user);

    for (int mp = 0; mp < 2 && count < max_drives; mp++) {
        DIR *volumes = opendir(media_paths[mp]);
        if (!volumes) continue;

        struct dirent *entry;
        while ((entry = readdir(volumes)) != NULL && count < max_drives) {
            if (entry->d_name[0] == '.') continue;

            char vol_path[MAX_PATH_LEN];
            snprintf(vol_path, sizeof(vol_path), "%s/%s", media_paths[mp], entry->d_name);

            struct stat st;
            if (stat(vol_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

            char nds_path[MAX_PATH_LEN];
            snprintf(nds_path, sizeof(nds_path), "%s/_nds", vol_path);
            bool has_nds = (stat(nds_path, &st) == 0 && S_ISDIR(st.st_mode));

            strncpy(drives[count].path, vol_path, MAX_PATH_LEN - 1);
            strncpy(drives[count].label, entry->d_name, 63);
            drives[count].has_twilight = has_nds;
            count++;
        }
        closedir(volumes);
    }
    return count;
}

#endif

// ---------- SD card formatting ----------

#if defined(_WIN32)

bool platform_format_sd(const char *volume_path, const char *new_label,
                        char *new_path_out, size_t new_path_len,
                        char *error_out, size_t error_len) {
    // Windows: use format command (only works for <= 32GB as FAT32)
    if (strlen(volume_path) < 2 || volume_path[1] != ':') {
        snprintf(error_out, error_len, "Invalid drive path");
        return false;
    }
    char drive_letter = volume_path[0];

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "format %c: /FS:FAT32 /Q /V:%s /Y 2>&1",
             drive_letter, new_label);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(error_out, error_len, "Failed to run format command");
        return false;
    }
    char output[1024] = {0};
    size_t total = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        if (total + ll < sizeof(output) - 1) {
            memcpy(output + total, line, ll);
            total += ll;
        }
    }
    output[total] = '\0';
    int status = pclose(fp);

    if (status != 0) {
        snprintf(error_out, error_len, "Format failed: %.200s", output);
        return false;
    }

    snprintf(new_path_out, new_path_len, "%c:", drive_letter);
    return true;
}

#elif defined(__APPLE__)

bool platform_format_sd(const char *volume_path, const char *new_label,
                        char *new_path_out, size_t new_path_len,
                        char *error_out, size_t error_len) {
    // Validate label (FAT32: max 11 chars, uppercase alphanumeric)
    size_t label_len = strlen(new_label);
    if (label_len == 0 || label_len > 11) {
        snprintf(error_out, error_len, "Volume label must be 1-11 characters");
        return false;
    }
    for (size_t i = 0; i < label_len; i++) {
        char c = new_label[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            snprintf(error_out, error_len, "Label must be uppercase alphanumeric");
            return false;
        }
    }

    // Get device identifier from diskutil info
    char cmd[MAX_PATH_LEN + 256];
    snprintf(cmd, sizeof(cmd),
             "diskutil info '%s' 2>/dev/null | awk '/Device Identifier/{print $NF}'",
             volume_path);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(error_out, error_len, "Failed to query disk info");
        return false;
    }
    char part_id[64] = {0};
    if (fgets(part_id, sizeof(part_id), fp) == NULL) {
        pclose(fp);
        snprintf(error_out, error_len, "Could not read device identifier");
        return false;
    }
    pclose(fp);

    size_t len = strlen(part_id);
    while (len > 0 && (part_id[len-1] == '\n' || part_id[len-1] == '\r'))
        part_id[--len] = '\0';

    if (len == 0) {
        snprintf(error_out, error_len, "Could not determine device for '%s'", volume_path);
        return false;
    }

    // Get whole disk (e.g., disk4s1 -> disk4)
    char whole_disk[64];
    strncpy(whole_disk, part_id, sizeof(whole_disk) - 1);
    whole_disk[sizeof(whole_disk) - 1] = '\0';
    for (int i = (int)strlen(whole_disk) - 1; i > 0; i--) {
        if (whole_disk[i] == 's') {
            bool all_digits = true;
            for (int j = i + 1; whole_disk[j]; j++) {
                if (whole_disk[j] < '0' || whole_disk[j] > '9') {
                    all_digits = false;
                    break;
                }
            }
            if (all_digits && whole_disk[i + 1] != '\0') {
                whole_disk[i] = '\0';
                break;
            }
        }
    }

    // Safety: verify the disk is external (not internal)
    snprintf(cmd, sizeof(cmd),
             "diskutil info /dev/%s 2>/dev/null | awk '/Internal:/{print $NF}'",
             whole_disk);
    fp = popen(cmd, "r");
    if (fp) {
        char check[16] = {0};
        if (fgets(check, sizeof(check), fp) != NULL) {
            pclose(fp);
            size_t cl = strlen(check);
            while (cl > 0 && (check[cl-1] == '\n' || check[cl-1] == '\r'))
                check[--cl] = '\0';
            if (strcmp(check, "No") != 0) {
                snprintf(error_out, error_len,
                         "Refusing to format internal disk (%s)", whole_disk);
                return false;
            }
        } else {
            pclose(fp);
        }
    }

    // Format: eraseDisk FAT32 <label> MBRFormat /dev/<whole_disk>
    snprintf(cmd, sizeof(cmd),
             "diskutil eraseDisk FAT32 %s MBRFormat /dev/%s 2>&1",
             new_label, whole_disk);

    fp = popen(cmd, "r");
    if (!fp) {
        snprintf(error_out, error_len, "Failed to run diskutil");
        return false;
    }
    char output[2048] = {0};
    size_t total = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        if (total + ll < sizeof(output) - 1) {
            memcpy(output + total, line, ll);
            total += ll;
        }
    }
    output[total] = '\0';
    int status = pclose(fp);

    if (status != 0) {
        char *nl = strchr(output, '\n');
        if (nl) *nl = '\0';
        snprintf(error_out, error_len, "%s", output);
        return false;
    }

    snprintf(new_path_out, new_path_len, "/Volumes/%s", new_label);
    return true;
}

#else // Linux

bool platform_format_sd(const char *volume_path, const char *new_label,
                        char *new_path_out, size_t new_path_len,
                        char *error_out, size_t error_len) {
    // Get device from mount point
    char cmd[MAX_PATH_LEN + 128];
    snprintf(cmd, sizeof(cmd), "findmnt -n -o SOURCE '%s' 2>/dev/null", volume_path);
    FILE *fp = popen(cmd, "r");
    if (!fp) {
        snprintf(error_out, error_len, "Failed to find device for mount point");
        return false;
    }
    char device[128] = {0};
    if (fgets(device, sizeof(device), fp) == NULL) {
        pclose(fp);
        snprintf(error_out, error_len, "Could not determine device for '%s'", volume_path);
        return false;
    }
    pclose(fp);

    size_t len = strlen(device);
    while (len > 0 && (device[len-1] == '\n' || device[len-1] == '\r'))
        device[--len] = '\0';

    if (len == 0) {
        snprintf(error_out, error_len, "No device found for '%s'", volume_path);
        return false;
    }

    // Safety: refuse system disks
    if (strncmp(device, "/dev/sda", 8) == 0 ||
        strncmp(device, "/dev/nvme0n1", 12) == 0 ||
        strncmp(device, "/dev/vda", 8) == 0) {
        snprintf(error_out, error_len, "Refusing to format suspected system disk (%s)", device);
        return false;
    }

    // Unmount
    snprintf(cmd, sizeof(cmd), "umount '%s' 2>&1", volume_path);
    if (system(cmd) != 0) {
        snprintf(cmd, sizeof(cmd), "pkexec umount '%s' 2>&1", volume_path);
        if (system(cmd) != 0) {
            snprintf(error_out, error_len, "Failed to unmount %s", volume_path);
            return false;
        }
    }

    // Format as FAT32
    snprintf(cmd, sizeof(cmd), "mkfs.fat -F 32 -n '%s' '%s' 2>&1", new_label, device);
    fp = popen(cmd, "r");
    if (!fp) {
        snprintf(error_out, error_len, "Failed to run mkfs.fat");
        return false;
    }
    char output[1024] = {0};
    size_t total = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        size_t ll = strlen(line);
        if (total + ll < sizeof(output) - 1) {
            memcpy(output + total, line, ll);
            total += ll;
        }
    }
    output[total] = '\0';
    int status = pclose(fp);

    if (status != 0) {
        // Try with pkexec
        snprintf(cmd, sizeof(cmd), "pkexec mkfs.fat -F 32 -n '%s' '%s' 2>&1",
                 new_label, device);
        status = system(cmd);
        if (status != 0) {
            snprintf(error_out, error_len, "mkfs.fat failed: %.200s", output);
            return false;
        }
    }

    // Remount via udisksctl
    snprintf(cmd, sizeof(cmd), "udisksctl mount -b '%s' 2>/dev/null", device);
    fp = popen(cmd, "r");
    if (fp) {
        char mount_out[256] = {0};
        if (fgets(mount_out, sizeof(mount_out), fp) != NULL) {
            pclose(fp);
            char *at = strstr(mount_out, " at ");
            if (at) {
                at += 4;
                size_t al = strlen(at);
                while (al > 0 && (at[al-1] == '\n' || at[al-1] == '.' || at[al-1] == '\r'))
                    at[--al] = '\0';
                strncpy(new_path_out, at, new_path_len - 1);
                new_path_out[new_path_len - 1] = '\0';
                return true;
            }
        } else {
            pclose(fp);
        }
    }

    // Fallback path
    const char *user = getenv("USER");
    if (user)
        snprintf(new_path_out, new_path_len, "/media/%s/%s", user, new_label);
    else
        snprintf(new_path_out, new_path_len, "/media/%s", new_label);
    return true;
}

#endif

// ---------- Completion notification ----------

void platform_notify_done(void) {
#if defined(__APPLE__)
    system("osascript -e 'display notification \"SD card setup complete!\" with title \"DS SD Setup\"' &");
#elif defined(__linux__)
    system("notify-send 'DS SD Setup' 'SD card setup complete!' 2>/dev/null &");
#endif
}
