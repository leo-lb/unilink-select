#ifndef UNILINK_H
#define UNILINK_H

#include <sys/queue.h>
#include <sys/select.h>
#include <sys/socket.h>

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

struct net_tcp_conn
{
  LIST_ENTRY(net_tcp_conn) entry;
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

  /*
    A list that contains every active TCP connections.
  */
  struct net_tcp_conns tcp_conns;

  /* A list that contains every registered event callback */
  struct net_callbacks callbacks;
};

void
net_fd_int_array_set(fd_set* set, int* nfds, int* array, size_t size);

enum
{
  NET_SET_NONBLOCK_OK,
  NET_SET_NONBLOCK_FCNTL_F_GETFD,
  NET_SET_NONBLOCK_FCNTL_F_SETFD,
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
#define NET_EVENT_CLOSED_NETWORK 0x2

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