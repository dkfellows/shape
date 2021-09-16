#ifndef PANIC_H
#define PANIC_H

#ifdef __GNUC__
EXTERN void	panic(char *message, ...) __attribute__((__noreturn__ , __format__(printf,1,2)));
#else
EXTERN void	panic(char *message, ...);
#endif

#endif
