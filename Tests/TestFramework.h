#pragma once

// Minimal hand-rolled test framework (no third-party dependency, per the
// Native-First rule). TEST(name) registers a test; CHECK(cond) records a
// failure with file/line. The Tests executable returns non-zero if any check
// fails, so CTest / CI reports the run as failed.

#include <vector>

struct TestCase
{
  const char* name;
  void (*fn)();
};

std::vector<TestCase>& AllTests();
int RegisterTest(const char* _name, void (*_fn)());
void Check(bool _ok, const char* _expr, const char* _file, int _line);

extern int g_failures;
extern int g_checks;

#define TEST(testname)                                                          \
  static void testname();                                                       \
  static const int g_reg_##testname = RegisterTest(#testname, &testname);       \
  static void testname()

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
