#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <stddef.h>
#include <sys/eventfd.h>
#include "mempool.h"


static int
__mp_create(mempool_priv_t *mp_priv, const char *name, unsigned int entries,
	    unsigned int buckets)
{
	int fd, i;
	mempool_t *mp;

	int size = sizeof(mempool_t)
		+ (sizeof(mp_ring_t) + sizeof(int) * entries) * buckets
		+ entries * sizeof(mp_buf_t);

	if (buckets < 2 || buckets > MEM_POOL_MAX_BUCKETS) {
		fprintf(stderr, "number of buckets must be greater than 1 and "
			"less than %d\n", MEM_POOL_MAX_BUCKETS);
		return -1;
	}

	if (!POWEROF2(entries)) {
		fprintf(stderr, "number of entries must be power of 2\n");
		return -1;
	}

	/* open shared memory */
	if ((fd = shm_open(name, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR)) < 0)
		return -1;

	if ((ftruncate(fd, size)) < 0) {
		shm_unlink(name);
		close(fd);
	}

	mp = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if ((long)mp == -1) {
		shm_unlink(name);
		close(fd);
	}
	atomic_add_fetch(&mp->refcnt, 1);
	mp->size = size;
	mp->entries = entries;
	strncpy(mp->name, name, MEM_POOL_MAX_NAME);
	mp->buckets = buckets;

	mp_priv->mp = mp;
	mp_priv->bucket[0] = (mp_ring_t *)((char *)mp + sizeof(mempool_t));

	for (i = 1; i < buckets; i++) {
		mp_priv->bucket[i] = (mp_ring_t *)((char *)mp_priv->bucket[i-1]
						   + sizeof(mp_ring_t)
						   + sizeof(int) * entries);
	}
	mp_priv->data = (mp_buf_t *)((char *)mp_priv->bucket[buckets-1]
		      + sizeof(mp_ring_t) + sizeof(int) * entries);

	if ((long)mp_priv->data & __cache_line_mask) {
		fprintf(stderr, "mp_priv->data not cache aligned\n");
		goto error;
	}
	mp_priv->mask = entries - 1;
	mp_priv->entries = entries;

	/* fill up the 1st ring */
	for (i = 0; i < entries; i++) {
		mp_buf_priv_t buf = {
			.offset = i,
#ifndef NDEBUG
			.buf = &mp_priv->data[i],
#endif
		};
#ifndef NDEBUG
		mp_priv->data[i].owner = -1;
		if ((long)buf.buf & __cache_line_mask) {
			fprintf(stderr, "buf not cache aligned\n");
			goto error;
		}
#endif
		if (mp_put(mp_priv, 0, &buf) < 0)
			goto error;
	}

	close(fd);

	return 0;

 error:
	munmap(mp, size);
	shm_unlink(name);
	close(fd);

	return -1;
}

int
mp_create(mempool_priv_t *mp_priv, const char *name, unsigned int entries,
	  unsigned int buckets, unsigned notifications)
{
	int i;

	if (notifications > MEM_POOL_MAX_FDS) {
		fprintf(stderr, "number of notifications cannot exceed %d\n",
			MEM_POOL_MAX_FDS);
		return -1;
	}

	memset(mp_priv->fds, -1, sizeof(int) * MEM_POOL_MAX_FDS);

	if (__mp_create(mp_priv, name, entries, buckets) < 0)
		return -1;

	for (i = 0; i < notifications; i++) {
		int efd = eventfd(0, 0);

		if (efd < 0) {
			mp_unregister(mp_priv);
			return -1;
		}
		mp_priv->fds[i] = efd;
	}

	return 0;
}

int mp_unregister(mempool_priv_t *mp_priv)
{
	int size, i;
	char name[MEM_POOL_MAX_NAME];

	if (!mp_priv) {
		fprintf(stderr, "invalid shared memory\n");
		return -1;
	}

	size = mp_priv->mp->size;
	strncpy(name, mp_priv->mp->name, MEM_POOL_MAX_NAME);

	if (atomic_sub_fetch(&mp_priv->mp->refcnt, 1) > 0)
		return 0;

	/* make sure all locks are released (eg: signal catching) */
	for (i = 0; i < mp_priv->mp->buckets; i++) {
		if (spin_trylock(&mp_priv->bucket[i]->lock))
			spin_unlock(&mp_priv->bucket[i]->lock);
		else {
			fprintf(stderr, "inconsistent mempool unregister\n");
			assert(0);
		}
	}
	munmap(mp_priv->mp, size);
	shm_unlink(name);

	return 0;
}

int mp_register(mempool_priv_t *mp_priv, const char *name)
{
	int fd;
	int size, buckets;
	mempool_t *mp;
	int i, entries;

	fd = shm_open(name, O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "can't open shared memory %s\n", name);
		return -1;
	}
	size = lseek(fd, 0L, SEEK_END);

	mp = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
	if ((long)mp == -1) {
		close(fd);
		return -1;
	}

	buckets = atomic_add_fetch(&mp->refcnt, 1);
	if (buckets > mp->buckets) {
		fprintf(stderr, "can't exceed %d registrations\n",
			mp->buckets);
		goto error;
	}

	mp_priv->mp = mp;
	entries = mp->entries;
	mp_priv->entries = entries;
	mp_priv->mask = entries - 1;
	memset(mp_priv->fds, -1, sizeof(int) * MEM_POOL_MAX_FDS);

	mp_priv->bucket[0] = (mp_ring_t *)((char *)mp + sizeof(mempool_t));
	for (i = 1; i < mp->buckets; i++) {
		mp_priv->bucket[i] = (mp_ring_t *)((char *)mp_priv->bucket[i-1]
						   + sizeof(mp_ring_t)
						   + sizeof(int) * entries);
	}

	mp_priv->data = (mp_buf_t *)((char *)mp_priv->bucket[mp->buckets-1]
		      + sizeof(mp_ring_t) + sizeof(int) * entries);
	if ((long)mp_priv->data & __cache_line_mask) {
		fprintf(stderr, "buf not cache aligned\n");
		goto error;
	}

	close(fd);

	return 0;

 error:
	close(fd);
	atomic_sub_fetch(&mp->refcnt, 1);
	munmap(mp, size);

	return -1;
}
