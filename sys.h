#ifndef _SYS_H_
#define _SYS_H_

#define POWEROF2(x) ((((x) - 1) & (x)) == 0)
#define __cache_line_size 64
#define __cache_line_mask (__cache_line_size - 1)
#define __cache_aligned  __attribute__((aligned(__cache_line_size)))

#endif
