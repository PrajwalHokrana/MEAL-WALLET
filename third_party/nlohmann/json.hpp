#ifndef NLOHMANN_JSON_HPP
#define NLOHMANN_JSON_HPP

/**
 * Single-Header C++17 JSON Library (nlohmann::json compatible interface)
 * Supports object(), array(), initializer_list, operator[], value(), contains(),
 * parse(), dump(), booleans, int64_t, doubles, strings, arrays, and objects.
 */

#include <cctype>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace nlohmann {

class json {
public:
    enum class value_t {
        null,
        boolean,
        number_integer,
        number_float,
        string,
        array,
        object
    };

    using object_t = std::map<std::string, json>;
    using array_t  = std::vector<json>;

private:
    value_t type_ = value_t::null;
    bool bool_val_ = false;
    int64_t int_val_ = 0;
    double float_val_ = 0.0;
    std::string str_val_;
    array_t arr_val_;
    object_t obj_val_;

public:
    json() noexcept : type_(value_t::null) {}
    json(std::nullptr_t) noexcept : type_(value_t::null) {}
    json(bool b) noexcept : type_(value_t::boolean), bool_val_(b) {}
    json(int v) noexcept : type_(value_t::number_integer), int_val_(v), float_val_(v) {}
    json(long v) noexcept : type_(value_t::number_integer), int_val_(v), float_val_(static_cast<double>(v)) {}
    json(long long v) noexcept : type_(value_t::number_integer), int_val_(v), float_val_(static_cast<double>(v)) {}
    json(unsigned int v) noexcept : type_(value_t::number_integer), int_val_(v), float_val_(v) {}
    json(unsigned long v) noexcept : type_(value_t::number_integer), int_val_(static_cast<int64_t>(v)), float_val_(static_cast<double>(v)) {}
    json(double d) noexcept : type_(value_t::number_float), int_val_(static_cast<int64_t>(d)), float_val_(d) {}
    json(const char* s) : type_(value_t::string), str_val_(s ? s : "") {}
    json(std::string s) : type_(value_t::string), str_val_(std::move(s)) {}
    json(array_t arr) : type_(value_t::array), arr_val_(std::move(arr)) {}
    json(object_t obj) : type_(value_t::object), obj_val_(std::move(obj)) {}

    // Support initializer_list for both objects {{"k", v}, ...} and arrays
    json(std::initializer_list<json> init) {
        bool isObj = true;
        for (const auto& el : init) {
            if (!el.is_array() || el.arr_val_.size() != 2 || !el.arr_val_[0].is_string()) {
                isObj = false;
                break;
            }
        }
        if (isObj && init.size() > 0) {
            type_ = value_t::object;
            for (const auto& el : init) {
                obj_val_[el.arr_val_[0].str_val_] = el.arr_val_[1];
            }
        } else {
            type_ = value_t::array;
            arr_val_.assign(init.begin(), init.end());
        }
    }

    static json object() { return json(object_t{}); }
    static json array()  { return json(array_t{}); }

    bool is_null() const noexcept    { return type_ == value_t::null; }
    bool is_boolean() const noexcept { return type_ == value_t::boolean; }
    bool is_number() const noexcept  { return type_ == value_t::number_integer || type_ == value_t::number_float; }
    bool is_string() const noexcept  { return type_ == value_t::string; }
    bool is_array() const noexcept   { return type_ == value_t::array; }
    bool is_object() const noexcept  { return type_ == value_t::object; }

    size_t size() const noexcept {
        if (type_ == value_t::array) return arr_val_.size();
        if (type_ == value_t::object) return obj_val_.size();
        return 0;
    }

    bool empty() const noexcept { return size() == 0; }

    void push_back(const json& item) {
        if (type_ == value_t::null) type_ = value_t::array;
        if (type_ != value_t::array) throw std::runtime_error("json::push_back requires array");
        arr_val_.push_back(item);
    }

    json& operator[](const std::string& key) {
        if (type_ == value_t::null) type_ = value_t::object;
        if (type_ != value_t::object) throw std::runtime_error("json::operator[key] requires object");
        return obj_val_[key];
    }

    json& operator[](const char* key) {
        return (*this)[std::string(key)];
    }

    const json& operator[](const std::string& key) const {
        static const json kNull;
        if (type_ != value_t::object) return kNull;
        auto it = obj_val_.find(key);
        return (it != obj_val_.end()) ? it->second : kNull;
    }

    const json& operator[](size_t idx) const {
        static const json kNull;
        if (type_ != value_t::array || idx >= arr_val_.size()) return kNull;
        return arr_val_[idx];
    }

    bool contains(const std::string& key) const noexcept {
        return type_ == value_t::object && obj_val_.find(key) != obj_val_.end();
    }

    std::string get_string(const std::string& fallback = "") const {
        if (type_ == value_t::string) return str_val_;
        if (type_ == value_t::number_integer) return std::to_string(int_val_);
        if (type_ == value_t::boolean) return bool_val_ ? "true" : "false";
        return fallback;
    }

    int64_t get_int64(int64_t fallback = 0) const {
        if (type_ == value_t::number_integer) return int_val_;
        if (type_ == value_t::number_float) return static_cast<int64_t>(float_val_);
        if (type_ == value_t::string) {
            try { return std::stoll(str_val_); } catch (...) { return fallback; }
        }
        return fallback;
    }

    bool get_bool(bool fallback = false) const {
        if (type_ == value_t::boolean) return bool_val_;
        if (type_ == value_t::number_integer) return int_val_ != 0;
        if (type_ == value_t::string) return (str_val_ == "true" || str_val_ == "1");
        return fallback;
    }

    const array_t& get_array() const {
        static const array_t kEmpty;
        return (type_ == value_t::array) ? arr_val_ : kEmpty;
    }

    // Typed value(key, default) helper matching nlohmann::json
    std::string value(const std::string& key, const std::string& defVal) const {
        if (!contains(key)) return defVal;
        return (*this)[key].get_string(defVal);
    }

    std::string value(const std::string& key, const char* defVal) const {
        return value(key, std::string(defVal ? defVal : ""));
    }

    int value(const std::string& key, int defVal) const {
        if (!contains(key)) return defVal;
        return static_cast<int>((*this)[key].get_int64(defVal));
    }

    int64_t value(const std::string& key, int64_t defVal) const {
        if (!contains(key)) return defVal;
        return (*this)[key].get_int64(defVal);
    }

    bool value(const std::string& key, bool defVal) const {
        if (!contains(key)) return defVal;
        return (*this)[key].get_bool(defVal);
    }

    // Serialization (dump)
    static std::string escapeJsonString(const std::string& s) {
        std::ostringstream oss;
        oss << '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"':  oss << "\\\""; break;
                case '\\': oss << "\\\\"; break;
                case '\b': oss << "\\b";  break;
                case '\f': oss << "\\f";  break;
                case '\n': oss << "\\n";  break;
                case '\r': oss << "\\r";  break;
                case '\t': oss << "\\t";  break;
                default:
                    if (c < 0x20) {
                        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                    } else {
                        oss << static_cast<char>(c);
                    }
            }
        }
        oss << '"';
        return oss.str();
    }

    std::string dump(int indent = -1, int currentDepth = 0) const {
        std::string pad = (indent > 0) ? std::string(static_cast<size_t>(currentDepth * indent), ' ') : "";
        std::string childPad = (indent > 0) ? std::string(static_cast<size_t>((currentDepth + 1) * indent), ' ') : "";
        std::string nl = (indent > 0) ? "\n" : "";
        std::string sep = (indent > 0) ? ": " : ":";

        switch (type_) {
            case value_t::null:
                return "null";
            case value_t::boolean:
                return bool_val_ ? "true" : "false";
            case value_t::number_integer:
                return std::to_string(int_val_);
            case value_t::number_float: {
                std::ostringstream oss;
                oss << float_val_;
                return oss.str();
            }
            case value_t::string:
                return escapeJsonString(str_val_);
            case value_t::array: {
                if (arr_val_.empty()) return "[]";
                std::ostringstream oss;
                oss << "[" << nl;
                for (size_t i = 0; i < arr_val_.size(); ++i) {
                    oss << childPad << arr_val_[i].dump(indent, currentDepth + 1);
                    if (i + 1 < arr_val_.size()) oss << ",";
                    oss << nl;
                }
                oss << pad << "]";
                return oss.str();
            }
            case value_t::object: {
                if (obj_val_.empty()) return "{}";
                std::ostringstream oss;
                oss << "{" << nl;
                size_t i = 0;
                for (const auto& kv : obj_val_) {
                    oss << childPad << escapeJsonString(kv.first) << sep << kv.second.dump(indent, currentDepth + 1);
                    if (++i < obj_val_.size()) oss << ",";
                    oss << nl;
                }
                oss << pad << "}";
                return oss.str();
            }
        }
        return "null";
    }

    // Recursive-descent JSON Parser
    static json parse(const std::string& text) {
        size_t pos = 0;
        skipWs(text, pos);
        if (pos >= text.size()) return json::object();
        json result = parseValue(text, pos);
        return result;
    }

private:
    static void skipWs(const std::string& s, size_t& pos) noexcept {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
            ++pos;
        }
    }

    static json parseValue(const std::string& s, size_t& pos) {
        skipWs(s, pos);
        if (pos >= s.size()) throw std::runtime_error("Unexpected end of JSON input");
        char c = s[pos];
        if (c == '{') return parseObject(s, pos);
        if (c == '[') return parseArray(s, pos);
        if (c == '"') return json(parseString(s, pos));
        if (c == 't' || c == 'f') return parseBool(s, pos);
        if (c == 'n') return parseNull(s, pos);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(s, pos);
        throw std::runtime_error(std::string("Unexpected JSON token: ") + c);
    }

    static std::string parseString(const std::string& s, size_t& pos) {
        if (s[pos] != '"') throw std::runtime_error("Expected '\"'");
        ++pos;
        std::string out;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos >= s.size()) throw std::runtime_error("Unterminated escape in JSON string");
                char esc = s[pos++];
                switch (esc) {
                    case '"':  out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos + 4 <= s.size()) pos += 4;
                        out.push_back('?');
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("Unterminated JSON string");
    }

    static json parseObject(const std::string& s, size_t& pos) {
        ++pos; // skip '{'
        object_t obj;
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == '}') { ++pos; return json(obj); }
        while (pos < s.size()) {
            skipWs(s, pos);
            std::string key = parseString(s, pos);
            skipWs(s, pos);
            if (pos >= s.size() || s[pos] != ':') throw std::runtime_error("Expected ':' in JSON object");
            ++pos;
            obj[key] = parseValue(s, pos);
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == '}') { ++pos; return json(obj); }
            throw std::runtime_error("Expected ',' or '}' in JSON object");
        }
        throw std::runtime_error("Unterminated JSON object");
    }

    static json parseArray(const std::string& s, size_t& pos) {
        ++pos; // skip '['
        array_t arr;
        skipWs(s, pos);
        if (pos < s.size() && s[pos] == ']') { ++pos; return json(arr); }
        while (pos < s.size()) {
            arr.push_back(parseValue(s, pos));
            skipWs(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == ']') { ++pos; return json(arr); }
            throw std::runtime_error("Expected ',' or ']' in JSON array");
        }
        throw std::runtime_error("Unterminated JSON array");
    }

    static json parseBool(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "true") == 0)  { pos += 4; return json(true); }
        if (s.compare(pos, 5, "false") == 0) { pos += 5; return json(false); }
        throw std::runtime_error("Invalid boolean literal in JSON");
    }

    static json parseNull(const std::string& s, size_t& pos) {
        if (s.compare(pos, 4, "null") == 0) { pos += 4; return json(nullptr); }
        throw std::runtime_error("Invalid null literal in JSON");
    }

    static json parseNumber(const std::string& s, size_t& pos) {
        size_t start = pos;
        bool isFloat = false;
        if (s[pos] == '-') ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        if (pos < s.size() && s[pos] == '.') {
            isFloat = true;
            ++pos;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            isFloat = true;
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) ++pos;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        std::string numStr = s.substr(start, pos - start);
        if (isFloat) {
            return json(std::stod(numStr));
        }
        return json(static_cast<long long>(std::stoll(numStr)));
    }
};

} // namespace nlohmann

#endif // NLOHMANN_JSON_HPP
