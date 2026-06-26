#include "pch.h"

using namespace winrt;
using namespace Windows::Foundation;

static uint32_t g_value = 12;

fire_and_forget SayHello()
{
	co_await resume_background();

  g_value = 34;
}

int main()
{
	printf("Starting DSOServer...\n");
	CoreEngine::Startup();
	SayHello();

  bool stop = false;
	while (!stop)
	{
	  Timer::Core::Update();
		if (Timer::Core::GetTotalSeconds() > 10)
      stop = true;
	}

  printf("g_value = %d\n", g_value);
}
