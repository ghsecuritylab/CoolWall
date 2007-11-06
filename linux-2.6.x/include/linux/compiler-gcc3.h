/* Never include this file directly.  Include <linux/compiler.h> instead.  */

/* These definitions are for GCC v3.x.  */
#include <linux/compiler-gcc.h>

#if __GNUC_MINOR__ >= 3
# define __used			__attribute__((__used__))
# define __attribute_used__	__used				/* deprecated */
#else
# define __used			__attribute__((__unused__))
# define __attribute_used__	__used				/* deprecated */
#endif

#if __GNUC_MINOR__ >= 4
#define __must_check		__attribute__((warn_unused_result))
#endif

/*
 * A trick to suppress uninitialized variable warning without generating any
 * code
 */
#define uninitialized_var(x) x = x

#define __always_inline		inline __attribute__((always_inline))

#if __GNUC_MINOR__ <= 0
#undef __always_inline
#define __always_inline		inline
#undef inline
#undef __inline__
#undef __inline
#undef __deprecated
#define __deprecated
#undef  noinline
#define  noinline
#endif