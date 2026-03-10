#include "proto_wire.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void proto_copy_text(char *target, const char *source, unsigned int capacity)
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

int proto_append_text(char *target, const char *source, unsigned int capacity)
{
    unsigned int target_length;
    unsigned int index;

    if (target == NULL || source == NULL || capacity == 0) {
        return 0;
    }

    target_length = (unsigned int)strlen(target);
    for (index = 0; source[index] != '\0'; ++index) {
        if (target_length + index + 1 >= capacity) {
            return 0;
        }
        target[target_length + index] = source[index];
    }

    target[target_length + index] = '\0';
    return 1;
}

int proto_append_encoded_text(char *target, const char *source, unsigned int capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int target_length;
    unsigned int source_index;
    unsigned char current;

    if (target == NULL || source == NULL || capacity == 0) {
        return 0;
    }

    target_length = (unsigned int)strlen(target);
    for (source_index = 0; source[source_index] != '\0'; ++source_index) {
        current = (unsigned char)source[source_index];
        if (isalnum(current) || current == '-' || current == '_' || current == '.' || current == '~') {
            if (target_length + 2 > capacity) {
                return 0;
            }
            target[target_length++] = (char)current;
        } else {
            if (target_length + 4 > capacity) {
                return 0;
            }
            target[target_length++] = '%';
            target[target_length++] = hex[(current >> 4) & 0x0F];
            target[target_length++] = hex[current & 0x0F];
        }
    }

    target[target_length] = '\0';
    return 1;
}

void proto_set_error(ProtoClient *client, const char *message)
{
    if (client == NULL) {
        return;
    }
    proto_copy_text(client->last_error, message, sizeof(client->last_error));
}

unsigned long proto_crc32_n(const char *data, unsigned int length)
{
    return ~proto_crc32_step(0xFFFFFFFFUL, (const unsigned char *)data, length);
}

unsigned long proto_crc32_step(unsigned long crc, const unsigned char *data, unsigned int length)
{
    unsigned long value;
    unsigned int index;
    unsigned int bit;
    unsigned char current;

    for (index = 0; index < length; ++index) {
        current = data[index];
        value = (crc ^ current) & 0xFFUL;
        for (bit = 0; bit < 8; ++bit) {
            if ((value & 1UL) != 0) {
                value = 0xEDB88320UL ^ (value >> 1);
            } else {
                value >>= 1;
            }
        }
        crc = (crc >> 8) ^ value;
    }

    return crc;
}

int proto_find_crc_marker(const char *line)
{
    int marker_length;
    int start;

    marker_length = 7;
    for (start = (int)strlen(line) - marker_length; start >= 0; --start) {
        if (strncmp(line + start, " crc32=", marker_length) == 0) {
            return start;
        }
    }

    return -1;
}

int proto_extract_crc(const char *line, char *crc_text, unsigned int capacity)
{
    int marker;

    marker = proto_find_crc_marker(line);
    if (marker < 0) {
        return 0;
    }

    proto_copy_text(crc_text, line + marker + 7, capacity);
    return crc_text[0] != '\0';
}

int proto_verify_line(const char *line)
{
    char crc_text[16];
    unsigned long expected;
    unsigned long actual;
    int marker;

    if (!proto_extract_crc(line, crc_text, sizeof(crc_text))) {
        return 0;
    }

    marker = proto_find_crc_marker(line);
    if (marker < 0) {
        return 0;
    }

    expected = proto_crc32_n(line, (unsigned int)marker);
    actual = strtoul(crc_text, NULL, 16);
    return expected == actual;
}

int proto_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

void proto_line_kind(const char *line, char *kind, unsigned int capacity)
{
    int marker;
    unsigned int index;

    marker = proto_find_crc_marker(line);
    if (marker < 0) {
        marker = (int)strlen(line);
    }

    for (index = 0; index < (unsigned int)marker && line[index] != ' ' && index + 1 < capacity; ++index) {
        kind[index] = line[index];
    }
    kind[index] = '\0';
}

int proto_extract_param(const char *line, const char *key, char *value, unsigned int capacity)
{
    unsigned int index;
    unsigned int end;
    unsigned int token_start;
    unsigned int token_end;
    unsigned int key_length;
    unsigned int equals_index;
    unsigned int out_index;
    int marker;
    int high;
    int low;

    if (value == NULL || capacity == 0) {
        return 0;
    }

    value[0] = '\0';
    key_length = (unsigned int)strlen(key);
    marker = proto_find_crc_marker(line);
    end = marker >= 0 ? (unsigned int)marker : (unsigned int)strlen(line);
    index = 0;

    while (index < end) {
        while (index < end && line[index] == ' ') {
            index += 1;
        }
        if (index >= end) {
            break;
        }

        token_start = index;
        while (index < end && line[index] != ' ') {
            index += 1;
        }
        token_end = index;

        equals_index = token_start;
        while (equals_index < token_end && line[equals_index] != '=') {
            equals_index += 1;
        }
        if (equals_index < token_end &&
            equals_index - token_start == key_length &&
            strncmp(line + token_start, key, key_length) == 0) {
            out_index = 0;
            for (equals_index += 1; equals_index < token_end && out_index + 1 < capacity; ++equals_index) {
                if (line[equals_index] == '%' && equals_index + 2 < token_end) {
                    high = proto_hex_value(line[equals_index + 1]);
                    low = proto_hex_value(line[equals_index + 2]);
                    if (high >= 0 && low >= 0) {
                        value[out_index++] = (char)((high << 4) | low);
                        equals_index += 2;
                        continue;
                    }
                }
                value[out_index++] = line[equals_index];
            }
            value[out_index] = '\0';
            return 1;
        }
    }

    return 0;
}

long proto_extract_long(const char *line, const char *key, long default_value)
{
    char buffer[64];

    if (!proto_extract_param(line, key, buffer, sizeof(buffer))) {
        return default_value;
    }
    return atol(buffer);
}
