// -*- c-basic-offset: 4 -*-
#ifndef CLICK_ATOMIC_HH
#define CLICK_ATOMIC_HH
#if CLICK_LINUXMODULE
# include <click/glue.hh>
#endif
CLICK_DECLS
#if CLICK_LINUXMODULE
# if HAVE_LINUX_ASM_SYSTEM_H
#  include <click/cxxprotect.h>
CLICK_CXX_PROTECT
#  include <asm/system.h>
CLICK_CXX_UNPROTECT
#  include <click/cxxunprotect.h>
# endif
# define CLICK_ATOMIC_VAL	_val.counter
#else
# define CLICK_ATOMIC_VAL	_val
#endif
#if defined(__i386__) || defined(__arch_um__) || defined(__x86_64__)
# if CLICK_LINUXMODULE || HAVE_MULTITHREAD
#  define CLICK_ATOMIC_X86	1
# endif
# if (CLICK_LINUXMODULE && defined(CONFIG_SMP)) || HAVE_MULTITHREAD
#  define CLICK_ATOMIC_LOCK	"lock ; "
# else
#  define CLICK_ATOMIC_LOCK	/* nothing */
# endif
#endif


#if HAVE_MULTITHREAD && HAVE_ATOMIC_BUILTINS
//    #pragma message "atomic.hh: using builtins implementation"
    # define CLICK_ATOMIC_BUILTINS 1
    # define CLICK_ATOMIC_MEMORDER __ATOMIC_SEQ_CST
    //	compare_swap methods are not atomic if using builtins. Disable them
    # define CLICK_BUILTINS_DEPRECATED = delete
    # define CLICK_BUILTINS_NONDEPRECATED
    # define CLICK_ATOMIC_COMPARE_SWAP 0
#else
    # define CLICK_ATOMIC_BUILTINS 0
//    #pragma message "atomic.hh: Using click own implementation"
    // compare_swap are truly atomics if correctly implemented (e.g. x86).
    // In this case, we maintain the original Click's deprecation flags
    # define CLICK_BUILTINS_DEPRECATED
    # define CLICK_BUILTINS_NONDEPRECATED CLICK_DEPRECATED
    # define CLICK_ATOMIC_COMPARE_SWAP 1
#endif

/** @file <click/atomic.hh>
 * @brief An atomic 32-bit integer.
 */

/** @class atomic_uint32_t
 * @brief A 32-bit integer with support for atomic operations.
 *
 * The atomic_uint32_t class represents a 32-bit integer, with support for
 * atomic operations.  The +=, -=, &=, |=, ++, and -- operations are
 * implemented using atomic instructions.  There are also atomic swap(),
 * fetch_and_add(), dec_and_test(), and compare_swap() operations.
 *
 * Because of some issues with compiler implementations, atomic_uint32_t has
 * no explicit constructor; to set an atomic_uint32_t to a value, use
 * operator=.
 *
 * The atomic_uint32_t only provides true atomic semantics when that has been
 * implemented.  It has been implemented in the Linux kernel, and at userlevel
 * (when --enable-multithread has been defined) for x86 machines.  In other
 * situations, it's not truly atomic (because it doesn't need to be).
 */
class atomic_uint32_t { public:

    // No constructors because, unfortunately, GCC generates worse code. Use
    // operator= instead.

    inline uint32_t value() const;
    inline uint32_t nonatomic_value() const;
    inline operator uint32_t() const;

    inline atomic_uint32_t &operator=(uint32_t x);

    inline atomic_uint32_t &operator+=(int32_t delta);
    inline atomic_uint32_t &operator-=(int32_t delta);
    inline atomic_uint32_t &operator|=(uint32_t mask);
    inline atomic_uint32_t &operator&=(uint32_t mask);

    inline void nonatomic_inc();
    inline void operator++();
    inline void operator++(int);
    inline void nonatomic_dec();
    inline void operator--();
    inline void operator--(int);

    inline uint32_t swap(uint32_t desired);
    inline uint32_t fetch_and_add(uint32_t delta);
    inline bool dec_and_test();
    inline bool nonatomic_dec_and_test();
    inline uint32_t compare_swap(uint32_t expected, uint32_t desired) CLICK_BUILTINS_DEPRECATED;
    inline bool compare_and_swap(uint32_t expected, uint32_t desired) CLICK_BUILTINS_NONDEPRECATED;

    inline static uint32_t swap(volatile uint32_t &x, uint32_t desired);
    inline static void inc(volatile uint32_t &x);
    inline static bool dec_and_test(volatile uint32_t &x);
    inline static uint32_t compare_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired) CLICK_BUILTINS_DEPRECATED;
    inline static bool compare_and_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired) CLICK_BUILTINS_NONDEPRECATED;

    inline static bool use_builtins(){return CLICK_ATOMIC_BUILTINS;}
  private:

#if CLICK_LINUXMODULE
    atomic_t _val;
#elif HAVE_MULTITHREAD
    volatile uint32_t _val;
#else
    uint32_t _val;
#endif

};

/** @class atomic_uint64_t
 * @brief A 64-bit integer with support for atomic operations.
 *
 * The atomic_uint64_t class represents a 64-bit integer, with support for
 * atomic operations.  The +=, -=, &=, |=, ++, and -- operations are
 * implemented using atomic instructions.  There are also atomic swap(),
 * fetch_and_add(), dec_and_test(), and compare_swap() operations.
 *
 * Because of some issues with compiler implementations, atomic_uint64_t has
 * no explicit constructor; to set an atomic_uint64_t to a value, use
 * operator=.
 *
 * The atomic_uint64_t only provides true atomic semantics when that has been
 * implemented.  It has been implemented in the Linux kernel, and at userlevel
 * (when --enable-multithread has been defined) for x86 machines.  In other
 * situations, it's not truly atomic (because it doesn't need to be).
 */
class atomic_uint64_t { public:

    // No constructors because, unfortunately, GCC generates worse code. Use
    // operator= instead.

    inline uint64_t value() const;
    inline uint64_t nonatomic_value() const;
    inline operator uint64_t() const;

    inline atomic_uint64_t &operator=(uint64_t x);

    inline atomic_uint64_t &operator+=(int64_t delta);
    inline atomic_uint64_t &operator-=(int64_t delta);

    inline void nonatomic_inc();
    inline void operator++();
    inline void operator++(int);
    inline void nonatomic_dec();
    inline void operator--();
    inline void operator--(int);

    inline uint64_t compare_swap(uint64_t expected, uint64_t desired) CLICK_BUILTINS_DEPRECATED;
    inline uint64_t fetch_and_add(uint64_t delta);

    inline static void add(volatile uint64_t &x, uint64_t delta);
    inline static uint64_t compare_swap(volatile uint64_t &x, uint64_t expected, uint64_t desired) CLICK_BUILTINS_DEPRECATED;

    inline static bool use_builtins(){return CLICK_ATOMIC_BUILTINS;}
  private:

#if CLICK_LINUXMODULE
    atomic64_t _val;
#elif HAVE_MULTITHREAD
    volatile uint64_t _val __attribute__((aligned));
#else
    uint64_t _val;
#endif

};

/** @brief  Return the value. */
inline uint32_t
atomic_uint32_t::value() const
{
#if CLICK_LINUXMODULE
    return atomic_read(&_val);
#else
    return CLICK_ATOMIC_VAL;
#endif
}

/** @brief  Return the value. */
inline uint32_t
atomic_uint32_t::nonatomic_value() const
{
    return *((uint32_t*)&_val);
}

/** @brief  Return the value. */
inline
atomic_uint32_t::operator uint32_t() const
{
    return value();
}

/** @brief  Set the value to @a x. */
inline atomic_uint32_t &
atomic_uint32_t::operator=(uint32_t x)
{
#if CLICK_LINUXMODULE
    atomic_set(&_val, x);
#else
    CLICK_ATOMIC_VAL = x;
#endif
    return *this;
}

/** @brief  Atomically add @a delta to the value. */
inline atomic_uint32_t &
atomic_uint32_t::operator+=(int32_t delta)
{
#if CLICK_LINUXMODULE
    atomic_add(delta, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&CLICK_ATOMIC_VAL, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "addl %1,%0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "r" (delta), "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL += delta;
#endif
    return *this;
}

/** @brief  Atomically subtract @a delta from the value. */
inline atomic_uint32_t &
atomic_uint32_t::operator-=(int32_t delta)
{
#if CLICK_LINUXMODULE
    atomic_sub(delta, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "subl %1,%0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "r" (delta), "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL -= delta;
#endif
    return *this;
}

/** @brief  Atomically bitwise-or the value with @a mask. */
inline atomic_uint32_t &
atomic_uint32_t::operator|=(uint32_t mask)
{
#if CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_SET_MASK
    atomic_set_mask(mask, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_or_fetch(&CLICK_ATOMIC_VAL, mask, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "orl %1,%0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "r" (mask), "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::operator|="
    unsigned long flags;
    local_irq_save(flags);
    CLICK_ATOMIC_VAL |= mask;
    local_irq_restore(flags);
#else
    CLICK_ATOMIC_VAL |= mask;
#endif
     return *this;
}

/** @brief  Atomically bitwise-and the value with @a mask. */
inline atomic_uint32_t &
atomic_uint32_t::operator&=(uint32_t mask)
{
#if CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_SET_MASK
    atomic_clear_mask(~mask, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_and_fetch(&CLICK_ATOMIC_VAL, mask, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "andl %1,%0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "r" (mask), "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::operator&="
    unsigned long flags;
    local_irq_save(flags);
    CLICK_ATOMIC_VAL &= mask;
    local_irq_restore(flags);
#else
    CLICK_ATOMIC_VAL &= mask;
#endif
    return *this;
}

/** @brief  Atomically increment value @a x. */
inline void
atomic_uint32_t::inc(volatile uint32_t &x)
{
#if CLICK_LINUXMODULE
    static_assert(sizeof(atomic_t) == sizeof(x), "atomic_t expected to take 32 bits.");
    atomic_inc((atomic_t *) &x);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&x, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "incl %0"
		  : "=m" (x)
		  : "m" (x)
		  : "cc");
#else
    x++;
#endif
}

/** @brief  Increment the value. */
inline void
atomic_uint32_t::nonatomic_inc()
{
    CLICK_ATOMIC_VAL++;
}

/** @brief  Atomically increment the value. */
inline void
atomic_uint32_t::operator++()
{
#if CLICK_LINUXMODULE
    atomic_inc(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&_val, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "incl %0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL++;
#endif
}

/** @brief  Atomically increment the value. */
inline void
atomic_uint32_t::operator++(int)
{
#if CLICK_LINUXMODULE
    atomic_inc(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "incl %0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL++;
#endif
}

/** @brief  Atomically decrement the value. */
inline void
atomic_uint32_t::operator--()
{
#if CLICK_LINUXMODULE
    atomic_dec(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "decl %0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL--;
#endif
}

/** @brief  Decrement the value. */
inline void
atomic_uint32_t::nonatomic_dec()
{
    CLICK_ATOMIC_VAL--;
}

/** @brief  Atomically decrement the value. */
inline void
atomic_uint32_t::operator--(int)
{
#if CLICK_LINUXMODULE
    atomic_dec(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "decl %0"
		  : "=m" (CLICK_ATOMIC_VAL)
		  : "m" (CLICK_ATOMIC_VAL)
		  : "cc");
#else
    CLICK_ATOMIC_VAL--;
#endif
}

/** @brief  Atomically assign the value to @a desired, returning the old value.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t actual = x;
 * x = desired;
 * return actual;
 * @endcode
 *
 * Also acts as a memory barrier. */
inline uint32_t
atomic_uint32_t::swap(volatile uint32_t &x, uint32_t desired)
{
#if CLICK_ATOMIC_BUILTINS
    uint32_t actual = x;
    __atomic_exchange(&x, &desired, &actual, CLICK_ATOMIC_MEMORDER);
    return actual;

#elif CLICK_ATOMIC_X86
    asm volatile ("xchgl %0,%1"
		  : "=r" (desired), "=m" (x)
		  : "0" (desired), "m" (x)
		  : "memory");
    return desired;
#elif CLICK_LINUXMODULE && defined(xchg)
    return xchg(&x, desired);
#elif CLICK_LINUXMODULE
# error "need xchg for atomic_uint32_t::swap"
#else
    uint32_t actual = x;
    x = desired;
    return actual;
#endif
}

/** @brief  Atomically assign the value to @a desired, returning the old value.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t old_value = value();
 * *this = desired;
 * return old_value;
 * @endcode
 *
 * Also acts as a memory barrier. */
inline uint32_t
atomic_uint32_t::swap(uint32_t desired)
{
#if CLICK_LINUXMODULE && defined(xchg)
    return atomic_xchg(&_val, desired);
#elif CLICK_LINUXMODULE
# error "need xchg for atomic_uint32_t::swap"
#else
    return swap(CLICK_ATOMIC_VAL, desired);
#endif
}

/** @brief  Atomically add @a delta to the value, returning the old value.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t old_value = value();
 * *this += delta;
 * return old_value;
 * @endcode */
inline uint32_t
atomic_uint32_t::fetch_and_add(uint32_t delta)
{
#if CLICK_ATOMIC_BUILTINS
    return __atomic_fetch_add(&_val, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "xaddl %0,%1"
		  : "=r" (delta), "=m" (CLICK_ATOMIC_VAL)
		  : "0" (delta), "m" (CLICK_ATOMIC_VAL)
		  : "cc");
    return delta;
#elif CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_ADD_RETURN
    return atomic_add_return(&_val, delta) - delta;
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::fetch_and_add"
    unsigned long flags;
    local_irq_save(flags);
    uint32_t old_value = value();
    CLICK_ATOMIC_VAL += delta;
    local_irq_restore(flags);
    return old_value;
#else
    uint32_t old_value = value();
    CLICK_ATOMIC_VAL += delta;
    return old_value;
#endif
}

/** @brief  Atomically decrement @a x, returning true if the new @a x
 *	    is 0.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * --x;
 * return x == 0;
 * @endcode */
inline bool
atomic_uint32_t::dec_and_test(volatile uint32_t &x)
{
#if CLICK_LINUXMODULE
    static_assert(sizeof(atomic_t) == sizeof(x), "atomic_t expected to take 32 bits.");
    return atomic_dec_and_test((atomic_t *) &x);
#elif CLICK_ATOMIC_BUILTINS
    return __atomic_sub_fetch(&x,1 , CLICK_ATOMIC_MEMORDER) == 0;
#elif CLICK_ATOMIC_X86
    uint8_t result;
    asm volatile (CLICK_ATOMIC_LOCK "decl %0 ; sete %1"
		  : "=m" (x), "=qm" (result)
		  : "m" (x)
		  : "cc");
    return result;
#else
    return (--x == 0);
#endif
}

/** @brief  Perform a compare-and-swap operation.
 *  @param  x         value
 *  @param  expected  test value
 *  @param  desired   new value
 *  @return The actual old value.  If it equaled @a expected, @a x has been
 *	    set to @a desired.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t actual = x;
 * if (x == expected)
 *     x = desired;
 * return actual;
 * @endcode
 *
 * Also acts as a memory barrier. */
#if CLICK_ATOMIC_COMPARE_SWAP
inline uint32_t
atomic_uint32_t::compare_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired)
{
#if CLICK_ATOMIC_BUILTINS
# warning "compare_swap is not truly atomic with system builtins"
    return __atomic_compare_exchange(&x,&expected,&desired,0,CLICK_ATOMIC_MEMORDER, CLICK_ATOMIC_MEMORDER)
	? expected : x;
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "cmpxchgl %2,%1"
		  : "=a" (expected), "=m" (x)
		  : "r" (desired), "0" (expected), "m" (x)
		  : "cc", "memory");
    return expected;
#elif CLICK_LINUXMODULE && defined(cmpxchg)
    return cmpxchg(&x, expected, desired);
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::compare_and_swap"
    unsigned long flags;
    local_irq_save(flags);
    uint32_t actual = x;
    if (actual == expected)
	x = desired;
    local_irq_restore(flags);
    return actual;
#else
    uint32_t actual = x;
    if (actual == expected)
	x = desired;
    return actual;
#endif
}
#endif

/** @brief  Perform a compare-and-swap operation.
 *  @param  x         value
 *  @param  expected  test value
 *  @param  desired   new value
 *  @return True if the old @a x equaled @a expected (in which case @a x
 *	    was set to @a desired), false otherwise.
 *  @deprecated Use compare_swap instead.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t old_value = x;
 * if (x == expected)
 *     x = desired;
 * return old_value == expected;
 * @endcode
 *
 * Also acts as a memory barrier. */
inline bool
atomic_uint32_t::compare_and_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired)
{
#if CLICK_ATOMIC_BUILTINS
    return __atomic_compare_exchange(&x,&expected,&desired,0,CLICK_ATOMIC_MEMORDER, CLICK_ATOMIC_MEMORDER);

#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "cmpxchgl %2,%0 ; sete %%al"
		  : "=m" (x), "=a" (expected)
		  : "r" (desired), "m" (x), "a" (expected)
		  : "cc", "memory");
    return (uint8_t) expected;
#elif CLICK_LINUXMODULE && defined(cmpxchg)
    return cmpxchg(&x, expected, desired) == expected;
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::compare_and_swap"
    unsigned long flags;
    local_irq_save(flags);
    uint32_t old_value = x;
    if (old_value == expected)
	x = desired;
    local_irq_restore(flags);
    return old_value == expected;
#else
    uint32_t old_value = x;
    if (old_value == expected)
	x = desired;
    return old_value == expected;
#endif
}

/** @brief  Atomically decrement the value, returning true if the new value
 *	    is 0.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * --*this;
 * return value() == 0;
 * @endcode */
inline bool
atomic_uint32_t::dec_and_test()
{
#if CLICK_LINUXMODULE
    return atomic_dec_and_test(&_val);
#elif CLICK_ATOMIC_BUILTINS
    return __atomic_sub_fetch(&_val, 1, CLICK_ATOMIC_MEMORDER) == 0;
#elif CLICK_ATOMIC_X86
    uint8_t result;
    asm volatile (CLICK_ATOMIC_LOCK "decl %0 ; sete %1"
		  : "=m" (CLICK_ATOMIC_VAL), "=qm" (result)
		  : "m" (CLICK_ATOMIC_VAL)
		  : "cc");
    return result;
#else
    return (--CLICK_ATOMIC_VAL == 0);
#endif
}

/** @brief  Decrement the value, returning true if the new value
 *      is 0.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * --*this;
 * return value() == 0;
 * @endcode */
inline bool
atomic_uint32_t::nonatomic_dec_and_test()
{
    CLICK_ATOMIC_VAL--;
    return CLICK_ATOMIC_VAL == 0;
}

/** @brief  Perform a compare-and-swap operation.
 *  @param  expected  test value
 *  @param  desired   new value
 *  @return The actual old value.  If @a expected is returned, the
 *          value has been set to @a desired.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t actual = value();
 * if (actual == expected)
 *     *this = desired;
 * return actual;
 * @endcode
 *
 * Also acts as a memory barrier. */
#if CLICK_ATOMIC_COMPARE_SWAP
inline uint32_t
atomic_uint32_t::compare_swap(uint32_t expected, uint32_t desired)
{
#if CLICK_ATOMIC_BUILTINS
# warning "compare_swap is not truly atomic with system builtins"
    return __atomic_compare_exchange(&_val, &expected, &desired, 0, CLICK_ATOMIC_MEMORDER, CLICK_ATOMIC_MEMORDER)
	? expected: _val;
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "cmpxchgl %2,%1"
		  : "=a" (expected), "=m" (CLICK_ATOMIC_VAL)
		  : "r" (desired), "0" (expected), "m" (CLICK_ATOMIC_VAL)
		  : "cc", "memory");
    return expected;
#elif CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_CMPXCHG
    return atomic_cmpxchg(&_val, expected, desired);
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::compare_swap"
    unsigned long flags;
    local_irq_save(flags);
    uint32_t actual = value();
    if (actual == expected)
	CLICK_ATOMIC_VAL = desired;
    local_irq_restore(flags);
    return actual;
#else
    uint32_t actual = value();
    if (actual == expected)
	CLICK_ATOMIC_VAL = desired;
    return actual;
#endif
}
#endif

/** @brief  Perform a compare-and-swap operation.
 *  @param  expected  test value
 *  @param  desired   new value
 *  @return True if the old value equaled @a expected (in which case the
 *	    value was set to @a desired), false otherwise.
 *  @deprecated  Use compare_swap instead.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t old_value = value();
 * if (old_value == expected)
 *     *this = desired;
 * return old_value == expected;
 * @endcode
 *
 * Also acts as a memory barrier. */
inline bool
atomic_uint32_t::compare_and_swap(uint32_t expected, uint32_t desired)
{
#if CLICK_ATOMIC_BUILTINS
    return __atomic_compare_exchange(&_val, &expected, &desired, 0, CLICK_ATOMIC_MEMORDER, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    asm volatile (CLICK_ATOMIC_LOCK "cmpxchgl %2,%0 ; sete %%al"
		  : "=m" (CLICK_ATOMIC_VAL), "=a" (expected)
		  : "r" (desired), "m" (CLICK_ATOMIC_VAL), "a" (expected)
		  : "cc", "memory");
    return (uint8_t) expected;
#elif CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_CMPXCHG
    return atomic_cmpxchg(&_val, expected, desired) == expected;
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint32_t::compare_and_swap"
    unsigned long flags;
    local_irq_save(flags);
    uint32_t old_value = value();
    if (old_value == expected)
	CLICK_ATOMIC_VAL = desired;
    local_irq_restore(flags);
    return old_value == expected;
#else
    uint32_t old_value = value();
    if (old_value == expected)
	CLICK_ATOMIC_VAL = desired;
    return old_value == expected;
#endif
}

inline uint32_t
operator+(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() + b.value();
}

inline uint32_t
operator-(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() - b.value();
}

inline bool
operator==(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() == b.value();
}

inline bool
operator!=(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() != b.value();
}

inline bool
operator>(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() > b.value();
}

inline bool
operator<(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() < b.value();
}

inline bool
operator>=(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() >= b.value();
}

inline bool
operator<=(const atomic_uint32_t &a, const atomic_uint32_t &b)
{
    return a.value() <= b.value();
}

typedef atomic_uint32_t uatomic32_t;

//Atomic 64
/** @brief  Return the value. */
inline uint64_t
atomic_uint64_t::value() const
{
#if CLICK_LINUXMODULE
    return atomic64_read(&_val);
#else
    #if defined(__x86_64__)
        return CLICK_ATOMIC_VAL;
    #else
        uint32_t low, high;

        asm volatile(
	"1:\n\t"
        "movl (%[addr]), %%eax\n\t"          // EAX = low
        "movl 4(%[addr]), %%edx\n\t"         // EDX = high
        "movl %%eax, %%ebx\n\t"         // EDX = high
        "movl %%edx, %%ecx\n\t"         // EDX = high
        CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"       // Compare with same value to validate consistency
        "jnz 1b\n\t"                         // Retry if value changed during read
        : "=&a" (low), "=&d" (high)
        : [addr] "r" (&_val)
        : "ebx", "ecx", "memory"
        );
 	return ((uint64_t)high << 32) | low;
    #endif
#endif
}

/** @brief  Return the value. */
inline uint64_t
atomic_uint64_t::nonatomic_value() const
{
    return *((uint64_t*)&_val);
}

/** @brief  Return the value. */
inline
atomic_uint64_t::operator uint64_t() const
{
    return value();
}

/** @brief  Set the value to @a x. */
inline atomic_uint64_t &
atomic_uint64_t::operator=(uint64_t x)
{
#if CLICK_LINUXMODULE
    atomic64_set(&_val, x);
#else
   #if defined(__x86_64__)
       CLICK_ATOMIC_VAL = x;
   #else
        uint32_t nlow = static_cast<uint32_t>(x & 0xFFFFFFFF);
        uint32_t nhigh = static_cast<uint32_t>(x >> 32);
        asm volatile (
        "1:\n\t"
        "movl (%[addr]), %%eax\n\t"          // EAX = old low
        "movl 4(%[addr]), %%edx\n\t"         // EDX = old high
        CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"       // Atomically write ECX:EBX if EDX:EAX match
        "jnz 1b\n\t"                         // Retry if compare failed
        :
        : [addr] "r" (&_val),
          "b" (nlow),"c"(nhigh)
        : "eax", "edx", "memory"
        );
  #endif
#endif
    return *this;
}

/** @brief  Atomically add @a delta to the value. */
inline atomic_uint64_t &
atomic_uint64_t::operator+=(int64_t delta)
{
#if CLICK_LINUXMODULE
    atomic64_add(delta, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&_val, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "addq %1,%0"
          : "=m" (CLICK_ATOMIC_VAL)
          : "r" (delta), "m" (CLICK_ATOMIC_VAL)
          : "cc");
    #else
        uint32_t dlow = static_cast<uint32_t>(delta & 0xFFFFFFFF);
        int32_t  dhigh = static_cast<int32_t>(delta >> 32);

        asm volatile (
            "movl %%ebx, %%esi\n\t"              // Save EBX (needed for PIC)
            "1:\n\t"
            "movl (%[addr]), %%eax\n\t"          // EAX = old low
            "movl 4(%[addr]), %%edx\n\t"         // EDX = old high
            "movl %%eax, %%ebx\n\t"              // EBX = expected low
            "movl %%edx, %%ecx\n\t"              // ECX = expected high
            "addl %[delta_low], %%ebx\n\t"       // EBX = new low
            "adcl %[delta_high], %%ecx\n\t"      // ECX = new high (+ carry)
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"       // Atomically compare and exchange
            "jnz 1b\n\t"                         // Retry if comparison failed
            "movl %%esi, %%ebx\n\t"              // Restore EBX
            :
	    : [addr]        "r" (&_val),
              [delta_low]   "m" (dlow),
              [delta_high]  "m" (dhigh) // sign-extend high part
           : "eax", "edx", "ecx", "esi", "memory"
    );
    #endif
#else
    CLICK_ATOMIC_VAL += delta;
#endif
    return *this;
}

/** @brief  Atomically increment value @a x. */
inline void
atomic_uint64_t::add(volatile uint64_t &x, uint64_t delta)
{
#if CLICK_LINUXMODULE
    atomic64_add(delta, (atomic64_t*)&x);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&x, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "addq %1,%0"
            : "=m" (x)
            : "r" (delta), "m" (x)
            : "cc");
    #else
        uint32_t dlow = static_cast<uint32_t>(delta & 0xFFFFFFFF);
        uint32_t  dhigh = static_cast<uint32_t>(delta >> 32);
        asm volatile (
            "movl %%ebx, %%esi\n\t"              // Save EBX
            "1:\n\t"
            "movl (%[addr]), %%eax\n\t"          // EAX = old low
            "movl 4(%[addr]), %%edx\n\t"         // EDX = old high
            "movl %%eax, %%ebx\n\t"              // EBX = expected low
            "movl %%edx, %%ecx\n\t"              // ECX = expected high
            "addl %[delta_low], %%ebx\n\t"       // EBX = new low
            "adcl %[delta_high], %%ecx\n\t"      // ECX = new high (+ carry)
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"       // Try to atomically swap
            "jnz 1b\n\t"                         // Retry if failed
            "movl %%esi, %%ebx\n\t"              // Restore EBX
            :
            : [addr]       "r" (&x),
              [delta_low]  "m" (dlow),
              [delta_high] "m" (dhigh)
            : "eax", "edx", "ecx", "esi", "memory");
    #endif
#else
    x += delta;
#endif
}

/** @brief  Atomically subtract @a delta from the value. */
inline atomic_uint64_t &
atomic_uint64_t::operator-=(int64_t delta)
{
#if CLICK_LINUXMODULE
    atomic64_sub(delta, &_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "subq %1,%0"
            : "=m" (CLICK_ATOMIC_VAL)
            : "r" (delta), "m" (CLICK_ATOMIC_VAL)
            : "cc");
    #else
        uint32_t dlow = static_cast<uint32_t>(delta & 0xFFFFFFFF);
        int32_t  dhigh = static_cast<int32_t>(delta >> 32);
        asm volatile("1:\n\t"
            "movl     (%[addr]), %%eax\n\t"        // EAX = old low
            "movl     4(%[addr]), %%edx\n\t"       // EDX = old high
            "movl     %%eax, %%ebx\n\t"            // EBX = old low
            "movl     %%edx, %%ecx\n\t"            // ECX = old high
            "subl     %[vlo], %%ebx\n\t"           // EBX -= value low
            "sbbl     %[vhi], %%ecx\n\t"           // ECX -= value high + borrow
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"         // Attempt atomic CAS
            "jnz      1b\n\t"                      // Retry if it failed
            :
            : [addr] "r" (&_val),
              [vlo]  "m" (dlow),
              [vhi]  "m" (dhigh)
            : "eax", "ebx", "ecx", "edx", "memory");
    #endif
#else
    CLICK_ATOMIC_VAL -= delta;
#endif
    return *this;
}


/** @brief  Increment the value. */
inline void
atomic_uint64_t::nonatomic_inc()
{
    CLICK_ATOMIC_VAL++;
}

/** @brief  Atomically increment the value. */
inline void
atomic_uint64_t::operator++()
{
#if CLICK_LINUXMODULE
    atomic64_inc(&_val);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "incq %0"
            : "=m" (CLICK_ATOMIC_VAL)
            : "m" (CLICK_ATOMIC_VAL)
            : "cc");
    #else
        asm volatile(
            "1:\n\t"
            "movl     (%[addr]), %%eax\n\t"        // Load old low 32 bits
            "movl     4(%[addr]), %%edx\n\t"       // Load old high 32 bits
            "movl     %%eax, %%ebx\n\t"            // EBX = copy of low
            "movl     %%edx, %%ecx\n\t"            // ECX = copy of high
            "addl     $1, %%ebx\n\t"               // Increment low
            "adcl     $0, %%ecx\n\t"               // Add carry to high
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"         // Attempt atomic swap
            "jnz      1b\n\t"                      // Retry if failed
            :
            : [addr] "r" (&_val)
            : "eax", "ebx", "ecx", "edx", "memory");
    #endif
#else
    CLICK_ATOMIC_VAL++;
#endif
}

/** @brief  Atomically increment the value. */
inline void
atomic_uint64_t::operator++(int)
{
#if CLICK_LINUXMODULE
    atomic64_inc(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_add_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64)
        asm volatile (CLICK_ATOMIC_LOCK "incq %0"
            : "=m" (CLICK_ATOMIC_VAL)
            : "m" (CLICK_ATOMIC_VAL)
            : "cc");
    #else
        asm volatile(
            "1:\n\t"
            "movl    (%[addr]), %%eax\n\t"
            "movl    4(%[addr]), %%edx\n\t"
            "movl    %%eax, %%ebx\n\t"
            "movl    %%edx, %%ecx\n\t"
            "addl    $1, %%ebx\n\t"
            "adcl    $0, %%ecx\n\t"
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"
            "jnz     1b\n\t"
            :
            : [addr] "r" (&_val)
            : "eax", "ebx", "ecx", "edx", "memory");
    #endif
#else
    CLICK_ATOMIC_VAL++;
#endif
}

/** @brief  Atomically decrement the value. */
inline void
atomic_uint64_t::operator--()
{
#if CLICK_LINUXMODULE
    atomic64_dec(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "decq %0"
            : "=m" (CLICK_ATOMIC_VAL)
            : "m" (CLICK_ATOMIC_VAL)
            : "cc");
    #else
        asm volatile(
            "1:\n\t"
            "movl     (%[addr]), %%eax\n\t"        // Load old low
	    "movl     4(%[addr]), %%edx\n\t"       // Load old high
	    "movl     %%eax, %%ebx\n\t"            // Copy low to EBX
	    "movl     %%edx, %%ecx\n\t"            // Copy high to ECX
	    "subl     $1, %%ebx\n\t"               // EBX -= 1 (low part)
	    "sbbl     $0, %%ecx\n\t"               // ECX -= borrow (high part)
	    "lock cmpxchg8b (%[addr])\n\t"         // Atomic compare & swap
	    "jnz      1b\n\t"                      // Retry on failure
	    :
	    : [addr] "r" (&_val)
	    : "eax", "ebx", "ecx", "edx", "memory");
    #endif
#else
    CLICK_ATOMIC_VAL--;
#endif
}

/** @brief  Decrement the value. */
inline void
atomic_uint64_t::nonatomic_dec()
{
    CLICK_ATOMIC_VAL--;
}

/** @brief  Atomically decrement the value. */
inline void
atomic_uint64_t::operator--(int)
{
#if CLICK_LINUXMODULE
    atomic64_dec(&_val);
#elif CLICK_ATOMIC_BUILTINS
    __atomic_sub_fetch(&CLICK_ATOMIC_VAL, 1, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "decq %0"
            : "=m" (CLICK_ATOMIC_VAL)
            : "m" (CLICK_ATOMIC_VAL)
            : "cc");
    #else
        asm volatile(
            "1:\n\t"
            "movl     (%[addr]), %%eax\n\t"        // Load old low
            "movl     4(%[addr]), %%edx\n\t"       // Load old high
            "movl     %%eax, %%ebx\n\t"            // Copy low to EBX
            "movl     %%edx, %%ecx\n\t"            // Copy high to ECX
            "subl     $1, %%ebx\n\t"               // EBX -= 1 (low part)
            "sbbl     $0, %%ecx\n\t"               // ECX -= borrow (high part)
            CLICK_ATOMIC_LOCK "cmpxchg8b (%[addr])\n\t"         // Atomic compare & swap
            "jnz      1b\n\t"                      // Retry on failure
            :
            : [addr] "r" (&_val)
            : "eax", "ebx", "ecx", "edx", "memory");
    #endif
#else
    CLICK_ATOMIC_VAL--;
#endif
}

/** @brief  Perform a compare-and-swap operation.
 *  @param  x         value
 *  @param  expected  test value
 *  @param  desired   new value
 *  @return The actual old value.  If it equaled @a expected, @a x has been
 *	    set to @a desired.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint62_t actual = x;
 * if (x == expected)
 *     x = desired;
 * return actual;
 * @endcode
 *
 * Also acts as a memory barrier. */
#if CLICK_ATOMIC_COMPARE_SWAP
inline uint64_t
atomic_uint64_t::compare_swap(volatile uint64_t &x, uint64_t expected, uint64_t desired)
{
#if CLICK_ATOMIC_BUILTINS
# warning "compare_swap is not truly atomic with system builtins"
    return __atomic_compare_exchange(&x,&expected,&desired,0,CLICK_ATOMIC_MEMORDER, CLICK_ATOMIC_MEMORDER)
	? expected : x;
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "cmpxchgq %2,%1"
            : "=a" (expected), "=m" (x)
            : "r" (desired), "0" (expected), "m" (x)
            : "cc", "memory");
    #else
	asm volatile (CLICK_ATOMIC_LOCK "cmpxchg8b %1\n\t"   // Compare EDX:EAX with [mem], replace with ECX:EBX if equal
            : "+A" (expected)          //load expected into EAX:EDX
            : "m" (x),            // preload memory value to compare.
              "b" ((uint32_t)desired), "c"((uint32_t) (desired>>32))
            : "memory");
    #endif
    return expected;
#elif CLICK_LINUXMODULE && defined(cmpxchg)
    return cmpxchg(&x, expected, desired);
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint§2_t::compare_and_swap"
    unsigned long flags;
    local_irq_save(flags);
    uint64_t actual = x;
    if (actual == expected)
	x = desired;
    local_irq_restore(flags);
    return actual;
#else
    uint64_t actual = x;
    if (actual == expected)
	x = desired;
    return actual;
#endif
}

inline uint64_t
atomic_uint64_t::compare_swap(uint64_t expected, uint64_t desired) {
#if CLICK_LINUXMODULE && defined(cmpxchg)
    return atomic64_cmpxchg(&_val, expected, desired);
#else
    return atomic_uint64_t::compare_swap(*(volatile uint64_t*)&CLICK_ATOMIC_VAL, expected, desired);
#endif
}
#endif

/** @brief  Atomically add @a delta to the value, returning the old value.
 *
 * Behaves like this, but in one atomic step:
 * @code
 * uint32_t old_value = value();
 * *this += delta;
 * return old_value;
 * @endcode */
inline uint64_t
atomic_uint64_t::fetch_and_add(uint64_t delta)
{
#if CLICK_ATOMIC_BUILTINS
    return __atomic_fetch_add(&_val, delta, CLICK_ATOMIC_MEMORDER);
#elif CLICK_ATOMIC_X86
    #if defined(__x86_64__)
        asm volatile (CLICK_ATOMIC_LOCK "xaddq %0,%1"
	    : "=r" (delta), "=m" (CLICK_ATOMIC_VAL)
	    : "0" (delta), "m" (CLICK_ATOMIC_VAL)
	    : "cc");
	return delta;
    #else
	uint32_t old_lo=0, old_hi=0;
	asm volatile (
            "1:\n\t"
	    "movl (%2), %%eax\n\t"           // Load old low 32 bits
	    "movl 4(%2), %%edx\n\t"          // Load old high 32 bits
	    "movl %%eax, %%ebx\n\t"          // Copy low to ebx (new low)
	    "movl %%edx, %%ecx\n\t"          // Copy high to ecx (new high)
	    "addl %3, %%ebx\n\t"             // new low += delta low
	    "adcl %4, %%ecx\n\t"             // new high += delta high + carry
	    CLICK_ATOMIC_LOCK "cmpxchg8b (%2)\n\t"        // atomic cmpxchg8b
	    "jnz 1b\n\t"                     // if fail, retry
	    "movl %%eax, %0\n\t"             // store old low
	    "movl %%edx, %1\n\t"             // store old high
	    : "=&a" (old_lo), "=&d" (old_hi)
	    : "r" (&_val), "r" ((uint32_t)delta), "r" ((uint32_t)(delta >> 32))
	    : "ebx", "ecx", "cc","memory");
	return (static_cast<uint64_t>(old_hi) << 32) | old_lo;
    #endif
#elif CLICK_LINUXMODULE && HAVE_LINUX_ATOMIC_ADD_RETURN
    return atomic64_add_return(&_val, delta) - delta;
#elif CLICK_LINUXMODULE
# warning "using nonatomic approximation for atomic_uint64_t::fetch_and_add"
    unsigned long flags;
    local_irq_save(flags);
    uint64_t old_value = value();
    CLICK_ATOMIC_VAL += delta;
    local_irq_restore(flags);
    return old_value;
#else
    uint64_t old_value = value();
    CLICK_ATOMIC_VAL += delta;
    return old_value;
#endif
}



class nonatomic_uint32_t { public:

    // Fake atomic to drop-in for atomic uint32_t.  Not actually atomic.

    inline uint32_t value() const { return _val; }
    inline uint32_t nonatomic_value() const { return _val; }
    inline operator uint32_t() const { return _val; }

    inline nonatomic_uint32_t &operator=(uint32_t x) { _val = x; return *this; }

    inline nonatomic_uint32_t &operator+=(int32_t delta) { _val += delta; return *this; }
    inline nonatomic_uint32_t &operator-=(int32_t delta) { _val -= delta; return *this; }
    inline nonatomic_uint32_t &operator|=(uint32_t mask) { _val |= mask; return *this; }
    inline nonatomic_uint32_t &operator&=(uint32_t mask) { _val &= mask; return *this; }

    inline void nonatomic_inc() { ++_val; }
    inline void operator++() { ++_val; }
    inline void operator++(int) { ++_val; }
    inline void nonatomic_dec() { --_val; }
    inline void operator--() { --_val; }
    inline void operator--(int) { --_val; }

    inline uint32_t swap(uint32_t desired) { uint32_t old = _val; _val = desired; return old; }
    inline uint32_t fetch_and_add(uint32_t delta) { uint32_t old = _val; _val += delta; return old; }
    inline bool dec_and_test() { return --_val == 0; }
    inline bool nonatomic_dec_and_test() { return --_val == 0; }
    inline uint32_t compare_swap(uint32_t expected, uint32_t desired) { uint32_t old = _val; if (_val == expected) _val = desired; return old; }
    inline bool compare_and_swap(uint32_t expected, uint32_t desired) { if (_val == expected) { _val = desired; return true; } return false; }

    inline static uint32_t swap(volatile uint32_t &x, uint32_t desired) { uint32_t old = x; x = desired; return old; }
    inline static void inc(volatile uint32_t &x) { ++x; }
    inline static bool dec_and_test(volatile uint32_t &x) { return --x == 0; }
    inline static uint32_t compare_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired) { uint32_t old = x; if (x == expected) x = desired; return old; }
    inline static bool compare_and_swap(volatile uint32_t &x, uint32_t expected, uint32_t desired) { if (x == expected) { x = desired; return true; } return false; }

    inline static bool use_builtins() { return false; }
  private:

    uint32_t _val;
};

CLICK_ENDDECLS
#endif
