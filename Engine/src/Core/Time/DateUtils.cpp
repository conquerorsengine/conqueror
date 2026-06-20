#include "DateUtils.h"
#include <chrono>
#include <ctime>

namespace Conqueror::DateUtils
{
    static std::tm GetTM(double ms) {
        time_t sec = static_cast<time_t>(ms / 1000.0);
        std::tm tm_val;
        localtime_r(&sec, &tm_val);
        return tm_val;
    }

    double NowMs()
    {
        auto now = std::chrono::system_clock::now();
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }

    int GetYear(double ms) { return GetTM(ms).tm_year + 1900; }
    int GetMonth(double ms) { return GetTM(ms).tm_mon + 1; }
    int GetDay(double ms) { return GetTM(ms).tm_mday; }
    int GetHours(double ms) { return GetTM(ms).tm_hour; }
    int GetMinutes(double ms) { return GetTM(ms).tm_min; }
    int GetSeconds(double ms) { return GetTM(ms).tm_sec; }
    
    std::string Format(double ms, const std::string& format)
    {
        auto tm = GetTM(ms);
        char buf[256];
        strftime(buf, sizeof(buf), format.c_str(), &tm);
        return std::string(buf);
    }
    
    int GetDayOfWeek(double ms) { return GetTM(ms).tm_wday; }
    int GetDayOfYear(double ms) { return GetTM(ms).tm_yday; }
    
    bool IsLeapYear(int year)
    {
        if (year % 400 == 0) return true;
        if (year % 100 == 0) return false;
        return year % 4 == 0;
    }

    int DaysInMonth(int month, int year)
    {
        if (month == 2) return IsLeapYear(year) ? 29 : 28;
        if (month == 4 || month == 6 || month == 9 || month == 11) return 30;
        return 31;
    }

    double AddDays(double ms, double days)
    {
        return ms + (days * 86400000.0);
    }

    double DiffSeconds(double ms1, double ms2)
    {
        return (ms1 - ms2) / 1000.0;
    }
}
