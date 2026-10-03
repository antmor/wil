//*********************************************************
//
//    Copyright (c) Microsoft. All rights reserved.
//    This code is licensed under the MIT License.
//    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
//    ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
//    TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
//    PARTICULAR PURPOSE AND NONINFRINGEMENT.
//
//*********************************************************
//! @file
//! Helpers for converting between C++ chrono types and Windows time representations.
#ifndef __WIL_CHRONO_INCLUDED
#define __WIL_CHRONO_INCLUDED

#include "result_macros.h"

#if WIL_USE_STL && !defined(WIL_NO_CHRONO) && !defined(__WIL_MIN_KERNEL) && !defined(WIL_KERNEL_MODE)

#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>
#include <ratio>
#include <type_traits>
#include <utility>

namespace wil
{
using file_time_period = std::ratio<1, 10000000>;

struct file_time
{
    std::uint64_t value{};

    constexpr file_time() WI_NOEXCEPT = default;
    constexpr explicit file_time(std::uint64_t rawValue) WI_NOEXCEPT : value(rawValue)
    {
    }

    constexpr file_time(const FILETIME& rawValue) WI_NOEXCEPT
        : value(static_cast<std::uint64_t>(rawValue.dwLowDateTime) | (static_cast<std::uint64_t>(rawValue.dwHighDateTime) << 32))
    {
    }

    constexpr FILETIME to_FILETIME() const WI_NOEXCEPT
    {
        return {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
    }
};

namespace details
{
    template <typename T>
    struct is_chrono_duration : std::false_type
    {
    };

    template <typename Rep, typename Period>
    struct is_chrono_duration<std::chrono::duration<Rep, Period>> : std::true_type
    {
    };

    template <typename Duration>
    using enable_if_chrono_duration_t = std::enable_if_t<is_chrono_duration<Duration>::value, int>;

    template <typename Duration, typename TargetPeriod>
    constexpr long double duration_count_as(Duration value) WI_NOEXCEPT
    {
        return std::chrono::duration<long double, TargetPeriod>{value}.count();
    }

    constexpr FILETIME file_time_from_raw(std::uint64_t value) WI_NOEXCEPT
    {
        return {static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
    }
} // namespace details

struct clock
{
    using rep = std::int64_t;
    using period = file_time_period;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<clock>;

    static constexpr bool is_steady = false;

    static time_point now() WI_NOEXCEPT
    {
        FILETIME value{};
#if defined(_WIN32_WINNT) && (_WIN32_WINNT >= _WIN32_WINNT_WIN8)
        ::GetSystemTimePreciseAsFileTime(&value);
#else
        ::GetSystemTimeAsFileTime(&value);
#endif
        return from_file_time(file_time{value});
    }

    template <typename Duration>
    static constexpr file_time to_file_time(std::chrono::time_point<clock, Duration> value) WI_NOEXCEPT
    {
        return file_time{static_cast<std::uint64_t>(std::chrono::duration_cast<duration>(value.time_since_epoch()).count())};
    }

    static constexpr time_point from_file_time(file_time value) WI_NOEXCEPT
    {
        return time_point{duration{static_cast<rep>(value.value)}};
    }

    static std::time_t to_time_t(time_point value) WI_NOEXCEPT
    {
        return std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(to_sys(value)));
    }

    static time_point from_time_t(std::time_t value) WI_NOEXCEPT
    {
        return std::chrono::time_point_cast<duration>(from_sys(std::chrono::system_clock::from_time_t(value)));
    }

    //! Converts to a standard system clock value. C++20 callers can format the result with std::format.
    template <typename Duration>
    static constexpr std::chrono::time_point<std::chrono::system_clock, std::common_type_t<Duration, std::chrono::seconds>> to_sys(
        std::chrono::time_point<clock, Duration> value) WI_NOEXCEPT
    {
        return epoch() + value.time_since_epoch();
    }

    template <typename Duration>
    static constexpr std::chrono::time_point<clock, std::common_type_t<Duration, std::chrono::seconds>> from_sys(
        std::chrono::time_point<std::chrono::system_clock, Duration> value) WI_NOEXCEPT
    {
        using result_type = std::chrono::time_point<clock, std::common_type_t<Duration, std::chrono::seconds>>;
        return result_type{value - epoch()};
    }

private:
    static constexpr std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds> epoch() WI_NOEXCEPT
    {
        return std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>{std::chrono::seconds{-11644473600LL}};
    }
};

//! Converts any chrono duration to fractional milliseconds for measurement and telemetry.
template <typename Rep, typename Period>
constexpr float to_float_ms(std::chrono::duration<Rep, Period> value) WI_NOEXCEPT
{
    return std::chrono::duration<float, std::milli>{value}.count();
}

//! Converts any chrono duration to a finite Win32 millisecond timeout, rounding positive fractions upward.
template <typename Rep, typename Period>
HRESULT try_to_dword_ms(std::chrono::duration<Rep, Period> value, DWORD* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    const auto milliseconds = details::duration_count_as<std::chrono::duration<Rep, Period>, std::milli>(value);
    if (!(milliseconds >= 0))
    {
        return E_INVALIDARG;
    }

    constexpr auto maximum = static_cast<long double>(INFINITE - 1);
    if (milliseconds > maximum)
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }

    auto converted = static_cast<DWORD>(milliseconds);
    if (static_cast<long double>(converted) < milliseconds)
    {
        ++converted;
    }

    *result = converted;
    return S_OK;
}

template <typename Rep, typename Period>
DWORD to_dword_ms_failfast(std::chrono::duration<Rep, Period> value) WI_NOEXCEPT
{
    DWORD result{};
    FAIL_FAST_IF_FAILED(try_to_dword_ms(value, &result));
    return result;
}

#if defined(WIL_ENABLE_EXCEPTIONS)
template <typename Rep, typename Period>
DWORD to_dword_ms(std::chrono::duration<Rep, Period> value)
{
    DWORD result{};
    THROW_IF_FAILED(try_to_dword_ms(value, &result));
    return result;
}
#endif

//! Encodes a nonnegative duration as the signed relative FILETIME representation used by threadpool timers.
template <typename Rep, typename Period>
HRESULT try_to_relative_file_time(std::chrono::duration<Rep, Period> value, FILETIME* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    std::int64_t roundedTicks{};
    if constexpr (std::ratio_equal_v<Period, file_time_period> && std::is_integral_v<Rep>)
    {
        if constexpr (std::is_signed_v<Rep>)
        {
            if (value.count() < 0)
            {
                return E_INVALIDARG;
            }
        }

        if (static_cast<std::uintmax_t>(value.count()) > static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
        roundedTicks = static_cast<std::int64_t>(value.count());
    }
    else
    {
        const auto ticks = details::duration_count_as<std::chrono::duration<Rep, Period>, file_time_period>(value);
        if (!(ticks >= 0))
        {
            return E_INVALIDARG;
        }

        constexpr auto exclusiveMaximum = 9223372036854775808.0L;
        if (ticks >= exclusiveMaximum)
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }

        roundedTicks = static_cast<std::int64_t>(ticks);
        if (static_cast<long double>(roundedTicks) < ticks)
        {
            ++roundedTicks;
        }
    }

    const auto encoded = roundedTicks == 0 ? 0ULL : static_cast<std::uint64_t>(-roundedTicks);
    *result = details::file_time_from_raw(encoded);
    return S_OK;
}

template <typename Duration>
HRESULT try_to_file_time(std::chrono::time_point<clock, Duration> value, FILETIME* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    if constexpr (std::ratio_equal_v<typename Duration::period, file_time_period> && std::is_integral_v<typename Duration::rep>)
    {
        const auto count = value.time_since_epoch().count();
        if constexpr (std::is_signed_v<typename Duration::rep>)
        {
            if (count < 0)
            {
                return E_INVALIDARG;
            }
        }

        if (static_cast<std::uintmax_t>(count) > static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)()))
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
        *result = details::file_time_from_raw(static_cast<std::uint64_t>(count));
    }
    else
    {
        const auto ticks = details::duration_count_as<Duration, file_time_period>(value.time_since_epoch());
        if (!(ticks >= 0))
        {
            return E_INVALIDARG;
        }

        constexpr auto exclusiveMaximum = 9223372036854775808.0L;
        if (ticks >= exclusiveMaximum)
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
        *result = details::file_time_from_raw(static_cast<std::uint64_t>(ticks));
    }
    return S_OK;
}

template <typename Duration>
HRESULT try_to_file_time(std::chrono::time_point<std::chrono::system_clock, Duration> value, FILETIME* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    constexpr auto unixEpochOffset = 116444736000000000.0L;
    const auto ticksSinceUnixEpoch = details::duration_count_as<Duration, file_time_period>(value.time_since_epoch());
    const auto ticksSinceWindowsEpoch = ticksSinceUnixEpoch + unixEpochOffset;
    if (!(ticksSinceWindowsEpoch >= 0))
    {
        return E_INVALIDARG;
    }

    constexpr auto maximum = static_cast<long double>((std::numeric_limits<std::int64_t>::max)());
    if (ticksSinceWindowsEpoch > maximum)
    {
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    }

    *result = details::file_time_from_raw(static_cast<std::uint64_t>(ticksSinceWindowsEpoch));
    return S_OK;
}

inline HRESULT try_to_system_time(clock::time_point value, SYSTEMTIME* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    FILETIME fileTime{};
    RETURN_IF_FAILED(try_to_file_time(value, &fileTime));
    return ::FileTimeToSystemTime(&fileTime, result) ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
}

inline HRESULT try_from_system_time(const SYSTEMTIME& value, clock::time_point* result) WI_NOEXCEPT
{
    if (result == nullptr)
    {
        return E_POINTER;
    }

    FILETIME fileTime{};
    if (!::SystemTimeToFileTime(&value, &fileTime))
    {
        return HRESULT_FROM_WIN32(::GetLastError());
    }

    *result = clock::from_file_time(file_time{fileTime});
    return S_OK;
}

inline SYSTEMTIME to_system_time_failfast(clock::time_point value) WI_NOEXCEPT
{
    SYSTEMTIME result{};
    FAIL_FAST_IF_FAILED(try_to_system_time(value, &result));
    return result;
}

inline clock::time_point from_system_time_failfast(const SYSTEMTIME& value) WI_NOEXCEPT
{
    clock::time_point result{};
    FAIL_FAST_IF_FAILED(try_from_system_time(value, &result));
    return result;
}

#if defined(WIL_ENABLE_EXCEPTIONS)
inline SYSTEMTIME to_system_time(clock::time_point value)
{
    SYSTEMTIME result{};
    THROW_IF_FAILED(try_to_system_time(value, &result));
    return result;
}

inline clock::time_point from_system_time(const SYSTEMTIME& value)
{
    clock::time_point result{};
    THROW_IF_FAILED(try_from_system_time(value, &result));
    return result;
}
#endif

namespace details
{
    template <typename DueDuration, typename PeriodDuration, typename WindowDuration>
    HRESULT prepare_threadpool_timer(
        DueDuration due, PeriodDuration period, WindowDuration window, FILETIME* dueTime, DWORD* periodMilliseconds, DWORD* windowMilliseconds) WI_NOEXCEPT
    {
        RETURN_IF_FAILED(try_to_relative_file_time(due, dueTime));
        RETURN_IF_FAILED(try_to_dword_ms(period, periodMilliseconds));
        return try_to_dword_ms(window, windowMilliseconds);
    }

    template <typename Clock, typename DueDuration, typename PeriodDuration, typename WindowDuration>
    HRESULT prepare_absolute_threadpool_timer(
        std::chrono::time_point<Clock, DueDuration> due,
        PeriodDuration period,
        WindowDuration window,
        FILETIME* dueTime,
        DWORD* periodMilliseconds,
        DWORD* windowMilliseconds) WI_NOEXCEPT
    {
        RETURN_IF_FAILED(try_to_file_time(due, dueTime));
        RETURN_IF_FAILED(try_to_dword_ms(period, periodMilliseconds));
        return try_to_dword_ms(window, windowMilliseconds);
    }
} // namespace details

//! Schedules a threadpool timer relative to the current time.
template <
    typename DueDuration,
    typename PeriodDuration = std::chrono::milliseconds,
    typename WindowDuration = std::chrono::milliseconds,
    details::enable_if_chrono_duration_t<DueDuration> = 0,
    details::enable_if_chrono_duration_t<PeriodDuration> = 0,
    details::enable_if_chrono_duration_t<WindowDuration> = 0>
HRESULT set_relative_threadpool_timer_nothrow(
    PTP_TIMER timer, DueDuration due, PeriodDuration period = PeriodDuration::zero(), WindowDuration window = WindowDuration::zero()) WI_NOEXCEPT
{
    if (timer == nullptr)
    {
        return E_INVALIDARG;
    }

    FILETIME dueTime{};
    DWORD periodMilliseconds{};
    DWORD windowMilliseconds{};
    RETURN_IF_FAILED(details::prepare_threadpool_timer(due, period, window, &dueTime, &periodMilliseconds, &windowMilliseconds));
    ::SetThreadpoolTimer(timer, &dueTime, periodMilliseconds, windowMilliseconds);
    return S_OK;
}

//! Schedules a threadpool timer for an absolute WIL or system clock time.
template <
    typename Clock,
    typename DueDuration,
    typename PeriodDuration = std::chrono::milliseconds,
    typename WindowDuration = std::chrono::milliseconds,
    details::enable_if_chrono_duration_t<PeriodDuration> = 0,
    details::enable_if_chrono_duration_t<WindowDuration> = 0>
HRESULT set_threadpool_timer_nothrow(
    PTP_TIMER timer,
    std::chrono::time_point<Clock, DueDuration> due,
    PeriodDuration period = PeriodDuration::zero(),
    WindowDuration window = WindowDuration::zero()) WI_NOEXCEPT
{
    static_assert(
        std::is_same<Clock, clock>::value || std::is_same<Clock, std::chrono::system_clock>::value,
        "Only wil::clock and std::chrono::system_clock absolute times are supported");

    if (timer == nullptr)
    {
        return E_INVALIDARG;
    }

    FILETIME dueTime{};
    DWORD periodMilliseconds{};
    DWORD windowMilliseconds{};
    RETURN_IF_FAILED(details::prepare_absolute_threadpool_timer(due, period, window, &dueTime, &periodMilliseconds, &windowMilliseconds));
    ::SetThreadpoolTimer(timer, &dueTime, periodMilliseconds, windowMilliseconds);
    return S_OK;
}

template <typename... Args>
void set_relative_threadpool_timer_failfast(Args&&... args) WI_NOEXCEPT
{
    FAIL_FAST_IF_FAILED(set_relative_threadpool_timer_nothrow(std::forward<Args>(args)...));
}

template <typename... Args>
void set_threadpool_timer_failfast(Args&&... args) WI_NOEXCEPT
{
    FAIL_FAST_IF_FAILED(set_threadpool_timer_nothrow(std::forward<Args>(args)...));
}

#if defined(WIL_ENABLE_EXCEPTIONS)
template <typename... Args>
void set_relative_threadpool_timer(Args&&... args)
{
    THROW_IF_FAILED(set_relative_threadpool_timer_nothrow(std::forward<Args>(args)...));
}

template <typename... Args>
void set_threadpool_timer(Args&&... args)
{
    THROW_IF_FAILED(set_threadpool_timer_nothrow(std::forward<Args>(args)...));
}
#endif

} // namespace wil

#endif // WIL_USE_STL && !defined(WIL_NO_CHRONO) && !defined(__WIL_MIN_KERNEL) && !defined(WIL_KERNEL_MODE)
#endif // __WIL_CHRONO_INCLUDED
