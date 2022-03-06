/**
 * Copyright (c) 2016 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */


#ifndef _POSIX_C_SOURCE
  #define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>

#include "sandbird.h"

typedef int sb_Socket;
#define INVALID_SOCKET -1

typedef struct sb_Buffer sb_Buffer;

struct sb_Buffer { char *s; size_t len, cap; };

struct sb_Stream {
  int state;                  /* Current state of the stream */
  sb_Server *server;          /* The server object which owns this stream */
  char address[46];           /* Remote IP address */
  time_t init_time;           /* Time the stream was created */
  time_t last_activity;       /* Time of Last I/O activity on the stream */
  size_t expected_recv_len;   /* Expected length of the stream's request */
  size_t data_idx;            /* Index of data section in recv_buf */
  sb_Socket sockfd;           /* Socket for this streams connection */
  sb_Buffer recv_buf;         /* Data received from client */
  sb_Buffer send_buf;         /* Data waiting to be sent to client */
  int send_fd;                /* File descriptor currently being sent to client */
  pthread_t thr;              /* Processing thread */
  sb_Stream *next;            /* Next stream in linked list */
  sb_Stream *prev;            /* Previous stream in linked list */
};

struct sb_Server {
  sb_Stream *streams;         /* Linked list of all streams */
  sb_Handler handler;         /* Event handler callback function */
  sb_Socket sockfd;           /* Listeneing server socket */
  void *udata;                /* User data value passed to all events */
  time_t now;                 /* The current time */
  time_t timeout;             /* Stream no-activity timeout */
  time_t max_lifetime;        /* Maximum time a stream can exist */
  size_t max_request_size;    /* Maximum request size in bytes */
  pthread_mutex_t stream_mtx; /* Mutex to lock stream access */
};

enum {
  STATE_RECEIVING_HEADER,
  STATE_RECEIVING_REQUEST,
  STATE_SENDING_STATUS,
  STATE_SENDING_HEADER,
  STATE_SENDING_DATA,
  STATE_SENDING_FILE,
  STATE_CLOSING
};


/*===========================================================================
 * Utility
 *===========================================================================*/

static int set_socket_blocking(sb_Socket sockfd, bool flag) {
  int flags;
  int ret;
  flags = fcntl(sockfd, F_GETFL);
  if (flags < 0) {
    return SB_EFAILURE;
  }
  flags &= ~O_NONBLOCK;
  if (!flag) {
    flags |= O_NONBLOCK;
  }
  ret = fcntl(sockfd, F_SETFL, flags);
  if (ret < 0) {
    return SB_EFAILURE;
  }
  return SB_ESUCCESS;
}


static int get_socket_address(sb_Socket sockfd, char *dst) {
  int err;
  union { struct sockaddr sa;
          struct sockaddr_storage sas;
          struct sockaddr_in sai;
  } addr;
  socklen_t sz = sizeof(addr);
  err = getpeername(sockfd, &addr.sa, &sz);
  if (err == -1) {
    *dst = '\0';
    return SB_EFAILURE;
  }
  inet_ntop(AF_INET, &addr.sai.sin_addr, dst, INET_ADDRSTRLEN);
  return SB_ESUCCESS;
}


static unsigned str_to_uint(const char *str) {
  unsigned n;
  if (!str || sscanf(str, "%u", &n) != 1) return 0;
  return n;
}


static int hex_to_int(int chr) {
  return isdigit(chr) ? (chr - '0') : (tolower(chr) - 'a' + 10);
}


static int url_decode(char *dst, const char *src, size_t len) {
  len--;
  while (*src && !strchr("?& \t\r\n", *src) && len) {
    if (src[0] == '%' && src[1] && src[2]) {
      *dst = (hex_to_int(src[1]) << 4) | hex_to_int(src[2]);
      src += 2;
    } else if (*src == '+') {
      *dst = ' ';
    } else {
      *dst = *src;
    }
    dst++, src++, len--;
  }
  *dst = '\0';
  return (len == 0) ? SB_ETRUNCATED : SB_ESUCCESS;
}


static int mem_equal(const void *a, const void *b, size_t len) {
  const char *p = a, *q = b;
  while (len) {
    if (*p != *q) return 0;
    p++, q++, len--;
  }
  return 1;
}


static int mem_case_equal(const void *a, const void *b, size_t len) {
  const char *p = a, *q = b;
  while (len) {
    if (tolower(*p) != tolower(*q)) return 0;
    p++, q++, len--;
  }
  return 1;
}


static const char *find_header_value(const char *str, const char *field) {
  size_t len = strlen(field);
  while (*str && !mem_equal(str, "\r\n", 2)) {
    if (mem_case_equal(str, field, len) && str[len] == ':') {
      str += len + 1;
      return str + strspn(str, " \t");
    }
    str += strcspn(str, "\r");
    str += mem_equal(str, "\r\n", 2) ? 2 : 0;
  }
  return NULL;
}


static const char *find_var_value(const char *str, const char *name) {
  size_t len = strlen(name);
  for (;;) {
    if (mem_equal(str, name, len) && str[len] == '=') {
      return str + len + 1;
    }
    str += strcspn(str, "& \t\r\n");
    if (*str != '&') break;
    str++;
  }
  return NULL;
}


const char *sb_error_str(int code) {
  switch (code) {
    case SB_ESUCCESS    : return "success";
    case SB_EFAILURE    : return "failure";
    case SB_EOUTOFMEM   : return "out of memory";
    case SB_ETRUNCATED  : return "result truncated";
    case SB_EBADSTATE   : return "bad stream state for this operation";
    case SB_EBADRESULT  : return "bad result code from event handler";
    case SB_ECANTOPEN   : return "cannot open file";
    case SB_ENOTFOUND   : return "not found";
    default             : return "unknown";
  }
}


/*===========================================================================
 * Buffer
 *===========================================================================*/

static void sb_buffer_init(sb_Buffer *buf) {
  memset(buf, 0, sizeof(*buf));
}


static void sb_buffer_deinit(sb_Buffer *buf) {
  free(buf->s);
}


static void sb_buffer_shift(sb_Buffer *buf, size_t n) {
  buf->len -= n;
  memmove(buf->s, buf->s + n, buf->len);
}


static int sb_buffer_reserve(sb_Buffer *buf, size_t n) {
  void *p;
  if (buf->cap >= n) return SB_ESUCCESS;
  p = realloc(buf->s, n);
  if (!p) return SB_EOUTOFMEM;
  buf->s = p;
  buf->cap = n;
  return SB_ESUCCESS;
}


static int sb_buffer_push_char(sb_Buffer *buf, char chr) {
  if (buf->len == buf->cap) {
    int err = sb_buffer_reserve(buf, (buf->cap << 1) | (!buf->cap));
    if (err) return err;
  }
  buf->s[buf->len++] = chr;
  return SB_ESUCCESS;
}


static int sb_buffer_push_str(sb_Buffer *buf, const char *p, size_t len) {
  int err;
  size_t orig_len = buf->len;
  while (len) {
    err = sb_buffer_push_char(buf, *p);
    if (err) {
      buf->len = orig_len;
      return err;
    }
    p++, len--;
  }
  return SB_ESUCCESS;
}


static inline bool fmt_is_modifier(int c) {
  return (c == 'l');
}


static int sb_buffer_vwritef(sb_Buffer *buf, const char *fmt, va_list args) {
  int err;
  size_t orig_len = buf->len;
  char fbuf[64];
  char lbuf[512];
  bool long_mode = false;
  char *s;

  while (*fmt) {
    if (*fmt == '%') {
      switch (*++fmt) {

        case 's':
          s = va_arg(args, char*);
          if (s == NULL) s = "(null)";
          err = sb_buffer_push_str(buf, s, strlen(s));
          if (err) goto fail;
          break;

        default:
          fbuf[0] = '%';
          s = fbuf + 1;
          while ( !isalpha(*fmt) && *fmt != '%' ) *s++ = *fmt++;
repeat:
          s[0] = *fmt, s[1] = '\0';
          switch (*fmt) {
            case 'l':
              long_mode = true;
              ++fmt;
              ++s;
              goto repeat;
            case 'f':
            case 'g':
              if (long_mode) {
                sprintf(lbuf, fbuf, va_arg(args, long double));
                long_mode = false;
              } else {
                sprintf(lbuf, fbuf, va_arg(args, double));
              }
            break;
            case 'c':
            case 'd':
            case 'i':
              if (long_mode) {
                sprintf(lbuf, fbuf, va_arg(args, long int));
                long_mode = false;
              } else {
                sprintf(lbuf, fbuf, va_arg(args, int));
              }
            break;
            case 'u':
            case 'x':
            case 'X':
              if (long_mode) {
                sprintf(lbuf, fbuf, va_arg(args, long unsigned));
                long_mode = false;
              } else {
                sprintf(lbuf, fbuf, va_arg(args, unsigned));
              }
             break;
            case 'p':
              if (long_mode) {
                sprintf(lbuf, fbuf, va_arg(args, void*));
                long_mode = false;
              } else {
                sprintf(lbuf, fbuf, va_arg(args, void*));
              }
            break;
            default:
              long_mode = false;
              lbuf[0] = *fmt;
              lbuf[1] = '\0';
            break;
          }
          err = sb_buffer_push_str(buf, lbuf, strlen(lbuf));
          if (err) goto fail;
      }
    } else {
      err = sb_buffer_push_char(buf, *fmt);
      if (err) goto fail;
    }
    fmt++;
  }

  return SB_ESUCCESS;

fail:
  buf->len = orig_len;
  return err;
}


static int sb_buffer_writef(sb_Buffer *buf, const char *fmt, ...) {
  int err;
  va_list args;
  va_start(args, fmt);
  err = sb_buffer_vwritef(buf, fmt, args);
  va_end(args);
  return err;
}


static int sb_buffer_null_terminate(sb_Buffer *buf) {
  int err = sb_buffer_push_char(buf, '\0');
  if (err) return err;
  buf->len--;
  return SB_ESUCCESS;
}


/*===========================================================================
 * Stream
 *===========================================================================*/

static sb_Stream *sb_stream_new(sb_Server *srv, sb_Socket sockfd) {
  sb_Stream *st = malloc( sizeof(*st) );
  if (!st) return NULL;
  memset(st, 0, sizeof(*st));
  sb_buffer_init(&st->recv_buf);
  sb_buffer_init(&st->send_buf);
  st->sockfd = sockfd;
  st->server = srv;
  st->init_time = time(NULL);
  st->last_activity = srv->now;
  set_socket_blocking(sockfd, true);
  get_socket_address(sockfd, st->address);
  return st;
}


static void sb_stream_close(sb_Stream *st) {
  st->state = STATE_CLOSING;
}


static int sb_stream_emit(sb_Stream *st, sb_Event *e) {
  int res;
  e->stream = st;
  e->udata = st->server->udata;
  e->server = st->server;
  e->address = st->address;
  res = e->server->handler(e);
  if (res < 0) return res;
  switch (res) {
    case SB_RES_CLOSE : sb_stream_close(st); /* Fall through */
    case SB_RES_OK    : return SB_ESUCCESS;
    default           : return SB_EBADRESULT;
  }
}


static void sb_stream_link(sb_Stream *st) {
  sb_Server *srv = st->server;
  struct sb_Stream *st2;

  /* Push stream to list */
  pthread_mutex_lock(&srv->stream_mtx);
  if (srv->streams) {
    st2 = srv->streams;
    while (st2->next) {
      st2 = st2->next;
    }
    st2->next = st;
    st->prev = st2;
  } else {
    srv->streams = st;
  }
  pthread_mutex_unlock(&srv->stream_mtx);
}


static void sb_stream_unlink(sb_Stream *st) {
  sb_Server *srv = st->server;

  /* Pop stream from list */
  pthread_mutex_lock(&srv->stream_mtx);
  if (st->next) {
    st->next->prev = st->prev;
  }
  if (st->prev) {
    st->prev->next = st->next;
  }
  if (srv->streams == st) {
    srv->streams = st->next;
  }
  pthread_mutex_unlock(&srv->stream_mtx);
}


static void sb_stream_destroy(sb_Stream *st) {
  sb_Event e;

  /* Emit close event */
  memset(&e, 0, sizeof(e));
  e.type = SB_EV_CLOSE;
  sb_stream_emit(st, &e);

  /* Clean up */
  shutdown(st->sockfd, SHUT_RDWR);
  close(st->sockfd);
  if (st->send_fd > 0) {
    shutdown(st->send_fd, SHUT_RDWR);
    close(st->send_fd);
  }
  sb_buffer_deinit(&st->recv_buf);
  sb_buffer_deinit(&st->send_buf);
  free(st);
}


static int sb_stream_recv(sb_Stream *st) {
  for (;;) {
    char buf[4096];
    size_t n;
    int err, i, sz;

    /* Receive data */
    sz = recv(st->sockfd, buf, sizeof(buf) - 1, 0);
    if (sz <= 0) {
      /* Disconnected? */
      if (sz == 0 || errno != EWOULDBLOCK) {
        sb_stream_close(st);
      }
      return SB_ESUCCESS;
    }

    /* Update last_activity */
    st->last_activity = st->server->now;

    /* Write to recv_buf */
    for (i = 0; i < sz; i++) {
      err = sb_buffer_push_char(&st->recv_buf, buf[i]);
      if (err) {
close_stream:
        sb_stream_close(st);
        return err;
      }

      /* Have we received the whole header? */
      if (
        st->state == STATE_RECEIVING_HEADER &&
        st->recv_buf.len >= 4 &&
        mem_equal(st->recv_buf.s + st->recv_buf.len - 4, "\r\n\r\n", 4)
      ) {
        const char *s;
        /* Update stream's current state */
        st->state = STATE_RECEIVING_REQUEST;
        /* Assure recv_buf is null-terminated */
        err = sb_buffer_null_terminate(&st->recv_buf);
        if (err) goto close_stream;
        /* If the header contains the Content-Length field we set the
         * expected_recv_len and continue writing to the recv_buf, otherwise we
         * assume the request is complete */
        s = find_header_value(st->recv_buf.s, "Content-Length");
        if (s) {
          st->expected_recv_len = st->recv_buf.len + str_to_uint(s);
          st->data_idx = st->recv_buf.len;
        } else {
          goto handle_request;
        }
      }

      /* Have we received all the data we're expecting? */
      if (st->expected_recv_len == st->recv_buf.len) {
        /* Handle request */
        sb_Event e;
        int n, path_idx;
        char method[16], path[512], ver[16];
handle_request:
        st->state = STATE_SENDING_STATUS;
        /* Assure recv_buf string is NULL-terminated */
        err = sb_buffer_null_terminate(&st->recv_buf);
        if (err) return err;
        /* Get method, path, version */
        n = sscanf(st->recv_buf.s, "%15s %n%*s %15s", method, &path_idx, ver);
        /* Is request line invalid? */
        if (n != 2 || !mem_equal(ver, "HTTP", 4)) {
          sb_stream_close(st);
          return SB_ESUCCESS;
        }
        /* Build and emit `request` event */
        url_decode(path, st->recv_buf.s + path_idx, sizeof(path));
        memset(&e, 0, sizeof(e));
        e.type = SB_EV_REQUEST;
        e.method = method;
        e.path = path;
        err = sb_stream_emit(st, &e);
        if (err) goto close_stream;
        /* No more data needs to be received (nor should it exist) */
        return SB_ESUCCESS;
      }
    }
  }

  return SB_ESUCCESS;
}


static int sb_stream_send(sb_Stream *st) {
  if (st->send_buf.len > 0) {
    int sz;

    /* Send data */
send_data:
    sz = send(st->sockfd, st->send_buf.s, st->send_buf.len, 0);
    if (sz <= 0) {
      /* Disconnected? */
      if (errno != EWOULDBLOCK) {
        sb_stream_close(st);
      }
      return SB_ESUCCESS;
    }

    /* Remove sent bytes from buffer */
    sb_buffer_shift(&st->send_buf, sz);

    /* Update last_activity */
    st->last_activity = st->server->now;

  } else if (st->send_fd > 0) {
    /* Read chunk, write to stream and continue sending */
    int err = sb_buffer_reserve(&st->send_buf, 8192);
    if (err) return err;
    st->send_buf.len = read(st->send_fd, st->send_buf.s, st->send_buf.cap);
    if (st->send_buf.len > 0) goto send_data;

    /* Reached end of file */
    close(st->send_fd);
    st->send_fd = -1;

  } else {
    /* No more data left -- disconnect */
    sb_stream_close(st);
  }

  return SB_ESUCCESS;
}


static int sb_stream_finalize_header(sb_Stream *st) {
  int err;
  if (st->state < STATE_SENDING_HEADER) {
    err = sb_send_status(st, 200, "OK");
    if (err) return err;
  }
  err = sb_buffer_push_str(&st->send_buf, "\r\n", 2);
  if (err) return err;
  st->state = STATE_SENDING_DATA;
  return SB_ESUCCESS;
}


int sb_send_status(sb_Stream *st, int code, const char *msg) {
  int err;
  if (st->state != STATE_SENDING_STATUS) {
    return SB_EBADSTATE;
  }
  err = sb_buffer_writef(&st->send_buf, "HTTP/1.1 %d %s\r\n", code, msg);
  if (err) return err;
  st->state = STATE_SENDING_HEADER;
  return SB_ESUCCESS;
}


int sb_send_header(sb_Stream *st, const char *field, const char *val) {
  int err;
  if (st->state > STATE_SENDING_HEADER) {
    return SB_EBADSTATE;
  }
  if (st->state < STATE_SENDING_HEADER) {
    err = sb_send_status(st, 200, "OK");
    if (err) return err;
  }
  err = sb_buffer_writef(&st->send_buf, "%s: %s\r\n", field, val);
  if (err) return err;
  return SB_ESUCCESS;
}


int sb_send_file(sb_Stream *st, const char *filename) {
  int err;
  char buf[32];
  struct stat stbuf;
  int fd = -1;
  if (st->state > STATE_SENDING_HEADER) {
    return SB_EBADSTATE;
  }
  /* Try to open file */
  fd = open(filename, O_RDONLY);
  if (fd <= 0) return SB_ECANTOPEN;

  /* Get file size and write headers */
  if (fstat(fd, &stbuf) < 0) {
    err = SB_EBADRESULT;
    goto fail;
  }
  snprintf(buf, sizeof(buf), "%" PRIuMAX, (uintmax_t)stbuf.st_size);
  err = sb_send_header(st, "Content-Length", buf);
  if (err) goto fail;
  err = sb_stream_finalize_header(st);
  if (err) goto fail;

  /* Set stream's file descriptor and state */
  st->send_fd = fd;
  st->state = STATE_SENDING_FILE;
  return SB_ESUCCESS;

fail:
  if (fd > 0) close(fd);
  return err;
}


int sb_write(sb_Stream *st, const void *data, size_t len) {
  if (st->state < STATE_SENDING_DATA) {
    int err = sb_stream_finalize_header(st);
    if (err) return err;
  }
  if (st->state != STATE_SENDING_DATA) return SB_EBADSTATE;
  return sb_buffer_push_str(&st->send_buf, data, len);
}


int sb_vwritef(sb_Stream *st, const char *fmt, va_list args) {
  if (st->state < STATE_SENDING_DATA) {
    int err = sb_stream_finalize_header(st);
    if (err) return err;
  }
  if (st->state != STATE_SENDING_DATA) return SB_EBADSTATE;
  return sb_buffer_vwritef(&st->send_buf, fmt, args);
}


int sb_writef(sb_Stream *st, const char *fmt, ...) {
  int err;
  va_list args;
  va_start(args, fmt);
  err = sb_vwritef(st, fmt, args);
  va_end(args);
  return err;
}


int sb_get_header(sb_Stream *st, const char *field, char *dst, size_t len) {
  size_t n;
  int res = SB_ESUCCESS;
  const char *s = find_header_value(st->recv_buf.s, field);
  if (!s) {
    *dst = '\0';
    return SB_ENOTFOUND;
  }
  n = strchr(s, '\r') - s;
  while (n > 1 && strchr(" \t", s[n-1])) n--; /* trim whitespace from end */
  if (n > len - 1) {
    n = len - 1;
    res = SB_ETRUNCATED;
  }
  memcpy(dst, s, n);
  dst[n] = '\0';
  return res;
}


int sb_get_var_ex(sb_Stream *st, const char *name, char *dst, size_t len, bool from_data_only) {
  const char *q, *s = NULL;

  if (!from_data_only) {
    /* Find beginning of query string */
    q = st->recv_buf.s + strcspn(st->recv_buf.s, "?\r");
    q = (*q == '?') ? (q + 1) : NULL;
  } else {
    q = NULL;
  }

  /* Try to get var from query string, then data string */
  if (q) s = find_var_value(q, name);
  if (!s && st->data_idx) {
    s = find_var_value(st->recv_buf.s + st->data_idx, name);
  }
  if (!s) {
    *dst = '\0';
    return SB_ENOTFOUND;
  }
  return url_decode(dst, s, len);
}


int sb_get_var(sb_Stream *st, const char *name, char *dst, size_t len) {
  return sb_get_var_ex(st, name, dst, len, false);
}


char *sb_get_content_data(sb_Stream *st, size_t *len) {
  if (st->data_idx) {
    if (len) {
      *len = st->expected_recv_len - st->data_idx;
    }
    return st->recv_buf.s + st->data_idx;
  } else {
    if (len) {
      *len = 0;
    }
    return NULL;
  }
}


int sb_get_cookie(sb_Stream *st, const char *name, char *dst, size_t len) {
  size_t n;
  const char *s = st->recv_buf.s;
  int res = SB_ESUCCESS;
  size_t name_len = strlen(name);

  /* Get cookie header */
  s = find_header_value(st->recv_buf.s, "Cookie");
  if (!s) goto fail;

  /* Find var */
  while (*s) {
    s += strspn(s, " \t");
    /* Found var? find value, get len, copy value and return */
    if ( mem_case_equal(s, name, name_len) && strchr(" =", s[name_len]) ) {
      s += name_len;
      s += strspn(s, "= \t\r");
      n = strcspn(s, ";\r");
      if (n >= len - 1) {
        n = len - 1;
        res = SB_ETRUNCATED;
      }
      memcpy(dst, s, n);
      dst[n] = '\0';
      return res;
    }
    s += strcspn(s, ";\r");
    if (*s != ';') goto fail;
    s++;
  }

fail:
  *dst = '\0';
  return SB_ENOTFOUND;
}


#define P_ATCHK(x)      do { if (!(p = (x))) goto fail; } while (0)
#define P_AFTERL(x, l)  do {\
                          size_t len__ = (l);\
                          for (;; p++) {\
                            if (p == end - len__) goto fail;\
                            if (mem_equal(p, x, len__)) break;\
                          }\
                          p += len__;\
                        } while (0)
#define P_AFTER(s)      P_AFTERL(s, strlen(s))

const void *sb_get_multipart(sb_Stream *st, const char *name, size_t *len) {
  const char *boundary;
  size_t boundary_len;
  size_t name_len = strlen(name);
  const char *p = st->recv_buf.s;
  char *end = st->recv_buf.s + st->recv_buf.len;

  /* Get boundary string */
  P_ATCHK( find_header_value(p, "Content-Type") );
  P_AFTER( "boundary=" );
  boundary = p;
  P_AFTER( "\r\n" );
  boundary_len = p - boundary - 2;

next:
  /* Move to after first boundary, then to start of name */
  P_AFTERL( boundary, boundary_len );
  P_AFTER( "\r\n" );
  P_ATCHK( find_header_value(p, "Content-Disposition") );
  P_AFTER( "name=\"" );

  /* Does the name match what we were looking for? */
  if (mem_equal(p, name, name_len) && p[name_len] == '"') {
    const char *res;
    /* Move to start of data */
    P_AFTER( "\r\n\r\n" );
    res = p;
    /* Find boundary, set length and return result */
    P_AFTERL( boundary, boundary_len );
    *len = p - res - boundary_len - 4;
    return res;
  }

  /* Try the next part */
  goto next;

fail:
  *len = 0;
  return NULL;
}


time_t sb_stream_get_init_time(sb_Stream *st) {
  return st->init_time;
}


/*===========================================================================
 * Server
 *===========================================================================*/

sb_Server *sb_new_server(const sb_Options *opt) {
  sb_Server *srv;
  struct sockaddr_in in_addr;
  char *tmp_end;
  long port;
  int err, optval;

  /* Create server object */
  srv = malloc( sizeof(*srv) );
  if (!srv) goto fail;
  memset(srv, 0, sizeof(*srv));
  srv->sockfd = INVALID_SOCKET;
  srv->handler = opt->handler;
  srv->udata = opt->udata;
  srv->timeout = opt->timeout ? str_to_uint(opt->timeout) : 30000;
  srv->max_request_size = str_to_uint(opt->max_request_size);
  srv->max_lifetime = str_to_uint(opt->max_lifetime);

  memset(&in_addr, 0, sizeof(in_addr));
  in_addr.sin_family = AF_INET;
  if (opt->port == NULL) goto fail;
  port = strtol(opt->port, &tmp_end, 0);
  if (
    (tmp_end == opt->port) || (errno == ERANGE) ||
    !(port > 0 && port <= USHRT_MAX)
  ) {
    goto fail;
  }
  in_addr.sin_port = htons((uint16_t)port);
  if (opt->host != NULL) {
    if (inet_pton(in_addr.sin_family, opt->host, &in_addr.sin_addr.s_addr) != 1) {
      goto fail;
    }
  } else {
    in_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  }

  /* Init socket */
  srv->sockfd = socket(in_addr.sin_family, SOCK_STREAM, IPPROTO_TCP);
  if (srv->sockfd == INVALID_SOCKET) goto fail;
  set_socket_blocking(srv->sockfd, false);

  /* Set SO_REUSEADDR so that the socket can be immediately bound without
   * having to wait for any closed socket on the same port to timeout */
  optval = 1;
  setsockopt(srv->sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  /* Bind and listen */
  err = bind(srv->sockfd, (struct sockaddr*)&in_addr, sizeof(in_addr));
  if (err) goto fail;
  err = listen(srv->sockfd, 1023);
  if (err) goto fail;

  /* Create mutex */
  err = pthread_mutex_init(&srv->stream_mtx, NULL);
  if (err) goto fail;

  return srv;

fail:
  if (srv) sb_close_server(srv);
  return NULL;
}


void sb_close_server(sb_Server *srv) {
  sb_Stream *st;

  /* Destroy all streams */
  pthread_mutex_lock(&srv->stream_mtx);
  st = srv->streams;
  while (st) {
    st = st->next;
    sb_stream_destroy(st);
  }
  srv->streams = NULL;
  pthread_mutex_unlock(&srv->stream_mtx);

  /* Clean up */
  if (srv->sockfd != INVALID_SOCKET) {
    shutdown(srv->sockfd, SHUT_RDWR);
    close(srv->sockfd);
  }

  pthread_mutex_destroy(&srv->stream_mtx);

  free(srv);
}


static void* conn_thread(void* arg) {
  struct sb_Stream *st = (struct sb_Stream *)arg;
  sb_Event e;
  int err;

  /* Do `connect` event */
  memset(&e, 0, sizeof(e));
  e.type = SB_EV_CONNECT;
  err = sb_stream_emit(st, &e);
  if (err) goto fail;

  /* Receive data */
  err = sb_stream_recv(st);
  if (err) goto fail;

  while (st->state != STATE_CLOSING) {
    err = sb_stream_send(st);
    if (err) goto fail;
  }

fail:
  /* Unlinking stream from list */
  sb_stream_unlink(st);

  /* Destroy stream */
  sb_stream_destroy(st);

  pthread_exit(NULL);
}


int sb_poll_server(sb_Server *srv) {
  struct sb_Stream *st;
  sb_Socket sockfd = INVALID_SOCKET;
  pthread_t thr;
  int err;

  /* Get and store current time */
  srv->now = time(NULL);

  /* Accept connections */
  while ( (sockfd = accept(srv->sockfd, NULL, NULL)) != INVALID_SOCKET ) {
    /* Init new stream */
    st = sb_stream_new(srv, sockfd);
    if (!st) {
      err = SB_EOUTOFMEM;
      goto fail;
    }

    /* Link stream to list */
    sb_stream_link(st);

    /* Create processing thread */
    err = pthread_create(&thr, NULL, &conn_thread, st);
    if (err) goto fail;

    sockfd = INVALID_SOCKET;
  }

  return SB_RES_OK;

fail:
  if (st) {
    /* Unlinking stream from list */
    sb_stream_unlink(st);
  }

  if (sockfd != INVALID_SOCKET) {
    shutdown(sockfd, SHUT_RDWR);
    close(sockfd);
  }
  return err;
}

