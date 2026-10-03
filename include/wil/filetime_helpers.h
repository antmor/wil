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
//! Low-level helpers for converting FILETIME values to and from integral 100-nanosecond counts.
#ifndef __WIL_FILETIME_HELPERS_INCLUDED
#define __WIL_FILETIME_HELPERS_INCLUDED

#include <minwindef.h>

#include "common.h"

#if WIL_USE_STL && (__WI_LIBCPP_STD_VER >= 20)
#if WI_HAS_INCLUDE(<bit>, 1)
#include <bit>
#endif
#endif

/// @cond
#if WIL_USE_STL && (__cpp_lib_bit_cast >= 201806L)
#define __WI_CONSTEXPR_BIT_CAST constexpr
#else
#define __WI_CONSTEXPR_BIT_CAST // All uses are templates, which is implicitly inline
#endif
/// @endcond

namespace wil
{
//! Common FILETIME durations, expressed in the 100-nanosecond units that `FILETIME` uses.
namespace filetime_duration
{
    //! One millisecond, in 100-nanosecond units.
    long long const one_millisecond = 10000LL;
    //! One second, in 100-nanosecond units.
    long long const one_second = 10000000LL;
    //! One minute, in 100-nanosecond units.
    long long const one_minute = 10000000LL * 60;        // 600000000    or 600000000LL
    //! One hour, in 100-nanosecond units.
    long long const one_hour = 10000000LL * 60 * 60;     // 36000000000  or 36000000000LL
    //! One day, in 100-nanosecond units.
    long long const one_day = 10000000LL * 60 * 60 * 24; // 864000000000 or 864000000000LL
} // namespace filetime_duration

namespace filetime
{
    /// Reinterprets a `FILETIME` as a 64-bit integer count of 100-nanosecond units.
    /// @tparam Int64 A 64-bit integral type to return the value as; defaults to `unsigned long long`.
    /// @param val The `FILETIME` to convert.
    /// @return The `FILETIME` reinterpreted as a single 64-bit integer.
    template <typename Int64 = unsigned long long, wistd::enable_if_t<wistd::is_integral_v<Int64> && (sizeof(Int64) == sizeof(FILETIME)), int> = 0>
    constexpr Int64 to_int64(const FILETIME& val) WI_NOEXCEPT
    {
#if WIL_USE_STL && (__cpp_lib_bit_cast >= 201806L)
        return std::bit_cast<Int64>(val);
#else
        // Cannot reinterpret_cast FILETIME* to Int64* due to alignment differences.
        return (static_cast<Int64>(val.dwHighDateTime) << 32) + val.dwLowDateTime;
#endif
    }

    /// @cond
    namespace details
    {
        template <typename Int>
        using select_int64 =
            wistd::conditional_t<sizeof(Int) == 8, Int, wistd::conditional_t<wistd::is_signed_v<Int>, long long, unsigned long long>>;
    }
    /// @endcond

    /// Converts an integer count of 100-nanosecond units into a `FILETIME`.
    /// @tparam Int An integral type no larger than `FILETIME` (8 bytes).
    /// @param val The 100-nanosecond count to convert.
    /// @return A `FILETIME` representing the given count.
    template <typename Int, wistd::enable_if_t<wistd::is_integral_v<Int> && (sizeof(Int) <= sizeof(FILETIME)), int> = 0>
    __WI_CONSTEXPR_BIT_CAST FILETIME from_int64(Int val) WI_NOEXCEPT
    {
        using Int64 = details::select_int64<Int>;
        auto i64 = static_cast<Int64>(val);

#if WIL_USE_STL && (__cpp_lib_bit_cast >= 201806L)
        return std::bit_cast<FILETIME>(i64);
#else
        static_assert(sizeof(i64) == sizeof(FILETIME), "sizes don't match");
        static_assert(__alignof(Int64) >= __alignof(FILETIME), "alignment not compatible with type pun");
        return *reinterpret_cast<FILETIME*>(&i64);
#endif
    }
} // namespace filetime
} // namespace wil

#endif // __WIL_FILETIME_HELPERS_INCLUDED
