#pragma once

#define _CRT_SECURE_NO_WARNINGS

#pragma warning(disable:4201) // nonstandard extension used : nameless struct/union
#pragma warning(disable:4238) // nonstandard extension used : class rvalue used as lvalue
#pragma warning(disable:4244) // conversion from 'x' to 'y', possible loss of data
#pragma warning(disable:4324) // structure was padded due to __declspec(align())

#include <algorithm>
#include <array>
#include <concurrent_queue.h>
#include <concurrent_unordered_map.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <format>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <mdspan>
#include <memory>
#include <queue>
#include <ranges>
#include <set>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Use the C++ standard templated min/max
#define NOMINMAX

// DirectX apps don't need GDI
#define NODRAWTEXT
// #define NOGDI
#define NOBITMAP

// Include <mcx.h> if you need this
#define NOMCX

// Include <winsvc.h> if you need this
#define NOSERVICE

// WinHelp is deprecated
#define NOHELP

#if !defined WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include <Windows.h>
#include <hstring.h>
#include <restrictederrorinfo.h>
#include <unknwn.h>

#include <winrt/base.h>

#include "Debug.h"
#include "GameMath.h"

#include "NeuronHelper.h"

using namespace Neuron;

#include "TasksCore.h"
#include "TimerCore.h"

namespace Neuron
{
  constexpr uint32_t ENGINE_VERSION = 0x00000001;

  class CoreEngine
  {
  public:
    static void Startup();
    static void Shutdown();
  };

}

#pragma comment(lib, "Ws2_32.lib")

#pragma comment(lib, "OneCore.lib")
