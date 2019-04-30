#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#include <errno.h>

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

void
command_state_free_ping(void* state)
{
  struct command_state_ping* state_ping = state;

  free(state_ping->data);
  state_ping->data = NULL;
  state_ping->size = 0;
}

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

      switch (header.type) {
        case COMMAND_PING:
          if (received->tcp_conn->receive_buf.size >= header.size) {
            if (header.flags & COMMAND_HEADER_IS_REQUEST) {
              if (mem_grow_buf(&received->tcp_conn->send_buf,
                               NULL,
                               1               /* flags */
                                 + 2 * 2       /* type and version */
                                 + 4 * 2       /* tag and size */
                                 + header.size /* data size */
                               ) != MEM_GROW_BUF_OK) {
                goto close_fd;
              }

              unsigned char* sbuf = received->tcp_conn->send_buf.p;

              write_net_octet(&sbuf, 0);              /* flags */
              write_net_4_octets(&sbuf, header.tag);  /* tag */
              write_net_2_octets(&sbuf, header.type); /* type */
              write_net_2_octets(&sbuf, 0);           /* version */
              write_net_4_octets(&sbuf, header.size); /* size */

              /* ping data */
              memcpy(sbuf, buf, header.size);

              buf += header.size;

              if (mem_shrink_buf_head(
                    &received->tcp_conn->receive_buf,
                    (size_t)buf - (size_t)received->tcp_conn->receive_buf.p) !=
                  MEM_SHRINK_BUF_HEAD_OK) {
                goto close_fd;
              }
            } else {
              struct command_state* state;

              LIST_FOREACH(state, &received->tcp_conn->states, entry)
              {
                if (state->type != COMMAND_PING)
                  continue;

                struct command_state_ping* state_ping = state->state;

                if (state_ping->tag != header.tag &&
                    !(state_ping->progress &
                      COMMAND_STATE_PING_AWAITING_RESPONSE))
                  continue;

                if (state_ping->size != header.size ||
                    (memcmp(state_ping->data, buf, header.size) != 0)) {
                  state_ping->progress |= COMMAND_STATE_PING_INVALID_RESPONSE;
                } else {
                  state_ping->progress |= COMMAND_STATE_PING_VALID_RESPONSE;
                }

                buf += header.size;

                if (mem_shrink_buf_head(
                      &received->tcp_conn->receive_buf,
                      (size_t)buf -
                        (size_t)received->tcp_conn->receive_buf.p) !=
                    MEM_SHRINK_BUF_HEAD_OK) {
                  goto close_fd;
                }

                break;
              }
            }
          }
          break;
        case COMMAND_ANNOUNCE:
          if (received->tcp_conn->receive_buf.size >= header.size) {
            if (header.flags & COMMAND_HEADER_IS_REQUEST) {
              struct command_announce announce = { 0 };
              unsigned long remaining = header.size;

              if (remaining < 1 /* role */
                                + 2 /* port */)
                goto close_fd;

              announce.role = read_net_octet(&buf);
              announce.port = read_net_2_octets(&buf);

              remaining -= 3;

              for (size_t index = 0;
                   remaining > 0 && index < sizeof announce.more_addrs /
                                              sizeof *announce.more_addrs;
                   ++index) {
                if (remaining < 2 /* family and size */)
                  goto close_fd;

                unsigned short family_and_size = read_net_2_octets(&buf);
                remaining -= 2;

                unsigned char family = family_and_size >> 12;
                unsigned short size = family_and_size & ~(~0U << 12U);

#ifdef DEBUG
                printf("family: %hhd size: %hd\n", family, size);
#endif

                if (remaining < size) {
#ifdef DEBUG
                  printf("close_fd: remaining (%ld) < size\n", remaining);
#endif
                  goto close_fd;
                }

#ifdef DEBUG
                char host[NI_MAXHOST];
                char serv[NI_MAXSERV];
#endif

                switch (family) {
                  case FAMILY_IPV4:
                    /* port */
                    if (remaining < 2 ||
                        (size != 2 /* port */ + 4 /* ipv4 */)) {
#ifdef DEBUG
                      printf("close_fd: port\n");
#endif
                      goto close_fd;
                    }

                    struct sockaddr_in* sin =
                      (struct sockaddr_in*)&announce.more_addrs[index];

                    sin->sin_family = AF_INET;

                    sin->sin_port = read_net_2_octets(&buf);
                    remaining -= 2;

                    /* address */
                    if (remaining < 4) {
#ifdef DEBUG
                      printf("close_fd: address\n");
#endif
                      goto close_fd;
                    }

                    memcpy(&sin->sin_addr, buf, 4);
                    buf += 4;

                    remaining -= 4;

#ifdef DEBUG
                    int err;
                    if ((err = getnameinfo((struct sockaddr*)sin,
                                           sizeof *sin,
                                           host,
                                           sizeof host,
                                           serv,
                                           sizeof serv,
                                           NI_NUMERICHOST | NI_NUMERICSERV)) ==
                        0)
                      printf(
                        "decoded address: %s - decoded port: %s\n", host, serv);
                    else
                      printf("%s %hd\n",
                             gai_strerror(err),
                             ((struct sockaddr*)sin)->sa_family);
#endif
                    break;
                  case FAMILY_IPV6:
                    /* port */
                    if (remaining < 2 || (size != 2 /* port */ + 16 /* ipv6 */))
                      goto close_fd;

                    struct sockaddr_in6* sin6 =
                      (struct sockaddr_in6*)&announce.more_addrs[index];

                    sin6->sin6_family = AF_INET6;

                    sin6->sin6_port = read_net_2_octets(&buf);
                    remaining -= 2;

                    /* address */
                    if (remaining < 16)
                      goto close_fd;

                    memcpy(&sin6->sin6_addr, buf, 16);
                    buf += 16;

                    remaining -= 16;

#ifdef DEBUG
                    if (getnameinfo((struct sockaddr*)sin6,
                                    sizeof *sin6,
                                    host,
                                    sizeof host,
                                    serv,
                                    sizeof serv,
                                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
                      printf(
                        "decoded address: %s - decoded port: %s\n", host, serv);
#endif

                    break;
                  default:
                    remaining -= size;
                    buf += size;
                }
              }

              /* TODO: Decide what to do with peer addresses */
            }
          }

          if (mem_shrink_buf_head(
                &received->tcp_conn->receive_buf,
                (size_t)buf - (size_t)received->tcp_conn->receive_buf.p) !=
              MEM_SHRINK_BUF_HEAD_OK) {
            goto close_fd;
          }
          break;
      }
    }

    /* don't go into this code path unless goto is used */
    if (0) {
      /* This will cause the networking loop to discard the fd and all resources
       * associated with it */

    close_fd:
      shutdown(received->tcp_conn->fd, SHUT_RDWR);
      close(received->tcp_conn->fd);
      return 0;
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
