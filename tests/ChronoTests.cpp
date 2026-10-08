#include "pch.h"

#include <wil/chrono.h>
#include <wil/resource.h>

#include <limits>

#include "common.h"

using namespace std::chrono_literals;

using floating_milliseconds = std::chrono::duration<double, std::milli>;
using third_seconds = std::chrono::duration<std::int64_t, std::ratio<1, 3>>;

template <typename Duration, typename = void>
struct can_try_to_dword_ms : std::false_type
{
};

template <typename Duration>
struct can_try_to_dword_ms<Duration, std::void_t<decltype(wil::try_to_dword_ms(std::declval<Duration>(), static_cast<DWORD*>(nullptr)))>>
    : std::true_type
{
};

template <typename Duration, typename = void>
struct can_to_float_ms : std::false_type
{
};

template <typename Duration>
struct can_to_float_ms<Duration, std::void_t<decltype(wil::to_float_ms(std::declval<Duration>()))>> : std::true_type
{
};

template <typename Duration, typename = void>
struct can_try_to_relative_file_time : std::false_type
{
};

template <typename Duration>
struct can_try_to_relative_file_time<Duration, std::void_t<decltype(wil::try_to_relative_file_time(std::declval<Duration>(), static_cast<FILETIME*>(nullptr)))>>
    : std::true_type
{
};

template <typename TimePoint, typename = void>
struct can_try_to_file_time : std::false_type
{
};

template <typename TimePoint>
struct can_try_to_file_time<TimePoint, std::void_t<decltype(wil::try_to_file_time(std::declval<TimePoint>(), static_cast<FILETIME*>(nullptr)))>>
    : std::true_type
{
};

static_assert(std::is_same_v<wil::clock::rep, std::int64_t>);
static_assert(std::ratio_equal_v<wil::clock::period, std::ratio<1, 10000000>>);
static_assert(!wil::clock::is_steady);
static_assert(sizeof(wil::file_time) == sizeof(FILETIME));
static_assert(std::is_trivially_copyable_v<wil::file_time>);
static_assert(wil::to_float_ms(1500us) == 1.5f);
static_assert(can_try_to_dword_ms<std::chrono::nanoseconds>::value);
static_assert(can_to_float_ms<std::chrono::seconds>::value);
static_assert(can_try_to_relative_file_time<std::chrono::nanoseconds>::value);
static_assert(!can_try_to_dword_ms<floating_milliseconds>::value);
static_assert(!can_to_float_ms<floating_milliseconds>::value);
static_assert(!can_try_to_relative_file_time<floating_milliseconds>::value);
static_assert(!can_try_to_dword_ms<third_seconds>::value);
static_assert(!can_to_float_ms<third_seconds>::value);
static_assert(!can_try_to_file_time<std::chrono::time_point<std::chrono::system_clock, floating_milliseconds>>::value);
static_assert(!can_try_to_file_time<std::chrono::time_point<std::chrono::system_clock, third_seconds>>::value);

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
        REQUIRE(converted.value == wil::filetime::to_int64<std::uint64_t>(value));

        const auto roundTrip = converted.to_FILETIME();
        REQUIRE(roundTrip.dwLowDateTime == value.dwLowDateTime);
        REQUIRE(roundTrip.dwHighDateTime == value.dwHighDateTime);
    }
}

TEST_CASE("ChronoTests::FileTimeHelpers", "[chrono]")
{
    const auto signedMinusOne = wil::filetime::from_int64(-1LL);
    REQUIRE(signedMinusOne.dwLowDateTime == 0xffffffff);
    REQUIRE(signedMinusOne.dwHighDateTime == 0xffffffff);
    REQUIRE(wil::filetime::to_int64<std::int64_t>(signedMinusOne) == -1);

    const FILETIME lowWordMaximum{0xffffffff, 0};
    const auto carryToHighWord = wil::filetime::add(lowWordMaximum, 1);
    REQUIRE(carryToHighWord.dwLowDateTime == 0);
    REQUIRE(carryToHighWord.dwHighDateTime == 1);

    const FILETIME highWordOne{0, 1};
    const auto borrowFromHighWord = wil::filetime::add(highWordOne, -1);
    REQUIRE(borrowFromHighWord.dwLowDateTime == 0xffffffff);
    REQUIRE(borrowFromHighWord.dwHighDateTime == 0);

    REQUIRE(wil::filetime::is_empty(FILETIME{}));
    REQUIRE(!wil::filetime::is_empty(wil::filetime::from_int64(1)));
    REQUIRE(wil::filetime::convert_msec_to_100ns(1) == 10000);
    REQUIRE(wil::filetime::convert_100ns_to_msec(19999) == 1);
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
    REQUIRE_SUCCEEDED(wil::try_to_dword_ms(std::chrono::milliseconds{INFINITE - 1}, &value));
    REQUIRE(value == INFINITE - 1);

    REQUIRE(wil::try_to_dword_ms(-1ms, &value) == E_INVALIDARG);
    REQUIRE(
        wil::try_to_dword_ms(std::chrono::milliseconds{static_cast<std::int64_t>(INFINITE)}, &value) ==
        HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
}

TEST_CASE("ChronoTests::FloatMilliseconds", "[chrono]")
{
    REQUIRE(wil::to_float_ms(1ns) == Catch::Approx(0.000001f));
    REQUIRE(wil::to_float_ms(1500us) == Catch::Approx(1.5f));
    REQUIRE(wil::to_float_ms(-2s) == Catch::Approx(-2000.0f));
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

    // INT64_MAX 100-nanosecond ticks is the largest supported relative interval. Its negative two's-complement
    // FILETIME representation is 0x8000000000000001.
    REQUIRE_SUCCEEDED(wil::try_to_relative_file_time(wil::clock::duration{(std::numeric_limits<std::int64_t>::max)()}, &value));
    REQUIRE(wil::file_time{value}.value == 0x8000000000000001ULL);

    REQUIRE(wil::try_to_relative_file_time(-1ns, &value) == E_INVALIDARG);
    using unsigned_file_time_duration = std::chrono::duration<std::uint64_t, wil::file_time_period>;
    REQUIRE(
        wil::try_to_relative_file_time(
            unsigned_file_time_duration{static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) + 1}, &value) ==
        HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
}

TEST_CASE("ChronoTests::ClockEpochAndSystemTime", "[chrono]")
{
    // FILETIME counts 100-nanosecond ticks from January 1, 1601 UTC. The Unix epoch is January 1, 1970 UTC.
    constexpr std::int64_t c_unixEpochOffsetInFileTimeTicks = 116444736000000000LL;

    const auto unixEpoch = wil::clock::from_sys(std::chrono::system_clock::time_point{});
    REQUIRE(unixEpoch.time_since_epoch().count() == c_unixEpochOffsetInFileTimeTicks);
    REQUIRE(wil::clock::to_sys(unixEpoch) == std::chrono::system_clock::time_point{});

    FILETIME unixEpochFileTime{};
    REQUIRE_SUCCEEDED(wil::try_to_file_time(std::chrono::system_clock::time_point{}, &unixEpochFileTime));
    REQUIRE(wil::file_time{unixEpochFileTime}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks));

    using system_file_time_point = std::chrono::time_point<std::chrono::system_clock, wil::clock::duration>;

    FILETIME adjacentToUnixEpoch{};
    REQUIRE_SUCCEEDED(wil::try_to_file_time(system_file_time_point{wil::clock::duration{1}}, &adjacentToUnixEpoch));
    REQUIRE(wil::file_time{adjacentToUnixEpoch}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks + 1));
    REQUIRE_SUCCEEDED(wil::try_to_file_time(system_file_time_point{wil::clock::duration{-1}}, &adjacentToUnixEpoch));
    REQUIRE(wil::file_time{adjacentToUnixEpoch}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks - 1));

    using system_nanosecond_time_point = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
    FILETIME fractionalSecond{};
    REQUIRE_SUCCEEDED(wil::try_to_file_time(system_nanosecond_time_point{1ns}, &fractionalSecond));
    REQUIRE(wil::file_time{fractionalSecond}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks));
    REQUIRE_SUCCEEDED(wil::try_to_file_time(system_nanosecond_time_point{-1ns}, &fractionalSecond));
    REQUIRE(wil::file_time{fractionalSecond}.value == static_cast<std::uint64_t>(c_unixEpochOffsetInFileTimeTicks - 1));

    FILETIME fileTimeEpoch{};
    REQUIRE_SUCCEEDED(
        wil::try_to_file_time(system_file_time_point{wil::clock::duration{-c_unixEpochOffsetInFileTimeTicks}}, &fileTimeEpoch));
    REQUIRE(wil::file_time{fileTimeEpoch}.value == 0);
    REQUIRE(
        wil::try_to_file_time(system_file_time_point{wil::clock::duration{-c_unixEpochOffsetInFileTimeTicks - 1}}, &fileTimeEpoch) ==
        E_INVALIDARG);

    // Preserve low 100-nanosecond tick bits in a representative modern timestamp.
    constexpr std::int64_t c_modernFileTimeTicks = 133485408001234567LL;
    FILETIME modernFileTime{};
    REQUIRE_SUCCEEDED(
        wil::try_to_file_time(
            system_file_time_point{wil::clock::duration{c_modernFileTimeTicks - c_unixEpochOffsetInFileTimeTicks}}, &modernFileTime));
    REQUIRE(wil::file_time{modernFileTime}.value == static_cast<std::uint64_t>(c_modernFileTimeTicks));

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

static void CALLBACK SetEventTimerCallback(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER)
{
    ::SetEvent(static_cast<HANDLE>(context));
}

TEST_CASE("ChronoTests::RelativeThreadpoolTimer", "[chrono]")
{
    wil::unique_event_nothrow event;
    REQUIRE_SUCCEEDED(event.create(wil::EventOptions::ManualReset));

    wil::unique_threadpool_timer timer{::CreateThreadpoolTimer(SetEventTimerCallback, event.get(), nullptr)};
    REQUIRE(timer);

    REQUIRE_SUCCEEDED(wil::set_relative_threadpool_timer_nothrow(timer.get(), 1ms));
    REQUIRE(event.wait(5s));

    REQUIRE(wil::set_relative_threadpool_timer_nothrow(timer.get(), -1ms) == E_INVALIDARG);

    // SetThreadpoolTimer treats any nonzero DWORD period/window as a value; INFINITE is not reserved for these parameters.
    REQUIRE_SUCCEEDED(
        wil::set_relative_threadpool_timer_nothrow(
            timer.get(),
            1h,
            std::chrono::milliseconds{static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)())},
            std::chrono::milliseconds{static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)())}));

    event.ResetEvent();
    REQUIRE_SUCCEEDED(wil::set_threadpool_timer_nothrow(timer.get(), std::chrono::system_clock::now() + 1ms));
    REQUIRE(event.wait(5s));
}
