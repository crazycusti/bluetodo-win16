#include <stdio.h>
#include <string.h>

#include <windows.h>

#define UPD_PATH_CAPACITY 144
#define UPD_ARG_COUNT 3
#define UPD_RETRY_COUNT 120

static void upd_copy_text(char *target, const char *source, unsigned int capacity);
static int upd_parse_arg(const char **cursor, char *target, unsigned int capacity);
static int upd_build_backup_path(char *backup_path, const char *target_path, unsigned int capacity);
static int upd_install_update(const char *source_path, const char *target_path, const char *backup_path);

static void upd_copy_text(char *target, const char *source, unsigned int capacity)
{
    unsigned int index;

    if (target == NULL || capacity == 0) {
        return;
    }
    if (source == NULL) {
        target[0] = '\0';
        return;
    }

    for (index = 0; index + 1 < capacity && source[index] != '\0'; ++index) {
        target[index] = source[index];
    }
    target[index] = '\0';
}

static int upd_parse_arg(const char **cursor, char *target, unsigned int capacity)
{
    const char *source;
    unsigned int index;
    int quoted;

    if (cursor == NULL || *cursor == NULL || target == NULL || capacity == 0) {
        return 0;
    }

    source = *cursor;
    while (*source == ' ' || *source == '\t') {
        source += 1;
    }
    if (*source == '\0') {
        target[0] = '\0';
        return 0;
    }

    quoted = 0;
    if (*source == '"') {
        quoted = 1;
        source += 1;
    }

    index = 0;
    while (*source != '\0') {
        if (quoted) {
            if (*source == '"') {
                source += 1;
                break;
            }
        } else if (*source == ' ' || *source == '\t') {
            break;
        }

        if (index + 1 >= capacity) {
            target[0] = '\0';
            return 0;
        }
        target[index++] = *source++;
    }
    target[index] = '\0';

    while (*source == ' ' || *source == '\t') {
        source += 1;
    }
    *cursor = source;
    return target[0] != '\0';
}

static int upd_build_backup_path(char *backup_path, const char *target_path, unsigned int capacity)
{
    unsigned int index;
    int dot_index;
    int separator_index;

    if (backup_path == NULL || capacity == 0 || target_path == NULL || target_path[0] == '\0') {
        return 0;
    }

    upd_copy_text(backup_path, target_path, capacity);
    dot_index = -1;
    separator_index = -1;
    for (index = 0; backup_path[index] != '\0'; ++index) {
        if (backup_path[index] == '\\' || backup_path[index] == '/' || backup_path[index] == ':') {
            separator_index = (int)index;
            dot_index = -1;
        } else if (backup_path[index] == '.') {
            dot_index = (int)index;
        }
    }

    if (dot_index > separator_index) {
        backup_path[dot_index] = '\0';
    }
    if ((unsigned int)strlen(backup_path) + 5U >= capacity) {
        return 0;
    }
    strcat(backup_path, ".BAK");
    return 1;
}

static int upd_install_update(const char *source_path, const char *target_path, const char *backup_path)
{
    int attempt;
    int moved_old;

    for (attempt = 0; attempt < UPD_RETRY_COUNT; ++attempt) {
        moved_old = 0;
        remove(backup_path);
        if (rename(target_path, backup_path) == 0) {
            moved_old = 1;
        }

        if (rename(source_path, target_path) == 0) {
            return 1;
        }

        if (moved_old) {
            rename(backup_path, target_path);
        }
        Yield();
        Yield();
    }

    return 0;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    const char *cursor;
    char source_path[UPD_PATH_CAPACITY];
    char target_path[UPD_PATH_CAPACITY];
    char restart_path[UPD_PATH_CAPACITY];
    char backup_path[UPD_PATH_CAPACITY];
    UINT winexec_result;

    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    cursor = lpCmdLine;
    if (!upd_parse_arg(&cursor, source_path, sizeof(source_path)) ||
        !upd_parse_arg(&cursor, target_path, sizeof(target_path)) ||
        !upd_parse_arg(&cursor, restart_path, sizeof(restart_path))) {
        MessageBox(
            0,
            "Updater erwartet Quelle, Ziel und Restart-Pfad.",
            "BlueTodo Updater",
            MB_OK | MB_ICONSTOP
        );
        return 1;
    }

    if (!upd_build_backup_path(backup_path, target_path, sizeof(backup_path))) {
        MessageBox(
            0,
            "Backup-Pfad konnte nicht erstellt werden.",
            "BlueTodo Updater",
            MB_OK | MB_ICONSTOP
        );
        return 1;
    }

    if (!upd_install_update(source_path, target_path, backup_path)) {
        MessageBox(
            0,
            "Update konnte nicht installiert werden.\r\nDie laufende EXE ist noch gesperrt oder das Ziel ist nicht beschreibbar.",
            "BlueTodo Updater",
            MB_OK | MB_ICONSTOP
        );
        return 1;
    }

    winexec_result = WinExec(restart_path, SW_SHOWNORMAL);
    if (winexec_result <= 31U) {
        MessageBox(
            0,
            "Update installiert, aber der Neustart ist fehlgeschlagen.",
            "BlueTodo Updater",
            MB_OK | MB_ICONEXCLAMATION
        );
        return 1;
    }

    return 0;
}
