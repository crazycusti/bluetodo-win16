#ifndef BLUETODO_PROTO_H
#define BLUETODO_PROTO_H

#include <windows.h>
#include <winsock.h>

#define PROTO_SUPPORTED_VERSION 2
#define PROTO_MAX_TODOS 64
#define PROTO_MAX_TASKS 128
#define PROTO_HOST_CAPACITY 64
#define PROTO_TOKEN_CAPACITY 128
#define PROTO_CLIENT_LABEL_CAPACITY 32
#define PROTO_APP_VERSION_CAPACITY 32
#define PROTO_UPDATE_TARGET_CAPACITY 16
#define PROTO_UPDATE_ARTIFACT_CAPACITY 16
#define PROTO_UPDATE_NAME_CAPACITY 64
#define PROTO_TITLE_CAPACITY 96
#define PROTO_DESC_CAPACITY 192
#define PROTO_MONEY_CAPACITY 24
#define PROTO_DEADLINE_CAPACITY 24
#define PROTO_ARCHIVE_CAPACITY 24
#define PROTO_ERROR_CAPACITY 128

#ifndef INADDR_NONE
#define INADDR_NONE 0xFFFFFFFFUL
#endif

typedef struct ProtoTodoTag {
    long id;
    int progress;
    char title[PROTO_TITLE_CAPACITY];
    char description[PROTO_DESC_CAPACITY];
    char order_number[PROTO_TITLE_CAPACITY];
    char purchaser[PROTO_TITLE_CAPACITY];
    char order_date[PROTO_DEADLINE_CAPACITY];
    char budget_spent[PROTO_MONEY_CAPACITY];
    char budget_planned[PROTO_MONEY_CAPACITY];
    char deadline[PROTO_DEADLINE_CAPACITY];
    char archived_at[PROTO_ARCHIVE_CAPACITY];
} ProtoTodo;

typedef struct ProtoTaskTag {
    long id;
    long todo_id;
    int done;
    char title[PROTO_TITLE_CAPACITY];
    char description[PROTO_DESC_CAPACITY];
    char amount[PROTO_MONEY_CAPACITY];
} ProtoTask;

typedef struct ProtoUpdateInfoTag {
    char target[PROTO_UPDATE_TARGET_CAPACITY];
    char artifact[PROTO_UPDATE_ARTIFACT_CAPACITY];
    char version[PROTO_APP_VERSION_CAPACITY];
    char name[PROTO_UPDATE_NAME_CAPACITY];
    unsigned long size;
    unsigned long file_crc32;
    int seen;
} ProtoUpdateInfo;

typedef struct ProtoClientTag {
    char host[PROTO_HOST_CAPACITY];
    unsigned short port;
    char token[PROTO_TOKEN_CAPACITY];
    char client_label[PROTO_CLIENT_LABEL_CAPACITY];
    char server_version[PROTO_APP_VERSION_CAPACITY];
    long server_schema;
    int server_proto;
    SOCKET socket_handle;
    unsigned long next_seq;
    char recv_buffer[1025];
    unsigned int recv_len;
    char last_error[PROTO_ERROR_CAPACITY];
    int wsa_ready;
    WSADATA wsa_data;
} ProtoClient;

void proto_client_init(ProtoClient *client);
void proto_client_disconnect(ProtoClient *client);
int proto_client_connected(const ProtoClient *client);
int proto_client_connect(ProtoClient *client, const char *host, unsigned short port, const char *token);
int proto_client_hello(ProtoClient *client);
int proto_client_hello_relaxed(ProtoClient *client);
int proto_client_auth(ProtoClient *client);
int proto_client_list_todos(ProtoClient *client, int archived, ProtoTodo *items, int max_items, int *out_count);
int proto_client_list_tasks(ProtoClient *client, long todo_id, ProtoTask *items, int max_items, int *out_count);
int proto_client_get_update_info(ProtoClient *client, const char *artifact, ProtoUpdateInfo *out_info);
int proto_client_download_update(ProtoClient *client, const char *artifact, const char *destination_path, ProtoUpdateInfo *out_info);
int proto_client_add_todo(
    ProtoClient *client,
    const char *title,
    const char *description,
    const char *order_number,
    const char *purchaser,
    const char *order_date,
    const char *budget_spent,
    const char *budget_planned,
    const char *deadline,
    long *out_id
);
int proto_client_update_todo(
    ProtoClient *client,
    long todo_id,
    const char *title,
    const char *description,
    const char *order_number,
    const char *purchaser,
    const char *order_date,
    const char *budget_spent,
    const char *budget_planned,
    const char *deadline
);
int proto_client_add_task(
    ProtoClient *client,
    long todo_id,
    const char *title,
    const char *description,
    const char *amount,
    long *out_id
);
int proto_client_update_task(
    ProtoClient *client,
    long task_id,
    const char *title,
    const char *description,
    const char *amount
);
int proto_client_toggle_task(ProtoClient *client, long task_id, long *out_done);
int proto_client_archive_todo(ProtoClient *client, long todo_id);
int proto_client_unarchive_todo(ProtoClient *client, long todo_id);
int proto_file_signature(const char *path, unsigned long *out_crc32, unsigned long *out_size);
const char *proto_client_last_error(const ProtoClient *client);

#endif
