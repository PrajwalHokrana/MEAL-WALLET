#ifndef MEALWALLET_IREPOSITORY_HPP
#define MEALWALLET_IREPOSITORY_HPP

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mealwallet {

// Helper to split a pipe-delimited line ("a|b|c") preserving empty trailing fields
inline std::vector<std::string> splitPipeLine(const std::string& line, char delim = '|') {
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, delim)) {
        parts.push_back(item);
    }
    if (!line.empty() && line.back() == delim) {
        parts.push_back("");
    }
    return parts;
}

// Escape pipe and newline characters so user text never corrupts delimited records
inline std::string escapeField(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == '|') out += "/";
        else if (c == '\n' || c == '\r') out += " ";
        else out += c;
    }
    return out;
}

/**
 * [OOP Concept: Templates & Abstract Interface (IRepository<T>)]
 * Generic repository interface decoupling the domain service from how entities
 * of type T are persisted. At startup, Config::repositoryMode() selects
 * TextFileRepository<T>, BinaryFileRepository<T>, or InMemoryRepository<T>
 * with zero changes to domain code.
 */
template <typename T>
class IRepository {
public:
    virtual ~IRepository() = default;

    virtual std::vector<T> loadAll() = 0;
    virtual void saveAll(const std::vector<T>& items) = 0;
    virtual void appendOne(const T& item) = 0;
    virtual std::string backendName() const = 0;
    virtual int lastCorruptedLinesSkipped() const = 0;
};

/**
 * [OOP Concept: Template Class Inheritance — InMemoryRepository<T>]
 * Stores records purely in a std::vector<T> in RAM for fast isolated unit tests.
 */
template <typename T>
class InMemoryRepository : public IRepository<T> {
private:
    std::vector<T> storage_;

public:
    std::vector<T> loadAll() override { return storage_; }
    void saveAll(const std::vector<T>& items) override { storage_ = items; }
    void appendOne(const T& item) override { storage_.push_back(item); }
    std::string backendName() const override { return "IN_MEMORY"; }
    int lastCorruptedLinesSkipped() const override { return 0; }
};

/**
 * [OOP Concept: Template Class Inheritance — TextFileRepository<T>]
 * Persists records as human-readable pipe-delimited text files with a version header
 * (e.g. "#MEALWALLET_V1|students").
 * - Rewritten files use atomic write-to-temp-then-rename (`<file>.tmp` -> `<file>`).
 * - Append-only files (such as `ledger.txt`) append with immediate `std::flush`.
 * - Corrupted lines on load are skipped and logged instead of crashing.
 */
template <typename T>
class TextFileRepository : public IRepository<T> {
public:
    using Serializer   = std::function<std::string(const T&)>;
    using Deserializer = std::function<bool(const std::vector<std::string>&, T&)>;

private:
    std::string filePath_;
    std::string headerTag_;
    size_t expectedColumns_;
    Serializer serializer_;
    Deserializer deserializer_;
    int corruptedSkipped_ = 0;

public:
    TextFileRepository(std::string filePath,
                       std::string headerTag,
                       size_t expectedColumns,
                       Serializer serializer,
                       Deserializer deserializer)
        : filePath_(std::move(filePath)),
          headerTag_(std::move(headerTag)),
          expectedColumns_(expectedColumns),
          serializer_(std::move(serializer)),
          deserializer_(std::move(deserializer)) {}

    std::string backendName() const override { return "TEXT_FILE"; }
    int lastCorruptedLinesSkipped() const override { return corruptedSkipped_; }
    const std::string& filePath() const noexcept { return filePath_; }

    std::vector<T> loadAll() override {
        corruptedSkipped_ = 0;
        std::vector<T> items;
        std::ifstream in(filePath_);
        if (!in.is_open()) {
            return items; // Missing file is gracefully treated as empty collection
        }

        std::string line;
        int lineNum = 0;
        while (std::getline(in, line)) {
            ++lineNum;
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) continue;
            // Skip version header or comment lines starting with '#'
            if (line[0] == '#') continue;

            std::vector<std::string> cols = splitPipeLine(line, '|');
            if (cols.size() < expectedColumns_) {
                ++corruptedSkipped_;
                std::cerr << "[WARN][TextFileRepository] Skipping corrupted line " << lineNum
                          << " in " << filePath_ << " (expected " << expectedColumns_
                          << " fields, got " << cols.size() << "): " << line << "\n";
                continue;
            }
            T obj{};
            try {
                if (deserializer_(cols, obj)) {
                    items.push_back(std::move(obj));
                } else {
                    ++corruptedSkipped_;
                    std::cerr << "[WARN][TextFileRepository] Deserializer rejected line "
                              << lineNum << " in " << filePath_ << "\n";
                }
            } catch (const std::exception& ex) {
                ++corruptedSkipped_;
                std::cerr << "[WARN][TextFileRepository] Exception parsing line " << lineNum
                          << " in " << filePath_ << ": " << ex.what() << "\n";
            }
        }
        return items;
    }

    void saveAll(const std::vector<T>& items) override {
        // Atomic write-to-temp-then-rename so an interrupted write never corrupts the live file
        std::string tmpPath = filePath_ + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot open temporary file for writing: " + tmpPath);
            }
            out << "#MEALWALLET_V1|" << headerTag_ << "\n";
            for (const auto& item : items) {
                out << serializer_(item) << "\n";
            }
            out.flush();
            if (!out.good()) {
                throw std::runtime_error("Failed flushing temporary file: " + tmpPath);
            }
        }
        if (std::rename(tmpPath.c_str(), filePath_.c_str()) != 0) {
            std::remove(filePath_.c_str());
            if (std::rename(tmpPath.c_str(), filePath_.c_str()) != 0) {
                throw std::runtime_error("Failed renaming temp file " + tmpPath + " to " + filePath_);
            }
        }
    }

    void appendOne(const T& item) override {
        bool needHeader = false;
        {
            std::ifstream check(filePath_);
            if (!check.is_open() || check.peek() == std::ifstream::traits_type::eof()) {
                needHeader = true;
            }
        }
        std::ofstream out(filePath_, std::ios::out | std::ios::app);
        if (!out.is_open()) {
            throw std::runtime_error("Cannot open file for append: " + filePath_);
        }
        if (needHeader) {
            out << "#MEALWALLET_V1|" << headerTag_ << "\n";
        }
        out << serializer_(item) << "\n";
        out.flush();
    }
};

/**
 * [OOP Concept: Template Class Inheritance — BinaryFileRepository<T>]
 * Persists length-prefixed binary records with a 16-byte magic header ("MWBIN_V1")
 * and an 8-byte checksum per record so corrupted trailing records from simulated
 * interrupted writes are detected and skipped cleanly.
 */
template <typename T>
class BinaryFileRepository : public IRepository<T> {
public:
    using Serializer   = std::function<std::string(const T&)>;
    using Deserializer = std::function<bool(const std::vector<std::string>&, T&)>;

private:
    std::string filePath_;
    size_t expectedColumns_;
    Serializer serializer_;
    Deserializer deserializer_;
    int corruptedSkipped_ = 0;

    static constexpr const char* kMagic = "MWBIN_V1";

    static uint32_t computeChecksum(const std::string& payload) noexcept {
        uint32_t hash = 2166136261u;
        for (unsigned char c : payload) {
            hash ^= c;
            hash *= 16777619u;
        }
        return hash;
    }

public:
    BinaryFileRepository(std::string filePath,
                         size_t expectedColumns,
                         Serializer serializer,
                         Deserializer deserializer)
        : filePath_(std::move(filePath)),
          expectedColumns_(expectedColumns),
          serializer_(std::move(serializer)),
          deserializer_(std::move(deserializer)) {}

    std::string backendName() const override { return "BINARY_FILE"; }
    int lastCorruptedLinesSkipped() const override { return corruptedSkipped_; }

    std::vector<T> loadAll() override {
        corruptedSkipped_ = 0;
        std::vector<T> items;
        std::ifstream in(filePath_, std::ios::in | std::ios::binary);
        if (!in.is_open()) return items;

        char magic[8] = {0};
        in.read(magic, 8);
        if (in.gcount() != 8 || std::string(magic, 8) != kMagic) {
            ++corruptedSkipped_;
            std::cerr << "[WARN][BinaryFileRepository] Invalid magic header in " << filePath_ << "\n";
            return items;
        }

        while (in.peek() != EOF) {
            uint32_t len = 0;
            uint32_t expectedCrc = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (in.gcount() != sizeof(len)) {
                ++corruptedSkipped_;
                break;
            }
            in.read(reinterpret_cast<char*>(&expectedCrc), sizeof(expectedCrc));
            if (in.gcount() != sizeof(expectedCrc) || len > 1024 * 1024) {
                ++corruptedSkipped_;
                break;
            }
            std::string payload(len, '\0');
            in.read(&payload[0], len);
            if (static_cast<uint32_t>(in.gcount()) != len) {
                // Simulated interrupted write: truncated record at EOF
                ++corruptedSkipped_;
                std::cerr << "[WARN][BinaryFileRepository] Truncated binary record in " << filePath_ << "\n";
                break;
            }
            if (computeChecksum(payload) != expectedCrc) {
                ++corruptedSkipped_;
                std::cerr << "[WARN][BinaryFileRepository] Checksum mismatch in " << filePath_ << "\n";
                continue;
            }
            auto cols = splitPipeLine(payload, '|');
            if (cols.size() < expectedColumns_) {
                ++corruptedSkipped_;
                continue;
            }
            T obj{};
            try {
                if (deserializer_(cols, obj)) {
                    items.push_back(std::move(obj));
                } else {
                    ++corruptedSkipped_;
                }
            } catch (...) {
                ++corruptedSkipped_;
            }
        }
        return items;
    }

    void saveAll(const std::vector<T>& items) override {
        std::string tmpPath = filePath_ + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out.is_open()) {
                throw std::runtime_error("Cannot open binary temp file: " + tmpPath);
            }
            out.write(kMagic, 8);
            for (const auto& item : items) {
                std::string payload = serializer_(item);
                uint32_t len = static_cast<uint32_t>(payload.size());
                uint32_t crc = computeChecksum(payload);
                out.write(reinterpret_cast<const char*>(&len), sizeof(len));
                out.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
                out.write(payload.data(), len);
            }
            out.flush();
        }
        if (std::rename(tmpPath.c_str(), filePath_.c_str()) != 0) {
            std::remove(filePath_.c_str());
            std::rename(tmpPath.c_str(), filePath_.c_str());
        }
    }

    void appendOne(const T& item) override {
        bool needHeader = false;
        {
            std::ifstream check(filePath_, std::ios::in | std::ios::binary);
            if (!check.is_open() || check.peek() == EOF) needHeader = true;
        }
        std::ofstream out(filePath_, std::ios::out | std::ios::binary | std::ios::app);
        if (needHeader) {
            out.write(kMagic, 8);
        }
        std::string payload = serializer_(item);
        uint32_t len = static_cast<uint32_t>(payload.size());
        uint32_t crc = computeChecksum(payload);
        out.write(reinterpret_cast<const char*>(&len), sizeof(len));
        out.write(reinterpret_cast<const char*>(&crc), sizeof(crc));
        out.write(payload.data(), len);
        out.flush();
    }
};

} // namespace mealwallet

#endif // MEALWALLET_IREPOSITORY_HPP
