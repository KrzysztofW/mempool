#ifndef _ATOMIC_H_
#define _ATOMIC_H_

typedef int spinlock_t;
typedef int atomic_t;

#define spin_trylock(ptr) (!__sync_lock_test_and_set(ptr, 1))
#define spin_lock(ptr) ({ while (unlikely(!spin_trylock(ptr))) { }})

#define spin_unlock(ptr) __sync_lock_release(ptr)

#define atomic_add_fetch(object, operand)                                    \
    __sync_add_and_fetch(object, operand)
#define atomic_sub_fetch(object, operand)                                    \
    __sync_sub_and_fetch(object, operand)
#define atomic_load(object) *(object)

#define likely(expr)   __builtin_expect(!!(expr), 1)
#define unlikely(expr) __builtin_expect((expr), 0)

#endif /* _ATOMIC_H_ */
