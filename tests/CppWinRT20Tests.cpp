
// Prior to C++/WinRT 2.0 this would cause issues since we're not including wil/cppwinrt.h in this translation unit.
// However, since we're going to link into the same executable as 'CppWinRTTests.cpp', the 'winrt_to_hresult_handler'
// global function pointer should be set, so these should all run successfully
#include "pch.h"

#include <inspectable.h> // Must be included before base.h

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <wil/chrono.h>
#include <wil/cppwinrt_helpers.h>
#include <wil/result.h>

#include "common.h"

TEST_CASE("CppWinRTTests::CppWinRT20Test", "[cppwinrt]")
{
    auto test = [](HRESULT hr) {
        try
        {
            THROW_HR(hr);
        }
        catch (...)
        {
            REQUIRE(hr == winrt::to_hresult());
        }
    };

    test(E_OUTOFMEMORY);
    test(E_INVALIDARG);
    test(E_UNEXPECTED);
}

TEST_CASE("CppWinRTTests::ChronoInterop", "[cppwinrt][chrono]")
{
    const auto time = wil::clock::time_point{wil::clock::duration{116444736000000000LL}};
    const auto dateTime = wil::to_winrt_datetime(time);
    REQUIRE(wil::from_winrt_datetime(dateTime) == time);

    const auto duration = wil::clock::duration{1234567};
    REQUIRE(wil::from_winrt_timespan(wil::to_winrt_timespan(duration)) == duration);

    const wil::file_time fileTime{0xfedcba9876543210ULL};
    REQUIRE(wil::from_winrt_file_time(wil::to_winrt_file_time(fileTime)).value == fileTime.value);
}
