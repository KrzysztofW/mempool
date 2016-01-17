#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_
#include <string.h>
#include <assert.h>
#include "atomic.h"

#define MEM_POOL_BUF_SIZE 8196
#define MEM_POOL_MAX_BUCKETS 16
#define MEM_POOL_MAX_FDS 16
#define MEM_POOL_MAX_NAME 100
#define POWEROF2(x) ((((x) - 1) & (x)) == 0)
#define __cache_line_size 64
#define __cache_line_mask (__cache_line_size - 1)
#define __cache_aligned  __attribute__((aligned(__cache_line_size)))

struct mp_buf {
	uint32_t len;
#ifndef NDEBUG
	int owner;
#endif
	struct mp_buf_t *next;
	char data[MEM_POOL_BUF_SIZE];
} __cache_aligned;

typedef struct mp_buf mp_buf_t;

typedef struct mp_buf_priv_t {
	int       offset;
	mp_buf_t *buf;
} mp_buf_priv_t;

struct mempool {
	uint32_t size;
	uint32_t entries;
	atomic_t refcnt;
	char     name[MEM_POOL_MAX_NAME];
	char     sun_path[MEM_POOL_MAX_NAME];
	uint32_t buckets;
} __cache_aligned;
typedef struct mempool mempool_t;

struct mp_ring {
	volatile uint32_t	prod_head;
	volatile uint32_t	prod_tail;
	volatile uint32_t	cons_head __cache_aligned;
	volatile uint32_t	cons_tail;
	int			offset[] __cache_aligned;
};
typedef struct mp_ring mp_ring_t;

typedef struct mempool_priv_t {
	mempool_t *mp;
	mp_ring_t *bucket[MEM_POOL_MAX_BUCKETS];
	int        fds[MEM_POOL_MAX_FDS];
	mp_buf_t  *data;
	int        entries;
	int        mask;
} mempool_priv_t;

int mp_create(mempool_priv_t *mp_priv, const char *name, unsigned int entries,
	      unsigned int buckets, unsigned notifications);
int mp_unregister(mempool_priv_t *mp_priv);
int mp_register(mempool_priv_t *mp_priv, const char *name);

static inline int
mp_get(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	int offset;
	mp_ring_t *ring = mp_priv->bucket[bucket];
	uint32_t cons_head, cons_next;

	assert(bucket < mp_priv->mp->buckets);
	do {
		cons_head = ring->cons_head;
		cons_next = (cons_head + 1) & mp_priv->mask;

		if (cons_head == ring->prod_tail) {
			return -1;
		}
	} while (!atomic_cmpset_int(&ring->cons_head, cons_head, cons_next));
	offset = ring->offset[cons_head];
	buf->offset = offset;
	buf->buf = &mp_priv->data[offset];

	assert(buf->buf->owner == bucket);
#ifndef NDEBUG
	buf->buf->owner = -1;
#endif
	atomic_store(&ring->cons_tail, cons_next);

	return 0;
}

static inline int
mp_put(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	mp_ring_t *ring = mp_priv->bucket[bucket];
	uint32_t prod_head, prod_next, cons_tail;

	assert(bucket < mp_priv->mp->buckets);
	do {
		prod_head = ring->prod_head;
		prod_next = (prod_head + 1) & mp_priv->mask;
		cons_tail = ring->cons_tail;

		if ((mp_priv->mask + cons_tail - prod_head) == 0) {
			return -1;
		}
	} while (!atomic_cmpset_int(&ring->prod_head, prod_head, prod_next));
	ring->offset[prod_head] = buf->offset;

	assert(buf->buf->owner == -1);
#ifndef NDEBUG
	buf->buf->owner = bucket;
#endif

	while (ring->prod_tail != prod_head)
		cpu_spinwait();
	atomic_store(&ring->prod_tail, prod_next);

	return 0;
}

static inline int mp_alloc(mempool_priv_t *mp_priv, mp_buf_priv_t *buf)
{
	return mp_get(mp_priv, 0, buf);
}

static inline int mp_free(mempool_priv_t *mp_priv, mp_buf_priv_t *buf)
{
	return mp_put(mp_priv, 0, buf);
}

static inline int mp_is_full(mempool_priv_t *mp, int bucket)
{
	mp_ring_t *ring = mp->bucket[bucket];

	uint32_t prod_tail_next = (ring->prod_tail + 1) & mp->mask;

	return !!(prod_tail_next == ring->cons_tail);
}

#endif /* _MEMPOOL_H_ */
