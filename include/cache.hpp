#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace cachesim {

struct Config {
    std::size_t size = 8192;
    std::size_t ways = 4;
    std::size_t block = 32;
    unsigned latency = 1;
};

struct Stats {
    std::uint64_t accesses = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t compulsory = 0;
    std::uint64_t capacity = 0;
    std::uint64_t conflict = 0;
    std::uint64_t policy_misses = 0;
    std::uint64_t evictions = 0;
    std::uint64_t writebacks = 0;
};

class ReplacementPolicy {
public:
    virtual ~ReplacementPolicy() = default;
    virtual void access(std::size_t set, std::size_t way, bool inserted) = 0;
    virtual std::size_t victim(std::size_t set) = 0;
};

std::unique_ptr<ReplacementPolicy> make_policy(const std::string& name,
                                                std::size_t sets,
                                                std::size_t ways,
                                                std::uint32_t seed = 1);

class Level {
public:
    virtual ~Level() = default;
    virtual std::uint64_t access(std::uint64_t addr, std::size_t size,
                                 bool write) = 0;
};

class Memory final : public Level {
public:
    explicit Memory(unsigned latency);
    std::uint64_t access(std::uint64_t addr, std::size_t size, bool write) override;
    std::uint64_t accesses() const { return accesses_; }
private:
    unsigned latency_;
    std::uint64_t accesses_ = 0;
};

class Cache final : public Level {
public:
    Cache(Config config, std::string policy, bool write_back,
          Level& next, std::uint32_t seed = 1);
    std::uint64_t access(std::uint64_t addr, std::size_t size, bool write) override;
    const Stats& stats() const { return stats_; }
    const Config& config() const { return config_; }
private:
    struct Line {
        std::uint64_t tag = 0;
        bool valid = false;
        bool dirty = false;
    };
    struct ShadowLine {
        std::uint64_t block = 0;
        std::uint64_t time = 0;
    };
    std::uint64_t access_block(std::uint64_t addr, std::size_t size, bool write);
    bool shadow_access(std::uint64_t block);
    Config config_;
    bool write_back_;
    Level& next_;
    std::size_t sets_;
    std::vector<std::vector<Line>> lines_;
    std::unique_ptr<ReplacementPolicy> policy_;
    std::vector<ShadowLine> shadow_;
    std::unordered_set<std::uint64_t> seen_;
    std::uint64_t time_ = 0;
    Stats stats_;
};

struct Summary {
    Stats l1i;
    Stats l1d;
    Stats l2;
    bool has_l2 = false;
    std::uint64_t memory_accesses = 0;
    std::uint64_t fetches = 0;
    std::uint64_t loads = 0;
    std::uint64_t stores = 0;
    std::uint64_t instructions = 0;
    std::uint64_t access_cycles = 0;
    double amat = 0;
    double estimated_cpi = 0;
};

class Hierarchy {
public:
    Hierarchy(Config l1i, Config l1d, const Config* l2,
              std::string policy, bool write_back,
              unsigned memory_latency, std::uint32_t seed = 1);
    Summary run(const std::string& trace_path, std::uint64_t instructions,
                double base_cpi);
private:
    Memory memory_;
    std::unique_ptr<Cache> l2_;
    Cache l1i_;
    Cache l1d_;
};

Config parse_config(const std::string& text, unsigned latency);
}
