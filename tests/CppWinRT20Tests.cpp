
// Prior to C++/WinRT 2.0 this would cause issues since we're not including wil/cppwinrt.h in this translation unit.
// However, since we're going to link into the same executable as 'CppWinRTTests.cpp', the 'winrt_to_hresult_handler'
// global function pointer should be set, so these should all run successfully
#include "pch.h"

#include <inspectable.h> // Must be included before base.h

#include <wil/chrono.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <wil/chrono.h>
#include <wil/cppwinrt_helpers.h>
#include <wil/resource.h>
#include <wil/result.h>

#include <format>

#include "common.h"

using namespace std::chrono_literals;

static void CALLBACK SetCppWinRTEventTimerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
{
    ::SetEvent(static_cast<HANDLE>(context));
}

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
    constexpr std::int64_t c_unixEpochOffsetInFileTimeTicks = 116444736000000000LL;

    const auto time = winrt::clock::from_sys(std::chrono::system_clock::time_point{});
    REQUIRE(time.time_since_epoch().count() == c_unixEpochOffsetInFileTimeTicks);
    REQUIRE(winrt::clock::to_sys(time) == std::chrono::system_clock::time_point{});
    REQUIRE(std::format("{:%Y-%m-%d %H:%M:%S}", winrt::clock::to_sys(time)) == "1970-01-01 00:00:00.0000000");

    FILETIME fileTime{};
    REQUIRE_SUCCEEDED(wil::try_to_file_time(time, &fileTime));
    REQUIRE(winrt::file_time{fileTime}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks));
    REQUIRE_SUCCEEDED(wil::try_to_file_time(std::chrono::system_clock::time_point{}, &fileTime));
    REQUIRE(winrt::file_time{fileTime}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks));

    const SYSTEMTIME source{2024, 2, 0, 29, 12, 34, 56, 789};
    winrt::clock::time_point systemTime{};
    REQUIRE_SUCCEEDED(wil::try_from_system_time(source, &systemTime));

    SYSTEMTIME roundTrip{};
    REQUIRE_SUCCEEDED(wil::try_to_system_time(systemTime, &roundTrip));
    REQUIRE(roundTrip.wYear == source.wYear);
    REQUIRE(roundTrip.wMonth == source.wMonth);
    REQUIRE(roundTrip.wDay == source.wDay);
    REQUIRE(roundTrip.wHour == source.wHour);
    REQUIRE(roundTrip.wMinute == source.wMinute);
    REQUIRE(roundTrip.wSecond == source.wSecond);
    REQUIRE(roundTrip.wMilliseconds == source.wMilliseconds);

    wil::unique_event_nothrow event;
    REQUIRE_SUCCEEDED(event.create(wil::EventOptions::ManualReset));
    wil::unique_threadpool_timer timer{::CreateThreadpoolTimer(SetCppWinRTEventTimerCallback, event.get(), nullptr)};
    REQUIRE(timer);
    REQUIRE_SUCCEEDED(wil::set_threadpool_timer_nothrow(timer.get(), std::chrono::system_clock::now() + 1ms));
    REQUIRE(event.wait(5s));
}
