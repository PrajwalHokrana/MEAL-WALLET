#ifndef MEALWALLET_USER_HPP
#define MEALWALLET_USER_HPP

#include "Enums.hpp"
#include "Subscription.hpp"
#include "Wallet.hpp"
#include <string>

namespace mealwallet {


class User {
protected:
    std::string id_;
    std::string username_;
    std::string fullName_;
    std::string passwordSalt_;
    std::string passwordHash_;
    bool mustChangePassword_;

public:
    User()
        : mustChangePassword_(true) {}

    User(std::string id,
         std::string username,
         std::string fullName,
         std::string passwordSalt,
         std::string passwordHash,
         bool mustChangePassword)
        : id_(std::move(id)),
          username_(std::move(username)),
          fullName_(std::move(fullName)),
          passwordSalt_(std::move(passwordSalt)),
          passwordHash_(std::move(passwordHash)),
          mustChangePassword_(mustChangePassword) {}

    virtual ~User() = default;

    // Pure virtual functions making User abstract
    virtual UserRole role() const = 0;
    virtual std::string dashboardRoute() const = 0;

    const std::string& id() const noexcept { return id_; }
    const std::string& username() const noexcept { return username_; }
    const std::string& fullName() const noexcept { return fullName_; }
    const std::string& passwordSalt() const noexcept { return passwordSalt_; }
    const std::string& passwordHash() const noexcept { return passwordHash_; }
    bool mustChangePassword() const noexcept { return mustChangePassword_; }

    void updatePasswordHash(const std::string& newSalt,
                            const std::string& newHash,
                            bool requireChange = false) {
        passwordSalt_ = newSalt;
        passwordHash_ = newHash;
        mustChangePassword_ = requireChange;
    }
};


class Student : public User {
private:
    std::string roomNumber_;
    Wallet wallet_;
    Subscription subscription_;

public:
    Student() : User(), roomNumber_("A-101"), wallet_("") {}

    Student(std::string id,
            std::string username, // e.g., roll number
            std::string fullName,
            std::string roomNumber,
            std::string passwordSalt,
            std::string passwordHash,
            bool mustChangePassword,
            Subscription sub = Subscription())
        : User(id, std::move(username), std::move(fullName),
               std::move(passwordSalt), std::move(passwordHash), mustChangePassword),
          roomNumber_(std::move(roomNumber)),
          wallet_(id),
          subscription_(std::move(sub)) {}

    UserRole role() const override { return UserRole::STUDENT; }
    std::string dashboardRoute() const override { return "/student.html"; }

    const std::string& roomNumber() const noexcept { return roomNumber_; }
    Wallet& wallet() noexcept { return wallet_; }
    const Wallet& wallet() const noexcept { return wallet_; }

    Subscription& subscription() noexcept { return subscription_; }
    const Subscription& subscription() const noexcept { return subscription_; }
    void setSubscription(const Subscription& sub) { subscription_ = sub; }
};


class Admin : public User {
private:
    std::string title_;

public:
    Admin() : User(), title_("Chief Mess Warden") {}

    Admin(std::string id,
          std::string username,
          std::string fullName,
          std::string passwordSalt,
          std::string passwordHash,
          bool mustChangePassword = false,
          std::string title = "Chief Mess Warden")
        : User(std::move(id), std::move(username), std::move(fullName),
               std::move(passwordSalt), std::move(passwordHash), mustChangePassword),
          title_(std::move(title)) {}

    UserRole role() const override { return UserRole::ADMIN; }
    std::string dashboardRoute() const override { return "/admin.html"; }
    const std::string& title() const noexcept { return title_; }
};


class MessStaff : public User {
private:
    std::string shift_;

public:
    MessStaff() : User(), shift_("ALL_MEALS") {}

    MessStaff(std::string id,
              std::string username,
              std::string fullName,
              std::string passwordSalt,
              std::string passwordHash,
              bool mustChangePassword = false,
              std::string shift = "ALL_MEALS")
        : User(std::move(id), std::move(username), std::move(fullName),
               std::move(passwordSalt), std::move(passwordHash), mustChangePassword),
          shift_(std::move(shift)) {}

    UserRole role() const override { return UserRole::MESS_STAFF; }
    std::string dashboardRoute() const override { return "/mess.html"; }
    const std::string& shift() const noexcept { return shift_; }
};

} // namespace mealwallet

#endif // MEALWALLET_USER_HPP
