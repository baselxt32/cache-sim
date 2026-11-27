#include "cache.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cachesim {
namespace {

class Lru final : public ReplacementPolicy {
public:
    Lru(std::size_t sets, std::size_t ways)
        : time_(sets, std::vector<std::uint64_t>(ways, 0)) {}
    void access(std::size_t set, std::size_t way, bool) override {
        time_[set][way] = ++clock_;
    }
    std::size_t victim(std::size_t set) override {
        const auto& row = time_[set];
        return std::min_element(row.begin(), row.end()) - row.begin();
    }
private:
    std::vector<std::vector<std::uint64_t>> time_;
    std::uint64_t clock_ = 0;
};

class Fifo final : public ReplacementPolicy {
public:
    Fifo(std::size_t sets, std::size_t ways)
        : time_(sets, std::vector<std::uint64_t>(ways, 0)) {}
    void access(std::size_t set, std::size_t way, bool inserted) override {
        if (inserted) time_[set][way] = ++clock_;
    }
    std::size_t victim(std::size_t set) override {
        const auto& row = time_[set];
        return std::min_element(row.begin(), row.end()) - row.begin();
    }
private:
    std::vector<std::vector<std::uint64_t>> time_;
    std::uint64_t clock_ = 0;
};

class Random final : public ReplacementPolicy {
public:
    explicit Random(std::size_t ways, std::uint32_t seed)
        : rng_(seed), pick_(0, ways - 1) {}
    void access(std::size_t, std::size_t, bool) override {}
    std::size_t victim(std::size_t) override { return pick_(rng_); }
private:
    std::mt19937 rng_;
    std::uniform_int_distribution<std::size_t> pick_;
};

bool power_of_two(std::size_t value) {
    return value && !(value & (value - 1));
}

std::size_t checked_sets(const Config& config) {
    if (!power_of_two(config.size) || !power_of_two(config.ways) ||
        !power_of_two(config.block) || config.block > config.size ||
        config.ways > config.size / config.block || !config.latency)
        throw std::invalid_argument("cache size, ways, and block must be powers of two with valid ratios");
    return config.size / (config.ways * config.block);
}
}

std::unique_ptr<ReplacementPolicy> make_policy(const std::string& name,
                                                std::size_t sets,
                                                std::size_t ways,
                                                std::uint32_t seed) {
    if (name == "lru") return std::make_unique<Lru>(sets, ways);
    if (name == "fifo") return std::make_unique<Fifo>(sets, ways);
    if (name == "random") return std::make_unique<Random>(ways, seed);
    throw std::invalid_argument("unknown policy: " + name);
}

Memory::Memory(unsigned latency) : latency_(latency) {
    if (!latency) throw std::invalid_argument("memory latency must be positive");
}

std::uint64_t Memory::access(std::uint64_t, std::size_t size, bool) {
    if (!size) throw std::invalid_argument("access size must be positive");
    ++accesses_;
    return latency_;
}

Cache::Cache(Config config, std::string policy, bool write_back,
             Level& next, std::uint32_t seed)
    : config_(config), write_back_(write_back), next_(next),
      sets_(checked_sets(config)),
      lines_(sets_, std::vector<Line>(config.ways)),
      policy_(make_policy(policy, sets_, config.ways, seed)) {}

bool Cache::shadow_access(std::uint64_t block) {
    ++time_;
    for (auto& line : shadow_) {
        if (line.block == block) {
            line.time = time_;
            return true;
        }
    }
    if (shadow_.size() < config_.size / config_.block) {
        shadow_.push_back({block, time_});
    } else {
        auto victim = std::min_element(shadow_.begin(), shadow_.end(),
            [](const ShadowLine& a, const ShadowLine& b) { return a.time < b.time; });
        *victim = {block, time_};
    }
    return false;
}

std::uint64_t Cache::access(std::uint64_t addr, std::size_t size, bool write) {
    if (!size || addr > std::numeric_limits<std::uint64_t>::max() - (size - 1))
        throw std::invalid_argument("invalid address range");
    std::uint64_t cycles = 0;
    while (size) {
        const auto chunk = std::min(size, config_.block - static_cast<std::size_t>(addr % config_.block));
        cycles += access_block(addr, chunk, write);
        addr += chunk;
        size -= chunk;
    }
    return cycles;
}

std::uint64_t Cache::access_block(std::uint64_t addr, std::size_t size, bool write) {
    const auto block = addr / config_.block;
    const auto set = block % sets_;
    const auto tag = block / sets_;
    ++stats_.accesses;
    const bool first = seen_.insert(block).second;
    const bool shadow_hit = shadow_access(block);
    auto& row = lines_[set];
    for (std::size_t way = 0; way < row.size(); ++way) {
        if (row[way].valid && row[way].tag == tag) {
            ++stats_.hits;
            policy_->access(set, way, false);
            if (write && write_back_) row[way].dirty = true;
            return config_.latency +
                   (write && !write_back_ ? next_.access(addr, size, true) : 0);
        }
    }

    ++stats_.misses;
    if (first) ++stats_.compulsory;
    else if (!shadow_hit) ++stats_.capacity;
    else if (write && !write_back_) ++stats_.policy_misses;
    else ++stats_.conflict;

    if (write && !write_back_)
        return config_.latency + next_.access(addr, size, true);

    std::size_t way = 0;
    while (way < row.size() && row[way].valid) ++way;
    if (way == row.size()) way = policy_->victim(set);
    std::uint64_t cycles = config_.latency;
    if (row[way].valid) {
        ++stats_.evictions;
        if (row[way].dirty) {
            ++stats_.writebacks;
            const auto old_block = row[way].tag * sets_ + set;
            cycles += next_.access(old_block * config_.block, config_.block, true);
        }
    }
    cycles += next_.access(block * config_.block, config_.block, false);
    row[way] = {tag, true, write};
    policy_->access(set, way, true);
    return cycles;
}

Hierarchy::Hierarchy(Config l1i, Config l1d, const Config* l2,
                     std::string policy, bool write_back,
                     unsigned memory_latency, std::uint32_t seed)
    : memory_(memory_latency),
      l2_(l2 ? std::make_unique<Cache>(*l2, policy, true, memory_, seed + 2) : nullptr),
      l1i_(l1i, policy, true, l2_ ? static_cast<Level&>(*l2_) :
            static_cast<Level&>(memory_), seed),
      l1d_(l1d, policy, write_back, l2_ ? static_cast<Level&>(*l2_) :
            static_cast<Level&>(memory_), seed + 1) {}

Summary Hierarchy::run(const std::string& trace_path, std::uint64_t instructions,
                       double base_cpi) {
    std::ifstream input(trace_path);
    if (!input) throw std::runtime_error("cannot open trace: " + trace_path);
    Summary out;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        std::istringstream fields(line);
        char type, extra;
        std::string hex_addr;
        std::size_t size;
        if (!(fields >> type >> hex_addr >> size) || (fields >> extra) ||
            (type != 'I' && type != 'L' && type != 'S') || !size)
            throw std::runtime_error("bad trace line " + std::to_string(line_number));
        std::size_t consumed = 0;
        const auto addr = std::stoull(hex_addr, &consumed, 16);
        if (consumed != hex_addr.size())
            throw std::runtime_error("bad address on line " + std::to_string(line_number));
        if (type == 'I') {
            ++out.fetches;
            out.access_cycles += l1i_.access(addr, size, false);
        } else if (type == 'L') {
            ++out.loads;
            out.access_cycles += l1d_.access(addr, size, false);
        } else {
            ++out.stores;
            out.access_cycles += l1d_.access(addr, size, true);
        }
    }
    out.l1i = l1i_.stats();
    out.l1d = l1d_.stats();
    out.has_l2 = static_cast<bool>(l2_);
    if (l2_) out.l2 = l2_->stats();
    out.memory_accesses = memory_.accesses();
    out.instructions = instructions ? instructions : out.fetches;
    const auto accesses = out.fetches + out.loads + out.stores;
    if (!accesses || !out.instructions)
        throw std::runtime_error("trace has no accesses or instructions");
    out.amat = static_cast<double>(out.access_cycles) / accesses;
    const auto stalls = out.access_cycles > accesses ? out.access_cycles - accesses : 0;
    out.estimated_cpi = base_cpi + static_cast<double>(stalls) / out.instructions;
    return out;
}

Config parse_config(const std::string& text, unsigned latency) {
    std::istringstream input(text);
    std::string size_text, ways_text, block_text;
    if (!std::getline(input, size_text, ':') ||
        !std::getline(input, ways_text, ':') ||
        !std::getline(input, block_text, ':') ||
        input.rdbuf()->in_avail() || size_text.empty() || ways_text.empty() || block_text.empty())
        throw std::invalid_argument("config must be SIZE:WAYS:BLOCK");
    std::size_t scale = 1;
    if (size_text.back() == 'K' || size_text.back() == 'k') {
        scale = 1024;
        size_text.pop_back();
    } else if (size_text.back() == 'M' || size_text.back() == 'm') {
        scale = 1024 * 1024;
        size_text.pop_back();
    }
    auto number = [](const std::string& value) -> std::size_t {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::invalid_argument("invalid cache number: " + value);
        return std::stoull(value);
    };
    const auto bytes = number(size_text);
    if (bytes > std::numeric_limits<std::size_t>::max() / scale)
        throw std::invalid_argument("cache size is too large");
    return {bytes * scale, number(ways_text), number(block_text), latency};
}
}
