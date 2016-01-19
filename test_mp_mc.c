#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include "atomic.h"
#include "mempool.h"
#include "command.h"

mempool_priv_t mp;
int debug;
int duration;
int is_consumer;

typedef enum bucket {
	BKT_MEMPOOL,
	BKT_CONSUMER,
	BKT_COUNT,
} bucket;

uint64_t stats;
struct timeval tv_start;
struct timeval tv_end;
int quit;

#define MP_ENTRIES 4096
#define MP_NAME "mp_shm"

static void usage(char *name)
{
	fprintf(stderr, "Usage: %s -m p|c [-d] [-t] [-r]\n"
		"\n"
		"m p|c - producer/consumer\n"
		"t     - duration (in seconds)\n"
		"d     - debug mode\n"
		"r     - register only (don't create the shared memory)\n",
		name);
	exit(EXIT_FAILURE);
}

static void cleanup()
{
	float diff;

	if (is_consumer) {
		gettimeofday(&tv_end, NULL);
		diff = (tv_end.tv_sec - tv_start.tv_sec) * 1000000ULL
			+ tv_end.tv_usec
			- tv_start.tv_usec;
		diff = diff / 1000000.0;

		printf("bytes read: %lu secs: %f bw=%f MB/s\n", stats, diff,
		       stats/diff/1024/1024);
	}
	mp_unregister(&mp);
	exit(0);
}

static inline void set_quit(int signo)
{
	if (++quit > 1)
		cleanup();
}

static void dump_infos(mempool_priv_t *mp)
{
	int i;

	printf("\nmp:        %p\n", mp->mp);
	printf("entries:   %d\n", mp->entries);
	printf("refcnt:    %d\n", mp->mp->refcnt);

	for (int i = 0; i < BKT_COUNT; i++) {
		printf("bucket[%d]: %p offset:%ld\n", i, mp->bucket[i],
		       (char *)mp->bucket[i] - (char *)mp->mp);
	}
	printf("data:      %p\n\n", mp->data);

	for (i = 0; i < MEM_POOL_MAX_FDS; i++) {
		int fd = mp->fds[i];

		if (fd < 0)
			break;

		printf("fd[%d]:%d\n", i, mp->fds[i]);
	}
}

static void producer(int register_only)
{
	uint64_t tosend = 0;
	mp_buf_priv_t buf;

	if (register_only == 0) {
		if (mp_create(&mp, MP_NAME, MP_ENTRIES, BKT_COUNT) < 0) {
			fprintf(stderr, "can't create shared memory\n");
			return;
		}
	} else {
		if (mp_register(&mp, MP_NAME) < 0) {
			fprintf(stderr, "can't register shared memory\n");
			return;
		}
	}

	if (debug)
		dump_infos(&mp);

	if (signal(SIGINT, set_quit) == SIG_ERR) {
		fprintf(stderr, "\ncan't catch SIGINT\n");
		mp_unregister(&mp);
		return;
	}

	while (1) {
		if (unlikely(quit))
			cleanup();

		if (unlikely(mp_get(&mp, BKT_MEMPOOL, &buf) < 0)) {
			/* fprintf(stderr, "no more memory\n"); */
			continue;
		}

		if (debug) {
			snprintf(buf.buf->data, MEM_POOL_BUF_SIZE,
				 "counter:%ld\n", tosend++);
		}

		if (unlikely(mp_put(&mp, BKT_CONSUMER, &buf) < 0)) {
			/* should never happen as BKT_CONSUMER and BKT_MEMPOOL
			 * are of the same size
			 */
			fprintf(stderr, "destination full\n");
			assert(0);
		}
	}

	mp_unregister(&mp);
}

static void consumer()
{
	mp_buf_priv_t buf;

	if (mp_register(&mp, MP_NAME) < 0) {
		fprintf(stderr, "can't register shared memory\n");
		return;
	}

	if (debug)
		dump_infos(&mp);

	if (signal(SIGINT, set_quit) == SIG_ERR) {
		fprintf(stderr, "\ncan't catch SIGINT\n");
		mp_unregister(&mp);
		return;
	}

	if (signal(SIGALRM, set_quit) == SIG_ERR) {
		fprintf(stderr, "\ncan't catch SIGALRM\n");
		mp_unregister(&mp);
		return;
	}

	if (duration)
		alarm(duration);

	printf("test duration set to: %d second(s)\n", duration);

	gettimeofday(&tv_start, NULL);

	while (1) {
		while (likely(mp_get(&mp, BKT_CONSUMER, &buf) >= 0)) {
			if (debug)
			    printf("%s", buf.buf->data);
			stats += MEM_POOL_BUF_SIZE;

			if (unlikely(mp_free(&mp, &buf) < 0)) {
				fprintf(stderr, "can't free buffer\n");
				break;
			}
			if (unlikely(quit))
				cleanup();
		}
	}

	mp_unregister(&mp);
}

int main(int argc, char *argv[])
{
	int opt, mode = 0;
	int register_only = 0;

	while ((opt = getopt(argc, argv, "m:dt:r")) != -1) {
		switch (opt) {
		case 'm':
			mode = *optarg;
			break;

		case 'd':
			debug = 1;
			break;

		case 't':
			duration = atoi(optarg);
			if (duration < 0 || duration > 3600) {
				fprintf(stderr, "bad duration %d\n", duration);
				usage(argv[0]);
			}
			break;

		case 'r':
			register_only = 1;
			break;

		default:
			usage(argv[0]);
		}
	}

	switch (mode) {
	case 'p':
		producer(register_only);
		break;

	case 'c':
		is_consumer = 1;
		consumer();
		break;

	default:
		usage(argv[0]);
	}

	return 0;
}
