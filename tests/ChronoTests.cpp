#include "pch.h"

#include <wil/chrono.h>
#include <wil/resource.h>

#include <cmath>
#include <limits>

#include "common.h"

using namespace std::chrono_literals;

static_assert(std::is_same_v<wil::clock::rep, std::int64_t>);
static_assert(std::ratio_equal_v<wil::clock::period, std::ratio<1, 10000000>>);
static_assert(!wil::clock::is_steady);
static_assert(wil::to_float_ms(1500us) == 1.5f);

TEST_CASE("ChronoTests::FileTimeRoundTrip", "[chrono]")
{
    const FILETIME values[] = {
        {0, 0},
        {0xffffffff, 0x7fffffff},
        {0, 0x80000000},
        {0xffffffff, 0xffffffff},
    };

    for (const auto& value : values)
    {
        const wil::file_time converted{value};
        const auto roundTrip = converted.to_FILETIME();
        REQUIRE(roundTrip.dwLowDateTime == value.dwLowDateTime);
        REQUIRE(roundTrip.dwHighDateTime == value.dwHighDateTime);
    }
}

TEST_CASE("ChronoTests::DwordMilliseconds", "[chrono]")
{
    DWORD value{};

    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(0ns, &value));
    REQUIRE(value == 0);
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(1ns, &value));
    REQUIRE(value == 1);
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(1001us, &value));
    REQUIRE(value == 2);
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(2s, &value));
    REQUIRE(value == 2000);
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(std::chrono::duration<double, std::milli>{1.25}, &value));
    REQUIRE(value == 2);
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(std::chrono::milliseconds{INFINITE - 1}, &value));
    REQUIRE(value == INFINITE - 1);

    REQUIRE(wil::try_to_dword_ms(-1ms, &value) == E_INVALIDARG);
    REQUIRE(wil::try_to_dword_ms(std::chrono::duration<double>{std::numeric_limits<double>::quiet_NaN()}, &value) == E_INVALIDARG);
    REQUIRE(
        wil::try_to_dword_ms(std::chrono::milliseconds{static_cast<std::int64_t>(INFINITE)}, &value) ==
        HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
}

TEST_CASE("ChronoTests::FloatMilliseconds", "[chrono]")
{
    REQUIRE(wil::to_float_ms(1ns) == Catch::Approx(0.000001f));
    REQUIRE(wil::to_float_ms(1500us) == Catch::Approx(1.5f));
    REQUIRE(wil::to_float_ms(-2s) == Catch::Approx(-2000.0f));
    REQUIRE(std::isinf(wil::to_float_ms(std::chrono::duration<double>{std::numeric_limits<double>::infinity()})));
}

TEST_CASE("ChronoTests::RelativeFileTime", "[chrono]")
{
    FILETIME value{};

    REQUIRE_SUCCEEDED(wil::try_to_relative_file_time(0ns, &value));
    REQUIRE(wil::file_time{value}.value == 0);

    REQUIRE_SUCCEEDED(wil::try_to_relative_file_time(1ns, &value));
    REQUIRE(wil::file_time{value}.value == static_cast<std::uint64_t>(-1LL));

    REQUIRE_SUCCEEDED(wil::try_to_relative_file_time(1ms, &value));
    REQUIRE(wil::file_time{value}.value == static_cast<std::uint64_t>(-10000LL));

    REQUIRE_SUCCEEDED(wil::try_to_relative_file_time(wil::clock::duration{(std::numeric_limits<std::int64_t>::max)()}, &value));
    REQUIRE(wil::file_time{value}.value == 0x8000000000000001ULL);

    REQUIRE(wil::try_to_relative_file_time(-1ns, &value) == E_INVALIDARG);
}

TEST_CASE("ChronoTests::ClockEpochAndSystemTime", "[chrono]")
{
    const auto unixEpoch = wil::clock::from_sys(std::chrono::system_clock::time_point{});
    REQUIRE(unixEpoch.time_since_epoch().count() == 116444736000000000LL);
    REQUIRE(wil::clock::to_sys(unixEpoch) == std::chrono::system_clock::time_point{});

    FILETIME unixEpochFileTime{};
    REQUIRE_SUCCEEDED(wil::try_to_file_time(std::chrono::system_clock::time_point{}, &unixEpochFileTime));
    REQUIRE(wil::file_time{unixEpochFileTime}.value == 116444736000000000ULL);

    const SYSTEMTIME source{2024, 2, 0, 29, 12, 34, 56, 789};
    wil::clock::time_point time{};
    REQUIRE_SUCCEEDED(wil::try_from_system_time(source, &time));

    SYSTEMTIME roundTrip{};
    REQUIRE_SUCCEEDED(wil::try_to_system_time(time, &roundTrip));
    REQUIRE(roundTrip.wYear == source.wYear);
    REQUIRE(roundTrip.wMonth == source.wMonth);
    REQUIRE(roundTrip.wDay == source.wDay);
    REQUIRE(roundTrip.wHour == source.wHour);
    REQUIRE(roundTrip.wMinute == source.wMinute);
    REQUIRE(roundTrip.wSecond == source.wSecond);
    REQUIRE(roundTrip.wMilliseconds == source.wMilliseconds);

    const SYSTEMTIME invalid{};
    REQUIRE_FAILED(wil::try_from_system_time(invalid, &time));
}

TEST_CASE("ChronoTests::ChronoEventWait", "[chrono]")
{
    wil::unique_event_nothrow event;
    REQUIRE_SUCCEEDED(event.create());
    REQUIRE(!event.wait(1ms));
    event.SetEvent();
    REQUIRE(event.wait(1s));
}

namespace
{
void CALLBACK SetEventTimerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
{
    ::SetEvent(static_cast<HANDLE>(context));
}
} // namespace

TEST_CASE("ChronoTests::RelativeThreadpoolTimer", "[chrono]")
{
    wil::unique_event_nothrow event;
    REQUIRE_SUCCEEDED(event.create(wil::EventOptions::ManualReset));

    wil::unique_threadpool_timer timer{::CreateThreadpoolTimer(SetEventTimerCallback, event.get(), nullptr)};
    REQUIRE(timer);

    REQUIRE_SUCCEEDED(wil::set_relative_threadpool_timer_nothrow(timer.get(), 1ms));
    REQUIRE(event.wait(5s));

    REQUIRE(wil::set_relative_threadpool_timer_nothrow(timer.get(), -1ms) == E_INVALIDARG);

    event.ResetEvent();
    REQUIRE_SUCCEEDED(wil::set_threadpool_timer_nothrow(timer.get(), std::chrono::system_clock::now() + 1ms));
    REQUIRE(event.wait(5s));
}
