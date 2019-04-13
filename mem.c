#include <stdlib.h>
#include <string.h>

/* DEBUG */
#include <stdio.h>
/* */

#include "unilink.h"

void
mem_free_buf(struct mem_buf* m)
{
  if (m) {
    free(m->p);
    m->p = NULL;
    m->size = 0;
  }
}

int
mem_grow_buf(struct mem_buf* m, void* p, size_t size)
{
  size_t new_size = m->size + size;

  if (new_size < m->size) {
    return E(MEM_GROW_BUF_OVERFLOW);
  }

  void* new_p = realloc(m->p, new_size);
  if (new_p == NULL && new_size > 0) {
    return E(MEM_GROW_BUF_ALLOC);
  }

  if (p)
    memcpy(new_p + m->size, p, size);

  m->p = new_p;
  m->size = new_size;

  return MEM_GROW_BUF_OK;
}

int
mem_shrink_buf_head(struct mem_buf* m, size_t size)
{
  if (m->size < size) {
    return E(MEM_SHRINK_BUF_HEAD_IS_SMALLER);
  }

  size_t new_size = m->size - size;

  if (new_size > m->size) {
    return E(MEM_SHRINK_BUF_HEAD_UNDERFLOW);
  }

  memmove(m->p, m->p + size, new_size);

  void* new_p = realloc(m->p, new_size);
  if (new_p == NULL && new_size > 0) {
    return E(MEM_SHRINK_BUF_HEAD_ALLOC);
  }

  m->p = new_p;
  m->size = new_size;

  return MEM_SHRINK_BUF_HEAD_OK;
}

int
mem_shrink_buf(struct mem_buf* m, size_t size)
{
  printf(
    "\n\n%s(%p (p: %p, size: %ld), %ld)\n\n", __func__, m, m->p, m->size, size);

  if (size > m->size) {
    return E(MEM_SHRINK_BUF_IS_SMALLER);
  }

  size_t new_size = m->size - size;

  if (new_size > m->size) {
    return E(MEM_SHRINK_BUF_UNDERFLOW);
  }

  void* new_p = realloc(m->p, new_size);
  if (new_p == NULL && new_size > 0) {
    return E(MEM_SHRINK_BUF_ALLOC);
  }

  m->p = new_p;
  m->size = new_size;

  return MEM_SHRINK_BUF_OK;
}