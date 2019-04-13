#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "queue.h"
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
  int s = fcntl(fd, F_GETFL);
  if (s == -1) {
    return E(NET_SET_NONBLOCK_FCNTL_F_GETFL);
  }

  s = s | O_NONBLOCK;

  s = fcntl(fd, F_SETFL, s);
  if (s == -1) {
    return E(NET_SET_NONBLOCK_FCNTL_F_SETFL);
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
    struct net_tcp_conn* tcp_conn_1;
    LIST_FOREACH(tcp_conn_1, &ctx->tcp_conns, entry)
    {
      if (tcp_conn_1->send_buf.size > 0) {
        FD_SET(tcp_conn_1->fd, &ctx->writefds);
      } else {
        FD_CLR(tcp_conn_1->fd, &ctx->writefds);
      }
    }

    for (int fd = 0; fd < FD_SETSIZE; ++fd) {
      if ((FD_ISSET(fd, &ctx->writefds) || FD_ISSET(fd, &ctx->readfds))) {
        ctx->nfds = fd + 1;
      }
    }

    fd_set readfds_copy = ctx->readfds;
    fd_set writefds_copy = ctx->writefds;

    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

    int select_ret =
      select(ctx->nfds, &readfds_copy, &writefds_copy, NULL, &tv);

    if (select_ret > 0) /* success */ {

      for (size_t i = 0;
           i < sizeof ctx->tcp_boundfds / sizeof *ctx->tcp_boundfds;
           ++i) {
        int fd = ctx->tcp_boundfds[i];

        if (fd >= 0) {

          /* checking if we can call accept(2) on any bound TCP sockets */
          if (FD_ISSET(fd, &readfds_copy)) /* ready to accept(2) */ {
            do {
              struct net_tcp_conn* tcp_conn = calloc(1, sizeof *tcp_conn);
              if (tcp_conn != NULL) {
                tcp_conn->sa_len = sizeof tcp_conn->sa;

                int accept_ret = accept(
                  fd, (struct sockaddr*)&tcp_conn->sa, &tcp_conn->sa_len);
                if (accept_ret != -1) /* success */ {
                  int conn_fd = accept_ret;

                  if (net_set_nonblock(conn_fd) != NET_SET_NONBLOCK_OK) {
                    close(conn_fd);
                    goto free_tcp_conn;
                  }

                  FD_SET(conn_fd, &ctx->readfds);
                  if (conn_fd >= ctx->nfds) {
                    ctx->nfds = conn_fd + 1;
                  }

                  tcp_conn->flags = NET_TCP_CONN_CONNECTED;
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
                } else {
                free_tcp_conn:
                  free(tcp_conn);

                  /* accept(2) until it returns an error saying it will block */
                  if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                  }
                }
              } else {
                /* stop trying to accept(2) if we're out of memory */
                break;
              }
            } while (1);
          }
        }
      }

      struct net_tcp_conn* tcp_conn_entry;
      LIST_FOREACH(tcp_conn_entry, &ctx->tcp_conns, entry)
      {
        int fd = tcp_conn_entry->fd;

        if (FD_ISSET(fd, &readfds_copy)) {
          if (tcp_conn_entry->flags & NET_TCP_CONN_CONNECTED) {
            do {
              struct net_event_data_closed event_data;

              int grow_ret =
                mem_grow_buf(&tcp_conn_entry->receive_buf, NULL, RECV_SIZE);

              if (grow_ret != MEM_GROW_BUF_OK) {

                /* the buffer is potentially corrupted, close the connection
                 */
                event_data.flags =
                  NET_EVENT_CLOSED_INTERNAL | NET_EVENT_CLOSED_RECV;
                goto close_fd_recv;
              }

              ssize_t recv_ret =
                recv(fd,
                     tcp_conn_entry->receive_buf.p +
                       tcp_conn_entry->receive_buf.size - RECV_SIZE,
                     RECV_SIZE,
                     0);

              if (recv_ret != -1 && recv_ret != 0) {
                int shrink_ret = mem_shrink_buf(&tcp_conn_entry->receive_buf,
                                                RECV_SIZE - (size_t)recv_ret);
                if (shrink_ret != MEM_SHRINK_BUF_HEAD_OK) {

                  /* the buffer is potentially corrupted, close the connection
                   */
                  event_data.flags =
                    NET_EVENT_CLOSED_INTERNAL | NET_EVENT_CLOSED_RECV;
                  goto close_fd_recv;
                }

                struct net_callback* callback_entry;
                LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
                {
                  if (callback_entry->events & NET_EVENT_RECEIVED) {
                    struct net_event_data_received event_data_received;

                    event_data_received.flags = 0;
                    event_data_received.count = (size_t)recv_ret;
                    event_data_received.tcp_conn = tcp_conn_entry;

                    callback_entry->cb(NET_EVENT_RECEIVED,
                                       &event_data_received,
                                       &callback_entry->p);
                  }
                }
              } else if (recv_ret == 0) {
                /* socket was shutdown, close it */
                event_data.flags = NET_EVENT_CLOSED_RECV;
                goto close_fd_recv;
              } else {

                /* recv(2) until it returns that it would block */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  break;
                }

                /* another kind of error, close the connection */
                event_data.flags = NET_EVENT_CLOSED_RECV;
                goto close_fd_recv;
              }

              continue;
              /* control flow must only go below if goto is used */

            close_fd_recv:

              FD_CLR(fd, &ctx->readfds);
              FD_CLR(fd, &ctx->writefds);

              close(fd);

              struct net_callback* callback_entry;
              LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
              {
                if (callback_entry->events & NET_EVENT_CLOSED) {

                  event_data.tcp_conn = tcp_conn_entry;

                  callback_entry->cb(
                    NET_EVENT_CLOSED, &event_data, &callback_entry->p);
                }
              }

              goto remove_list_entry;
            } while (1);
          }
        }

        if (FD_ISSET(fd, &writefds_copy)) {
          if (tcp_conn_entry->flags & NET_TCP_CONN_CONNECTED) {
            while (tcp_conn_entry->send_buf.size >
                   0) /* send(2) until buffer is empty */ {
              struct net_event_data_closed event_data;

              ssize_t send_ret = send(fd,
                                      tcp_conn_entry->send_buf.p,
                                      tcp_conn_entry->send_buf.size,
                                      0);

              if (send_ret != -1) {
                int shrink_ret = mem_shrink_buf_head(&tcp_conn_entry->send_buf,
                                                     (size_t)send_ret);
                if (shrink_ret != MEM_SHRINK_BUF_HEAD_OK) {

                  /* the buffer is potentially corrupted, close the connection
                   */
                  event_data.flags =
                    NET_EVENT_CLOSED_INTERNAL | NET_EVENT_CLOSED_SEND;
                  goto close_fd_send;
                }

                struct net_callback* callback_entry;
                LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
                {
                  if (callback_entry->events & NET_EVENT_SENT) {
                    struct net_event_data_sent event_data_sent;

                    event_data_sent.flags = 0;
                    event_data_sent.count = (size_t)send_ret;
                    event_data_sent.tcp_conn = tcp_conn_entry;

                    callback_entry->cb(
                      NET_EVENT_SENT, &event_data_sent, &callback_entry->p);
                  }
                }

              } else {

                /* send(2) until it returns that it would block */
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                  break;
                }

                /* another kind of error, close the connection */
                event_data.flags = NET_EVENT_CLOSED_SEND;
                goto close_fd_send;
              }

              continue;
              /* control flow must only go below if goto is used */

            close_fd_send:

              FD_CLR(fd, &ctx->readfds);
              FD_CLR(fd, &ctx->writefds);

              close(fd);

              struct net_callback* callback_entry;
              LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
              {
                if (callback_entry->events & NET_EVENT_CLOSED) {
                  event_data.tcp_conn = tcp_conn_entry;

                  callback_entry->cb(
                    NET_EVENT_CLOSED, &event_data, &callback_entry->p);
                }
              }

              goto remove_list_entry;
            }
          } else /* not yet connected. check result of connect(2) */ {

            /*
              Windows compatibility note:
                The writability indicates success of connect(2).
                So we must handle this differently, either timeout
                or find another way to determine if connect(2) has
                failed.
            */

            int connect_ret;
            socklen_t optlen = sizeof connect_ret;

            int getsockopt_ret =
              getsockopt(fd, SOL_SOCKET, SO_ERROR, &connect_ret, &optlen);
            if (getsockopt_ret != -1) {
              if (connect_ret == 0) {
                tcp_conn_entry->flags |= NET_TCP_CONN_CONNECTED;

                struct net_callback* callback_entry;
                LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
                {
                  if (callback_entry->events & NET_EVENT_ESTABLISHED) {
                    struct net_event_data_established event_data;

                    event_data.flags = NET_EVENT_ESTABLISHED_CONNECT;
                    event_data.tcp_conn = tcp_conn_entry;

                    callback_entry->cb(
                      NET_EVENT_ESTABLISHED, &event_data, &callback_entry->p);
                  }
                }
              } else {
                /* connect(2) failed, close the fd */

                FD_CLR(fd, &ctx->readfds);
                FD_CLR(fd, &ctx->writefds);

                close(fd);

                struct net_callback* callback_entry;
                LIST_FOREACH(callback_entry, &ctx->callbacks, entry)
                {
                  if (callback_entry->events & NET_EVENT_CLOSED) {
                    struct net_event_data_closed event_data;

                    event_data.flags = NET_EVENT_CLOSED_CONNECT;
                    event_data.tcp_conn = tcp_conn_entry;

                    callback_entry->cb(
                      NET_EVENT_CLOSED, &event_data, &callback_entry->p);
                  }
                }

                goto remove_list_entry;
              }
            }
          }
        }

        continue;
        /* control flow must only go below if goto is used */

      remove_list_entry:
        LIST_REMOVE(tcp_conn_entry, entry);

        /*
          HACK:
            Create a fake temporary element with LIST_NEXT set to
            the one of the element we are removing so that
            LIST_FOREACH does not crash.
        */
        struct net_tcp_conn temp_entry;
        LIST_NEXT(&temp_entry, entry) = LIST_NEXT(tcp_conn_entry, entry);

        mem_free_buf(&tcp_conn_entry->receive_buf);
        mem_free_buf(&tcp_conn_entry->send_buf);
        free(tcp_conn_entry);

        tcp_conn_entry = &temp_entry;
      }
    } else if (select_ret == 0) /* timeout */ {
    } else /* error */ {
    }
  } while (1);

  return NET_LOOP_OK;
}