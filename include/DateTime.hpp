#ifndef MEALWALLET_DATETIME_HPP
#define MEALWALLET_DATETIME_HPP

#include "Enums.hpp"
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>

namespace mealwallet {


class Date {
private:
    int year_;
    int month_;
    int day_;

public:
    Date() : year_(2026), month_(11), day_(13) {}
    Date(int y, int m, int d) : year_(y), month_(m), day_(d) {
        if (!isValid(y, m, d)) {
            throw std::invalid_argument("Invalid calendar date: " + toString());
        }
    }

    int year() const noexcept { return year_; }
    int month() const noexcept { return month_; }
    int day() const noexcept { return day_; }

    static bool isLeapYear(int y) noexcept {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    static int daysInMonth(int y, int m) noexcept {
        static const int kDays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (m < 1 || m > 12) return 30;
        if (m == 2 && isLeapYear(y)) return 29;
        return kDays[m - 1];
    }

    static bool isValid(int y, int m, int d) noexcept {
        if (y < 1970 || y > 2100) return false;
        if (m < 1 || m > 12) return false;
        if (d < 1 || d > daysInMonth(y, m)) return false;
        return true;
    }

    static Date parse(const std::string& yyyy_mm_dd) {
        if (yyyy_mm_dd.size() < 10 || yyyy_mm_dd[4] != '-' || yyyy_mm_dd[7] != '-') {
            throw std::invalid_argument("Expected YYYY-MM-DD date string, got: " + yyyy_mm_dd);
        }
        int y = std::stoi(yyyy_mm_dd.substr(0, 4));
        int m = std::stoi(yyyy_mm_dd.substr(5, 2));
        int d = std::stoi(yyyy_mm_dd.substr(8, 2));
        return Date(y, m, d);
    }

    std::string toString() const {
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << year_ << "-"
            << std::setw(2) << std::setfill('0') << month_ << "-"
            << std::setw(2) << std::setfill('0') << day_;
        return oss.str();
    }

    std::string yearMonth() const {
        return toString().substr(0, 7); // "YYYY-MM"
    }

    // Add N days handling month and year boundaries cleanly
    Date addDays(int deltaDays) const {
        int y = year_, m = month_, d = day_;
        while (deltaDays > 0) {
            int dim = daysInMonth(y, m);
            if (d + deltaDays <= dim) {
                d += deltaDays;
                deltaDays = 0;
            } else {
                deltaDays -= (dim - d + 1);
                d = 1;
                if (++m > 12) { m = 1; ++y; }
            }
        }
        while (deltaDays < 0) {
            if (d + deltaDays >= 1) {
                d += deltaDays;
                deltaDays = 0;
            } else {
                deltaDays += d;
                if (--m < 1) { m = 12; --y; }
                d = daysInMonth(y, m);
            }
        }
        return Date(y, m, d);
    }

    // Generate inclusive range [start, end]
    static std::vector<Date> rangeInclusive(const Date& start, const Date& end) {
        std::vector<Date> out;
        if (end < start) return out;
        Date cur = start;
        while (cur <= end) {
            out.push_back(cur);
            cur = cur.addDays(1);
        }
        return out;
    }

    // Operator overloading for Date comparisons
    bool operator==(const Date& rhs) const noexcept {
        return year_ == rhs.year_ && month_ == rhs.month_ && day_ == rhs.day_;
    }
    bool operator!=(const Date& rhs) const noexcept { return !(*this == rhs); }
    bool operator<(const Date& rhs) const noexcept {
        if (year_ != rhs.year_) return year_ < rhs.year_;
        if (month_ != rhs.month_) return month_ < rhs.month_;
        return day_ < rhs.day_;
    }
    bool operator<=(const Date& rhs) const noexcept { return (*this < rhs) || (*this == rhs); }
    bool operator>(const Date& rhs) const noexcept { return rhs < *this; }
    bool operator>=(const Date& rhs) const noexcept { return !(*this < rhs); }

    friend std::ostream& operator<<(std::ostream& os, const Date& dt) {
        os << dt.toString();
        return os;
    }
};

/**
 * Time Utilities:
 * All timestamps are stored as UTC epoch seconds (int64_t) or UTC ISO-8601 strings,
 * and displayed in Asia/Kolkata (IST = UTC + 05:30 = +19800 seconds).
 */
class TimeUtils {
public:
    static constexpr int64_t kIstOffsetSeconds = 5 * 3600 + 30 * 60; // +05:30

    // Convert civil date/time (UTC) to epoch seconds (portable timegm algorithm)
    static int64_t civilToEpochUtc(int y, int m, int d, int hh, int mm, int ss) noexcept {
        int yAdj = y - (m <= 2 ? 1 : 0);
        const int era = (yAdj >= 0 ? yAdj : yAdj - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(yAdj - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        int64_t days = era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
        return days * 86400LL + hh * 3600LL + mm * 60LL + ss;
    }

    // Convert IST civil date/time to UTC epoch seconds
    static int64_t istToEpochUtc(const Date& istDate, int hourIst, int minIst = 0, int secIst = 0) noexcept {
        int64_t istAsUtc = civilToEpochUtc(istDate.year(), istDate.month(), istDate.day(), hourIst, minIst, secIst);
        return istAsUtc - kIstOffsetSeconds;
    }

    // Convert UTC epoch seconds to broken-down civil components (with arbitrary offset)
    static void epochToCivil(int64_t epochSec, int64_t offsetSec,
                             int& y, int& m, int& d, int& hh, int& mm, int& ss) noexcept {
        int64_t localSec = epochSec + offsetSec;
        int64_t z = localSec / 86400LL;
        int64_t rem = localSec % 86400LL;
        if (rem < 0) { rem += 86400LL; --z; }
        hh = static_cast<int>(rem / 3600LL);
        rem %= 3600LL;
        mm = static_cast<int>(rem / 60LL);
        ss = static_cast<int>(rem % 60LL);

        z += 719468LL;
        const int64_t era = (z >= 0 ? z : z - 146096LL) / 146097LL;
        const unsigned doe = static_cast<unsigned>(z - era * 146097LL);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
        y += (m <= 2 ? 1 : 0);
    }

    static Date currentIstDate(int64_t utcEpoch) {
        int y, m, d, hh, mm, ss;
        epochToCivil(utcEpoch, kIstOffsetSeconds, y, m, d, hh, mm, ss);
        return Date(y, m, d);
    }

    // Format UTC epoch as ISO-8601 UTC string for persistence: "2026-11-13T15:00:00Z"
    static std::string formatUtcIso(int64_t utcEpoch) {
        int y, m, d, hh, mm, ss;
        epochToCivil(utcEpoch, 0, y, m, d, hh, mm, ss);
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << y << "-"
            << std::setw(2) << std::setfill('0') << m << "-"
            << std::setw(2) << std::setfill('0') << d << "T"
            << std::setw(2) << std::setfill('0') << hh << ":"
            << std::setw(2) << std::setfill('0') << mm << ":"
            << std::setw(2) << std::setfill('0') << ss << "Z";
        return oss.str();
    }

    // Format UTC epoch as friendly IST string: "2026-11-13 08:30 PM IST"
    static std::string formatIstPretty(int64_t utcEpoch) {
        int y, m, d, hh, mm, ss;
        epochToCivil(utcEpoch, kIstOffsetSeconds, y, m, d, hh, mm, ss);
        std::string ampm = (hh >= 12) ? "PM" : "AM";
        int h12 = hh % 12;
        if (h12 == 0) h12 = 12;
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << y << "-"
            << std::setw(2) << std::setfill('0') << m << "-"
            << std::setw(2) << std::setfill('0') << d << " "
            << std::setw(2) << std::setfill('0') << h12 << ":"
            << std::setw(2) << std::setfill('0') << mm << " " << ampm << " IST";
        return oss.str();
    }

    // Format only time portion in IST: "06:30 AM IST"
    static std::string formatIstTimeOnly(int64_t utcEpoch) {
        int y, m, d, hh, mm, ss;
        epochToCivil(utcEpoch, kIstOffsetSeconds, y, m, d, hh, mm, ss);
        std::string ampm = (hh >= 12) ? "PM" : "AM";
        int h12 = hh % 12;
        if (h12 == 0) h12 = 12;
        std::ostringstream oss;
        oss << h12 << ":" << std::setw(2) << std::setfill('0') << mm << " " << ampm;
        return oss.str();
    }

    // Format IST ISO for <input type="datetime-local">: "YYYY-MM-DDTHH:MM"
    static std::string formatIstInputLocal(int64_t utcEpoch) {
        int y, m, d, hh, mm, ss;
        epochToCivil(utcEpoch, kIstOffsetSeconds, y, m, d, hh, mm, ss);
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << y << "-"
            << std::setw(2) << std::setfill('0') << m << "-"
            << std::setw(2) << std::setfill('0') << d << "T"
            << std::setw(2) << std::setfill('0') << hh << ":"
            << std::setw(2) << std::setfill('0') << mm;
        return oss.str();
    }

    // Parse "YYYY-MM-DDTHH:MM" in IST to UTC epoch
    static int64_t parseIstInputLocal(const std::string& s) {
        if (s.size() < 16) throw std::invalid_argument("Invalid IST datetime string: " + s);
        Date d = Date::parse(s.substr(0, 10));
        int hh = std::stoi(s.substr(11, 2));
        int mm = std::stoi(s.substr(14, 2));
        return istToEpochUtc(d, hh, mm, 0);
    }

    // Standard Meal Start Times in IST (Breakfast 08:00, Lunch 12:30, Dinner 20:00)
    static int64_t mealStartUtc(const Date& mealDateIst, MealType meal) noexcept {
        switch (meal) {
            case MealType::BREAKFAST: return istToEpochUtc(mealDateIst, 8, 0, 0);
            case MealType::LUNCH:     return istToEpochUtc(mealDateIst, 12, 30, 0);
            case MealType::DINNER:    return istToEpochUtc(mealDateIst, 20, 0, 0);
        }
        return istToEpochUtc(mealDateIst, 8, 0, 0);
    }

    static int64_t mealCutoffUtc(const Date& mealDateIst, MealType meal, int cutoffHours) noexcept {
        return mealStartUtc(mealDateIst, meal) - static_cast<int64_t>(cutoffHours) * 3600LL;
    }
};


class Clock {
public:
    virtual ~Clock() = default;
    virtual int64_t now() const = 0; // Returns current UTC epoch seconds
    virtual bool isDemoMode() const = 0;
    virtual std::string modeName() const = 0;
};


class SystemClock : public Clock {
public:
    int64_t now() const override {
        return static_cast<int64_t>(std::time(nullptr));
    }
    bool isDemoMode() const override { return false; }
    std::string modeName() const override { return "SYSTEM"; }
};


class DemoClock : public Clock {
private:
    int64_t simulatedUtc_;

public:
    explicit DemoClock(int64_t initialUtc) : simulatedUtc_(initialUtc) {}

    int64_t now() const override { return simulatedUtc_; }
    bool isDemoMode() const override { return true; }
    std::string modeName() const override { return "DEMO"; }

    void setNow(int64_t utcSec) noexcept { simulatedUtc_ = utcSec; }
    void advanceSeconds(int64_t deltaSec) noexcept { simulatedUtc_ += deltaSec; }
    void advanceHours(int hours) noexcept { simulatedUtc_ += static_cast<int64_t>(hours) * 3600LL; }
    void advanceDays(int days) noexcept { simulatedUtc_ += static_cast<int64_t>(days) * 86400LL; }
};

} // namespace mealwallet

#endif // MEALWALLET_DATETIME_HPP
