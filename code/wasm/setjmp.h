#include <stddef.h>

typedef void * jmp_buf;

void Sys_longjmp(jmp_buf env, int val);
#define longjmp(p, i) Sys_longjmp(p, i)
int Sys_setjmp(jmp_buf env);
#define setjmp(p) Sys_setjmp(p)
