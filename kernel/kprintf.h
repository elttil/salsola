#include <stdarg.h>
#include <sb.h>

int kprintf(const char *format, ...);
int vkprintf(const char *format, va_list ap);
int vksbprintf(struct sb *ctx, const char *format, va_list ap);
int ksbprintf(struct sb *ctx, const char *format, ...);
