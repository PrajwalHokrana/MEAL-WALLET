#ifndef MEALWALLET_REQUEST_HPP
#define MEALWALLET_REQUEST_HPP

#include "DateTime.hpp"
#include "Enums.hpp"
#include <memory>
#include <string>
#include <vector>

namespace mealwallet {

struct MealSlotSpec {
    Date mealDate;
    MealType mealType;
};


class Request {
protected:
    std::string id_;
    std::string studentId_;
    std::string reason_;
    int64_t createdAtUtc_;

public:
    Request(std::string id, std::string studentId, std::string reason, int64_t createdAtUtc)
        : id_(std::move(id)),
          studentId_(std::move(studentId)),
          reason_(std::move(reason)),
          createdAtUtc_(createdAtUtc) {}

    virtual ~Request() = default;

    const std::string& id() const noexcept { return id_; }
    const std::string& studentId() const noexcept { return studentId_; }
    const std::string& reason() const noexcept { return reason_; }
    int64_t createdAtUtc() const noexcept { return createdAtUtc_; }

    // Pure virtual functions
    virtual std::string requestKind() const = 0;
    virtual std::vector<MealSlotSpec> expandSlots() const = 0;
};


class SingleSkipRequest : public Request {
private:
    Date mealDate_;
    MealType mealType_;
    RequestStatus status_;
    bool appealed_;
    std::string appealReason_;
    std::string adminNote_;
    int64_t decidedAtUtc_;
    std::string batchGroupId_; // Links slots expanded from a MultiDaySkipRequest

public:
    SingleSkipRequest()
        : Request("", "", "", 0),
          mealDate_(Date(2026, 11, 13)),
          mealType_(MealType::BREAKFAST),
          status_(RequestStatus::PENDING),
          appealed_(false),
          decidedAtUtc_(0) {}

    SingleSkipRequest(std::string id,
                      std::string studentId,
                      Date mealDate,
                      MealType mealType,
                      std::string reason,
                      int64_t createdAtUtc,
                      RequestStatus status = RequestStatus::PENDING,
                      bool appealed = false,
                      std::string appealReason = "",
                      std::string adminNote = "",
                      int64_t decidedAtUtc = 0,
                      std::string batchGroupId = "")
        : Request(std::move(id), std::move(studentId), std::move(reason), createdAtUtc),
          mealDate_(mealDate),
          mealType_(mealType),
          status_(status),
          appealed_(appealed),
          appealReason_(std::move(appealReason)),
          adminNote_(std::move(adminNote)),
          decidedAtUtc_(decidedAtUtc),
          batchGroupId_(std::move(batchGroupId)) {}

    std::string requestKind() const override { return "SINGLE_SKIP"; }

    std::vector<MealSlotSpec> expandSlots() const override {
        return { MealSlotSpec{mealDate_, mealType_} };
    }

    const Date& mealDate() const noexcept { return mealDate_; }
    MealType mealType() const noexcept { return mealType_; }
    RequestStatus status() const noexcept { return status_; }
    bool appealed() const noexcept { return appealed_; }
    const std::string& appealReason() const noexcept { return appealReason_; }
    const std::string& adminNote() const noexcept { return adminNote_; }
    int64_t decidedAtUtc() const noexcept { return decidedAtUtc_; }
    const std::string& batchGroupId() const noexcept { return batchGroupId_; }

    void setStatus(RequestStatus st, const std::string& note, int64_t decidedUtc) {
        status_ = st;
        adminNote_ = note;
        decidedAtUtc_ = decidedUtc;
    }

    void markAppealed(const std::string& appealNote) {
        appealed_ = true;
        appealReason_ = appealNote;
        status_ = RequestStatus::APPEALED;
    }
};


class MultiDaySkipRequest : public Request {
private:
    Date startDate_;
    Date endDate_;
    std::vector<MealType> mealsToSkip_;

public:
    MultiDaySkipRequest(std::string id,
                        std::string studentId,
                        Date startDate,
                        Date endDate,
                        std::vector<MealType> mealsToSkip,
                        std::string reason,
                        int64_t createdAtUtc)
        : Request(std::move(id), std::move(studentId), std::move(reason), createdAtUtc),
          startDate_(startDate),
          endDate_(endDate),
          mealsToSkip_(std::move(mealsToSkip)) {
        if (mealsToSkip_.empty()) {
            mealsToSkip_ = {MealType::BREAKFAST, MealType::LUNCH, MealType::DINNER};
        }
    }

    std::string requestKind() const override { return "MULTI_DAY_SKIP"; }

    std::vector<MealSlotSpec> expandSlots() const override {
        std::vector<MealSlotSpec> slots;
        for (const Date& d : Date::rangeInclusive(startDate_, endDate_)) {
            for (MealType m : mealsToSkip_) {
                slots.push_back(MealSlotSpec{d, m});
            }
        }
        return slots;
    }

    const Date& startDate() const noexcept { return startDate_; }
    const Date& endDate() const noexcept { return endDate_; }
    const std::vector<MealType>& mealsToSkip() const noexcept { return mealsToSkip_; }
};

} // namespace mealwallet

#endif // MEALWALLET_REQUEST_HPP
