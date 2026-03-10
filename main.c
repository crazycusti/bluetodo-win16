#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "client_version.h"
#include "proto.h"
#include "resource.h"

#ifdef WIN32
#define COMMAND_ID(wparam, lparam) LOWORD(wparam)
#define COMMAND_NOTIFY(wparam, lparam) HIWORD(wparam)
#else
#define COMMAND_ID(wparam, lparam) ((UINT)(wparam))
#define COMMAND_NOTIFY(wparam, lparam) HIWORD(lparam)
#endif

#define APP_PATH_CAPACITY 144
#define EDITOR_MODE_TODO 1
#define EDITOR_MODE_TASK 2

typedef struct AppStateTag {
    HINSTANCE instance;
    HWND window;
    int show_archived;
    char config_path[APP_PATH_CAPACITY];
    char pending_update_command[512];
    ProtoClient client;
    ProtoTodo *todos;
    int todo_count;
    ProtoTask *tasks;
    int task_count;
    struct MainLayoutTag {
        int initialized;
        int min_client_width;
        int min_client_height;
        int left_margin;
        int right_margin;
        int button_gap;
        int column_gap;
        int server_info_top;
        int server_info_height;
        int button_row_top;
        int button_height;
        int labels_top;
        int label_height;
        int lists_top;
        int gap_list_to_todo_info;
        int todo_info_height;
        int gap_todo_to_budget;
        int budget_info_height;
        int gap_budget_to_status;
        int status_height;
        int bottom_margin;
    } layout;
} AppState;

typedef struct EditorDialogTag {
    char caption[48];
    int mode;
    char title[PROTO_TITLE_CAPACITY];
    char description[PROTO_DESC_CAPACITY];
    char order_number[PROTO_TITLE_CAPACITY];
    char purchaser[PROTO_TITLE_CAPACITY];
    char order_date[PROTO_DEADLINE_CAPACITY];
    char budget_spent[PROTO_MONEY_CAPACITY];
    char budget_planned[PROTO_MONEY_CAPACITY];
    char deadline[PROTO_DEADLINE_CAPACITY];
    char amount[PROTO_MONEY_CAPACITY];
} EditorDialog;

static AppState g_app;
static ProtoTodo g_todo_storage[PROTO_MAX_TODOS];
static ProtoTask g_task_storage[PROTO_MAX_TASKS];
static EditorDialog *g_editor_dialog;

static void app_copy_text(char *target, const char *source, unsigned int capacity);
static int app_get_module_path(char *path, unsigned int capacity);
static int app_get_windows_path(char *path, unsigned int capacity);
static void app_get_directory_path(char *path);
static void app_get_stage_directory(char *path, unsigned int capacity);
static int app_join_path(char *target, unsigned int capacity, const char *directory, const char *name);
static int app_file_exists(const char *path);
static int app_directory_writable(const char *directory);
static void app_select_config_path(void);
static int app_save_config(void);
static void app_load_config(void);
static int app_compare_versions(const char *left, const char *right);
static int app_build_update_command(
    char *command,
    unsigned int capacity,
    const char *helper_path,
    const char *source_path,
    const char *target_path,
    const char *restart_path
);
static void app_set_status(const char *message);
static void app_update_view_mode(void);
static void app_update_server_info(void);
static void app_update_server_manager_state(HWND window);
static void app_clear_lists(void);
static void app_init_layout(void);
static void app_layout_main_window(void);
static void app_update_todo_details(void);
static int app_selected_todo_index(void);
static long app_selected_todo_id(void);
static long app_selected_task_id(void);
static void app_select_todo_by_id(long todo_id);
static void app_load_tasks(void);
static void app_load_todos(void);
static int app_prompt_for_item(EditorDialog *dialog);
static unsigned short app_parse_port_text(const char *text);
static int app_money_text_to_cents(const char *text, long *out_cents);
static void app_format_cents(long cents, char *buffer, unsigned int capacity);
static int app_normalize_money_text(char *target, const char *source, unsigned int capacity);
static int app_connect_to(const char *host, unsigned short port, const char *token);
static void app_disconnect(void);
static void app_show_server_manager(void);
static void app_update_client(int force_update);
static void app_refresh(void);
static void app_add_todo(void);
static void app_add_task(void);
static void app_edit_todo(void);
static void app_edit_task(void);
static void app_toggle_task(void);
static void app_toggle_archive_view(void);
static void app_archive_todo(void);
static void app_restore_todo(void);
static void app_show_info(void);

static BOOL FAR PASCAL main_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static BOOL FAR PASCAL editor_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
static BOOL FAR PASCAL server_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

static void app_copy_text(char *target, const char *source, unsigned int capacity)
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

static int app_get_module_path(char *path, unsigned int capacity)
{
    if (path == NULL || capacity == 0) {
        return 0;
    }

    path[0] = '\0';
    if (GetModuleFileName(g_app.instance, path, capacity) <= 0) {
        return 0;
    }
    return path[0] != '\0';
}

static int app_get_windows_path(char *path, unsigned int capacity)
{
    if (path == NULL || capacity == 0) {
        return 0;
    }

    path[0] = '\0';
    if (GetWindowsDirectory(path, capacity) == 0) {
        return 0;
    }
    return path[0] != '\0';
}

static void app_get_directory_path(char *path)
{
    int index;
    int separator;

    if (path == NULL || path[0] == '\0') {
        return;
    }

    separator = -1;
    for (index = 0; path[index] != '\0'; ++index) {
        if (path[index] == '\\' || path[index] == '/' || path[index] == ':') {
            separator = index;
        }
    }

    if (separator < 0) {
        path[0] = '.';
        path[1] = '\0';
    } else if (separator == 1 && path[1] == ':') {
        path[2] = '\0';
    } else {
        path[separator] = '\0';
    }
}

static void app_get_stage_directory(char *path, unsigned int capacity)
{
    char module_path[APP_PATH_CAPACITY];

    if (path == NULL || capacity == 0) {
        return;
    }

    path[0] = '\0';
    if (g_app.config_path[0] != '\0') {
        app_copy_text(path, g_app.config_path, capacity);
        app_get_directory_path(path);
        return;
    }

    if (app_get_module_path(module_path, sizeof(module_path))) {
        app_copy_text(path, module_path, capacity);
        app_get_directory_path(path);
        return;
    }

    if (app_get_windows_path(path, capacity)) {
        return;
    }

    app_copy_text(path, ".", capacity);
}

static int app_join_path(char *target, unsigned int capacity, const char *directory, const char *name)
{
    unsigned int length;

    if (target == NULL || capacity == 0 || name == NULL || name[0] == '\0') {
        return 0;
    }

    target[0] = '\0';
    if (directory != NULL && directory[0] != '\0' && !(directory[0] == '.' && directory[1] == '\0')) {
        app_copy_text(target, directory, capacity);
        length = (unsigned int)strlen(target);
        if (length + 1 >= capacity) {
            return 0;
        }
        if (target[length - 1] != '\\' && target[length - 1] != '/' && target[length - 1] != ':') {
            target[length++] = '\\';
            target[length] = '\0';
        }
    }

    length = (unsigned int)strlen(target);
    if (length + (unsigned int)strlen(name) + 1U > capacity) {
        return 0;
    }

    strcat(target, name);
    return 1;
}

static int app_file_exists(const char *path)
{
    FILE *file;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

static int app_directory_writable(const char *directory)
{
    char probe_path[APP_PATH_CAPACITY];
    FILE *file;

    if (directory == NULL || directory[0] == '\0') {
        return 0;
    }
    if (!app_join_path(probe_path, sizeof(probe_path), directory, "BLUETODO.$$$")) {
        return 0;
    }

    remove(probe_path);
    file = fopen(probe_path, "wb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    remove(probe_path);
    return 1;
}

static void app_select_config_path(void)
{
    char module_path[APP_PATH_CAPACITY];
    char module_dir[APP_PATH_CAPACITY];
    char module_ini[APP_PATH_CAPACITY];
    char windows_dir[APP_PATH_CAPACITY];
    char windows_ini[APP_PATH_CAPACITY];

    g_app.config_path[0] = '\0';
    module_ini[0] = '\0';
    windows_ini[0] = '\0';

    if (app_get_module_path(module_path, sizeof(module_path))) {
        app_copy_text(module_dir, module_path, sizeof(module_dir));
        app_get_directory_path(module_dir);
        app_join_path(module_ini, sizeof(module_ini), module_dir, "BLUETODO.INI");
    }
    if (app_get_windows_path(windows_dir, sizeof(windows_dir))) {
        app_join_path(windows_ini, sizeof(windows_ini), windows_dir, "BLUETODO.INI");
    }

    if (module_ini[0] != '\0' && app_file_exists(module_ini)) {
        app_copy_text(g_app.config_path, module_ini, sizeof(g_app.config_path));
        return;
    }
    if (windows_ini[0] != '\0' && app_file_exists(windows_ini)) {
        app_copy_text(g_app.config_path, windows_ini, sizeof(g_app.config_path));
        return;
    }
    if (module_ini[0] != '\0' && app_directory_writable(module_dir)) {
        app_copy_text(g_app.config_path, module_ini, sizeof(g_app.config_path));
        return;
    }
    if (windows_ini[0] != '\0') {
        app_copy_text(g_app.config_path, windows_ini, sizeof(g_app.config_path));
        return;
    }
    if (module_ini[0] != '\0') {
        app_copy_text(g_app.config_path, module_ini, sizeof(g_app.config_path));
    }
}

static int app_save_config(void)
{
    char port_text[16];
    char windows_dir[APP_PATH_CAPACITY];
    char windows_ini[APP_PATH_CAPACITY];
    const char *path;

    if (g_app.config_path[0] == '\0') {
        app_select_config_path();
    }
    if (g_app.config_path[0] == '\0') {
        return 0;
    }

    path = g_app.config_path;
    wsprintf(port_text, "%u", (unsigned int)(g_app.client.port > 0 ? g_app.client.port : 5877));
    if (WritePrivateProfileString("server", "host", g_app.client.host, path) &&
        WritePrivateProfileString("server", "port", port_text, path) &&
        WritePrivateProfileString("server", "token", g_app.client.token, path)) {
        WritePrivateProfileString(NULL, NULL, NULL, path);
        return 1;
    }

    if (!app_get_windows_path(windows_dir, sizeof(windows_dir)) ||
        !app_join_path(windows_ini, sizeof(windows_ini), windows_dir, "BLUETODO.INI")) {
        return 0;
    }
    if (strcmp(path, windows_ini) == 0) {
        return 0;
    }

    if (!WritePrivateProfileString("server", "host", g_app.client.host, windows_ini) ||
        !WritePrivateProfileString("server", "port", port_text, windows_ini) ||
        !WritePrivateProfileString("server", "token", g_app.client.token, windows_ini)) {
        return 0;
    }

    WritePrivateProfileString(NULL, NULL, NULL, windows_ini);
    app_copy_text(g_app.config_path, windows_ini, sizeof(g_app.config_path));
    return 1;
}

static void app_load_config(void)
{
    char host[PROTO_HOST_CAPACITY];
    char port_text[16];
    char token[PROTO_TOKEN_CAPACITY];
    int existed;

    app_select_config_path();
    if (g_app.config_path[0] == '\0') {
        return;
    }

    existed = app_file_exists(g_app.config_path);
    GetPrivateProfileString("server", "host", g_app.client.host, host, sizeof(host), g_app.config_path);
    GetPrivateProfileString("server", "port", "5877", port_text, sizeof(port_text), g_app.config_path);
    GetPrivateProfileString("server", "token", g_app.client.token, token, sizeof(token), g_app.config_path);

    app_copy_text(g_app.client.host, host, sizeof(g_app.client.host));
    app_copy_text(g_app.client.token, token, sizeof(g_app.client.token));
    g_app.client.port = app_parse_port_text(port_text);

    if (!existed) {
        app_save_config();
    }
}

static int app_compare_versions(const char *left, const char *right)
{
    const char *left_cursor;
    const char *right_cursor;

    left_cursor = left != NULL ? left : "";
    right_cursor = right != NULL ? right : "";

    while (*left_cursor != '\0' || *right_cursor != '\0') {
        long left_value;
        long right_value;

        left_value = 0L;
        while (*left_cursor >= '0' && *left_cursor <= '9') {
            left_value = (left_value * 10L) + (long)(*left_cursor - '0');
            left_cursor += 1;
        }

        right_value = 0L;
        while (*right_cursor >= '0' && *right_cursor <= '9') {
            right_value = (right_value * 10L) + (long)(*right_cursor - '0');
            right_cursor += 1;
        }

        if (left_value < right_value) {
            return -1;
        }
        if (left_value > right_value) {
            return 1;
        }

        while (*left_cursor != '\0' && *left_cursor != '.') {
            left_cursor += 1;
        }
        while (*right_cursor != '\0' && *right_cursor != '.') {
            right_cursor += 1;
        }
        if (*left_cursor == '.') {
            left_cursor += 1;
        }
        if (*right_cursor == '.') {
            right_cursor += 1;
        }
    }

    return 0;
}

static int app_build_update_command(
    char *command,
    unsigned int capacity,
    const char *helper_path,
    const char *source_path,
    const char *target_path,
    const char *restart_path
)
{
    unsigned int needed;

    if (command == NULL ||
        capacity == 0 ||
        helper_path == NULL ||
        source_path == NULL ||
        target_path == NULL ||
        restart_path == NULL) {
        return 0;
    }

    needed = (unsigned int)strlen(helper_path) +
        (unsigned int)strlen(source_path) +
        (unsigned int)strlen(target_path) +
        (unsigned int)strlen(restart_path) +
        14U;
    if (needed >= capacity) {
        command[0] = '\0';
        return 0;
    }

    wsprintf(command, "%s %s %s %s", helper_path, source_path, target_path, restart_path);
    return 1;
}

static void app_set_status(const char *message)
{
    char status_text[160];

    if (g_app.window == 0) {
        return;
    }

    wsprintf(status_text, "Status: %s", message != NULL ? message : "");
    SetDlgItemText(g_app.window, IDC_STATUS, status_text);
}

static void app_update_view_mode(void)
{
    if (g_app.window == 0) {
        return;
    }

    SetDlgItemText(g_app.window, IDC_ARCHIVEVIEW, g_app.show_archived ? "Aktiv" : "Archiv");
    EnableWindow(GetDlgItem(g_app.window, IDC_ARCHIVE), g_app.show_archived ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g_app.window, IDC_RESTORE), g_app.show_archived ? TRUE : FALSE);
    EnableWindow(GetDlgItem(g_app.window, IDC_ADDTODO), g_app.show_archived ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g_app.window, IDC_ADDTASK), g_app.show_archived ? FALSE : TRUE);
    EnableWindow(GetDlgItem(g_app.window, IDC_TOGGLE), g_app.show_archived ? FALSE : TRUE);
}

static void app_update_server_info(void)
{
    char info[176];

    if (g_app.window == 0) {
        return;
    }

    if (proto_client_connected(&g_app.client) && g_app.client.server_version[0] != '\0') {
        wsprintf(
            info,
            "Server: %s:%d | verbunden | %s",
            g_app.client.host,
            (int)g_app.client.port,
            g_app.client.server_version
        );
    } else {
        wsprintf(
            info,
            "Server: %s:%d | %s",
            g_app.client.host,
            (int)g_app.client.port,
            proto_client_connected(&g_app.client) ? "verbunden" : "nicht verbunden"
        );
    }
    SetDlgItemText(g_app.window, IDC_SERVERINFO, info);
}

static void app_update_server_manager_state(HWND window)
{
    char info[176];

    if (window == 0) {
        return;
    }

    if (proto_client_connected(&g_app.client) && g_app.client.server_version[0] != '\0') {
        wsprintf(
            info,
            "Aktiv: %s:%d | %s",
            g_app.client.host,
            (int)g_app.client.port,
            g_app.client.server_version
        );
    } else {
        wsprintf(
            info,
            "Aktiv: %s:%d | %s",
            g_app.client.host,
            (int)g_app.client.port,
            proto_client_connected(&g_app.client) ? "verbunden" : "nicht verbunden"
        );
    }

    SetDlgItemText(window, IDC_SERVER_STATE, info);
    EnableWindow(GetDlgItem(window, IDC_DISCONNECT), proto_client_connected(&g_app.client) ? TRUE : FALSE);
}

static void app_control_rect(HWND parent, int control_id, RECT *rect)
{
    HWND control;
    POINT points[2];

    if (rect == NULL) {
        return;
    }

    control = GetDlgItem(parent, control_id);
    if (control == 0) {
        memset(rect, 0, sizeof(*rect));
        return;
    }

    GetWindowRect(control, rect);
    points[0].x = rect->left;
    points[0].y = rect->top;
    points[1].x = rect->right;
    points[1].y = rect->bottom;
    ScreenToClient(parent, &points[0]);
    ScreenToClient(parent, &points[1]);
    rect->left = points[0].x;
    rect->top = points[0].y;
    rect->right = points[1].x;
    rect->bottom = points[1].y;
}

static void app_init_layout(void)
{
    RECT client_rect;
    RECT server_info_rect;
    RECT refresh_rect;
    RECT add_todo_rect;
    RECT todos_label_rect;
    RECT todos_rect;
    RECT tasks_rect;
    RECT todo_info_rect;
    RECT budget_info_rect;
    RECT status_rect;

    if (g_app.window == 0) {
        return;
    }

    GetClientRect(g_app.window, &client_rect);
    app_control_rect(g_app.window, IDC_SERVERINFO, &server_info_rect);
    app_control_rect(g_app.window, IDC_REFRESH, &refresh_rect);
    app_control_rect(g_app.window, IDC_ADDTODO, &add_todo_rect);
    app_control_rect(g_app.window, IDC_TODOS_LABEL, &todos_label_rect);
    app_control_rect(g_app.window, IDC_TODOS, &todos_rect);
    app_control_rect(g_app.window, IDC_TASKS, &tasks_rect);
    app_control_rect(g_app.window, IDC_TODOINFO, &todo_info_rect);
    app_control_rect(g_app.window, IDC_BUDGETINFO, &budget_info_rect);
    app_control_rect(g_app.window, IDC_STATUS, &status_rect);

    g_app.layout.initialized = 1;
    g_app.layout.min_client_width = client_rect.right;
    g_app.layout.min_client_height = client_rect.bottom;
    g_app.layout.left_margin = server_info_rect.left;
    g_app.layout.right_margin = client_rect.right - server_info_rect.right;
    g_app.layout.button_gap = add_todo_rect.left - refresh_rect.right;
    g_app.layout.column_gap = tasks_rect.left - todos_rect.right;
    g_app.layout.server_info_top = server_info_rect.top;
    g_app.layout.server_info_height = server_info_rect.bottom - server_info_rect.top;
    g_app.layout.button_row_top = refresh_rect.top;
    g_app.layout.button_height = refresh_rect.bottom - refresh_rect.top;
    g_app.layout.labels_top = todos_label_rect.top;
    g_app.layout.label_height = todos_label_rect.bottom - todos_label_rect.top;
    g_app.layout.lists_top = todos_rect.top;
    g_app.layout.gap_list_to_todo_info = todo_info_rect.top - todos_rect.bottom;
    g_app.layout.todo_info_height = todo_info_rect.bottom - todo_info_rect.top;
    g_app.layout.gap_todo_to_budget = budget_info_rect.top - todo_info_rect.bottom;
    g_app.layout.budget_info_height = budget_info_rect.bottom - budget_info_rect.top;
    g_app.layout.gap_budget_to_status = status_rect.top - budget_info_rect.bottom;
    g_app.layout.status_height = status_rect.bottom - status_rect.top;
    g_app.layout.bottom_margin = client_rect.bottom - status_rect.bottom;
    if (g_app.layout.status_height < 12) {
        g_app.layout.status_height = 12;
    }
}

static void app_layout_main_window(void)
{
    static const int buttons[] = {
        IDC_REFRESH,
        IDC_ADDTODO,
        IDC_ADDTASK,
        IDC_TOGGLE,
        IDC_ARCHIVEVIEW,
        IDC_ARCHIVE,
        IDC_RESTORE
    };
    RECT client_rect;
    int client_width;
    int client_height;
    int available_width;
    int button_width;
    int button_row_width;
    int column_width;
    int status_y;
    int budget_y;
    int todo_y;
    int list_height;
    int left;
    int row_left;
    int tasks_left;
    int index;

    if (g_app.window == 0 || !g_app.layout.initialized) {
        return;
    }

    GetClientRect(g_app.window, &client_rect);
    client_width = client_rect.right;
    client_height = client_rect.bottom;
    available_width = client_width - g_app.layout.left_margin - g_app.layout.right_margin;
    if (available_width < 140) {
        return;
    }

    MoveWindow(
        GetDlgItem(g_app.window, IDC_SERVERINFO),
        g_app.layout.left_margin,
        g_app.layout.server_info_top,
        available_width,
        g_app.layout.server_info_height,
        TRUE
    );

    button_width = (available_width - g_app.layout.button_gap * 6) / 7;
    if (button_width < 28) {
        button_width = 28;
    }
    button_row_width = (button_width * 7) + (g_app.layout.button_gap * 6);
    if (button_row_width > available_width) {
        button_width = (available_width - g_app.layout.button_gap * 6) / 7;
        if (button_width < 22) {
            button_width = 22;
        }
        button_row_width = (button_width * 7) + (g_app.layout.button_gap * 6);
    }

    row_left = g_app.layout.left_margin + (available_width - button_row_width) / 2;
    if (row_left < g_app.layout.left_margin) {
        row_left = g_app.layout.left_margin;
    }

    left = row_left;
    for (index = 0; index < 7; ++index) {
        MoveWindow(
            GetDlgItem(g_app.window, buttons[index]),
            left,
            g_app.layout.button_row_top,
            button_width,
            g_app.layout.button_height,
            TRUE
        );
        left += button_width + g_app.layout.button_gap;
    }

    column_width = (available_width - g_app.layout.column_gap) / 2;
    if (column_width < 60) {
        column_width = 60;
    }
    tasks_left = g_app.layout.left_margin + column_width + g_app.layout.column_gap;
    MoveWindow(
        GetDlgItem(g_app.window, IDC_TODOS_LABEL),
        g_app.layout.left_margin,
        g_app.layout.labels_top,
        column_width,
        g_app.layout.label_height,
        TRUE
    );
    MoveWindow(
        GetDlgItem(g_app.window, IDC_TASKS_LABEL),
        tasks_left,
        g_app.layout.labels_top,
        column_width,
        g_app.layout.label_height,
        TRUE
    );

    status_y = client_height - g_app.layout.bottom_margin - g_app.layout.status_height;
    budget_y = status_y - g_app.layout.gap_budget_to_status - g_app.layout.budget_info_height;
    todo_y = budget_y - g_app.layout.gap_todo_to_budget - g_app.layout.todo_info_height;
    list_height = todo_y - g_app.layout.gap_list_to_todo_info - g_app.layout.lists_top;
    if (list_height < 48) {
        list_height = 48;
    }

    MoveWindow(
        GetDlgItem(g_app.window, IDC_TODOS),
        g_app.layout.left_margin,
        g_app.layout.lists_top,
        column_width,
        list_height,
        TRUE
    );
    MoveWindow(
        GetDlgItem(g_app.window, IDC_TASKS),
        tasks_left,
        g_app.layout.lists_top,
        column_width,
        list_height,
        TRUE
    );
    MoveWindow(
        GetDlgItem(g_app.window, IDC_TODOINFO),
        g_app.layout.left_margin,
        todo_y,
        available_width,
        g_app.layout.todo_info_height,
        TRUE
    );
    MoveWindow(
        GetDlgItem(g_app.window, IDC_BUDGETINFO),
        g_app.layout.left_margin,
        budget_y,
        available_width,
        g_app.layout.budget_info_height,
        TRUE
    );
    MoveWindow(
        GetDlgItem(g_app.window, IDC_STATUS),
        g_app.layout.left_margin,
        status_y,
        available_width,
        g_app.layout.status_height,
        TRUE
    );
}

static void app_clear_lists(void)
{
    g_app.todo_count = 0;
    g_app.task_count = 0;
    if (g_app.window != 0) {
        SendDlgItemMessage(g_app.window, IDC_TODOS, LB_RESETCONTENT, 0, 0L);
        SendDlgItemMessage(g_app.window, IDC_TASKS, LB_RESETCONTENT, 0, 0L);
        app_update_todo_details();
    }
}

static int app_money_text_to_cents(const char *text, long *out_cents)
{
    unsigned int index;
    long whole;
    long fraction;
    int sign;
    int separator_seen;
    int digits_seen;
    int fraction_digits;
    char current;

    if (out_cents != NULL) {
        *out_cents = 0L;
    }
    if (text == NULL || text[0] == '\0') {
        return 1;
    }

    index = 0;
    sign = 1;
    if (text[index] == '-') {
        sign = -1;
        index += 1;
    } else if (text[index] == '+') {
        index += 1;
    }

    whole = 0L;
    fraction = 0L;
    separator_seen = 0;
    digits_seen = 0;
    fraction_digits = 0;

    while (text[index] != '\0') {
        current = text[index];
        if (current == ' ' || current == '\t') {
            index += 1;
            continue;
        }
        if (current == '.' || current == ',') {
            if (separator_seen) {
                return 0;
            }
            separator_seen = 1;
            index += 1;
            continue;
        }
        if (current < '0' || current > '9') {
            return 0;
        }

        digits_seen = 1;
        if (!separator_seen) {
            whole = (whole * 10L) + (long)(current - '0');
        } else {
            if (fraction_digits >= 2) {
                return 0;
            }
            fraction = (fraction * 10L) + (long)(current - '0');
            fraction_digits += 1;
        }
        index += 1;
    }

    if (!digits_seen) {
        return 0;
    }
    if (fraction_digits == 1) {
        fraction *= 10L;
    }

    if (out_cents != NULL) {
        *out_cents = sign * ((whole * 100L) + fraction);
    }
    return 1;
}

static void app_format_cents(long cents, char *buffer, unsigned int capacity)
{
    long absolute_cents;

    if (buffer == NULL || capacity == 0) {
        return;
    }

    absolute_cents = cents < 0 ? -cents : cents;
    if (cents < 0) {
        sprintf(buffer, "-%ld.%02ld", absolute_cents / 100L, absolute_cents % 100L);
    } else {
        sprintf(buffer, "%ld.%02ld", absolute_cents / 100L, absolute_cents % 100L);
    }
}

static int app_normalize_money_text(char *target, const char *source, unsigned int capacity)
{
    long cents;

    if (target == NULL || capacity == 0) {
        return 0;
    }
    if (source == NULL || source[0] == '\0') {
        target[0] = '\0';
        return 1;
    }
    if (!app_money_text_to_cents(source, &cents)) {
        target[0] = '\0';
        return 0;
    }

    app_format_cents(cents, target, capacity);
    return 1;
}

static int app_selected_todo_index(void)
{
    long index;

    if (g_app.window == 0) {
        return -1;
    }

    index = SendDlgItemMessage(g_app.window, IDC_TODOS, LB_GETCURSEL, 0, 0L);
    if (index < 0 || index >= g_app.todo_count) {
        return -1;
    }

    return (int)index;
}

static void app_update_todo_details(void)
{
    int index;
    long spent_cents;
    long planned_cents;
    char todo_text[320];
    char budget_text[320];
    char spent_text[PROTO_MONEY_CAPACITY];
    char planned_text[PROTO_MONEY_CAPACITY];
    char rest_text[PROTO_MONEY_CAPACITY];
    char percent_text[24];
    char detail_text[160];

    if (g_app.window == 0) {
        return;
    }

    index = app_selected_todo_index();
    if (index < 0) {
        SetDlgItemText(g_app.window, IDC_TODOINFO, "Todo: -");
        SetDlgItemText(g_app.window, IDC_BUDGETINFO, "Budget: -");
        return;
    }

    detail_text[0] = '\0';
    if (g_app.todos[index].order_number[0] != '\0' ||
        g_app.todos[index].purchaser[0] != '\0' ||
        g_app.todos[index].order_date[0] != '\0') {
        wsprintf(
            detail_text,
            " | Auftr %s | %s | %s",
            g_app.todos[index].order_number[0] != '\0' ? g_app.todos[index].order_number : "-",
            g_app.todos[index].purchaser[0] != '\0' ? g_app.todos[index].purchaser : "-",
            g_app.todos[index].order_date[0] != '\0' ? g_app.todos[index].order_date : "-"
        );
    } else if (g_app.todos[index].description[0] != '\0') {
        wsprintf(todo_text, "Todo: %s | %s", g_app.todos[index].title, g_app.todos[index].description);
    } else {
        wsprintf(todo_text, "Todo: %s", g_app.todos[index].title);
    }
    if (detail_text[0] != '\0') {
        wsprintf(todo_text, "Todo: %s%s", g_app.todos[index].title, detail_text);
    }

    spent_cents = 0L;
    planned_cents = 0L;
    app_money_text_to_cents(g_app.todos[index].budget_spent, &spent_cents);
    app_money_text_to_cents(g_app.todos[index].budget_planned, &planned_cents);
    app_format_cents(spent_cents, spent_text, sizeof(spent_text));
    app_format_cents(planned_cents, planned_text, sizeof(planned_text));
    app_format_cents(planned_cents - spent_cents, rest_text, sizeof(rest_text));

    percent_text[0] = '\0';
    if (planned_cents > 0L) {
        wsprintf(percent_text, " | %ld%%", (spent_cents * 100L) / planned_cents);
    }

    wsprintf(
        budget_text,
        "Budget: Ist %s | Plan %s | Rest %s%s%s%s",
        spent_text,
        planned_text,
        rest_text,
        percent_text,
        g_app.todos[index].deadline[0] != '\0' ? " | Faellig " : "",
        g_app.todos[index].deadline[0] != '\0' ? g_app.todos[index].deadline : ""
    );

    SetDlgItemText(g_app.window, IDC_TODOINFO, todo_text);
    SetDlgItemText(g_app.window, IDC_BUDGETINFO, budget_text);
}

static long app_selected_todo_id(void)
{
    int index;

    index = app_selected_todo_index();
    if (index < 0) {
        return -1L;
    }
    return g_app.todos[index].id;
}

static long app_selected_task_id(void)
{
    long index;

    if (g_app.window == 0) {
        return -1L;
    }

    index = SendDlgItemMessage(g_app.window, IDC_TASKS, LB_GETCURSEL, 0, 0L);
    if (index < 0 || index >= g_app.task_count) {
        return -1L;
    }
    return g_app.tasks[index].id;
}

static void app_select_todo_by_id(long todo_id)
{
    int index;

    if (g_app.window == 0 || todo_id < 0) {
        return;
    }

    for (index = 0; index < g_app.todo_count; ++index) {
        if (g_app.todos[index].id == todo_id) {
            SendDlgItemMessage(g_app.window, IDC_TODOS, LB_SETCURSEL, index, 0L);
            app_load_tasks();
            app_update_todo_details();
            return;
        }
    }
}

static void app_load_tasks(void)
{
    long todo_id;
    int index;
    char display[320];

    g_app.task_count = 0;
    SendDlgItemMessage(g_app.window, IDC_TASKS, LB_RESETCONTENT, 0, 0L);

    todo_id = app_selected_todo_id();
    if (todo_id < 0) {
        app_update_todo_details();
        return;
    }

    if (!proto_client_list_tasks(
            &g_app.client,
            todo_id,
            g_app.tasks,
            PROTO_MAX_TASKS,
            &g_app.task_count)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    for (index = 0; index < g_app.task_count; ++index) {
        if (g_app.tasks[index].amount[0] != '\0' && g_app.tasks[index].description[0] != '\0') {
            wsprintf(
                display,
                "%s %s | %s | %s",
                g_app.tasks[index].done ? "[x]" : "[ ]",
                g_app.tasks[index].title,
                g_app.tasks[index].amount,
                g_app.tasks[index].description
            );
        } else if (g_app.tasks[index].amount[0] != '\0') {
            wsprintf(
                display,
                "%s %s | %s",
                g_app.tasks[index].done ? "[x]" : "[ ]",
                g_app.tasks[index].title,
                g_app.tasks[index].amount
            );
        } else if (g_app.tasks[index].description[0] != '\0') {
            wsprintf(
                display,
                "%s %s | %s",
                g_app.tasks[index].done ? "[x]" : "[ ]",
                g_app.tasks[index].title,
                g_app.tasks[index].description
            );
        } else {
            wsprintf(
                display,
                "%s %s",
                g_app.tasks[index].done ? "[x]" : "[ ]",
                g_app.tasks[index].title
            );
        }
        SendDlgItemMessage(g_app.window, IDC_TASKS, LB_ADDSTRING, 0, (LPARAM)(LPSTR)display);
    }
}

static void app_load_todos(void)
{
    int index;
    char display[320];
    long spent_cents;
    long planned_cents;

    app_clear_lists();
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    if (!proto_client_list_todos(
            &g_app.client,
            g_app.show_archived,
            g_app.todos,
            PROTO_MAX_TODOS,
            &g_app.todo_count)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    for (index = 0; index < g_app.todo_count; ++index) {
        spent_cents = 0L;
        planned_cents = 0L;
        app_money_text_to_cents(g_app.todos[index].budget_spent, &spent_cents);
        app_money_text_to_cents(g_app.todos[index].budget_planned, &planned_cents);

        display[0] = '\0';
        if (g_app.show_archived && g_app.todos[index].archived_at[0] != '\0') {
            if (spent_cents != 0L || planned_cents != 0L) {
                wsprintf(
                    display,
                    "%s (%d%%) [%s/%s]%s%s [archiv %s]",
                    g_app.todos[index].title,
                    g_app.todos[index].progress,
                    g_app.todos[index].budget_spent,
                    g_app.todos[index].budget_planned,
                    g_app.todos[index].order_number[0] != '\0' ? " | " : "",
                    g_app.todos[index].order_number[0] != '\0' ? g_app.todos[index].order_number : "",
                    g_app.todos[index].archived_at
                );
            } else {
                wsprintf(
                    display,
                    "%s (%d%%)%s%s [archiv %s]",
                    g_app.todos[index].title,
                    g_app.todos[index].progress,
                    g_app.todos[index].order_number[0] != '\0' ? " | " : "",
                    g_app.todos[index].order_number[0] != '\0' ? g_app.todos[index].order_number : "",
                    g_app.todos[index].archived_at
                );
            }
        } else if (spent_cents != 0L || planned_cents != 0L) {
            wsprintf(
                display,
                "%s (%d%%) [%s/%s]%s%s",
                g_app.todos[index].title,
                g_app.todos[index].progress,
                g_app.todos[index].budget_spent,
                g_app.todos[index].budget_planned,
                g_app.todos[index].order_number[0] != '\0' ? " | " : "",
                g_app.todos[index].order_number[0] != '\0' ? g_app.todos[index].order_number : ""
            );
        } else {
            wsprintf(
                display,
                "%s (%d%%)%s%s",
                g_app.todos[index].title,
                g_app.todos[index].progress,
                g_app.todos[index].order_number[0] != '\0' ? " | " : "",
                g_app.todos[index].order_number[0] != '\0' ? g_app.todos[index].order_number : ""
            );
        }
        SendDlgItemMessage(g_app.window, IDC_TODOS, LB_ADDSTRING, 0, (LPARAM)(LPSTR)display);
    }

    if (g_app.todo_count > 0) {
        SendDlgItemMessage(g_app.window, IDC_TODOS, LB_SETCURSEL, 0, 0L);
        app_load_tasks();
        app_update_todo_details();
        app_set_status("Todos geladen");
    } else if (g_app.show_archived) {
        app_update_todo_details();
        app_set_status("Archiv ist leer");
    } else {
        app_update_todo_details();
        app_set_status("Keine Todos");
    }
}

static int app_prompt_for_item(EditorDialog *dialog)
{
    if (dialog == NULL) {
        return 0;
    }

    g_editor_dialog = dialog;
    if (DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_EDIT_ITEM), g_app.window, (DLGPROC)editor_dialog_proc) != IDOK) {
        g_editor_dialog = NULL;
        return 0;
    }
    g_editor_dialog = NULL;

    return dialog->title[0] != '\0';
}

static unsigned short app_parse_port_text(const char *text)
{
    long port_value;

    port_value = atol(text != NULL ? text : "");
    if (port_value <= 0L || port_value > 65535L) {
        port_value = 5877;
    }
    return (unsigned short)port_value;
}

static int app_connect_to(const char *host, unsigned short port, const char *token)
{
    if (!proto_client_connect(&g_app.client, host, port, token)) {
        app_clear_lists();
        app_update_server_info();
        app_set_status(proto_client_last_error(&g_app.client));
        return 0;
    }
    if (!proto_client_hello(&g_app.client)) {
        proto_client_disconnect(&g_app.client);
        app_clear_lists();
        app_update_server_info();
        app_set_status(proto_client_last_error(&g_app.client));
        return 0;
    }
    if (!proto_client_auth(&g_app.client)) {
        proto_client_disconnect(&g_app.client);
        app_clear_lists();
        app_update_server_info();
        app_set_status(proto_client_last_error(&g_app.client));
        return 0;
    }

    app_update_server_info();
    app_set_status("Verbunden");
    app_load_todos();
    return 1;
}

static void app_disconnect(void)
{
    proto_client_disconnect(&g_app.client);
    app_clear_lists();
    app_update_server_info();
    app_set_status("Nicht verbunden");
}

static void app_show_server_manager(void)
{
    DialogBox(g_app.instance, MAKEINTRESOURCE(IDD_SERVER_MANAGER), g_app.window, (DLGPROC)server_dialog_proc);
}

static void app_update_client(int force_update)
{
    char module_path[144];
    char stage_dir[144];
    char helper_path[144];
    char update_path[144];
    char command_line[512];
    char prompt[384];
    ProtoUpdateInfo client_info;
    ProtoUpdateInfo updater_info;
    int temporary_connection;
    int have_local_signature;
    int version_cmp;
    int same_binary;
    unsigned long local_crc32;
    unsigned long local_size;
    UINT message_result;

    temporary_connection = 0;
    module_path[0] = '\0';
    local_crc32 = 0UL;
    local_size = 0UL;
    have_local_signature = 0;
    if (app_get_module_path(module_path, sizeof(module_path))) {
        have_local_signature = proto_file_signature(module_path, &local_crc32, &local_size);
    }

    if (!proto_client_connected(&g_app.client)) {
        if (!proto_client_connect(&g_app.client, g_app.client.host, g_app.client.port, g_app.client.token)) {
            app_set_status(proto_client_last_error(&g_app.client));
            return;
        }
        temporary_connection = 1;
        if (!proto_client_hello_relaxed(&g_app.client)) {
            app_set_status(proto_client_last_error(&g_app.client));
            app_disconnect();
            return;
        }
        if (!proto_client_auth(&g_app.client)) {
            app_set_status(proto_client_last_error(&g_app.client));
            app_disconnect();
            return;
        }
        app_update_server_info();
    }

    if (!proto_client_get_update_info(&g_app.client, "client", &client_info)) {
        app_set_status(proto_client_last_error(&g_app.client));
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    version_cmp = app_compare_versions(CLIENT_APP_VERSION, client_info.version);
    same_binary = have_local_signature &&
        local_size == client_info.size &&
        local_crc32 == client_info.file_crc32;

    if (!force_update) {
        if (version_cmp > 0) {
            wsprintf(
                prompt,
                "Lokal: %s\r\nServer: %s\r\n\r\nDer Server bietet kein neueres Update an.\r\nMit 'Update erzwingen...' kannst du trotzdem neu laden.",
                CLIENT_APP_VERSION,
                client_info.version[0] != '\0' ? client_info.version : "(unbekannt)"
            );
            MessageBox(g_app.window, prompt, "BlueTodo Update", MB_OK | MB_ICONINFORMATION);
            app_set_status("Kein neueres Update verfuegbar");
            if (temporary_connection) {
                app_disconnect();
            }
            return;
        }

        if (version_cmp == 0 && same_binary) {
            wsprintf(
                prompt,
                "Lokal: %s\r\nServer: %s\r\nCRC: %08lX\r\n\r\nKein Update verfuegbar.",
                CLIENT_APP_VERSION,
                client_info.version[0] != '\0' ? client_info.version : "(unbekannt)",
                client_info.file_crc32
            );
            MessageBox(g_app.window, prompt, "BlueTodo Update", MB_OK | MB_ICONINFORMATION);
            app_set_status("Kein Update verfuegbar");
            if (temporary_connection) {
                app_disconnect();
            }
            return;
        }
    }

    if (have_local_signature) {
        wsprintf(
            prompt,
            "%s\r\nLokal: %s | Lokal-CRC: %08lX\r\nServer: %s | Server-CRC: %08lX\r\nDateigroesse: %lu Bytes\r\n\r\nJetzt laden und BlueTodo neu starten?",
            force_update ? "Update wird erzwungen." :
                (version_cmp < 0 ? "Neues Update gefunden." : "Version identisch, aber Build unterscheidet sich."),
            CLIENT_APP_VERSION,
            local_crc32,
            client_info.version[0] != '\0' ? client_info.version : "(unbekannt)",
            client_info.file_crc32,
            client_info.size
        );
    } else {
        wsprintf(
            prompt,
            "%s\r\nLokal: %s\r\nServer: %s | Server-CRC: %08lX\r\nDateigroesse: %lu Bytes\r\n\r\nJetzt laden und BlueTodo neu starten?",
            force_update ? "Update wird erzwungen." :
                (version_cmp < 0 ? "Neues Update gefunden." : "Version identisch, aber Build unterscheidet sich."),
            CLIENT_APP_VERSION,
            client_info.version[0] != '\0' ? client_info.version : "(unbekannt)",
            client_info.file_crc32,
            client_info.size
        );
    }
    message_result = MessageBox(g_app.window, prompt, "BlueTodo Update", MB_OKCANCEL | MB_ICONQUESTION);
    if (message_result != IDOK) {
        app_set_status("Update abgebrochen");
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    if (module_path[0] == '\0') {
        app_set_status("Eigenen EXE-Pfad nicht gefunden");
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    app_get_stage_directory(stage_dir, sizeof(stage_dir));
    if (!app_join_path(helper_path, sizeof(helper_path), stage_dir, "BTUPDT16.EXE") ||
        !app_join_path(update_path, sizeof(update_path), stage_dir, "BLUETODO.NEW")) {
        app_set_status("Update-Pfad zu lang");
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    app_set_status("Updater wird geladen");
    if (!proto_client_download_update(&g_app.client, "updater", helper_path, &updater_info)) {
        app_set_status(proto_client_last_error(&g_app.client));
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    app_set_status("Client-Update wird geladen");
    if (!proto_client_download_update(&g_app.client, "client", update_path, &client_info)) {
        app_set_status(proto_client_last_error(&g_app.client));
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    if (!app_build_update_command(
            command_line,
            sizeof(command_line),
            helper_path,
            update_path,
            module_path,
            module_path)) {
        app_set_status("Update-Befehl zu lang");
        if (temporary_connection) {
            app_disconnect();
        }
        return;
    }

    app_copy_text(g_app.pending_update_command, command_line, sizeof(g_app.pending_update_command));
    if (temporary_connection) {
        app_set_status("Update bereit, Client wird beendet");
    } else {
        app_set_status("Update bereit");
    }
    PostMessage(g_app.window, WM_CLOSE, 0, 0L);
}

static void app_refresh(void)
{
    app_load_todos();
}

static void app_add_todo(void)
{
    EditorDialog dialog;
    char normalized_spent[PROTO_MONEY_CAPACITY];
    char normalized_planned[PROTO_MONEY_CAPACITY];
    long new_id;

    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    memset(&dialog, 0, sizeof(dialog));
    dialog.mode = EDITOR_MODE_TODO;
    app_copy_text(dialog.caption, "Neues Todo", sizeof(dialog.caption));
    if (!app_prompt_for_item(&dialog)) {
        app_set_status("Abgebrochen");
        return;
    }

    if ((dialog.order_number[0] != '\0' || dialog.purchaser[0] != '\0' || dialog.order_date[0] != '\0') &&
        (dialog.order_number[0] == '\0' || dialog.purchaser[0] == '\0' || dialog.order_date[0] == '\0')) {
        app_set_status("Bestelldaten nur vollstaendig");
        return;
    }
    if (!app_normalize_money_text(normalized_spent, dialog.budget_spent, sizeof(normalized_spent)) ||
        !app_normalize_money_text(normalized_planned, dialog.budget_planned, sizeof(normalized_planned))) {
        app_set_status("Budget ungueltig");
        return;
    }

    if (!proto_client_add_todo(
            &g_app.client,
            dialog.title,
            dialog.description,
            dialog.order_number,
            dialog.purchaser,
            dialog.order_date,
            normalized_spent,
            normalized_planned,
            dialog.deadline,
            &new_id)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Todo hinzugefuegt");
    app_load_todos();
}

static void app_add_task(void)
{
    EditorDialog dialog;
    char normalized_amount[PROTO_MONEY_CAPACITY];
    long todo_id;
    long new_id;

    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    todo_id = app_selected_todo_id();
    if (todo_id < 0) {
        app_set_status("Todo auswaehlen");
        return;
    }

    memset(&dialog, 0, sizeof(dialog));
    dialog.mode = EDITOR_MODE_TASK;
    app_copy_text(dialog.caption, "Neue Task", sizeof(dialog.caption));
    if (!app_prompt_for_item(&dialog)) {
        app_set_status("Abgebrochen");
        return;
    }

    if (!app_normalize_money_text(normalized_amount, dialog.amount, sizeof(normalized_amount))) {
        app_set_status("Betrag ungueltig");
        return;
    }

    if (!proto_client_add_task(&g_app.client, todo_id, dialog.title, dialog.description, normalized_amount, &new_id)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Task hinzugefuegt");
    app_load_todos();
    app_select_todo_by_id(todo_id);
}

static void app_edit_todo(void)
{
    EditorDialog dialog;
    char normalized_spent[PROTO_MONEY_CAPACITY];
    char normalized_planned[PROTO_MONEY_CAPACITY];
    int index;
    long todo_id;

    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    index = app_selected_todo_index();
    if (index < 0) {
        app_set_status("Todo auswaehlen");
        return;
    }

    memset(&dialog, 0, sizeof(dialog));
    dialog.mode = EDITOR_MODE_TODO;
    app_copy_text(dialog.caption, "Todo bearbeiten", sizeof(dialog.caption));
    app_copy_text(dialog.title, g_app.todos[index].title, sizeof(dialog.title));
    app_copy_text(dialog.description, g_app.todos[index].description, sizeof(dialog.description));
    app_copy_text(dialog.order_number, g_app.todos[index].order_number, sizeof(dialog.order_number));
    app_copy_text(dialog.purchaser, g_app.todos[index].purchaser, sizeof(dialog.purchaser));
    app_copy_text(dialog.order_date, g_app.todos[index].order_date, sizeof(dialog.order_date));
    app_copy_text(dialog.budget_spent, g_app.todos[index].budget_spent, sizeof(dialog.budget_spent));
    app_copy_text(dialog.budget_planned, g_app.todos[index].budget_planned, sizeof(dialog.budget_planned));
    app_copy_text(dialog.deadline, g_app.todos[index].deadline, sizeof(dialog.deadline));
    if (!app_prompt_for_item(&dialog)) {
        app_set_status("Abgebrochen");
        return;
    }

    if ((dialog.order_number[0] != '\0' || dialog.purchaser[0] != '\0' || dialog.order_date[0] != '\0') &&
        (dialog.order_number[0] == '\0' || dialog.purchaser[0] == '\0' || dialog.order_date[0] == '\0')) {
        app_set_status("Bestelldaten nur vollstaendig");
        return;
    }
    if (!app_normalize_money_text(normalized_spent, dialog.budget_spent, sizeof(normalized_spent)) ||
        !app_normalize_money_text(normalized_planned, dialog.budget_planned, sizeof(normalized_planned))) {
        app_set_status("Budget ungueltig");
        return;
    }

    todo_id = g_app.todos[index].id;
    if (!proto_client_update_todo(
            &g_app.client,
            todo_id,
            dialog.title,
            dialog.description,
            dialog.order_number,
            dialog.purchaser,
            dialog.order_date,
            normalized_spent,
            normalized_planned,
            dialog.deadline)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Todo aktualisiert");
    app_load_todos();
    app_select_todo_by_id(todo_id);
}

static void app_edit_task(void)
{
    EditorDialog dialog;
    char normalized_amount[PROTO_MONEY_CAPACITY];
    int index;
    long task_id;
    long todo_id;

    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    task_id = app_selected_task_id();
    if (task_id < 0) {
        app_set_status("Task auswaehlen");
        return;
    }

    index = (int)SendDlgItemMessage(g_app.window, IDC_TASKS, LB_GETCURSEL, 0, 0L);
    if (index < 0 || index >= g_app.task_count) {
        app_set_status("Task auswaehlen");
        return;
    }

    memset(&dialog, 0, sizeof(dialog));
    dialog.mode = EDITOR_MODE_TASK;
    app_copy_text(dialog.caption, "Task bearbeiten", sizeof(dialog.caption));
    app_copy_text(dialog.title, g_app.tasks[index].title, sizeof(dialog.title));
    app_copy_text(dialog.description, g_app.tasks[index].description, sizeof(dialog.description));
    app_copy_text(dialog.amount, g_app.tasks[index].amount, sizeof(dialog.amount));
    if (!app_prompt_for_item(&dialog)) {
        app_set_status("Abgebrochen");
        return;
    }
    if (!app_normalize_money_text(normalized_amount, dialog.amount, sizeof(normalized_amount))) {
        app_set_status("Betrag ungueltig");
        return;
    }

    todo_id = app_selected_todo_id();
    if (!proto_client_update_task(&g_app.client, task_id, dialog.title, dialog.description, normalized_amount)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Task aktualisiert");
    app_load_todos();
    app_select_todo_by_id(todo_id);
}

static void app_toggle_task(void)
{
    long task_id;
    long done_value;
    long todo_id;

    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }
    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }

    task_id = app_selected_task_id();
    if (task_id < 0) {
        app_set_status("Task auswaehlen");
        return;
    }
    todo_id = app_selected_todo_id();

    if (!proto_client_toggle_task(&g_app.client, task_id, &done_value)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status(done_value ? "Task erledigt" : "Task offen");
    app_load_todos();
    app_select_todo_by_id(todo_id);
}

static void app_toggle_archive_view(void)
{
    g_app.show_archived = !g_app.show_archived;
    app_update_view_mode();
    app_load_todos();
}

static void app_archive_todo(void)
{
    long todo_id;

    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }
    if (g_app.show_archived) {
        app_set_status("Archiv ist read-only");
        return;
    }

    todo_id = app_selected_todo_id();
    if (todo_id < 0) {
        app_set_status("Todo auswaehlen");
        return;
    }

    if (!proto_client_archive_todo(&g_app.client, todo_id)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Todo archiviert");
    app_load_todos();
}

static void app_restore_todo(void)
{
    long todo_id;

    if (!proto_client_connected(&g_app.client)) {
        app_set_status("Nicht verbunden");
        return;
    }
    if (!g_app.show_archived) {
        app_set_status("Nur im Archiv");
        return;
    }

    todo_id = app_selected_todo_id();
    if (todo_id < 0) {
        app_set_status("Todo auswaehlen");
        return;
    }

    if (!proto_client_unarchive_todo(&g_app.client, todo_id)) {
        app_set_status(proto_client_last_error(&g_app.client));
        return;
    }

    app_set_status("Todo wiederhergestellt");
    app_load_todos();
}

static void app_show_info(void)
{
    char info[320];
    const char *server_version;

    if (!proto_client_connected(&g_app.client) || g_app.client.server_proto < 0) {
        wsprintf(
            info,
            "Nicht verbunden.\r\nClient-Version: %s\r\nClient-ID: %s",
            CLIENT_APP_VERSION,
            g_app.client.client_label
        );
        MessageBox(g_app.window, info, "BlueTodo Info", MB_OK | MB_ICONINFORMATION);
        return;
    }

    server_version = g_app.client.server_version[0] != '\0'
        ? g_app.client.server_version
        : "(unbekannt)";

    wsprintf(
        info,
        "Client-Version: %s\r\nClient-ID: %s\r\nServer-Version: %s\r\nSchema: %ld\r\nProto: %d\r\nHost: %s:%d",
        CLIENT_APP_VERSION,
        g_app.client.client_label,
        server_version,
        g_app.client.server_schema,
        g_app.client.server_proto,
        g_app.client.host,
        (int)g_app.client.port
    );
    MessageBox(g_app.window, info, "BlueTodo Info", MB_OK | MB_ICONINFORMATION);
}

static BOOL FAR PASCAL main_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    UINT control_id;
    UINT notification;
    HMENU menu;

    switch (message) {
    case WM_INITDIALOG:
        g_app.window = window;
        menu = LoadMenu(g_app.instance, MAKEINTRESOURCE(IDM_MAIN_MENU));
        if (menu != 0) {
            SetMenu(window, menu);
            DrawMenuBar(window);
        }
        app_init_layout();
        app_layout_main_window();
        app_update_view_mode();
        app_update_server_info();
        app_set_status("Bereit");
        return TRUE;

    case WM_SIZE:
        app_layout_main_window();
        return TRUE;

    case WM_COMMAND:
        control_id = COMMAND_ID(wparam, lparam);
        notification = COMMAND_NOTIFY(wparam, lparam);
        if (control_id == IDC_TODOS && notification == LBN_SELCHANGE) {
            app_load_tasks();
            app_update_todo_details();
            return TRUE;
        }
        if (control_id == IDC_TODOS && notification == LBN_DBLCLK) {
            app_edit_todo();
            return TRUE;
        }
        if (control_id == IDC_TASKS && notification == LBN_DBLCLK) {
            app_edit_task();
            return TRUE;
        }

        switch (control_id) {
        case IDC_REFRESH:
            app_refresh();
            return TRUE;
        case IDC_ADDTODO:
            app_add_todo();
            return TRUE;
        case IDC_ADDTASK:
            app_add_task();
            return TRUE;
        case IDC_TOGGLE:
            app_toggle_task();
            return TRUE;
        case IDC_ARCHIVEVIEW:
            app_toggle_archive_view();
            return TRUE;
        case IDC_ARCHIVE:
            app_archive_todo();
            return TRUE;
        case IDC_RESTORE:
            app_restore_todo();
            return TRUE;
        case IDM_CONNECTION_MANAGER:
            app_show_server_manager();
            return TRUE;
        case IDM_CONNECTION_UPDATE:
            app_update_client(0);
            return TRUE;
        case IDM_CONNECTION_UPDATE_FORCE:
            app_update_client(1);
            return TRUE;
        case IDM_CONNECTION_EXIT:
            SendMessage(window, WM_CLOSE, 0, 0L);
            return TRUE;
        case IDM_HELP_INFO:
            app_show_info();
            return TRUE;
        case IDCANCEL:
            SendMessage(window, WM_CLOSE, 0, 0L);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        app_disconnect();
        g_app.window = 0;
        EndDialog(window, 0);
        return TRUE;
    }

    return FALSE;
}

static BOOL FAR PASCAL editor_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    int todo_mode;

    switch (message) {
    case WM_INITDIALOG:
        if (g_editor_dialog != NULL) {
            todo_mode = g_editor_dialog->mode == EDITOR_MODE_TODO;
            SetWindowText(window, g_editor_dialog->caption);
            SetDlgItemText(window, IDC_EDIT_TITLE, g_editor_dialog->title);
            SetDlgItemText(window, IDC_EDIT_DESC, g_editor_dialog->description);
            SetDlgItemText(window, IDC_EDIT_ORDER_NUMBER, g_editor_dialog->order_number);
            SetDlgItemText(window, IDC_EDIT_PURCHASER, g_editor_dialog->purchaser);
            SetDlgItemText(window, IDC_EDIT_ORDER_DATE, g_editor_dialog->order_date);
            SetDlgItemText(window, IDC_EDIT_BUDGET_SPENT, g_editor_dialog->budget_spent);
            SetDlgItemText(window, IDC_EDIT_BUDGET_PLANNED, g_editor_dialog->budget_planned);
            SetDlgItemText(window, IDC_EDIT_DEADLINE, g_editor_dialog->deadline);
            SetDlgItemText(window, IDC_EDIT_AMOUNT, g_editor_dialog->amount);
            SendDlgItemMessage(window, IDC_EDIT_TITLE, EM_LIMITTEXT, PROTO_TITLE_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_DESC, EM_LIMITTEXT, PROTO_DESC_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_ORDER_NUMBER, EM_LIMITTEXT, PROTO_TITLE_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_PURCHASER, EM_LIMITTEXT, PROTO_TITLE_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_ORDER_DATE, EM_LIMITTEXT, PROTO_DEADLINE_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_BUDGET_SPENT, EM_LIMITTEXT, PROTO_MONEY_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_BUDGET_PLANNED, EM_LIMITTEXT, PROTO_MONEY_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_DEADLINE, EM_LIMITTEXT, PROTO_DEADLINE_CAPACITY - 1, 0L);
            SendDlgItemMessage(window, IDC_EDIT_AMOUNT, EM_LIMITTEXT, PROTO_MONEY_CAPACITY - 1, 0L);
            ShowWindow(GetDlgItem(window, IDC_ORDER_NUMBER_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_ORDER_NUMBER), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_PURCHASER_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_PURCHASER), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_ORDER_DATE_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_ORDER_DATE), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_BUDGET_SPENT_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_BUDGET_SPENT), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_BUDGET_PLANNED_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_BUDGET_PLANNED), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_DEADLINE_LABEL), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_EDIT_DEADLINE), todo_mode ? SW_SHOW : SW_HIDE);
            ShowWindow(GetDlgItem(window, IDC_AMOUNT_LABEL), todo_mode ? SW_HIDE : SW_SHOW);
            ShowWindow(GetDlgItem(window, IDC_EDIT_AMOUNT), todo_mode ? SW_HIDE : SW_SHOW);
        }
        return TRUE;

    case WM_COMMAND:
        switch (COMMAND_ID(wparam, lparam)) {
        case IDOK:
            if (g_editor_dialog != NULL) {
                GetDlgItemText(window, IDC_EDIT_TITLE, g_editor_dialog->title, sizeof(g_editor_dialog->title));
                GetDlgItemText(window, IDC_EDIT_DESC, g_editor_dialog->description, sizeof(g_editor_dialog->description));
                if (g_editor_dialog->mode == EDITOR_MODE_TODO) {
                    GetDlgItemText(window, IDC_EDIT_ORDER_NUMBER, g_editor_dialog->order_number, sizeof(g_editor_dialog->order_number));
                    GetDlgItemText(window, IDC_EDIT_PURCHASER, g_editor_dialog->purchaser, sizeof(g_editor_dialog->purchaser));
                    GetDlgItemText(window, IDC_EDIT_ORDER_DATE, g_editor_dialog->order_date, sizeof(g_editor_dialog->order_date));
                    GetDlgItemText(window, IDC_EDIT_BUDGET_SPENT, g_editor_dialog->budget_spent, sizeof(g_editor_dialog->budget_spent));
                    GetDlgItemText(window, IDC_EDIT_BUDGET_PLANNED, g_editor_dialog->budget_planned, sizeof(g_editor_dialog->budget_planned));
                    GetDlgItemText(window, IDC_EDIT_DEADLINE, g_editor_dialog->deadline, sizeof(g_editor_dialog->deadline));
                } else {
                    GetDlgItemText(window, IDC_EDIT_AMOUNT, g_editor_dialog->amount, sizeof(g_editor_dialog->amount));
                }
            }
            EndDialog(window, IDOK);
            return TRUE;
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

static BOOL FAR PASCAL server_dialog_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    char host[PROTO_HOST_CAPACITY];
    char port_text[16];
    char token[PROTO_TOKEN_CAPACITY];

    (void)lparam;

    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemText(window, IDC_HOST, g_app.client.host);
        wsprintf(port_text, "%u", (unsigned int)(g_app.client.port > 0 ? g_app.client.port : 5877));
        SetDlgItemText(window, IDC_PORT, port_text);
        SetDlgItemText(window, IDC_TOKEN, g_app.client.token);
        SendDlgItemMessage(window, IDC_HOST, EM_LIMITTEXT, PROTO_HOST_CAPACITY - 1, 0L);
        SendDlgItemMessage(window, IDC_PORT, EM_LIMITTEXT, 5, 0L);
        SendDlgItemMessage(window, IDC_TOKEN, EM_LIMITTEXT, PROTO_TOKEN_CAPACITY - 1, 0L);
        app_update_server_manager_state(window);
        return TRUE;

    case WM_COMMAND:
        switch (COMMAND_ID(wparam, lparam)) {
        case IDC_CONNECT:
            GetDlgItemText(window, IDC_HOST, host, sizeof(host));
            GetDlgItemText(window, IDC_PORT, port_text, sizeof(port_text));
            GetDlgItemText(window, IDC_TOKEN, token, sizeof(token));
            app_copy_text(g_app.client.host, host, sizeof(g_app.client.host));
            app_copy_text(g_app.client.token, token, sizeof(g_app.client.token));
            g_app.client.port = app_parse_port_text(port_text);
            app_save_config();
            if (app_connect_to(host, app_parse_port_text(port_text), token)) {
                EndDialog(window, IDOK);
            } else {
                SetDlgItemText(window, IDC_SERVER_STATE, proto_client_last_error(&g_app.client));
            }
            return TRUE;
        case IDC_DISCONNECT:
            app_disconnect();
            app_update_server_manager_state(window);
            return TRUE;
        case IDCANCEL:
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        break;
    }

    return FALSE;
}

int PASCAL WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    int result;
    UINT exec_result;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    memset(&g_app, 0, sizeof(g_app));
    g_app.instance = hInstance;
    g_app.todos = g_todo_storage;
    g_app.tasks = g_task_storage;
    proto_client_init(&g_app.client);
    app_load_config();
    result = DialogBox(hInstance, MAKEINTRESOURCE(IDD_MAIN), 0, (DLGPROC)main_dialog_proc);

    if (g_app.pending_update_command[0] != '\0') {
        exec_result = WinExec(g_app.pending_update_command, SW_SHOWNORMAL);
        if (exec_result <= 31U) {
            MessageBox(
                0,
                "Update heruntergeladen, aber der Updater konnte nach dem Beenden nicht gestartet werden.",
                "BlueTodo Update",
                MB_OK | MB_ICONSTOP
            );
        }
    }

    return result;
}
