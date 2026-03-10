#include "proto.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROTO_LINE_CAPACITY 1024
#define PROTO_UPDATE_TARGET "win16"
#define PROTO_UPDATE_CHUNK_CAPACITY 256

#include "proto_wire.h"

typedef struct ProtoParamTag {
    const char *key;
    const char *value;
} ProtoParam;

typedef int (*ProtoLineVisitor)(ProtoClient *client, const char *line, void *context);

typedef struct TodoCollectorTag {
    ProtoTodo *items;
    int max_items;
    int count;
} TodoCollector;

typedef struct TaskCollectorTag {
    ProtoTask *items;
    int max_items;
    int count;
} TaskCollector;

typedef struct LongResultTag {
    const char *key;
    long value;
    int seen;
} LongResult;

typedef struct HelloResultTag {
    int proto_version;
    int seen;
} HelloResult;

typedef struct UpdateDownloadTag {
    FILE *file;
    ProtoUpdateInfo info;
    unsigned long bytes_written;
    unsigned long end_size;
    unsigned long end_crc32;
    int end_seen;
} UpdateDownload;

typedef struct ProtoWinsockApiTag {
    HINSTANCE module;
    int (PASCAL FAR *wsa_startup)(WORD, LPWSADATA);
    int (PASCAL FAR *wsa_cleanup)(void);
    SOCKET (PASCAL FAR *socket_create)(int, int, int);
    int (PASCAL FAR *socket_close)(SOCKET);
    int (PASCAL FAR *socket_connect)(SOCKET, const struct sockaddr FAR *, int);
    int (PASCAL FAR *socket_recv)(SOCKET, char FAR *, int, int);
    int (PASCAL FAR *socket_send)(SOCKET, const char FAR *, int, int);
    unsigned long (PASCAL FAR *addr_parse)(const char FAR *);
    unsigned short (PASCAL FAR *port_host_to_net)(unsigned short);
} ProtoWinsockApi;

static ProtoWinsockApi g_winsock;

static void proto_param_push_if_text(ProtoParam *params, int *param_count, const char *key, const char *value);
static unsigned long proto_next_seq(ProtoClient *client);
static int proto_send_all(ProtoClient *client, const char *buffer, int length);
static int proto_send_line(ProtoClient *client, const char *line);
static int proto_read_line(ProtoClient *client, char *line, unsigned int capacity);
static int proto_build_base_line(char *base, unsigned int capacity, const char *command, const ProtoParam *params, int param_count, unsigned long seq);
static int proto_send_command(
    ProtoClient *client,
    const char *command,
    const ProtoParam *params,
    int param_count,
    int expect_end,
    ProtoLineVisitor visitor,
    void *context
);
static int proto_collect_todo(ProtoClient *client, const char *line, void *context);
static int proto_collect_task(ProtoClient *client, const char *line, void *context);
static int proto_collect_long_result(ProtoClient *client, const char *line, void *context);
static int proto_collect_hello(ProtoClient *client, const char *line, void *context);
static int proto_fill_update_info(ProtoClient *client, const char *line, ProtoUpdateInfo *info);
static int proto_decode_hex_buffer(const char *text, unsigned char *buffer, unsigned int capacity, unsigned int *out_length);
static int proto_compute_file_crc32(const char *path, unsigned long *out_crc32, unsigned long *out_size);
static int proto_collect_update_info(ProtoClient *client, const char *line, void *context);
static int proto_collect_update_download(ProtoClient *client, const char *line, void *context);
static int proto_client_hello_internal(ProtoClient *client, int enforce_version);
static void proto_winsock_unload(void);
static int proto_winsock_load(ProtoClient *client);

static void proto_winsock_unload(void)
{
    if (g_winsock.module != NULL) {
        FreeLibrary(g_winsock.module);
    }
    memset(&g_winsock, 0, sizeof(g_winsock));
}

static int proto_winsock_load(ProtoClient *client)
{
    if (g_winsock.module != NULL) {
        return 1;
    }

    g_winsock.module = LoadLibrary("WINSOCK.DLL");
    if ((UINT)g_winsock.module <= HINSTANCE_ERROR) {
        proto_winsock_unload();
        proto_set_error(client, "Missing WINSOCK.DLL (WinSock 1.1 stack)");
        return 0;
    }

    g_winsock.wsa_startup = (int (PASCAL FAR *)(WORD, LPWSADATA))GetProcAddress(g_winsock.module, "WSAStartup");
    g_winsock.wsa_cleanup = (int (PASCAL FAR *)(void))GetProcAddress(g_winsock.module, "WSACleanup");
    g_winsock.socket_create = (SOCKET (PASCAL FAR *)(int, int, int))GetProcAddress(g_winsock.module, "socket");
    g_winsock.socket_close = (int (PASCAL FAR *)(SOCKET))GetProcAddress(g_winsock.module, "closesocket");
    g_winsock.socket_connect = (int (PASCAL FAR *)(SOCKET, const struct sockaddr FAR *, int))GetProcAddress(g_winsock.module, "connect");
    g_winsock.socket_recv = (int (PASCAL FAR *)(SOCKET, char FAR *, int, int))GetProcAddress(g_winsock.module, "recv");
    g_winsock.socket_send = (int (PASCAL FAR *)(SOCKET, const char FAR *, int, int))GetProcAddress(g_winsock.module, "send");
    g_winsock.addr_parse = (unsigned long (PASCAL FAR *)(const char FAR *))GetProcAddress(g_winsock.module, "inet_addr");
    g_winsock.port_host_to_net = (unsigned short (PASCAL FAR *)(unsigned short))GetProcAddress(g_winsock.module, "htons");

    if (g_winsock.wsa_startup == NULL ||
        g_winsock.wsa_cleanup == NULL ||
        g_winsock.socket_create == NULL ||
        g_winsock.socket_close == NULL ||
        g_winsock.socket_connect == NULL ||
        g_winsock.socket_recv == NULL ||
        g_winsock.socket_send == NULL ||
        g_winsock.addr_parse == NULL ||
        g_winsock.port_host_to_net == NULL) {
        proto_winsock_unload();
        proto_set_error(client, "Incomplete WINSOCK.DLL exports");
        return 0;
    }

    return 1;
}

static void proto_param_push_if_text(ProtoParam *params, int *param_count, const char *key, const char *value)
{
    if (params == NULL || param_count == NULL || key == NULL || value == NULL || value[0] == '\0') {
        return;
    }

    params[*param_count].key = key;
    params[*param_count].value = value;
    *param_count += 1;
}

static unsigned long proto_next_seq(ProtoClient *client)
{
    unsigned long seq;

    seq = client->next_seq;
    client->next_seq += 1;
    return seq;
}

static int proto_send_all(ProtoClient *client, const char *buffer, int length)
{
    int sent_total;
    int sent_now;

    sent_total = 0;
    while (sent_total < length) {
        sent_now = g_winsock.socket_send(client->socket_handle, (char FAR *)(buffer + sent_total), length - sent_total, 0);
        if (sent_now == SOCKET_ERROR || sent_now <= 0) {
            proto_set_error(client, "send failed");
            return 0;
        }
        sent_total += sent_now;
    }

    return 1;
}

static int proto_send_line(ProtoClient *client, const char *line)
{
    if (!proto_client_connected(client)) {
        proto_set_error(client, "Not connected");
        return 0;
    }

    if (!proto_send_all(client, line, (int)strlen(line))) {
        return 0;
    }

    return proto_send_all(client, "\r\n", 2);
}

static int proto_read_line(ProtoClient *client, char *line, unsigned int capacity)
{
    int received;
    unsigned int index;
    unsigned int line_length;
    unsigned int remaining;

    if (line == NULL || capacity == 0) {
        proto_set_error(client, "Buffer error");
        return 0;
    }

    line[0] = '\0';
    while (1) {
        for (index = 0; index < client->recv_len; ++index) {
            if (client->recv_buffer[index] == '\n') {
                line_length = index;
                if (line_length > 0 && client->recv_buffer[line_length - 1] == '\r') {
                    line_length -= 1;
                }
                if (line_length + 1 > capacity) {
                    proto_set_error(client, "Response line too long");
                    return 0;
                }

                memcpy(line, client->recv_buffer, line_length);
                line[line_length] = '\0';

                remaining = client->recv_len - (index + 1);
                if (remaining > 0) {
                    memmove(client->recv_buffer, client->recv_buffer + index + 1, remaining);
                }
                client->recv_len = remaining;
                client->recv_buffer[client->recv_len] = '\0';
                return 1;
            }
        }

        if (client->recv_len >= sizeof(client->recv_buffer) - 1) {
            proto_set_error(client, "Receive buffer exhausted");
            return 0;
        }

        received = g_winsock.socket_recv(
            client->socket_handle,
            client->recv_buffer + client->recv_len,
            sizeof(client->recv_buffer) - 1 - client->recv_len,
            0
        );

        if (received == SOCKET_ERROR) {
            proto_set_error(client, "recv failed");
            return 0;
        }
        if (received == 0) {
            proto_set_error(client, "connection closed");
            return 0;
        }

        client->recv_len += (unsigned int)received;
        client->recv_buffer[client->recv_len] = '\0';
    }
}

static int proto_build_base_line(char *base, unsigned int capacity, const char *command, const ProtoParam *params, int param_count, unsigned long seq)
{
    int index;
    char number[32];

    if (base == NULL || capacity == 0) {
        return 0;
    }

    base[0] = '\0';
    if (!proto_append_text(base, command, capacity)) {
        return 0;
    }

    for (index = 0; index < param_count; ++index) {
        if (params[index].key == NULL || params[index].value == NULL) {
            continue;
        }

        if (!proto_append_text(base, " ", capacity) ||
            !proto_append_text(base, params[index].key, capacity) ||
            !proto_append_text(base, "=", capacity) ||
            !proto_append_encoded_text(base, params[index].value, capacity)) {
            return 0;
        }
    }

    sprintf(number, "%lu", seq);
    if (!proto_append_text(base, " seq=", capacity) || !proto_append_text(base, number, capacity)) {
        return 0;
    }

    return 1;
}

static int proto_send_command(
    ProtoClient *client,
    const char *command,
    const ProtoParam *params,
    int param_count,
    int expect_end,
    ProtoLineVisitor visitor,
    void *context
)
{
    char line[PROTO_LINE_CAPACITY];
    char response[PROTO_LINE_CAPACITY];
    char kind[16];
    char message[PROTO_ERROR_CAPACITY];
    char code[32];
    unsigned long crc;
    int ok_seen;

    client->last_error[0] = '\0';
    if (!proto_build_base_line(line, sizeof(line), command, params, param_count, proto_next_seq(client))) {
        proto_set_error(client, "Request too large");
        return 0;
    }

    crc = proto_crc32_n(line, (unsigned int)strlen(line));
    if (!proto_append_text(line, " crc32=", sizeof(line))) {
        proto_set_error(client, "Request too large");
        return 0;
    }
    sprintf(message, "%08lX", crc);
    if (!proto_append_text(line, message, sizeof(line))) {
        proto_set_error(client, "Request too large");
        return 0;
    }
    if (!proto_send_line(client, line)) {
        return 0;
    }

    ok_seen = 0;
    while (1) {
        if (!proto_read_line(client, response, sizeof(response))) {
            return 0;
        }
        if (response[0] == '\0') {
            continue;
        }
        if (!proto_verify_line(response)) {
            proto_set_error(client, "Bad CRC");
            return 0;
        }

        proto_line_kind(response, kind, sizeof(kind));
        if (strcmp(kind, "ERR") == 0) {
            if (proto_extract_param(response, "msg", message, sizeof(message)) && message[0] != '\0') {
                proto_set_error(client, message);
            } else if (proto_extract_param(response, "code", code, sizeof(code)) && code[0] != '\0') {
                proto_set_error(client, code);
            } else {
                proto_set_error(client, "Server error");
            }
            return 0;
        }

        if (visitor != NULL && !visitor(client, response, context)) {
            return 0;
        }

        if (expect_end) {
            if (strcmp(kind, "OK") == 0) {
                ok_seen = 1;
            } else if (strcmp(kind, "END") == 0) {
                if (!ok_seen) {
                    proto_set_error(client, "Missing OK");
                    return 0;
                }
                return 1;
            }
        } else if (strcmp(kind, "OK") == 0) {
            return 1;
        }
    }
}

static int proto_collect_todo(ProtoClient *client, const char *line, void *context)
{
    TodoCollector *collector;
    ProtoTodo *item;

    collector = (TodoCollector *)context;
    if (strncmp(line, "TODO", 4) != 0) {
        return 1;
    }

    if (collector->count >= collector->max_items) {
        proto_set_error(client, "Too many todos");
        return 0;
    }

    item = &collector->items[collector->count];
    item->id = proto_extract_long(line, "id", -1L);
    item->progress = (int)proto_extract_long(line, "progress", 0L);
    proto_extract_param(line, "title", item->title, sizeof(item->title));
    if (!proto_extract_param(line, "desc", item->description, sizeof(item->description))) {
        item->description[0] = '\0';
    }
    if (!proto_extract_param(line, "order_number", item->order_number, sizeof(item->order_number))) {
        item->order_number[0] = '\0';
    }
    if (!proto_extract_param(line, "purchaser", item->purchaser, sizeof(item->purchaser))) {
        item->purchaser[0] = '\0';
    }
    if (!proto_extract_param(line, "order_date", item->order_date, sizeof(item->order_date))) {
        item->order_date[0] = '\0';
    }
    if (!proto_extract_param(line, "budget_spent", item->budget_spent, sizeof(item->budget_spent))) {
        item->budget_spent[0] = '\0';
    }
    if (!proto_extract_param(line, "budget_planned", item->budget_planned, sizeof(item->budget_planned))) {
        item->budget_planned[0] = '\0';
    }
    if (!proto_extract_param(line, "deadline", item->deadline, sizeof(item->deadline))) {
        item->deadline[0] = '\0';
    }
    if (!proto_extract_param(line, "archived_at", item->archived_at, sizeof(item->archived_at))) {
        item->archived_at[0] = '\0';
    }

    collector->count += 1;
    return 1;
}

static int proto_collect_task(ProtoClient *client, const char *line, void *context)
{
    TaskCollector *collector;
    ProtoTask *item;

    collector = (TaskCollector *)context;
    if (strncmp(line, "TASK", 4) != 0) {
        return 1;
    }

    if (collector->count >= collector->max_items) {
        proto_set_error(client, "Too many tasks");
        return 0;
    }

    item = &collector->items[collector->count];
    item->id = proto_extract_long(line, "id", -1L);
    item->todo_id = proto_extract_long(line, "todo_id", -1L);
    item->done = (int)proto_extract_long(line, "done", 0L);
    proto_extract_param(line, "title", item->title, sizeof(item->title));
    if (!proto_extract_param(line, "desc", item->description, sizeof(item->description))) {
        item->description[0] = '\0';
    }
    if (!proto_extract_param(line, "amount", item->amount, sizeof(item->amount))) {
        item->amount[0] = '\0';
    }

    collector->count += 1;
    return 1;
}

static int proto_collect_long_result(ProtoClient *client, const char *line, void *context)
{
    LongResult *result;
    char kind[16];

    result = (LongResult *)context;
    proto_line_kind(line, kind, sizeof(kind));
    if (strcmp(kind, "OK") != 0) {
        return 1;
    }

    result->value = proto_extract_long(line, result->key, -1L);
    result->seen = result->value != -1L;
    if (!result->seen) {
        proto_set_error(client, "Missing response value");
        return 0;
    }

    return 1;
}

static int proto_collect_hello(ProtoClient *client, const char *line, void *context)
{
    HelloResult *result;
    char kind[16];
    long proto_version;
    char version_text[PROTO_APP_VERSION_CAPACITY];

    result = (HelloResult *)context;
    proto_line_kind(line, kind, sizeof(kind));
    if (strcmp(kind, "OK") != 0) {
        return 1;
    }

    proto_version = proto_extract_long(line, "proto", -1L);
    if (proto_version == -1L) {
        proto_set_error(client, "Missing proto version");
        return 0;
    }

    if (proto_extract_param(line, "version", version_text, sizeof(version_text))) {
        proto_copy_text(client->server_version, version_text, sizeof(client->server_version));
    } else {
        client->server_version[0] = '\0';
    }
    client->server_schema = proto_extract_long(line, "schema", -1L);
    client->server_proto = (int)proto_version;

    result->proto_version = (int)proto_version;
    result->seen = 1;

    return 1;
}

static int proto_fill_update_info(ProtoClient *client, const char *line, ProtoUpdateInfo *info)
{
    char crc_text[32];

    if (info == NULL) {
        proto_set_error(client, "Update info missing");
        return 0;
    }

    if (!proto_extract_param(line, "target", info->target, sizeof(info->target)) || info->target[0] == '\0') {
        proto_set_error(client, "Missing update target");
        return 0;
    }
    if (!proto_extract_param(line, "artifact", info->artifact, sizeof(info->artifact)) || info->artifact[0] == '\0') {
        proto_set_error(client, "Missing update artifact");
        return 0;
    }
    if (!proto_extract_param(line, "version", info->version, sizeof(info->version)) || info->version[0] == '\0') {
        proto_set_error(client, "Missing update version");
        return 0;
    }
    if (!proto_extract_param(line, "name", info->name, sizeof(info->name)) || info->name[0] == '\0') {
        proto_set_error(client, "Missing update filename");
        return 0;
    }

    info->size = (unsigned long)proto_extract_long(line, "size", -1L);
    if (info->size == (unsigned long)-1L) {
        proto_set_error(client, "Missing update size");
        return 0;
    }

    if (!proto_extract_param(line, "file_crc32", crc_text, sizeof(crc_text)) || crc_text[0] == '\0') {
        proto_set_error(client, "Missing update CRC");
        return 0;
    }
    info->file_crc32 = strtoul(crc_text, NULL, 16);

    info->seen = 1;
    return 1;
}

static int proto_decode_hex_buffer(const char *text, unsigned char *buffer, unsigned int capacity, unsigned int *out_length)
{
    unsigned int length;
    unsigned int index;
    int high;
    int low;

    if (out_length != NULL) {
        *out_length = 0;
    }
    if (text == NULL || buffer == NULL) {
        return 0;
    }

    length = (unsigned int)strlen(text);
    if ((length % 2U) != 0U) {
        return 0;
    }
    if ((length / 2U) > capacity) {
        return 0;
    }

    for (index = 0; index < length; index += 2U) {
        high = proto_hex_value(text[index]);
        low = proto_hex_value(text[index + 1U]);
        if (high < 0 || low < 0) {
            return 0;
        }
        buffer[index / 2U] = (unsigned char)((high << 4) | low);
    }

    if (out_length != NULL) {
        *out_length = length / 2U;
    }
    return 1;
}

static int proto_compute_file_crc32(const char *path, unsigned long *out_crc32, unsigned long *out_size)
{
    FILE *file;
    unsigned char buffer[PROTO_UPDATE_CHUNK_CAPACITY];
    size_t read_now;
    unsigned long crc;
    unsigned long total_size;

    if (out_crc32 != NULL) {
        *out_crc32 = 0UL;
    }
    if (out_size != NULL) {
        *out_size = 0UL;
    }
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    crc = 0xFFFFFFFFUL;
    total_size = 0UL;
    while ((read_now = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        crc = proto_crc32_step(crc, buffer, (unsigned int)read_now);
        total_size += (unsigned long)read_now;
    }

    fclose(file);
    if (out_crc32 != NULL) {
        *out_crc32 = ~crc;
    }
    if (out_size != NULL) {
        *out_size = total_size;
    }
    return 1;
}

static int proto_collect_update_info(ProtoClient *client, const char *line, void *context)
{
    ProtoUpdateInfo *info;
    char kind[16];

    info = (ProtoUpdateInfo *)context;
    proto_line_kind(line, kind, sizeof(kind));
    if (strcmp(kind, "OK") != 0) {
        return 1;
    }

    return proto_fill_update_info(client, line, info);
}

static int proto_collect_update_download(ProtoClient *client, const char *line, void *context)
{
    UpdateDownload *download;
    char kind[16];
    char hex_text[PROTO_LINE_CAPACITY];
    char crc_text[32];
    unsigned char buffer[PROTO_UPDATE_CHUNK_CAPACITY];
    unsigned int decoded_length;
    unsigned long offset;
    unsigned long size_value;

    download = (UpdateDownload *)context;
    proto_line_kind(line, kind, sizeof(kind));
    if (strcmp(kind, "OK") == 0) {
        return proto_fill_update_info(client, line, &download->info);
    }
    if (strcmp(kind, "DATA") == 0) {
        if (!download->info.seen) {
            proto_set_error(client, "Missing update header");
            return 0;
        }

        offset = (unsigned long)proto_extract_long(line, "offset", -1L);
        if (offset != download->bytes_written) {
            proto_set_error(client, "Unexpected update offset");
            return 0;
        }
        size_value = (unsigned long)proto_extract_long(line, "size", -1L);
        if (!proto_extract_param(line, "hex", hex_text, sizeof(hex_text)) ||
            !proto_decode_hex_buffer(hex_text, buffer, sizeof(buffer), &decoded_length)) {
            proto_set_error(client, "Bad update data");
            return 0;
        }
        if (size_value != (unsigned long)decoded_length) {
            proto_set_error(client, "Update chunk size mismatch");
            return 0;
        }
        if (download->file == NULL || fwrite(buffer, 1, decoded_length, download->file) != decoded_length) {
            proto_set_error(client, "Update write failed");
            return 0;
        }

        download->bytes_written += (unsigned long)decoded_length;
        return 1;
    }
    if (strcmp(kind, "END") == 0) {
        download->end_size = (unsigned long)proto_extract_long(line, "size", -1L);
        if (!proto_extract_param(line, "file_crc32", crc_text, sizeof(crc_text)) || crc_text[0] == '\0') {
            proto_set_error(client, "Missing update end CRC");
            return 0;
        }
        download->end_crc32 = strtoul(crc_text, NULL, 16);
        download->end_seen = 1;
        return 1;
    }

    return 1;
}

void proto_client_init(ProtoClient *client)
{
    memset(client, 0, sizeof(*client));
    proto_copy_text(client->host, "127.0.0.1", sizeof(client->host));
    client->port = 5877;
    proto_copy_text(client->client_label, "BTWIN16OW", sizeof(client->client_label));
    client->server_version[0] = '\0';
    client->server_schema = -1L;
    client->server_proto = -1;
    client->socket_handle = INVALID_SOCKET;
    client->next_seq = 1;
}

void proto_client_disconnect(ProtoClient *client)
{
    if (client->socket_handle != INVALID_SOCKET) {
        if (g_winsock.socket_close != NULL) {
            g_winsock.socket_close(client->socket_handle);
        }
        client->socket_handle = INVALID_SOCKET;
    }

    if (client->wsa_ready) {
        if (g_winsock.wsa_cleanup != NULL) {
            g_winsock.wsa_cleanup();
        }
        client->wsa_ready = 0;
    }

    proto_winsock_unload();

    client->recv_len = 0;
    client->recv_buffer[0] = '\0';
    client->server_version[0] = '\0';
    client->server_schema = -1L;
    client->server_proto = -1;
}

int proto_client_connected(const ProtoClient *client)
{
    return client->socket_handle != INVALID_SOCKET;
}

int proto_client_connect(ProtoClient *client, const char *host, unsigned short port, const char *token)
{
    struct sockaddr_in address;
    unsigned long ipv4;

    proto_client_disconnect(client);
    proto_copy_text(client->host, host != NULL && host[0] != '\0' ? host : "127.0.0.1", sizeof(client->host));
    proto_copy_text(client->token, token != NULL ? token : "", sizeof(client->token));
    client->port = port > 0 ? port : 5877;
    client->next_seq = 1;
    client->last_error[0] = '\0';

    if (!proto_winsock_load(client)) {
        return 0;
    }

    if (!client->wsa_ready) {
        if (g_winsock.wsa_startup(0x0101, &client->wsa_data) != 0) {
            proto_set_error(client, "WSAStartup failed");
            return 0;
        }
        client->wsa_ready = 1;
    }

    client->socket_handle = g_winsock.socket_create(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client->socket_handle == INVALID_SOCKET) {
        proto_set_error(client, "socket failed");
        proto_client_disconnect(client);
        return 0;
    }

    ipv4 = g_winsock.addr_parse(client->host);
    if (ipv4 == INADDR_NONE) {
        proto_set_error(client, "Only numeric IPv4 addresses are supported");
        proto_client_disconnect(client);
        return 0;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = g_winsock.port_host_to_net(client->port);
    address.sin_addr.s_addr = ipv4;

    if (g_winsock.socket_connect(client->socket_handle, (struct sockaddr FAR *)&address, sizeof(address)) != 0) {
        proto_set_error(client, "connect failed");
        proto_client_disconnect(client);
        return 0;
    }

    return 1;
}

static int proto_client_hello_internal(ProtoClient *client, int enforce_version)
{
    HelloResult result;

    result.proto_version = -1;
    result.seen = 0;
    if (!proto_send_command(client, "HELLO", NULL, 0, 0, proto_collect_hello, &result)) {
        return 0;
    }
    if (!result.seen) {
        proto_set_error(client, "No HELLO response");
        return 0;
    }
    if (enforce_version && result.proto_version != PROTO_SUPPORTED_VERSION) {
        proto_set_error(client, "Proto mismatch");
        return 0;
    }

    return 1;
}

int proto_client_hello(ProtoClient *client)
{
    return proto_client_hello_internal(client, 1);
}

int proto_client_hello_relaxed(ProtoClient *client)
{
    return proto_client_hello_internal(client, 0);
}

int proto_client_auth(ProtoClient *client)
{
    ProtoParam params[2];
    int param_count;

    param_count = 0;
    if (client->client_label[0] != '\0') {
        params[param_count].key = "client";
        params[param_count].value = client->client_label;
        param_count += 1;
    }
    if (client->token[0] != '\0') {
        params[param_count].key = "token";
        params[param_count].value = client->token;
        param_count += 1;
    }

    return proto_send_command(client, "AUTH", params, param_count, 0, NULL, NULL);
}

int proto_client_get_update_info(ProtoClient *client, const char *artifact, ProtoUpdateInfo *out_info)
{
    ProtoParam params[2];
    ProtoUpdateInfo info;

    memset(&info, 0, sizeof(info));
    params[0].key = "target";
    params[0].value = PROTO_UPDATE_TARGET;
    params[1].key = "artifact";
    params[1].value = artifact != NULL && artifact[0] != '\0' ? artifact : "client";

    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }

    if (!proto_send_command(client, "GET_CLIENT_UPDATE_INFO", params, 2, 0, proto_collect_update_info, &info)) {
        return 0;
    }
    if (!info.seen) {
        proto_set_error(client, "Missing update info");
        return 0;
    }

    if (out_info != NULL) {
        *out_info = info;
    }
    return 1;
}

int proto_client_download_update(ProtoClient *client, const char *artifact, const char *destination_path, ProtoUpdateInfo *out_info)
{
    ProtoParam params[2];
    UpdateDownload download;
    unsigned long actual_crc32;
    unsigned long actual_size;

    if (destination_path == NULL || destination_path[0] == '\0') {
        proto_set_error(client, "Missing destination path");
        return 0;
    }

    memset(&download, 0, sizeof(download));
    if (out_info != NULL) {
        memset(out_info, 0, sizeof(*out_info));
    }

    download.file = fopen(destination_path, "wb");
    if (download.file == NULL) {
        proto_set_error(client, "Cannot create update file");
        return 0;
    }

    params[0].key = "target";
    params[0].value = PROTO_UPDATE_TARGET;
    params[1].key = "artifact";
    params[1].value = artifact != NULL && artifact[0] != '\0' ? artifact : "client";

    if (!proto_send_command(
            client,
            "DOWNLOAD_CLIENT_UPDATE",
            params,
            2,
            1,
            proto_collect_update_download,
            &download)) {
        fclose(download.file);
        remove(destination_path);
        return 0;
    }

    fclose(download.file);
    download.file = NULL;

    if (!download.info.seen) {
        remove(destination_path);
        proto_set_error(client, "Missing update header");
        return 0;
    }
    if (!download.end_seen) {
        remove(destination_path);
        proto_set_error(client, "Missing update end");
        return 0;
    }
    if (download.end_size != download.bytes_written || download.info.size != download.bytes_written) {
        remove(destination_path);
        proto_set_error(client, "Update size mismatch");
        return 0;
    }
    if (!proto_compute_file_crc32(destination_path, &actual_crc32, &actual_size)) {
        remove(destination_path);
        proto_set_error(client, "Update verify failed");
        return 0;
    }
    if (actual_size != download.bytes_written ||
        actual_crc32 != download.info.file_crc32 ||
        actual_crc32 != download.end_crc32) {
        remove(destination_path);
        proto_set_error(client, "Update CRC mismatch");
        return 0;
    }

    if (out_info != NULL) {
        *out_info = download.info;
    }
    return 1;
}

int proto_client_list_todos(ProtoClient *client, int archived, ProtoTodo *items, int max_items, int *out_count)
{
    TodoCollector collector;

    collector.items = items;
    collector.max_items = max_items;
    collector.count = 0;
    if (out_count != NULL) {
        *out_count = 0;
    }

    if (!proto_send_command(
            client,
            archived ? "LIST_ARCHIVED" : "LIST_TODOS",
            NULL,
            0,
            1,
            proto_collect_todo,
            &collector)) {
        return 0;
    }

    if (out_count != NULL) {
        *out_count = collector.count;
    }
    return 1;
}

int proto_client_list_tasks(ProtoClient *client, long todo_id, ProtoTask *items, int max_items, int *out_count)
{
    TaskCollector collector;
    ProtoParam params[1];
    char todo_id_text[32];

    sprintf(todo_id_text, "%ld", todo_id);
    params[0].key = "todo_id";
    params[0].value = todo_id_text;

    collector.items = items;
    collector.max_items = max_items;
    collector.count = 0;
    if (out_count != NULL) {
        *out_count = 0;
    }

    if (!proto_send_command(client, "LIST_TASKS", params, 1, 1, proto_collect_task, &collector)) {
        return 0;
    }

    if (out_count != NULL) {
        *out_count = collector.count;
    }
    return 1;
}

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
)
{
    ProtoParam params[8];
    LongResult result;
    int param_count;

    param_count = 0;
    params[param_count].key = "title";
    params[param_count].value = title;
    param_count += 1;
    proto_param_push_if_text(params, &param_count, "desc", description);
    proto_param_push_if_text(params, &param_count, "order_number", order_number);
    proto_param_push_if_text(params, &param_count, "purchaser", purchaser);
    proto_param_push_if_text(params, &param_count, "order_date", order_date);
    proto_param_push_if_text(params, &param_count, "budget_spent", budget_spent);
    proto_param_push_if_text(params, &param_count, "budget_planned", budget_planned);
    proto_param_push_if_text(params, &param_count, "deadline", deadline);

    result.key = "id";
    result.value = -1L;
    result.seen = 0;
    if (out_id != NULL) {
        *out_id = -1L;
    }

    if (!proto_send_command(client, "ADD_TODO", params, param_count, 0, proto_collect_long_result, &result)) {
        return 0;
    }

    if (!result.seen) {
        proto_set_error(client, "Missing id");
        return 0;
    }
    if (out_id != NULL) {
        *out_id = result.value;
    }

    return 1;
}

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
)
{
    ProtoParam params[9];
    char todo_id_text[32];
    int param_count;

    sprintf(todo_id_text, "%ld", todo_id);
    param_count = 0;
    params[param_count].key = "id";
    params[param_count].value = todo_id_text;
    param_count += 1;
    params[param_count].key = "title";
    params[param_count].value = title;
    param_count += 1;
    proto_param_push_if_text(params, &param_count, "desc", description);
    proto_param_push_if_text(params, &param_count, "order_number", order_number);
    proto_param_push_if_text(params, &param_count, "purchaser", purchaser);
    proto_param_push_if_text(params, &param_count, "order_date", order_date);
    proto_param_push_if_text(params, &param_count, "budget_spent", budget_spent);
    proto_param_push_if_text(params, &param_count, "budget_planned", budget_planned);
    proto_param_push_if_text(params, &param_count, "deadline", deadline);

    return proto_send_command(client, "UPDATE_TODO", params, param_count, 0, NULL, NULL);
}

int proto_client_add_task(ProtoClient *client, long todo_id, const char *title, const char *description, const char *amount, long *out_id)
{
    ProtoParam params[4];
    LongResult result;
    char todo_id_text[32];
    int param_count;

    sprintf(todo_id_text, "%ld", todo_id);
    param_count = 0;
    params[param_count].key = "todo_id";
    params[param_count].value = todo_id_text;
    param_count += 1;
    params[param_count].key = "title";
    params[param_count].value = title;
    param_count += 1;
    proto_param_push_if_text(params, &param_count, "desc", description);
    proto_param_push_if_text(params, &param_count, "amount", amount);

    result.key = "id";
    result.value = -1L;
    result.seen = 0;
    if (out_id != NULL) {
        *out_id = -1L;
    }

    if (!proto_send_command(client, "ADD_TASK", params, param_count, 0, proto_collect_long_result, &result)) {
        return 0;
    }

    if (!result.seen) {
        proto_set_error(client, "Missing id");
        return 0;
    }
    if (out_id != NULL) {
        *out_id = result.value;
    }

    return 1;
}

int proto_client_update_task(ProtoClient *client, long task_id, const char *title, const char *description, const char *amount)
{
    ProtoParam params[4];
    char task_id_text[32];
    int param_count;

    sprintf(task_id_text, "%ld", task_id);
    param_count = 0;
    params[param_count].key = "id";
    params[param_count].value = task_id_text;
    param_count += 1;
    params[param_count].key = "title";
    params[param_count].value = title;
    param_count += 1;
    proto_param_push_if_text(params, &param_count, "desc", description);
    proto_param_push_if_text(params, &param_count, "amount", amount);

    return proto_send_command(client, "UPDATE_TASK", params, param_count, 0, NULL, NULL);
}

int proto_client_toggle_task(ProtoClient *client, long task_id, long *out_done)
{
    ProtoParam params[1];
    LongResult result;
    char task_id_text[32];

    sprintf(task_id_text, "%ld", task_id);
    params[0].key = "id";
    params[0].value = task_id_text;

    result.key = "done";
    result.value = 0L;
    result.seen = 0;
    if (out_done != NULL) {
        *out_done = 0L;
    }

    if (!proto_send_command(client, "TOGGLE_TASK", params, 1, 0, proto_collect_long_result, &result)) {
        return 0;
    }

    if (!result.seen) {
        proto_set_error(client, "Missing done flag");
        return 0;
    }
    if (out_done != NULL) {
        *out_done = result.value;
    }

    return 1;
}

int proto_client_archive_todo(ProtoClient *client, long todo_id)
{
    ProtoParam params[1];
    char todo_id_text[32];

    sprintf(todo_id_text, "%ld", todo_id);
    params[0].key = "id";
    params[0].value = todo_id_text;
    return proto_send_command(client, "ARCHIVE_TODO", params, 1, 0, NULL, NULL);
}

int proto_client_unarchive_todo(ProtoClient *client, long todo_id)
{
    ProtoParam params[1];
    char todo_id_text[32];

    sprintf(todo_id_text, "%ld", todo_id);
    params[0].key = "id";
    params[0].value = todo_id_text;
    return proto_send_command(client, "UNARCHIVE_TODO", params, 1, 0, NULL, NULL);
}

const char *proto_client_last_error(const ProtoClient *client)
{
    if (client->last_error[0] == '\0') {
        return "Unknown error";
    }
    return client->last_error;
}

int proto_file_signature(const char *path, unsigned long *out_crc32, unsigned long *out_size)
{
    return proto_compute_file_crc32(path, out_crc32, out_size);
}
