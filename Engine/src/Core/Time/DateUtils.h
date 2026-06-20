#pragma once
#include "Core/Base/Base.h"
#include <string>

namespace Conqueror::DateUtils
{
    CQ_API double NowMs();
    CQ_API int GetYear(double ms);
    CQ_API int GetMonth(double ms);
    CQ_API int GetDay(double ms);
    CQ_API int GetHours(double ms);
    CQ_API int GetMinutes(double ms);
    CQ_API int GetSeconds(double ms);
    CQ_API std::string Format(double ms, const std::string& format);
    CQ_API int GetDayOfWeek(double ms);
    CQ_API int GetDayOfYear(double ms);
    CQ_API bool IsLeapYear(int year);
    CQ_API int DaysInMonth(int month, int year);
    CQ_API double AddDays(double ms, double days);
    CQ_API double DiffSeconds(double ms1, double ms2);
}
