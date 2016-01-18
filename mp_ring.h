#ifndef _MP_RING_H_
#define _MP_RING_H_
#include <assert.h>
#include "sys.h"
#include "atomic.h"

typedef struct mp_ring {
	volatile uint32_t   prod_head;
	volatile uint32_t   prod_tail;
	int                 mask;
	volatile uint32_t   cons_head __cache_aligned;
	volatile uint32_t   cons_tail;
	void               *data[] __cache_aligned;
} mp_ring_t;

static inline int mp_ring_get(mp_ring_t *ring, void **ptr)
{
	uint32_t cons_head, cons_next;

	do {
		cons_head = ring->cons_head;
		cons_next = (cons_head + 1) & ring->mask;

		if (cons_head == ring->prod_tail)
			return -1;

	} while (!atomic_cmpset_int(&ring->cons_head, cons_head, cons_next));
	atomic_store(&ring->cons_tail, cons_next);

	*ptr =  ring->data[cons_head];

	return 0;
}

static inline int mp_ring_put(mp_ring_t *ring, void *ptr)
{
	uint32_t prod_head, prod_next, cons_tail;

	do {
		prod_head = ring->prod_head;
		prod_next = (prod_head + 1) & ring->mask;
		cons_tail = ring->cons_tail;

		if ((ring->mask + cons_tail - prod_head) == 0) {
			return -1;
		}
	} while (!atomic_cmpset_int(&ring->prod_head, prod_head, prod_next));
	ring->data[prod_head] = ptr;

	while (ring->prod_tail != prod_head)
		cpu_spinwait();
	atomic_store(&ring->prod_tail, prod_next);

	return 0;
}

static inline int mp_ring_is_full(mp_ring_t *ring)
{
	uint32_t prod_tail_next = (ring->prod_tail + 1) & ring->mask;

	return !!(prod_tail_next == ring->cons_tail);
}

static inline int mp_ring_size(mp_ring_t *ring)
{
	return ring->mask;
}

#endif
