/*
 * Copyright (C) 2026 Raphaël Bély
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* A test harness small enough to read in one sitting, with no dependency
 * beyond libc — in keeping with the rest of the project. */

#ifndef GOODIX_TEST_H
#define GOODIX_TEST_H

#include <math.h>
#include <stdio.h>
#include <string.h>

static int         test_checks;
static int         test_failures;
static const char *test_current = "";

#define TEST_RUN(fn)      \
    do {                  \
        test_current = #fn; \
        fn ();            \
    } while (0)

#define TEST_FAIL(fmt, ...)                                              \
    do {                                                                 \
        test_failures++;                                                 \
        printf ("  FAIL  %-30s " fmt "   (%s:%d)\n", test_current,       \
                __VA_ARGS__, __FILE__, __LINE__);                        \
    } while (0)

#define TEST_TRUE(cond)                 \
    do {                                \
        test_checks++;                  \
        if (!(cond))                    \
            TEST_FAIL ("%s", #cond);    \
    } while (0)

#define TEST_EQ(got, want)                                               \
    do {                                                                 \
        long long got_ = (long long) (got);                              \
        long long want_ = (long long) (want);                            \
        test_checks++;                                                   \
        if (got_ != want_)                                               \
            TEST_FAIL ("%s: got %lld, want %lld", #got, got_, want_);    \
    } while (0)

#define TEST_NEAR(got, want, tolerance)                                  \
    do {                                                                 \
        double got_ = (got);                                             \
        double want_ = (want);                                           \
        test_checks++;                                                   \
        if (fabs (got_ - want_) > (tolerance))                           \
            TEST_FAIL ("%s: got %.4f, want %.4f", #got, got_, want_);    \
    } while (0)

#define TEST_MEM(got, want, len)                                         \
    do {                                                                 \
        test_checks++;                                                   \
        if (memcmp ((got), (want), (len)) != 0)                          \
            TEST_FAIL ("%s differs over %d bytes", #got, (int) (len));   \
    } while (0)

#define TEST_SUMMARY(name)                                               \
    do {                                                                 \
        printf ("%-14s %3d checks, %d failed\n", name, test_checks,      \
                test_failures);                                          \
        return test_failures == 0 ? 0 : 1;                               \
    } while (0)

#endif /* GOODIX_TEST_H */
