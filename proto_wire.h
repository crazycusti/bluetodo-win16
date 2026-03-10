#ifndef BLUETODO_PROTO_WIRE_H
#define BLUETODO_PROTO_WIRE_H

#include "proto.h"

void proto_copy_text(char *target, const char *source, unsigned int capacity);
int proto_append_text(char *target, const char *source, unsigned int capacity);
int proto_append_encoded_text(char *target, const char *source, unsigned int capacity);
void proto_set_error(ProtoClient *client, const char *message);
unsigned long proto_crc32_n(const char *data, unsigned int length);
unsigned long proto_crc32_step(unsigned long crc, const unsigned char *data, unsigned int length);
int proto_find_crc_marker(const char *line);
int proto_extract_crc(const char *line, char *crc_text, unsigned int capacity);
int proto_verify_line(const char *line);
int proto_hex_value(char c);
void proto_line_kind(const char *line, char *kind, unsigned int capacity);
int proto_extract_param(const char *line, const char *key, char *value, unsigned int capacity);
long proto_extract_long(const char *line, const char *key, long default_value);

#endif
