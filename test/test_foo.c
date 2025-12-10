#include "unity.h"

#include "foo.h"

void setUp(void) {}

void tearDown(void) {}

void test_foo_NeedToImplement(void) {
  TEST_IGNORE_MESSAGE("Need to Implement foo");
}

void test_foo_123Equal123(void) {
  TEST_ASSERT_EQUAL(123, 123);
}
