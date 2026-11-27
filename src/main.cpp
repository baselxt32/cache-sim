#include "cache.hpp"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

using cachesim::Config;
using cachesim::Stats;

namespace {
void print_stats(const char* name, const Stats& stats) {
    std::cout << "  \"" << name << "\": {"
              << "\"accesses\":" << stats.accesses
              << ",\"hits\":" << stats.hits
              << ",\"misses\":" << stats.misses
              << ",\"compulsory\":" << stats.compulsory
              << ",\"capacity\":" << stats.capacity
              << ",\"conflict\":" << stats.conflict
              << ",\"policy_misses\":" << stats.policy_misses
              << ",\"evictions\":" << stats.evictions
              << ",\"writebacks\":" << stats.writebacks << "}";
}

void usage() {
    std::cerr << "usage: cachesim --trace FILE [--l1i 8K:2:32] [--l1d 8K:4:32]"
              << " [--l2 64K:8:64] [--policy lru|fifo|random]"
              << " [--write-policy wbwa|wtna] [--l1-latency N]"
              << " [--l2-latency N] [--memory-latency N]"
              << " [--base-cpi N] [--instructions N] [--seed N]\n";
}
}

int main(int argc, char** argv) {
    try {
        std::string trace, l1i_text = "8K:2:32", l1d_text = "8K:4:32";
        std::string l2_text, policy = "lru", write_policy = "wbwa";
        unsigned l1_latency = 1, l2_latency = 10, memory_latency = 100;
        std::uint64_t instructions = 0;
        std::uint32_t seed = 1;
        double base_cpi = 1.0;
        for (int arg = 1; arg < argc; ++arg) {
            const std::string key = argv[arg];
            if (key == "--help") { usage(); return 0; }
            if (arg + 1 == argc) throw std::invalid_argument("missing value for " + key);
            const std::string value = argv[++arg];
            if (key == "--trace") trace = value;
            else if (key == "--l1i") l1i_text = value;
            else if (key == "--l1d") l1d_text = value;
            else if (key == "--l2") l2_text = value;
            else if (key == "--policy") policy = value;
            else if (key == "--write-policy") write_policy = value;
            else if (key == "--l1-latency") l1_latency = std::stoul(value);
            else if (key == "--l2-latency") l2_latency = std::stoul(value);
            else if (key == "--memory-latency") memory_latency = std::stoul(value);
            else if (key == "--base-cpi") base_cpi = std::stod(value);
            else if (key == "--instructions") instructions = std::stoull(value);
            else if (key == "--seed") seed = std::stoul(value);
            else throw std::invalid_argument("unknown option: " + key);
        }
        if (trace.empty()) throw std::invalid_argument("--trace is required");
        if (write_policy != "wbwa" && write_policy != "wtna")
            throw std::invalid_argument("write policy must be wbwa or wtna");
        if (!std::isfinite(base_cpi) || base_cpi < 0)
            throw std::invalid_argument("base CPI must be finite and nonnegative");
        Config l1i = cachesim::parse_config(l1i_text, l1_latency);
        Config l1d = cachesim::parse_config(l1d_text, l1_latency);
        Config l2;
        const Config* l2_ptr = nullptr;
        if (!l2_text.empty()) {
            l2 = cachesim::parse_config(l2_text, l2_latency);
            l2_ptr = &l2;
        }
        cachesim::Hierarchy sim(l1i, l1d, l2_ptr, policy,
                                write_policy == "wbwa", memory_latency, seed);
        const auto result = sim.run(trace, instructions, base_cpi);
        std::cout << std::fixed << std::setprecision(9) << "{\n";
        print_stats("l1i", result.l1i); std::cout << ",\n";
        print_stats("l1d", result.l1d);
        if (result.has_l2) {
            std::cout << ",\n";
            print_stats("l2", result.l2);
        }
        std::cout << ",\n  \"fetches\":" << result.fetches
                  << ",\"loads\":" << result.loads
                  << ",\"stores\":" << result.stores
                  << ",\"instructions\":" << result.instructions
                  << ",\"memory_accesses\":" << result.memory_accesses
                  << ",\"access_cycles\":" << result.access_cycles
                  << ",\"amat\":" << result.amat
                  << ",\"estimated_cpi\":" << result.estimated_cpi << "\n}\n";
    } catch (const std::exception& error) {
        std::cerr << "cachesim: " << error.what() << '\n';
        usage();
        return 1;
    }
}
