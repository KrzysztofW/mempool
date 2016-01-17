#ifndef _ATOMIC_H_
#define _ATOMIC_H_

typedef int spinlock_t;
typedef int atomic_t;

/* #define __USE_GCC_BUILTIN */

#define spin_trylock(ptr) (!__sync_lock_test_and_set(ptr, 1))
#define spin_lock(ptr) ({ while (unlikely(!spin_trylock(ptr))) { }})

#define spin_unlock(ptr) __sync_lock_release(ptr)

#define atomic_add_fetch(object, operand)                                    \
    __sync_add_and_fetch(object, operand)
#define atomic_sub_fetch(object, operand)                                    \
    __sync_sub_and_fetch(object, operand)
#define atomic_load(object) *(object)
#define atomic_store(object, operand) *object = operand

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect((expr), 0)

static __inline void cpu_spinwait(void)
{
	__asm __volatile("pause");
}

/*
 * Atomic compare and set, used by the mutex functions
 *
 * if (*dst == expect) *dst = src (all 32 bit words)
 *
 * Returns 0 on failure, non-zero on success
 */

#ifndef __USE_GCC_BUILTIN
/* XXX this performs better than gcc's __sync_bool_compare_and_swap() */
static __inline int
atomic_cmpset_int(volatile u_int *dst, u_int expect, u_int src)
{
	u_char res;

	__asm __volatile(
	"	lock ;			"
	"	cmpxchgl %3,%1 ;	"
	"       sete	%0 ;		"
	"# atomic_cmpset_int"
	: "=q" (res),			/* 0 */
	  "+m" (*dst),			/* 1 */
	  "+a" (expect)			/* 2 */
	: "r" (src)			/* 3 */
	: "memory", "cc");
	return res;
}
#else
static __inline int
atomic_cmpset_int(volatile u_int *dst, u_int expect, u_int src)
{
	return __sync_bool_compare_and_swap(dst, expect, src);
}
#endif

#define	mb()	__asm __volatile("mfence;" : : : "memory")
#define	wmb()	__asm __volatile("sfence;" : : : "memory")
#define	rmb()	__asm __volatile("lfence;" : : : "memory")

#endif /* _ATOMIC_H_ */
