#include <functional>
#include "SimpleIrqTest.h"

typedef std::function<CTest*()> TestFactoryFunction;

// clang-format off
static const TestFactoryFunction s_factories[] =
{
	[]() { return new CSimpleIrqTest(); },
};
// clang-format on

int main(int argc, const char** argv)
{
	for(const auto& factory : s_factories)
	{
		auto test = factory();
		test->Execute();
		delete test;
	}
	return 0;
}