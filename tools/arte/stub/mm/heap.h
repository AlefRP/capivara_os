/* Fora do kernel, o heap é o malloc da libc. */
#pragma once
#include <stdlib.h>
#define kmalloc malloc
#define kfree free
