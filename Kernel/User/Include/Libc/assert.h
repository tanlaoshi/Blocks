/*
 * assert.h — C99 assert（hosted-stdlib-misc）
 */
#ifndef ASSERT_H
#define ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
#define assert(Ignore) ((void)0)
#else
#define assert(Expression) \
    ((Expression) ? (void)0 : \
     (fprintf(stderr, "Assertion failed: %s (%s:%d)\n", \
              #Expression, __FILE__, __LINE__), abort()))
#endif

#endif
