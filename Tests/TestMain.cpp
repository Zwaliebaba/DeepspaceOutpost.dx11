#include "TestFramework.h"

#include <cstdio>

int g_failures = 0;
int g_checks = 0;

std::vector<TestCase>& AllTests()
{
  static std::vector<TestCase> tests;
  return tests;
}

int RegisterTest(const char* _name, void (*_fn)())
{
  AllTests().push_back(TestCase{ _name, _fn });
  return 0;
}

void Check(bool _ok, const char* _expr, const char* _file, int _line)
{
  ++g_checks;
  if (!_ok)
  {
    ++g_failures;
    std::printf("    FAIL  %s:%d  CHECK(%s)\n", _file, _line, _expr);
  }
}

int main()
{
  int failedTests = 0;

  for (const TestCase& test : AllTests())
  {
    const int before = g_failures;
    test.fn();
    const bool ok = (g_failures == before);
    if (!ok)
      ++failedTests;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", test.name);
  }

  std::printf("\n%zu tests, %d checks, %d failed checks, %d failed tests\n",
              AllTests().size(), g_checks, g_failures, failedTests);

  return g_failures == 0 ? 0 : 1;
}
