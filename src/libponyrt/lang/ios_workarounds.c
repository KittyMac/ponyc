#include <platform.h>
#include <stdio.h>

PONY_EXTERN_C_BEGIN

// int snprintf(char * restrict str, size_t size, const char * restrict format, ...);
int snprintf_Double(char * restrict str, size_t size, const char * restrict format, double f) {
	return snprintf(str, size, format, f);
}

PONY_EXTERN_C_END
