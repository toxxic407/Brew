/**
 * Copyright (c) 2016 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */


#ifndef SANDBIRD_H
#define SANDBIRD_H

#include <stddef.h>
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SB_VERSION "0.1.4"

typedef struct sb_Server  sb_Server;
typedef struct sb_Stream  sb_Stream;
typedef struct sb_Event   sb_Event;
typedef struct sb_Options sb_Options;
typedef int (*sb_Handler)(sb_Event*);

struct sb_Event {
  int type;
  void *udata;
  sb_Server *server;
  sb_Stream *stream;
  const char *address;
  const char *method;
  const char *path;
};

struct sb_Options {
  sb_Handler handler;
  void *udata;
  const char *host;
  const char *port;
  const char *timeout;
  const char *max_lifetime;
  const char *max_request_size;
};

enum {
  SB_ESUCCESS     =  0,
  SB_EFAILURE     = -1,
  SB_EOUTOFMEM    = -2,
  SB_ETRUNCATED   = -3,
  SB_EBADSTATE    = -4,
  SB_EBADRESULT   = -5,
  SB_ECANTOPEN    = -6,
  SB_ENOTFOUND    = -7,
};

enum {
  SB_EV_CONNECT,
  SB_EV_CLOSE,
  SB_EV_REQUEST
};

enum {
  SB_RES_OK,
  SB_RES_CLOSE
};

const char *sb_error_str(int code);
sb_Server *sb_new_server(const sb_Options *opt);
void sb_close_server(sb_Server *srv);
int sb_poll_server(sb_Server *srv);
int sb_send_status(sb_Stream *st, int code, const char *msg);
int sb_send_header(sb_Stream *st, const char *field, const char *val);
int sb_send_file(sb_Stream *st, const char *filename);
int sb_write(sb_Stream *st, const void *data, size_t len);
int sb_vwritef(sb_Stream *st, const char *fmt, va_list args);
int sb_writef(sb_Stream *st, const char *fmt, ...);
int sb_get_header(sb_Stream *st, const char *field, char *dst, size_t len);
int sb_get_var_ex(sb_Stream *st, const char *name, char *dst, size_t len, bool from_data_only);
int sb_get_var(sb_Stream *st, const char *name, char *dst, size_t len);
char *sb_get_content_data(sb_Stream *st, size_t *len);
int sb_get_cookie(sb_Stream *st, const char *name, char *dst, size_t len);
const void *sb_get_multipart(sb_Stream *st, const char *name, size_t *len);
time_t sb_stream_get_init_time(sb_Stream *st);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
