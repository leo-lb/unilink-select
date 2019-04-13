CFLAGS += -Wall -Werror -Wextra
LDFLAGS += 

NAME = unilink-select

SRCS = main.c mem.c net.c
OBJS = ${SRCS:.c=.o}

$(NAME): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -o $(NAME)

all: $(NAME)

clean:
	$(RM) $(OBJS)

fclean: clean
	$(RM) $(NAME)

.PHONY: all clean fclean