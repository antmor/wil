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
#include "filetime_helpers.h"

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
        : value(filetime::to_int64<std::uint64_t>(rawValue))
    {
    }

    __WI_CONSTEXPR_BIT_CAST FILETIME to_FILETIME() const WI_NOEXCEPT
    {
        return filetime::from_int64(value);
    }
};

namespace details
{
    // Number of 100-nanosecond ticks between the FILETIME epoch (January 1, 1601 UTC) and the Unix epoch
    // (January 1, 1970 UTC).
    constexpr std::int64_t c_unixEpochOffsetInFileTimeTicks = 116444736000000000LL;

    // 9223372036854775808 is 2^63, one past INT64_MAX, and is exactly representable as a floating-point value. Using this
    // exclusive bound avoids converting INT64_MAX to long double, which rounds up to 2^63 on MSVC where long double has the
    // same precision as double.
    constexpr long double c_int64ExclusiveMaximum = 9223372036854775808.0L;

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

    template <typename Duration>
    using file_time_ratio_t = std::ratio_divide<typename Duration::period, file_time_period>;

    inline HRESULT try_unsigned_multiply_divide(
        std::uintmax_t multiplicand,
        std::uintmax_t multiplier,
        std::uintmax_t divisor,
        std::uintmax_t maximum,
        std::uintmax_t* result,
        std::uintmax_t* remainderResult) WI_NOEXCEPT
    {
        const auto multiplierQuotient = multiplier / divisor;
        const auto multiplierRemainder = multiplier % divisor;
        std::uintmax_t quotient{};
        std::uintmax_t remainder{};

        for (int bitIndex = std::numeric_limits<std::uintmax_t>::digits - 1; bitIndex >= 0; --bitIndex)
        {
            const bool bit = ((multiplicand >> bitIndex) & 1) != 0;

            if (quotient > maximum / 2)
            {
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            }
            quotient *= 2;

            if (bit)
            {
                if (multiplierQuotient > maximum - quotient)
                {
                    return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
                }
                quotient += multiplierQuotient;
            }

            std::uintmax_t carry{};
            if (remainder >= divisor - remainder)
            {
                remainder -= divisor - remainder;
                ++carry;
            }
            else
            {
                remainder += remainder;
            }

            if (bit && multiplierRemainder != 0)
            {
                if (remainder >= divisor - multiplierRemainder)
                {
                    remainder -= divisor - multiplierRemainder;
                    ++carry;
                }
                else
                {
                    remainder += multiplierRemainder;
                }
            }

            if (carry > maximum - quotient)
            {
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            }
            quotient += carry;
        }

        *result = quotient;
        *remainderResult = remainder;
        return S_OK;
    }

    template <typename Duration>
    HRESULT try_integral_duration_floor_as_file_time_ticks(Duration value, std::int64_t* result) WI_NOEXCEPT
    {
        static_assert(std::is_integral_v<typename Duration::rep>);
        using ratio = file_time_ratio_t<Duration>;
        using rep = typename Duration::rep;
        constexpr auto multiplier = static_cast<std::uintmax_t>(ratio::num);
        constexpr auto divisor = static_cast<std::uintmax_t>(ratio::den);

        if constexpr (std::is_signed_v<rep>)
        {
            static_assert(sizeof(rep) <= sizeof(std::int64_t));
            const auto count = static_cast<std::int64_t>(value.count());
            const bool negative = count < 0;
            const auto magnitude = negative ?
                                       static_cast<std::uintmax_t>(-(count + 1)) + 1 :
                                       static_cast<std::uintmax_t>(count);
            const auto maximumMagnitude =
                negative ?
                    static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)()) + 1 :
                    static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)());

            std::uintmax_t converted{};
            std::uintmax_t remainder{};
            RETURN_IF_FAILED(
                try_unsigned_multiply_divide(magnitude, multiplier, divisor, maximumMagnitude, &converted, &remainder));

            if (negative && remainder != 0)
            {
                if (converted == maximumMagnitude)
                {
                    return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
                }
                ++converted;
            }

            if (negative)
            {
                *result = converted == maximumMagnitude ?
                              (std::numeric_limits<std::int64_t>::min)() :
                              -static_cast<std::int64_t>(converted);
            }
            else
            {
                *result = static_cast<std::int64_t>(converted);
            }
        }
        else
        {
            static_assert(sizeof(rep) <= sizeof(std::uint64_t));
            const auto count = static_cast<std::uint64_t>(value.count());
            std::uintmax_t converted{};
            std::uintmax_t remainder{};
            RETURN_IF_FAILED(try_unsigned_multiply_divide(
                count,
                multiplier,
                divisor,
                static_cast<std::uintmax_t>((std::numeric_limits<std::int64_t>::max)()),
                &converted,
                &remainder));
            *result = static_cast<std::int64_t>(converted);
        }

        return S_OK;
    }

    template <typename Rep, typename Period>
    HRESULT try_to_dword_ms_with_maximum(
        std::chrono::duration<Rep, Period> value, DWORD maximum, DWORD* result) WI_NOEXCEPT
    {
        const auto milliseconds = duration_count_as<std::chrono::duration<Rep, Period>, std::milli>(value);
        if (!(milliseconds >= 0))
        {
            return E_INVALIDARG;
        }

        if (milliseconds > static_cast<long double>(maximum))
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }

        auto converted = static_cast<DWORD>(milliseconds);
        if (static_cast<long double>(converted) < milliseconds)
        {
            if (converted == maximum)
            {
                return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
            }
            ++converted;
        }

        *result = converted;
        return S_OK;
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
        return std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>{
            std::chrono::seconds{-details::c_unixEpochOffsetInFileTimeTicks / filetime_duration::one_second}};
    }
};

//! Converts any chrono duration to fractional milliseconds for measurement and telemetry. Unlike `to_dword_ms`, this preserves
//! negative values and follows normal floating-point behavior for NaN, infinity, precision loss, and overflow.
//! For example, `wil::to_float_ms(end - start)` converts an elapsed duration to a floating-point telemetry field.
template <typename Rep, typename Period>
constexpr float to_float_ms(std::chrono::duration<Rep, Period> value) WI_NOEXCEPT
{
    return std::chrono::duration<float, std::milli>{value}.count();
}

//! Converts any chrono duration to a finite Win32 millisecond timeout, rounding positive fractions upward.
template <typename Rep, typename Period>
HRESULT try_to_dword_ms(std::chrono::duration<Rep, Period> value, DWORD* result) WI_NOEXCEPT
{
    return details::try_to_dword_ms_with_maximum(value, INFINITE - 1, result);
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

        if (ticks >= details::c_int64ExclusiveMaximum)
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
    *result = filetime::from_int64(encoded);
    return S_OK;
}

template <typename Duration>
HRESULT try_to_file_time(std::chrono::time_point<clock, Duration> value, FILETIME* result) WI_NOEXCEPT
{
    if constexpr (std::is_integral_v<typename Duration::rep>)
    {
        std::int64_t ticks{};
        RETURN_IF_FAILED(details::try_integral_duration_floor_as_file_time_ticks(value.time_since_epoch(), &ticks));
        if (ticks < 0)
        {
            return E_INVALIDARG;
        }
        *result = filetime::from_int64(ticks);
    }
    else
    {
        const auto ticks = details::duration_count_as<Duration, file_time_period>(value.time_since_epoch());
        if (!(ticks >= 0))
        {
            return E_INVALIDARG;
        }

        if (ticks >= details::c_int64ExclusiveMaximum)
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }
        *result = filetime::from_int64(static_cast<std::uint64_t>(ticks));
    }
    return S_OK;
}

template <typename Duration>
HRESULT try_to_file_time(std::chrono::time_point<std::chrono::system_clock, Duration> value, FILETIME* result) WI_NOEXCEPT
{
    if constexpr (std::is_integral_v<typename Duration::rep>)
    {
        std::int64_t ticksSinceUnixEpoch{};
        RETURN_IF_FAILED(
            details::try_integral_duration_floor_as_file_time_ticks(value.time_since_epoch(), &ticksSinceUnixEpoch));

        if (ticksSinceUnixEpoch < -details::c_unixEpochOffsetInFileTimeTicks)
        {
            return E_INVALIDARG;
        }
        if (ticksSinceUnixEpoch >
            (std::numeric_limits<std::int64_t>::max)() - details::c_unixEpochOffsetInFileTimeTicks)
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }

        *result = filetime::from_int64(ticksSinceUnixEpoch + details::c_unixEpochOffsetInFileTimeTicks);
    }
    else
    {
        const auto ticksSinceUnixEpoch = details::duration_count_as<Duration, file_time_period>(value.time_since_epoch());
        const auto ticksSinceWindowsEpoch =
            ticksSinceUnixEpoch + static_cast<long double>(details::c_unixEpochOffsetInFileTimeTicks);
        if (!(ticksSinceWindowsEpoch >= 0))
        {
            return E_INVALIDARG;
        }

        if (ticksSinceWindowsEpoch >= details::c_int64ExclusiveMaximum)
        {
            return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        }

        *result = filetime::from_int64(static_cast<std::uint64_t>(ticksSinceWindowsEpoch));
    }
    return S_OK;
}

inline HRESULT try_to_system_time(clock::time_point value, SYSTEMTIME* result) WI_NOEXCEPT
{
    FILETIME fileTime{};
    RETURN_IF_FAILED(try_to_file_time(value, &fileTime));
    RETURN_IF_WIN32_BOOL_FALSE(::FileTimeToSystemTime(&fileTime, result));
    return S_OK;
}

inline HRESULT try_from_system_time(const SYSTEMTIME& value, clock::time_point* result) WI_NOEXCEPT
{
    FILETIME fileTime{};
    RETURN_IF_WIN32_BOOL_FALSE(::SystemTimeToFileTime(&value, &fileTime));

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
        RETURN_IF_FAILED(try_to_dword_ms_with_maximum(period, (std::numeric_limits<DWORD>::max)(), periodMilliseconds));
        return try_to_dword_ms_with_maximum(window, (std::numeric_limits<DWORD>::max)(), windowMilliseconds);
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
        RETURN_IF_FAILED(try_to_dword_ms_with_maximum(period, (std::numeric_limits<DWORD>::max)(), periodMilliseconds));
        return try_to_dword_ms_with_maximum(window, (std::numeric_limits<DWORD>::max)(), windowMilliseconds);
    }
} // namespace details

/** Schedules a threadpool timer relative to the current time.
Use the `_nothrow` form to return conversion failures, the `_failfast` form when invalid values are fatal, or the throwing form
when exceptions are enabled.
@code
RETURN_IF_FAILED(wil::set_relative_threadpool_timer_nothrow(timer.get(), std::chrono::milliseconds{250}));
wil::set_relative_threadpool_timer_failfast(timer.get(), std::chrono::milliseconds{250});
wil::set_relative_threadpool_timer(timer.get(), std::chrono::milliseconds{250});
@endcode
*/
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

/** Schedules a threadpool timer for an absolute WIL or system clock time.
Use the `_nothrow` form to return conversion failures, the `_failfast` form when invalid values are fatal, or the throwing form
when exceptions are enabled.
@code
const auto due = std::chrono::system_clock::now() + std::chrono::seconds{1};
RETURN_IF_FAILED(wil::set_threadpool_timer_nothrow(timer.get(), due));
wil::set_threadpool_timer_failfast(timer.get(), due);
wil::set_threadpool_timer(timer.get(), due);
@endcode
*/
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
