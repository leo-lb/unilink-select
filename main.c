#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#ifdef DEBUG
#include <stdio.h>
#endif

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"
#include "unilink.h"

#ifdef DEBUG
int
net_cb_fn_test(int event, void* event_data, void** p)
{
  (void)event_data;
  (void)p;

  printf("event: ");

  switch (event) {
    case NET_EVENT_ESTABLISHED:
      printf("NET_EVENT_ESTABLISHED - fd: %d",
             ((struct net_event_data_established*)event_data)->tcp_conn->fd);
      break;
    case NET_EVENT_CLOSED:
      printf("NET_EVENT_CLOSED - fd: %d",
             ((struct net_event_data_closed*)event_data)->tcp_conn->fd);
      break;
    case NET_EVENT_SENT:
      printf("NET_EVENT_SENT - fd: %d count: %ld",
             ((struct net_event_data_sent*)event_data)->tcp_conn->fd,
             ((struct net_event_data_sent*)event_data)->count);
      break;
    case NET_EVENT_RECEIVED:
      printf("NET_EVENT_RECEIVED - fd: %d count: %ld size: %ld size_int: %d "
             "data: \"%.*s\"",
             ((struct net_event_data_received*)event_data)->tcp_conn->fd,
             ((struct net_event_data_received*)event_data)->count,
             ((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (int)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (int)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.size,
             (const char*)((struct net_event_data_received*)event_data)
               ->tcp_conn->receive_buf.p);

      /* mem_free_buf(
        &((struct net_event_data_received*)event_data)->tcp_conn->receive_buf);
      */

      /* char* to_send = "Hello\n";
      mem_grow_buf(
        &((struct net_event_data_received*)event_data)->tcp_conn->send_buf,
        to_send,
        strlen(to_send)); */
      break;
  }

  printf("\n");
  return 0;
}
#endif

#if CHAR_BIT == 8
inline unsigned char
read_net_octet(unsigned char** p)
{
  /* Read 1 octet. TCP/UDP is octet oriented so we are
   * guaranteed to have 8 bits per unsigned char and an unsigned char is
   * guaranteed by the C standard to be able to hold at least 2^8-1. */
  unsigned char v = (*p)[0];

  *p += 1;

  return v;
}

inline unsigned short
read_net_2_octets(unsigned char** p)
{
  unsigned short v;

  /* Read 2 octets in network byte order. TCP/UDP is octet oriented so we are
   * guaranteed to have 8 bits per unsigned char and an unsigned short is
   * guaranteed by the C standard to be able to hold at least 2^16-1. */
  v = ((*p)[1] << 0) | ((*p)[0] << 8);

  *p += 2;

  return v;
}

inline unsigned long
read_net_4_octets(unsigned char** p)
{
  unsigned long v;

  /* Read 4 octets in network byte order. TCP/UDP is octet oriented so we are
   * guaranteed to have 8 bits per unsigned char and an unsigned long is
   * guaranteed by the C standard to be able to hold at least 2^32-1. */
  v = ((*p)[3] << 0) | ((*p)[2] << 8) | ((*p)[1] << 16) | ((*p)[0] << 24);

  *p += 4;

  return v;
}

inline void
write_net_octet(unsigned char** p, unsigned char v)
{
  (*p)[0] = v;

  *p += 1;
}

inline void
write_net_2_octets(unsigned char** p, unsigned short v)
{
  (*p)[1] = (v >> (0 * 8)) & UCHAR_MAX;
  (*p)[0] = (v >> (1 * 8)) & UCHAR_MAX;

  *p += 2;
}

inline void
write_net_4_octets(unsigned char** p, unsigned long v)
{
  (*p)[3] = (v >> (0 * 8)) & UCHAR_MAX;
  (*p)[2] = (v >> (1 * 8)) & UCHAR_MAX;
  (*p)[1] = (v >> (2 * 8)) & UCHAR_MAX;
  (*p)[0] = (v >> (3 * 8)) & UCHAR_MAX;

  *p += 4;
}

#else
#error "Implement for CHAR_BIT != 8"
#endif

int
net_cb_command_received(int event, void* event_data, void** p)
{
  (void)p;

  if (event == NET_EVENT_RECEIVED) {
    struct net_event_data_received* received = event_data;

    if (received->tcp_conn->receive_buf.size >= (1       /* flags */
                                                 + 2 * 2 /* type and version */
                                                 + 4 * 2 /* tag and size */
                                                 )) {
      struct command_header header;
      unsigned char* buf = received->tcp_conn->receive_buf.p;

      header.flags = read_net_octet(&buf);
      header.tag = read_net_4_octets(&buf);
      header.type = read_net_2_octets(&buf);
      header.version = read_net_2_octets(&buf);
      header.size = read_net_4_octets(&buf);

      mem_shrink_buf_head(&received->tcp_conn->receive_buf,
                          (size_t)buf -
                            (size_t)received->tcp_conn->receive_buf.p);

      buf = received->tcp_conn->receive_buf.p;

#ifdef DEBUG
      printf("command_header {\n\tflags: 0x%hhx\n\ttag: 0x%lx\n\ttype: "
             "0x%hx\n\tversion: 0x%hx\n"
             "\tsize: 0x%lx\n}\n",
             header.flags,
             header.tag,
             header.type,
             header.version,
             header.size);
#endif

      if (header.flags & COMMAND_HEADER_IS_REQUEST) {
        switch (header.type) {
          case COMMAND_PING:
            if (received->tcp_conn->receive_buf.size >= header.size) {
              if (mem_grow_buf(&received->tcp_conn->send_buf,
                               NULL,
                               1               /* flags */
                                 + 2 * 2       /* type and version */
                                 + 4 * 2       /* tag and size */
                                 + header.size /* data size */
                               ) == MEM_GROW_BUF_OK) {
                unsigned char* sbuf = received->tcp_conn->send_buf.p;

                write_net_octet(&sbuf, 0);              /* flags */
                write_net_4_octets(&sbuf, header.tag);  /* tag */
                write_net_2_octets(&sbuf, header.type); /* type */
                write_net_2_octets(&sbuf, 0);           /* version */
                write_net_4_octets(&sbuf, header.size); /* size */

                memcpy(sbuf, buf, header.size);

                buf += header.size;

                mem_shrink_buf_head(
                  &received->tcp_conn->receive_buf,
                  (size_t)buf - (size_t)received->tcp_conn->receive_buf.p);
              } else {
                shutdown(received->tcp_conn->fd, SHUT_RDWR);
                close(received->tcp_conn->fd);
              }
            }
            break;
        }
      } else {
      }
    }
  }

  return 0;
}

int
main(int argc, char* argv[])
{
  unsigned short port = 0;

  if (argc > 1) {
    port = (unsigned short)atoi(argv[1]);
  }

  int socket_ret = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_ret == -1) {
#ifdef DEBUG
    perror("socket");
#endif
    return EXIT_FAILURE;
  }

  int tcp_fd = socket_ret;

  struct sockaddr_in sa;
  memset(&sa, 0, sizeof sa);

  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  socklen_t salen = sizeof sa;

  int bind_ret = bind(tcp_fd, (struct sockaddr*)&sa, salen);
  if (bind_ret == -1) {
#ifdef DEBUG
    perror("bind");
#endif
    close(tcp_fd);
    return EXIT_FAILURE;
  }

  int listen_ret = listen(tcp_fd, 256);
  if (listen_ret == -1) {
#ifdef DEBUG
    perror("listen");
#endif
    close(tcp_fd);
    return EXIT_FAILURE;
  }

  struct net_context ctx;
  memset(&ctx, 0, sizeof ctx);

  ctx.tcp_boundfds[0] = tcp_fd;
  ctx.tcp_boundfds[1] = -1;
  ctx.tcp_boundfds[2] = -1;
  ctx.tcp_boundfds[3] = -1;

  ctx.udp_boundfds[0] = -1;
  ctx.udp_boundfds[1] = -1;
  ctx.udp_boundfds[2] = -1;
  ctx.udp_boundfds[3] = -1;

  struct sockaddr_in sa2;
  memset(&sa2, 0, sizeof sa2);

  socklen_t sa2len = sizeof sa2;

  int getsockname_ret = getsockname(tcp_fd, (struct sockaddr*)&sa2, &sa2len);
  if (getsockname_ret == -1) {
#ifdef DEBUG
    perror("getsockname");
#endif
    close(tcp_fd);
    return EXIT_FAILURE;
  }

#ifdef DEBUG
  printf("listening on port %hu\n", ntohs(sa2.sin_port));
#endif

#ifdef DEBUG
  struct net_callback* net_cb = calloc(1, sizeof *net_cb);
  if (net_cb == NULL) {
    perror("calloc");
    close(tcp_fd);
    return EXIT_FAILURE;
  }

  net_cb->events = ~0;
  net_cb->cb = net_cb_fn_test;

  LIST_INSERT_HEAD(&ctx.callbacks, net_cb, entry);
#endif

  struct net_callback* net_cb_received = calloc(1, sizeof *net_cb_received);
  if (net_cb_received == NULL) {
#ifdef DEBUG
    perror("calloc");
#endif
    close(tcp_fd);
    return EXIT_FAILURE;
  }

  net_cb_received->events = NET_EVENT_RECEIVED;
  net_cb_received->cb = net_cb_command_received;

  LIST_INSERT_HEAD(&ctx.callbacks, net_cb_received, entry);

  int nonblock_ret = net_set_nonblock(tcp_fd);
  if (nonblock_ret != NET_SET_NONBLOCK_OK) {
    close(tcp_fd);
    return EXIT_FAILURE;
  }

  net_loop(&ctx);

#ifdef DEBUG
  free(net_cb);
#endif

  return EXIT_SUCCESS;
}