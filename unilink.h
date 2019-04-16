#ifndef UNILINK_H
#define UNILINK_H

#include <sys/select.h>
#include <sys/socket.h>

#include <unistd.h>

#include "queue.h"

#define RECV_SIZE sysconf(_SC_PAGESIZE)

#define E(x) (-(x))

typedef int
net_cb_fn(int event, void* event_data, void** p);

struct net_callback
{
  LIST_ENTRY(net_callback) entry;
  int events;
  void* p;
  net_cb_fn* cb;
};

LIST_HEAD(net_callbacks, net_callback);

struct mem_buf
{
  void* p;
  size_t size;
};

void
mem_free_buf(struct mem_buf* m);

enum
{
  MEM_GROW_BUF_OK,
  MEM_GROW_BUF_ALLOC,
  MEM_GROW_BUF_OVERFLOW,
} mem_grow_buf_errors;

int
mem_grow_buf(struct mem_buf* m, void* p, size_t size);

enum
{
  MEM_SHRINK_BUF_HEAD_OK,
  MEM_SHRINK_BUF_HEAD_IS_SMALLER,
  MEM_SHRINK_BUF_HEAD_UNDERFLOW,
  MEM_SHRINK_BUF_HEAD_ALLOC
} mem_shrink_buf_head_errors;

int
mem_shrink_buf_head(struct mem_buf* m, size_t size);

enum
{
  MEM_SHRINK_BUF_OK,
  MEM_SHRINK_BUF_IS_SMALLER,
  MEM_SHRINK_BUF_UNDERFLOW,
  MEM_SHRINK_BUF_ALLOC,
} mem_shrink_buf_errors;

int
mem_shrink_buf(struct mem_buf* m, size_t size);

#define NET_TCP_CONN_CONNECTED 0x1

struct net_tcp_conn
{
  LIST_ENTRY(net_tcp_conn) entry;
  int flags;
  int fd;
  struct sockaddr_storage sa;
  socklen_t sa_len;
  struct mem_buf send_buf;
  struct mem_buf receive_buf;
};

LIST_HEAD(net_tcp_conns, net_tcp_conn);

struct net_context
{
  /*
    Store bound non-blocking TCP sockets in this array.
    Unused elements must be negative.
  */
  int tcp_boundfds[4];

  /*
    Store bound non-blocking UDP sockets in this array.
    Unused elements must be negative.
  */
  int udp_boundfds[4];

  /*
    We use readfds to:
      - Perform non-blocking accept(2).
      - Perform non-blocking recv(2), filling local read buffers.
  */
  fd_set readfds;

  /*
    We use writefds to:
      - Perform non-blocking connect(2).
      - Perform non-blocking send(2), emptying local write buffers.
  */
  fd_set writefds;

  int nfds;

  /* A list that contains every active TCP connections. */
  struct net_tcp_conns tcp_conns;

  /* A list that contains every registered event callback */
  struct net_callbacks callbacks;
};

void
net_fd_int_array_set(fd_set* set, int* nfds, int* array, size_t size);

enum
{
  NET_SET_NONBLOCK_OK,
  NET_SET_NONBLOCK_FCNTL_F_GETFL,
  NET_SET_NONBLOCK_FCNTL_F_SETFL,
} net_set_nonblock_errors;

int
net_set_nonblock(int fd);

#define NET_EVENT_ESTABLISHED 0x1
#define NET_EVENT_CLOSED 0x2
#define NET_EVENT_SENT 0x4
#define NET_EVENT_RECEIVED 0x8

#define NET_EVENT_ESTABLISHED_ACCEPT 0x1
#define NET_EVENT_ESTABLISHED_CONNECT 0x2

struct net_event_data_established
{
  int flags;
  struct net_tcp_conn* tcp_conn;
};

#define NET_EVENT_CLOSED_INTERNAL 0x1
#define NET_EVENT_CLOSED_SEND 0x2
#define NET_EVENT_CLOSED_RECV 0x4
#define NET_EVENT_CLOSED_CONNECT 0x8

struct net_event_data_closed
{
  int flags;
  struct net_tcp_conn* tcp_conn;
};

struct net_event_data_sent
{
  int flags;
  size_t count;
  struct net_tcp_conn* tcp_conn;
};

struct net_event_data_received
{
  int flags;
  size_t count;
  struct net_tcp_conn* tcp_conn;
};

enum
{
  NET_LOOP_OK,
} net_loop_errors;

int
net_loop(struct net_context* ctx);

#endif