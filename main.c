#include <stdio.h>
#include <stdlib.h>

#include <sys/queue.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <error.h>
#include <fcntl.h>
#include <unistd.h>

#include "unilink.h"

void
net_fd_int_array_set(fd_set* set, int* nfds, int* array, size_t size)
{
  for (size_t i = 0; i < size / *array; ++i) {
    int fd = array[i];

    if (fd >= 0) {
      FD_SET(fd, set);

      if (fd >= *nfds) {
        *nfds = fd + 1;
      }
    }
  }
}

int
net_set_nonblock(int fd)
{
  int s = fcntl(fd, F_GETFD);
  if (s < 0) {
    return E(NET_SET_NONBLOCK_FCNTL_F_GETFD);
  }

  s = s | O_NONBLOCK;

  s = fcntl(fd, F_SETFD, s);
  if (s < 0) {
    return E(NET_SET_NONBLOCK_FCNTL_F_SETFD);
  }

  return NET_SET_NONBLOCK_OK;
}

int
net_loop(struct net_context* ctx)
{
  FD_ZERO(&ctx->readfds);
  FD_ZERO(&ctx->writefds);

  net_fd_int_array_set(
    &ctx->readfds, &ctx->nfds, ctx->tcp_boundfds, sizeof ctx->tcp_boundfds);
  net_fd_int_array_set(
    &ctx->writefds, &ctx->nfds, ctx->tcp_boundfds, sizeof ctx->tcp_boundfds);
  net_fd_int_array_set(
    &ctx->readfds, &ctx->nfds, ctx->udp_boundfds, sizeof ctx->udp_boundfds);
  net_fd_int_array_set(
    &ctx->writefds, &ctx->nfds, ctx->udp_boundfds, sizeof ctx->udp_boundfds);

  do {
    fd_set readfds_copy = ctx->readfds;
    fd_set writefds_copy = ctx->writefds;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

    int select_ret =
      select(ctx->nfds, &ctx->readfds, &ctx->writefds, NULL, &tv);

    if (select_ret > 0) /* success */ {

      for (size_t i = 0;
           i < sizeof ctx->tcp_boundfds / sizeof *ctx->tcp_boundfds;
           ++i) {
        int fd = ctx->tcp_boundfds[i];

        if (fd >= 0) {

          /* checking if we can call accept(2) on any bound TCP sockets */
          if (FD_ISSET(fd, &readfds_copy)) /* ready to accept(2) */ {
            struct net_tcp_conn* tcp_conn = calloc(1, sizeof *tcp_conn);
            if (tcp_conn != NULL) {
              tcp_conn->sa_len = sizeof tcp_conn->sa;

              int accept_ret =
                accept(fd, (struct sockaddr*)&tcp_conn->sa, &tcp_conn->sa_len);
              if (accept_ret >= 0) /* success */ {
                int conn_fd = accept_ret;

                if (net_set_nonblock(conn_fd) != NET_SET_NONBLOCK_OK) {
                  goto close_conn_fd;
                }

                FD_SET(conn_fd, &ctx->readfds);
                FD_SET(conn_fd, &ctx->writefds);

                tcp_conn->fd = conn_fd;

                LIST_INSERT_HEAD(&ctx->tcp_conns, tcp_conn, entry);

                struct net_callback* callback_entry;
                LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
                {
                  if (callback_entry->events & NET_EVENT_ESTABLISHED) {
                    struct net_event_data_established event_data;

                    event_data.flags = NET_EVENT_ESTABLISHED_ACCEPT;
                    event_data.tcp_conn = tcp_conn;

                    callback_entry->cb(
                      NET_EVENT_ESTABLISHED, &event_data, &callback_entry->p);
                  }
                }

              close_conn_fd:
                close(conn_fd);
                goto free_tcp_conn;
              }

            free_tcp_conn:
              free(tcp_conn);
            }
          }
        }
      }

      struct net_tcp_conn* tcp_conn_entry;
      LIST_FOREACH(tcp_conn_entry, &ctx->tcp_conns, entry)
      {
        int fd = tcp_conn_entry->fd;

        /* checking for results of connect(2) on any bound TCP sockets */
        if (FD_ISSET(fd, &writefds_copy)) /* connect(2) returned */ {
        }
      }

    } else if (select_ret == 0) /* timeout */ {

    } else /* error */ {
    }

  } while (1);

  return NET_LOOP_OK;
}

int
main(void)
{}