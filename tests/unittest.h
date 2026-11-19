// Minimal unit test helpers. Each test executable defines test functions
// and runs them with RUN_TEST from main(), returning TEST_RESULT().
#ifndef UNITTEST_H
#define UNITTEST_H

#include <stdio.h>
#include <string.h>

static int unittest_failures = 0;
static int unittest_checks = 0;

#define CHECK(cond) do { \
    unittest_checks++; \
    if (!(cond)) { \
      unittest_failures++; \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    } \
  } while (0)

#define CHECK_EQ_INT(expected, actual) do { \
    long long e_ = (long long)(expected), a_ = (long long)(actual); \
    unittest_checks++; \
    if (e_ != a_) { \
      unittest_failures++; \
      fprintf(stderr, "%s:%d: %s == %s failed: expected %lld, got %lld\n", \
              __FILE__, __LINE__, #expected, #actual, e_, a_); \
    } \
  } while (0)

#define CHECK_EQ_STR(expected, actual) do { \
    const char *e_ = (expected), *a_ = (actual); \
    unittest_checks++; \
    if (!e_ || !a_ || strcmp(e_, a_) != 0) { \
      unittest_failures++; \
      fprintf(stderr, "%s:%d: %s == %s failed:\n  expected \"%s\"\n  got      \"%s\"\n", \
              __FILE__, __LINE__, #expected, #actual, \
              e_ ? e_ : "(null)", a_ ? a_ : "(null)"); \
    } \
  } while (0)

#define RUN_TEST(fn) do { \
    int before_ = unittest_failures; \
    fn(); \
    printf("%s %s\n", unittest_failures == before_ ? "PASS" : "FAIL", #fn); \
  } while (0)

#define TEST_RESULT() \
  (printf("%d checks, %d failures\n", unittest_checks, unittest_failures), \
   unittest_failures ? 1 : 0)

#endif
