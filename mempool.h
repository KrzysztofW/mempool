#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_
#include <string.h>
#include <assert.h>
#include "atomic.h"
#include "mp_ring.h"

#define MEM_POOL_BUF_SIZE 8196
#define MEM_POOL_MAX_BUCKETS 16
#define MEM_POOL_MAX_FDS 16
#define MEM_POOL_MAX_NAME 100

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
	uintptr_t offset;
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

typedef struct mempool_priv_t {
	mempool_t *mp;
	mp_ring_t *bucket[MEM_POOL_MAX_BUCKETS];
	int        fds[MEM_POOL_MAX_FDS];
	mp_buf_t  *data;
	int        entries;
} mempool_priv_t;

int mp_create(mempool_priv_t *mp_priv, const char *name, unsigned int entries,
	      unsigned int buckets);
int mp_unregister(mempool_priv_t *mp_priv);
int mp_register(mempool_priv_t *mp_priv, const char *name);
int mp_create_notifs(mempool_priv_t *mp_priv, unsigned notifications);
void mp_retain(mempool_t *mp);

static inline int
__mp_get(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf, int mp)
{
	uintptr_t offset;
	void *ptr;
	mp_ring_t *ring = mp_priv->bucket[bucket];

	assert(bucket < mp_priv->mp->buckets);

	if (mp) {
		if (mp_ring_get(ring, &ptr) < 0)
			return -1;
	} else {
		if (mp_ring_get_sc(ring, &ptr) < 0)
			return -1;
	}

	offset = (uintptr_t)ptr;
	buf->offset = offset;
	buf->buf = &mp_priv->data[offset];

	assert(buf->buf->owner == bucket);
#ifndef NDEBUG
	buf->buf->owner = -1;
#endif
	return 0;
}

/* mempool get - multi consumer safe */
static inline int
mp_get(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	return __mp_get(mp_priv, bucket, buf, 1);
}

/* mempool get - single consumer safe */
static inline int
mp_get_sc(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	return __mp_get(mp_priv, bucket, buf, 0);
}

static inline int
__mp_put(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf, int mp)
{
	mp_ring_t *ring = mp_priv->bucket[bucket];
	void *ptr = (void *)buf->offset;

	assert(bucket < mp_priv->mp->buckets);

	if (mp) {
		if (mp_ring_put(ring, ptr) < 0)
			return -1;
	} else {
		if (mp_ring_put_sp(ring, ptr) < 0)
			return -1;
	}
	buf->offset = (uintptr_t)ptr;
	assert(buf->buf->owner == -1);
#ifndef NDEBUG
	buf->buf->owner = bucket;
#endif
	return 0;
}

/* mempool put - multi producer safe */
static inline int
mp_put(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	return __mp_put(mp_priv, bucket, buf, 1);
}

/* mempool put - single producer safe */
static inline int
mp_put_sp(mempool_priv_t *mp_priv, int bucket, mp_buf_priv_t *buf)
{
	return __mp_put(mp_priv, bucket, buf, 0);
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
	return mp_ring_is_full(mp->bucket[bucket]);
}

#endif /* _MEMPOOL_H_ */
