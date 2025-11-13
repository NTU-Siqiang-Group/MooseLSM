#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <functional>

class HyperLogLog {
public:
    explicit HyperLogLog(uint8_t precision = 10): p(precision), m(1 << p), registers(m, 0) {}
    void add(const std::string& item) {
      uint32_t x = hash(item);
      uint32_t index = x >> (32 - p);
      uint32_t w = x << p;
      uint8_t r = rho(w, 32 - p);
      if (r > registers[index]) {
        registers[index] = r;
      }
    }
    void merge(const HyperLogLog& other) {
      for (size_t i = 0; i < m; ++i) {
        if (other.registers[i] > registers[i]) {
          registers[i] = other.registers[i];
        }
      }
    }
    double estimate() const {
      double alphaMM;
      switch (m) {
        case 16: alphaMM = 0.673 * m * m; break;
        case 32: alphaMM = 0.697 * m * m; break;
        case 64: alphaMM = 0.709 * m * m; break;
        default: alphaMM = (0.7213 / (1 + 1.079 / m)) * m * m; break;
      }

      double Z = 0.0;
      for (auto reg : registers) {
        Z += 1.0 / (1 << reg);
      }

      double E = alphaMM / Z;

      // Bias correction for small ranges (optional)
      if (E <= 2.5 * m) {
        int V = std::count(registers.begin(), registers.end(), 0);
        if (V != 0) {
          E = m * std::log(static_cast<double>(m) / V);
        }
      }
      return E;
    }

private:
  uint8_t p;
  uint32_t m;
  std::vector<uint8_t> registers;
  uint32_t hash(const std::string& data) const {
    std::hash<std::string> hasher;
    return static_cast<uint32_t>(hasher(data));  // Use std::hash for simplicity
  }
  uint8_t rho(uint32_t hashval, uint8_t max) const {
    uint8_t rank = 1;
    while ((hashval & (1 << (32 - rank))) == 0 && rank <= max) {
      rank++;
    }
    return rank;
  }
};