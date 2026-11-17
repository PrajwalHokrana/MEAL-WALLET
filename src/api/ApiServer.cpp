#include "ApiServer.hpp"
#include "nlohmann/json.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;

namespace mealwallet {

namespace {

std::string extractBearerOrCookieToken(const httplib::Request& req) {
    if (req.has_header("x-session-token")) {
        return req.get_header_value("x-session-token");
    }
    if (req.has_header("authorization")) {
        std::string auth = req.get_header_value("authorization");
        if (auth.rfind("Bearer ", 0) == 0) {
            return auth.substr(7);
        }
        return auth;
    }
    if (req.has_header("cookie")) {
        std::string cookie = req.get_header_value("cookie");
        auto pos = cookie.find("mw_session=");
        if (pos != std::string::npos) {
            pos += 11;
            auto end = cookie.find(';', pos);
            return cookie.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        }
    }
    return "";
}

void sendJson(httplib::Response& res, int status, const json& payload) {
    res.status = status;
    res.set_content(payload.dump(), "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const std::string& msg, const std::string& rule = "RULE_VIOLATION") {
    json err = json::object();
    err["ok"] = false;
    err["error"] = msg;
    err["rule"] = rule;
    sendJson(res, status, err);
}

json buildClockStateJson(const MealWalletService& svc) {
    int64_t now = svc.nowUtc();
    Date istDate = TimeUtils::currentIstDate(now);
    json c = json::object();
    c["demoMode"] = svc.isDemoMode();
    c["modeName"] = svc.clock().modeName();
    c["nowUtc"] = now;
    c["nowUtcIso"] = TimeUtils::formatUtcIso(now);
    c["nowIstPretty"] = TimeUtils::formatIstPretty(now);
    c["nowIstInputLocal"] = TimeUtils::formatIstInputLocal(now);
    c["todayIstDate"] = istDate.toString();
    c["tomorrowIstDate"] = istDate.addDays(1).toString();
    c["currentYearMonth"] = istDate.yearMonth();
    return c;
}

json buildRulesJson(const MealWalletService& svc) {
    json r = json::object();
    r["cutoffHours"] = Config::cutoffHoursBeforeMeal();
    r["monthlySkipCap"] = Config::monthlySkipCap();
    r["refundPolicyName"] = svc.refundPolicy().name();
    r["refundPercentage"] = svc.refundPolicy().percentage();
    r["refundPolicyDescription"] = svc.refundPolicy().description();
    r["repositoryBackend"] = Config::repositoryMode();
    r["verificationExplanation"] =
        "Each skip request is checked against the " + std::to_string(Config::cutoffHoursBeforeMeal()) +
        "-hour cutoff window and cross-referenced with the mess counter attendance log. "
        "If a student was logged eating at the counter, the system flags the request as SUSPICIOUS (Suggested: REJECT).";
    r["monthEndExplanation"] =
        "At month end or plan validity expiry, students choose REFUND (creates an offline payout record for Admin) "
        "or CARRY_FORWARD (deducts the wallet balance from next month's mess plan fee; default if no choice is made).";
    r["ephemeralDiskNote"] =
        "Data is stored in text files; on free hosting, data resets when the server restarts.";
    return r;
}

json serializeSkipRequest(const MealWalletService& svc, const SingleSkipRequest& req) {
    json j = json::object();
    j["id"] = req.id();
    j["studentId"] = req.studentId();
    try {
        Student stu = svc.getStudentById(req.studentId());
        j["studentUsername"] = stu.username();
        j["studentName"] = stu.fullName();
        j["roomNumber"] = stu.roomNumber();
        j["planId"] = stu.subscription().planId();
        const MealPlan* plan = svc.getPlanById(stu.subscription().planId());
        if (plan) {
            Money basePrice = plan->mealPrice(req.mealType());
            Money estRefund = svc.refundPolicy().compute(basePrice);
            j["baseMealPricePaise"] = basePrice.paise();
            j["baseMealPriceFormatted"] = basePrice.toRupeeString();
            j["estimatedRefundPaise"] = estRefund.paise();
            j["estimatedRefundFormatted"] = estRefund.toRupeeString();
        }
    } catch (...) {
        j["studentUsername"] = req.studentId();
        j["studentName"] = req.studentId();
    }

    j["mealDate"] = req.mealDate().toString();
    j["mealType"] = toString(req.mealType());
    j["status"] = toString(req.status());
    j["reason"] = req.reason();
    j["createdAtUtc"] = req.createdAtUtc();
    j["createdAtIst"] = TimeUtils::formatIstPretty(req.createdAtUtc());
    int64_t cutoffUtc = TimeUtils::mealCutoffUtc(req.mealDate(), req.mealType(), Config::cutoffHoursBeforeMeal());
    j["cutoffIst"] = TimeUtils::formatIstPretty(cutoffUtc);
    j["appealed"] = req.appealed();
    j["appealReason"] = req.appealReason();
    j["adminNote"] = req.adminNote();
    j["batchGroupId"] = req.batchGroupId();

    SuggestedVerdict sv = svc.computeSuggestedVerdict(req);
    json svJson = json::object();
    svJson["verdict"] = sv.verdict;
    svJson["seenAtCounter"] = sv.seenAtCounter;
    svJson["beforeCutoff"] = sv.beforeCutoff;
    json reasonsArr = json::array();
    for (const auto& rs : sv.reasons) reasonsArr.push_back(rs);
    svJson["reasons"] = reasonsArr;
    j["suggested"] = svJson;
    return j;
}

} // anonymous namespace

ApiServer::ApiServer(MealWalletService& service, std::string webRootDir)
    : service_(service), webRootDir_(std::move(webRootDir)) {
    svr_.set_mount_point("/", webRootDir_);
    registerRoutes();
}

bool ApiServer::start(const std::string& host, int port) {
    return svr_.listen(host, port);
}

void ApiServer::stop() {
    svr_.stop();
}

void ApiServer::registerRoutes() {
    // Page routes
    svr_.Get("/student", [this](const httplib::Request&, httplib::Response& res) {
        std::string path = webRootDir_ + "/student.html";
        std::ifstream f(path);
        if (!f) { res.status = 404; res.set_content("Not found", "text/plain"); return; }
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(body, "text/html; charset=utf-8");
    });

    svr_.Get("/admin", [this](const httplib::Request&, httplib::Response& res) {
        std::string path = webRootDir_ + "/admin.html";
        std::ifstream f(path);
        if (!f) { res.status = 404; res.set_content("Not found", "text/plain"); return; }
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(body, "text/html; charset=utf-8");
    });

    svr_.Get("/mess", [this](const httplib::Request&, httplib::Response& res) {
        std::string path = webRootDir_ + "/mess.html";
        std::ifstream f(path);
        if (!f) { res.status = 404; res.set_content("Not found", "text/plain"); return; }
        std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        res.set_content(body, "text/html; charset=utf-8");
    });

    // ------------------------------------------------------------------------
    // 1. Health Check: GET /healthz
    // ------------------------------------------------------------------------
    svr_.Get("/healthz", [this](const httplib::Request&, httplib::Response& res) {
        json out = json::object();
        out["ok"] = true;
        out["service"] = "meal-wallet-cpp17";
        out["clock"] = buildClockStateJson(service_);
        out["repositoryMode"] = Config::repositoryMode();
        sendJson(res, 200, out);
    });

    // ------------------------------------------------------------------------
    // 2. Auth: POST /api/login, POST /api/logout, POST /api/change-password
    // ------------------------------------------------------------------------
    svr_.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            std::string username = body.value("username", "");
            std::string password = body.value("password", "");
            auto loginRes = service_.login(username, password);

            res.set_header("Set-Cookie",
                           "mw_session=" + loginRes.session.token + "; Path=/; SameSite=Lax");
            json out = json::object();
            out["ok"] = true;
            out["token"] = loginRes.session.token;
            out["userId"] = loginRes.session.userId;
            out["username"] = loginRes.session.username;
            out["fullName"] = loginRes.fullName;
            out["role"] = toString(loginRes.session.role);
            out["mustChangePassword"] = loginRes.mustChangePassword;
            out["dashboardRoute"] = loginRes.dashboardRoute;
            out["clock"] = buildClockStateJson(service_);
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "BAD_REQUEST");
        }
    });

    svr_.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
        std::string token = extractBearerOrCookieToken(req);
        if (!token.empty()) {
            service_.logout(token);
        }
        res.set_header("Set-Cookie", "mw_session=; Path=/; Max-Age=0");
        json out = json::object();
        out["ok"] = true;
        sendJson(res, 200, out);
    });

    svr_.Post("/api/change-password", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            json body = json::parse(req.body);
            std::string oldPass = body.value("oldPassword", "");
            std::string newPass = body.value("newPassword", "");
            service_.changePassword(sess.userId, oldPass, newPass);

            json out = json::object();
            out["ok"] = true;
            out["message"] = "Password updated. You can now access your dashboard.";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "BAD_REQUEST");
        }
    });

    // ------------------------------------------------------------------------
    // 3. Student Endpoints
    // ------------------------------------------------------------------------
    svr_.Get("/api/student/dashboard", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            if (sess.role != UserRole::STUDENT) {
                sendError(res, 403, "Student account required to view student dashboard.", "ROLE_RULE");
                return;
            }
            Student stu = service_.getStudentById(sess.userId);
            const MealPlan* plan = service_.getPlanById(stu.subscription().planId());
            int64_t now = service_.nowUtc();
            Date todayIst = TimeUtils::currentIstDate(now);

            json out = json::object();
            out["ok"] = true;
            out["clock"] = buildClockStateJson(service_);
            out["rules"] = buildRulesJson(service_);

            // Student & Subscription summary
            json stuJson = json::object();
            stuJson["id"] = stu.id();
            stuJson["username"] = stu.username();
            stuJson["fullName"] = stu.fullName();
            stuJson["roomNumber"] = stu.roomNumber();
            stuJson["mustChangePassword"] = stu.mustChangePassword();
            stuJson["walletBalancePaise"] = stu.wallet().balance().paise();
            stuJson["walletBalanceFormatted"] = stu.wallet().balance().toRupeeString();
            stuJson["totalCreditedFormatted"] = stu.wallet().totalCredited().toRupeeString();
            stuJson["skipsUsedThisMonth"] = service_.countStudentSkipsInMonth(stu.id(), todayIst.yearMonth());
            stuJson["monthlySkipCap"] = Config::monthlySkipCap();

            json subJson = json::object();
            subJson["id"] = stu.subscription().id();
            subJson["planId"] = stu.subscription().planId();
            subJson["planName"] = plan ? plan->displayName() : stu.subscription().planId();
            subJson["variant"] = toString(stu.subscription().planVariant());
            subJson["startDate"] = stu.subscription().startDate().toString();
            subJson["endDate"] = stu.subscription().endDate().toString();
            subJson["settlementChoice"] = toString(stu.subscription().settlementChoice());
            subJson["nextCyclePlanId"] = stu.subscription().nextCyclePlanId();
            subJson["carryForwardAppliedFormatted"] = stu.subscription().carryForwardCreditApplied().toRupeeString();

            int64_t todayEpoch = TimeUtils::civilToEpochUtc(todayIst.year(), todayIst.month(), todayIst.day(), 0, 0, 0);
            Date subEnd = stu.subscription().endDate();
            int64_t endEpoch = TimeUtils::civilToEpochUtc(subEnd.year(), subEnd.month(), subEnd.day(), 0, 0, 0);
            int daysRemaining = static_cast<int>((endEpoch - todayEpoch) / 86400LL);
            subJson["daysRemaining"] = daysRemaining;
            subJson["isExpiringSoon"] = (daysRemaining <= 2 && daysRemaining >= 0);
            subJson["isExpired"] = (daysRemaining < 0);

            stuJson["subscription"] = subJson;
            out["student"] = stuJson;

            // Today's 3 Meals Cards with live cutoff timestamps
            auto studentReqs = service_.listSkipRequests("", stu.id());
            auto closures = service_.listClosures();
            const MealType mealTypes[3] = {MealType::BREAKFAST, MealType::LUNCH, MealType::DINNER};
            const char* mealTimeLabels[3] = {"08:00 AM IST", "12:30 PM IST", "08:00 PM IST"};

            json todayMeals = json::array();
            for (int i = 0; i < 3; ++i) {
                MealType mt = mealTypes[i];
                int64_t startUtc = TimeUtils::mealStartUtc(todayIst, mt);
                int64_t cutoffUtc = TimeUtils::mealCutoffUtc(todayIst, mt, Config::cutoffHoursBeforeMeal());
                Money price = plan ? plan->mealPrice(mt) : Money::fromRupees(50);
                Money refundAmt = service_.refundPolicy().compute(price);

                std::string slotStatus = "AVAILABLE";
                std::string existingReqId = "";
                for (const auto& cl : closures) {
                    if (cl.closureDate == todayIst && cl.mealType == mt) {
                        slotStatus = "MESS_CLOSED";
                        break;
                    }
                }
                if (slotStatus == "AVAILABLE") {
                    for (const auto& r : studentReqs) {
                        if (r.mealDate() == todayIst && r.mealType() == mt && r.status() != RequestStatus::CANCELLED) {
                            slotStatus = toString(r.status());
                            existingReqId = r.id();
                            break;
                        }
                    }
                }

                json mCard = json::object();
                mCard["mealType"] = toString(mt);
                mCard["mealDate"] = todayIst.toString();
                mCard["serveTimeIst"] = mealTimeLabels[i];
                mCard["startUtc"] = startUtc;
                mCard["cutoffUtc"] = cutoffUtc;
                mCard["cutoffIstPretty"] = TimeUtils::formatIstPretty(cutoffUtc);
                mCard["cutoffTimeOnlyIst"] = TimeUtils::formatIstTimeOnly(cutoffUtc);
                mCard["secondsUntilCutoff"] = cutoffUtc - now;
                mCard["cutoffPassed"] = (now > cutoffUtc);
                mCard["basePriceFormatted"] = price.toRupeeString();
                mCard["refundAmountFormatted"] = refundAmt.toRupeeString();
                mCard["slotStatus"] = slotStatus;
                mCard["existingRequestId"] = existingReqId;
                todayMeals.push_back(mCard);
            }
            out["todayMeals"] = todayMeals;

            // Available plans for meal swap / next-cycle change
            json plansArr = json::array();
            for (const MealPlan* p : service_.listPlans()) {
                json pj = json::object();
                pj["id"] = p->id();
                pj["name"] = p->displayName();
                pj["variant"] = toString(p->variant());
                pj["breakfastFormatted"] = p->mealPrice(MealType::BREAKFAST).toRupeeString();
                pj["lunchFormatted"] = p->mealPrice(MealType::LUNCH).toRupeeString();
                pj["dinnerFormatted"] = p->mealPrice(MealType::DINNER).toRupeeString();
                plansArr.push_back(pj);
            }
            out["plans"] = plansArr;

            // Skip requests list (for Month Calendar & Request History)
            json reqArr = json::array();
            for (const auto& r : studentReqs) {
                reqArr.push_back(serializeSkipRequest(service_, r));
            }
            out["skipRequests"] = reqArr;

            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 500, ex.what(), "SERVER_ERROR");
        }
    });

    svr_.Get("/api/student/wallet", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            Student stu = service_.getStudentById(sess.userId);
            json out = json::object();
            out["ok"] = true;
            out["balancePaise"] = stu.wallet().balance().paise();
            out["balanceFormatted"] = stu.wallet().balance().toRupeeString();
            out["totalCreditedFormatted"] = stu.wallet().totalCredited().toRupeeString();

            json txns = json::array();
            const auto& ledger = stu.wallet().transactions();
            for (auto it = ledger.rbegin(); it != ledger.rend(); ++it) {
                const auto& t = *it;
                if (!t) continue;
                json tj = json::object();
                tj["id"] = t->id();
                tj["type"] = toString(t->type());
                tj["amountPaise"] = t->amount().paise();
                tj["amountFormatted"] = t->amount().toRupeeString();
                tj["signedAmountFormatted"] = t->signedAmount().toRupeeString();
                tj["reason"] = t->reason();
                tj["refId"] = t->refId();
                tj["timestampUtc"] = t->timestampUtc();
                tj["timestampIst"] = TimeUtils::formatIstPretty(t->timestampUtc());
                txns.push_back(tj);
            }
            out["ledger"] = txns;
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        }
    });

    svr_.Post("/api/student/skips", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            json body = json::parse(req.body);
            std::string mode = body.value("mode", "SINGLE");
            std::string reason = body.value("reason", "Skipping meal");

            if (mode == "MULTI") {
                Date startDate = Date::parse(body.value("startDate", ""));
                Date endDate   = Date::parse(body.value("endDate", ""));
                std::vector<MealType> meals;
                if (body.contains("meals") && body["meals"].is_array()) {
                    for (const auto& mItem : body["meals"].get_array()) {
                        meals.push_back(mealTypeFromString(mItem.get_string()));
                    }
                }
                auto created = service_.submitMultiDaySkip(sess.userId, startDate, endDate, meals, reason);
                json out = json::object();
                out["ok"] = true;
                out["count"] = static_cast<int>(created.size());
                out["message"] = "Expanded multi-day request into " + std::to_string(created.size()) + " individual meal slots.";
                sendJson(res, 201, out);
            } else if (mode == "SWAP") {
                Date mealDate = Date::parse(body.value("mealDate", ""));
                MealType mealType = mealTypeFromString(body.value("mealType", "LUNCH"));
                std::string targetPlanId = body.value("targetPlanId", "PLAN-NONVEG");
                Money debited = service_.swapMealUpgrade(sess.userId, mealDate, mealType, targetPlanId);
                json out = json::object();
                out["ok"] = true;
                out["message"] = "Meal swap confirmed! Debited price difference of " + debited.toRupeeString() + " from wallet.";
                sendJson(res, 200, out);
            } else {
                Date mealDate = Date::parse(body.value("mealDate", ""));
                MealType mealType = mealTypeFromString(body.value("mealType", "LUNCH"));
                auto created = service_.submitSingleSkip(sess.userId, mealDate, mealType, reason);
                json out = json::object();
                out["ok"] = true;
                out["request"] = serializeSkipRequest(service_, created);
                out["message"] = "Skip request " + created.id() + " submitted for Admin review.";
                sendJson(res, 201, out);
            }
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "VALIDATION_RULE");
        }
    });

    svr_.Delete(R"(/api/student/skips/([A-Za-z0-9\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            std::string reqId = req.matches[1].str();
            service_.cancelPendingSkip(sess.userId, reqId);
            json out = json::object();
            out["ok"] = true;
            out["message"] = "Cancelled pending skip request " + reqId + ".";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Post(R"(/api/student/skips/([A-Za-z0-9\-]+)/appeal)", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            std::string reqId = req.matches[1].str();
            json body = json::parse(req.body);
            std::string appealReason = body.value("appealReason", "Requesting manual re-verification");
            auto updated = service_.appealRejectedSkip(sess.userId, reqId, appealReason);
            json out = json::object();
            out["ok"] = true;
            out["request"] = serializeSkipRequest(service_, updated);
            out["message"] = "Appeal submitted for request " + reqId + ". It is back in the Admin review queue.";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Post("/api/student/settlement-choice", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            json body = json::parse(req.body);
            if (body.contains("nextCyclePlanId") && !body.value("nextCyclePlanId", "").empty()) {
                service_.requestNextCyclePlanChange(sess.userId, body.value("nextCyclePlanId", ""));
            }
            if (body.contains("choice")) {
                SettlementChoice choice = settlementChoiceFromString(body.value("choice", "CARRY_FORWARD"));
                service_.setSettlementChoice(sess.userId, choice);
            }
            json out = json::object();
            out["ok"] = true;
            out["message"] = "Updated month-end settlement & next-cycle preferences.";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Get("/api/student/inbox", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
            auto items = service_.listNotificationsForStudent(sess.userId);
            json arr = json::array();
            for (const auto& n : items) {
                json nj = json::object();
                nj["id"] = n.id;
                nj["title"] = n.title;
                nj["message"] = n.message;
                nj["createdAtUtc"] = n.createdAtUtc;
                nj["createdAtIst"] = TimeUtils::formatIstPretty(n.createdAtUtc);
                arr.push_back(nj);
            }
            json out = json::object();
            out["ok"] = true;
            out["notifications"] = arr;
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        }
    });

    // ------------------------------------------------------------------------
    // 4. Admin Endpoints
    // ------------------------------------------------------------------------
    auto requireAdminOrStaff = [this](const httplib::Request& req) -> SessionRecord {
        SessionRecord sess = service_.authenticateToken(extractBearerOrCookieToken(req));
        if (sess.role != UserRole::ADMIN && sess.role != UserRole::MESS_STAFF) {
            throw MealWalletException("Admin or Mess Staff privileges required.", "ADMIN_AUTH_RULE");
        }
        return sess;
    };

    svr_.Post("/api/admin/students", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            json body = json::parse(req.body);
            std::string username   = body.value("username", "");
            std::string fullName   = body.value("fullName", "");
            std::string roomNumber = body.value("roomNumber", "B-101");
            std::string planId     = body.value("planId", "PLAN-VEG");

            Date todayIst = TimeUtils::currentIstDate(service_.nowUtc());
            Date startDate = body.contains("startDate") && !body.value("startDate", "").empty()
                ? Date::parse(body.value("startDate", ""))
                : Date(todayIst.year(), todayIst.month(), 1);
            Date endDate = body.contains("endDate") && !body.value("endDate", "").empty()
                ? Date::parse(body.value("endDate", ""))
                : Date(todayIst.year(), todayIst.month(), Date::daysInMonth(todayIst.year(), todayIst.month()));

            auto created = service_.createStudent(username, fullName, roomNumber, planId, startDate, endDate);

            json out = json::object();
            out["ok"] = true;
            out["studentId"] = created.student.id();
            out["username"] = created.student.username();
            out["fullName"] = created.student.fullName();
            out["roomNumber"] = created.student.roomNumber();
            out["planId"] = created.student.subscription().planId();
            out["generatedPassword"] = created.generatedPassword;
            out["message"] = "Created student account. Share initial password '" + created.generatedPassword + "' (forced change on first login).";
            sendJson(res, 201, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "VALIDATION_RULE");
        }
    });

    svr_.Post("/api/admin/subscriptions/renew", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            json body = json::parse(req.body);
            std::string studentId = body.value("studentId", "");
            std::string planId    = body.value("planId", "PLAN-VEG");
            std::string startStr  = body.value("startDate", "");
            std::string endStr    = body.value("endDate", "");

            if (studentId.empty() || startStr.empty() || endStr.empty()) {
                sendError(res, 400, "Missing required fields (studentId, planId, startDate, endDate)", "VALIDATION_RULE");
                return;
            }

            Date startDate = Date::parse(startStr);
            Date endDate   = Date::parse(endStr);
            auto renewedSub = service_.renewStudentSubscription(studentId, planId, startDate, endDate, "admin");

            json out = json::object();
            out["ok"] = true;
            out["message"] = "Plan renewed successfully until " + endDate.toString();
            out["startDate"] = renewedSub.startDate().toString();
            out["endDate"] = renewedSub.endDate().toString();
            out["planId"] = renewedSub.planId();
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "VALIDATION_RULE");
        }
    });

    svr_.Get("/api/admin/skips", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            std::string statusFilter = req.has_param("status") ? req.get_param_value("status") : "";
            auto list = service_.listSkipRequests(statusFilter);
            json arr = json::array();
            for (const auto& r : list) {
                arr.push_back(serializeSkipRequest(service_, r));
            }

            // Also include student roster & payouts for the Admin Console
            json stuArr = json::array();
            for (const auto& stu : service_.listStudents()) {
                json sj = json::object();
                sj["id"] = stu.id();
                sj["username"] = stu.username();
                sj["fullName"] = stu.fullName();
                sj["roomNumber"] = stu.roomNumber();
                sj["planId"] = stu.subscription().planId();
                sj["endDate"] = stu.subscription().endDate().toString();
                sj["settlementChoice"] = toString(stu.subscription().settlementChoice());
                sj["balancePaise"] = stu.wallet().balance().paise();
                sj["balanceFormatted"] = stu.wallet().balance().toRupeeString();
                sj["carryForwardPaise"] = stu.subscription().carryForwardCreditApplied().paise();
                sj["carryForwardFormatted"] = stu.subscription().carryForwardCreditApplied().toRupeeString();
                stuArr.push_back(sj);
            }

            json payArr = json::array();
            for (const auto& p : service_.listPayouts()) {
                json pj = json::object();
                pj["id"] = p.id;
                pj["studentId"] = p.studentId;
                try {
                    Student st = service_.getStudentById(p.studentId);
                    pj["studentName"] = st.fullName() + " (" + st.username() + ")";
                } catch (...) {
                    pj["studentName"] = p.studentId;
                }
                pj["settlementId"] = p.settlementId;
                pj["amountPaise"] = p.amount.paise();
                pj["amountFormatted"] = p.amount.toRupeeString();
                pj["paidOffline"] = p.paidOffline;
                pj["createdAtIst"] = TimeUtils::formatIstPretty(p.createdAtUtc);
                pj["paidAtIst"] = p.paidAtUtc > 0 ? TimeUtils::formatIstPretty(p.paidAtUtc) : "-";
                pj["paidNote"] = p.paidNote;
                payArr.push_back(pj);
            }

            json setArr = json::array();
            for (const auto& s : service_.listSettlements()) {
                json sj = json::object();
                sj["id"] = s.id;
                sj["studentId"] = s.studentId;
                try {
                    Student st = service_.getStudentById(s.studentId);
                    sj["studentName"] = st.fullName() + " (" + st.username() + ")";
                } catch (...) {
                    sj["studentName"] = s.studentId;
                }
                sj["yearMonth"] = s.yearMonth;
                sj["choice"] = toString(s.choice);
                sj["amountPaise"] = s.settledAmount.paise();
                sj["amountFormatted"] = s.settledAmount.toRupeeString();
                sj["payoutId"] = s.payoutId;
                sj["settledAtIst"] = TimeUtils::formatIstPretty(s.settledAtUtc);
                setArr.push_back(sj);
            }

            json out = json::object();
            out["ok"] = true;
            out["clock"] = buildClockStateJson(service_);
            out["skips"] = arr;
            out["students"] = stuArr;
            out["payouts"] = payArr;
            out["settlements"] = setArr;
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        }
    });

    svr_.Post(R"(/api/admin/skips/([A-Za-z0-9\-]+)/decision)", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = requireAdminOrStaff(req);
            std::string reqId = req.matches[1].str();
            json body = json::parse(req.body);
            std::string decision = body.value("decision", "APPROVE");
            bool approve = (decision == "APPROVE" || decision == "approve" || body.value("approve", false));
            std::string note = body.value("note", "");

            auto decided = service_.decideSkipRequest(reqId, approve, note, sess.username);
            json out = json::object();
            out["ok"] = true;
            out["request"] = serializeSkipRequest(service_, decided);
            out["message"] = approve
                ? ("Approved request " + reqId + " and credited student's wallet.")
                : ("Rejected request " + reqId + ".");
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Get("/api/admin/forecast", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            Date targetDate = TimeUtils::currentIstDate(service_.nowUtc()).addDays(1); // Default: tomorrow
            if (req.has_param("date") && !req.get_param_value("date").empty()) {
                targetDate = Date::parse(req.get_param_value("date"));
            }
            auto items = service_.getForecastForDate(targetDate);
            json arr = json::array();
            for (const auto& f : items) {
                json fj = json::object();
                fj["date"] = f.date.toString();
                fj["mealType"] = toString(f.mealType);
                fj["totalEnrolled"] = f.totalEnrolled;
                fj["approvedSkips"] = f.approvedSkips;
                fj["messClosed"] = f.messClosed;
                fj["expectedHeadcount"] = f.expectedHeadcount;
                arr.push_back(fj);
            }
            json out = json::object();
            out["ok"] = true;
            out["date"] = targetDate.toString();
            out["forecast"] = arr;
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "DATE_FORMAT_RULE");
        }
    });

    svr_.Post("/api/admin/closures", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = requireAdminOrStaff(req);
            json body = json::parse(req.body);
            Date d = Date::parse(body.value("closureDate", ""));
            MealType m = mealTypeFromString(body.value("mealType", "LUNCH"));
            std::string reason = body.value("reason", "Mess maintenance closure");

            ClosureRecord cl = service_.markMessClosure(d, m, reason, sess.username);
            json out = json::object();
            out["ok"] = true;
            out["closureId"] = cl.id;
            out["studentsCredited"] = cl.studentsCredited;
            out["totalCreditedFormatted"] = cl.totalCredited.toRupeeString();
            out["message"] = "Marked " + d.toString() + " " + toString(m) +
                             " closed. Auto-credited " + std::to_string(cl.studentsCredited) +
                             " active students (" + cl.totalCredited.toRupeeString() + " total).";
            sendJson(res, 201, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "VALIDATION_RULE");
        }
    });

    svr_.Post("/api/admin/attendance", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = requireAdminOrStaff(req);
            json body = json::parse(req.body);
            std::string studentId = body.value("studentId", "");
            Date d = Date::parse(body.value("mealDate", ""));
            MealType m = mealTypeFromString(body.value("mealType", "BREAKFAST"));

            auto att = service_.markAttendance(studentId, d, m, sess.username);
            json out = json::object();
            out["ok"] = true;
            out["attendanceId"] = att.id;
            out["message"] = "Logged counter attendance for " + studentId + " on " + d.toString() + " (" + toString(m) + "). Any skip request for this slot is now flagged SUSPICIOUS.";
            sendJson(res, 201, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "VALIDATION_RULE");
        }
    });

    svr_.Post("/api/admin/settlement/run", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = requireAdminOrStaff(req);
            json body = req.body.empty() ? json::object() : json::parse(req.body);
            std::string ym = body.value("yearMonth", "");
            auto results = service_.runMonthlySettlement(ym, sess.username);

            json arr = json::array();
            for (const auto& s : results) {
                json sj = json::object();
                sj["id"] = s.id;
                sj["studentId"] = s.studentId;
                sj["yearMonth"] = s.yearMonth;
                sj["choice"] = toString(s.choice);
                sj["settledFormatted"] = s.settledAmount.toRupeeString();
                sj["payoutId"] = s.payoutId;
                arr.push_back(sj);
            }
            json out = json::object();
            out["ok"] = true;
            out["settledCount"] = static_cast<int>(results.size());
            out["settlements"] = arr;
            out["message"] = "Processed month-end settlement for " + std::to_string(results.size()) + " student(s) with positive balance.";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Post(R"(/api/admin/payouts/([A-Za-z0-9\-]+)/paid)", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            SessionRecord sess = requireAdminOrStaff(req);
            std::string payoutId = req.matches[1].str();
            json body = req.body.empty() ? json::object() : json::parse(req.body);
            std::string note = body.value("note", "Paid offline via UPI / Hostel Office");
            auto updated = service_.markPayoutPaid(payoutId, note, sess.username);

            json out = json::object();
            out["ok"] = true;
            out["payoutId"] = updated.id;
            out["message"] = "Marked payout " + updated.id + " (" + updated.amount.toRupeeString() + ") as paid offline.";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        }
    });

    svr_.Get("/api/admin/audit", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            auto lines = service_.readAuditLogLines(120);
            json arr = json::array();
            for (const auto& l : lines) arr.push_back(l);
            json out = json::object();
            out["ok"] = true;
            out["lines"] = arr;
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        }
    });

    svr_.Get("/api/admin/rules", [this](const httplib::Request&, httplib::Response& res) {
        json out = json::object();
        out["ok"] = true;
        out["rules"] = buildRulesJson(service_);
        out["clock"] = buildClockStateJson(service_);
        sendJson(res, 200, out);
    });

    svr_.Get("/api/admin/backup", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            std::string dump = service_.exportBackupDump();
            res.status = 200;
            res.set_header("Content-Disposition", "attachment; filename=\"mealwallet_backup_dump.csv\"");
            res.set_content(dump, "text/csv; charset=utf-8");
        } catch (const MealWalletException& ex) {
            sendError(res, 401, ex.what(), ex.ruleName());
        }
    });

    // ------------------------------------------------------------------------
    // 5. Demo Mode Endpoints: GET/POST /api/admin/demo/clock, POST /api/admin/demo/reset
    // ------------------------------------------------------------------------
    svr_.Get("/api/admin/demo/clock", [this](const httplib::Request&, httplib::Response& res) {
        json out = json::object();
        out["ok"] = true;
        out["clock"] = buildClockStateJson(service_);
        sendJson(res, 200, out);
    });

    svr_.Post("/api/admin/demo/clock", [this, requireAdminOrStaff](const httplib::Request& req, httplib::Response& res) {
        try {
            requireAdminOrStaff(req);
            json body = json::parse(req.body);
            std::string action = body.value("action", "SET_MODE");

            if (action == "ADVANCE") {
                int hours = body.value("hours", 0);
                int days  = body.value("days", 0);
                service_.advanceDemoClock(hours, days);
            } else {
                bool enableDemo = body.value("demoMode", true);
                int64_t customUtc = 0;
                if (body.contains("istDateTime") && !body.value("istDateTime", "").empty()) {
                    customUtc = TimeUtils::parseIstInputLocal(body.value("istDateTime", ""));
                }
                service_.setDemoMode(enableDemo, customUtc);
            }

            json out = json::object();
            out["ok"] = true;
            out["clock"] = buildClockStateJson(service_);
            out["message"] = "Clock updated: " + TimeUtils::formatIstPretty(service_.nowUtc()) +
                             " (" + service_.clock().modeName() + ")";
            sendJson(res, 200, out);
        } catch (const MealWalletException& ex) {
            sendError(res, 400, ex.what(), ex.ruleName());
        } catch (const std::exception& ex) {
            sendError(res, 400, ex.what(), "CLOCK_PARSE_RULE");
        }
    });
}

} // namespace mealwallet
