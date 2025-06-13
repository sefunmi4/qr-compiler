#pragma once
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <string>
#include <fstream>
#include "wavefunction.h"

namespace qpp {
struct QRegister {
    explicit QRegister(size_t n)
        : num_qubits(n), start_time(std::chrono::steady_clock::now()) {}

    void ensure_allocated() const {
        if (!wf) wf = std::make_unique<Wavefunction<>>(num_qubits);
    }

    Wavefunction<> &wave() const {
        ensure_allocated();
        return *wf;
    }

    void reset_metrics() { op_count = 0; start_time = std::chrono::steady_clock::now(); }
    double elapsed_seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    }

    void h(std::size_t q) { ++op_count; wave().apply_h(q); }
    void x(std::size_t q) { ++op_count; wave().apply_x(q); }
    void y(std::size_t q) { ++op_count; wave().apply_y(q); }
    void z(std::size_t q) { ++op_count; wave().apply_z(q); }
    void rx(std::size_t q, double theta) { ++op_count; wave().apply_rx(q, theta); }
    void ry(std::size_t q, double theta) { ++op_count; wave().apply_ry(q, theta); }
    void rz(std::size_t q, double theta) { ++op_count; wave().apply_rz(q, theta); }
    void cnot(std::size_t c, std::size_t t) { ++op_count; wave().apply_cnot(c, t); }
    void cz(std::size_t c, std::size_t t) { ++op_count; wave().apply_cz(c, t); }
    void ccnot(std::size_t c1, std::size_t c2, std::size_t t) { ++op_count; wave().apply_ccnot(c1, c2, t); }
    void s(std::size_t q) { ++op_count; wave().apply_s(q); }
    void t(std::size_t q) { ++op_count; wave().apply_t(q); }
    void swap(std::size_t a, std::size_t b) { ++op_count; wave().apply_swap(a, b); }
    int measure(std::size_t q) { ++op_count; return wave().measure(q); }
    std::size_t measure(const std::vector<std::size_t>& qs) { op_count += qs.size(); return wave().measure(qs); }
    void reset() { wave().reset(); reset_metrics(); }

    std::complex<double> amp(std::size_t idx) const { return wave().amplitude(idx); }
    void resize(std::size_t n) { wf = std::make_unique<Wavefunction<>>(n); }
    void compress() { wave().compress(); }
    void decompress() { wave().decompress(); }
    std::size_t nnz() const { return wave().nnz(); }
    bool using_sparse() const { return wave().using_sparse(); }
    std::size_t ops() const { return op_count; }
  
  
    mutable std::unique_ptr<Wavefunction<>> wf;
    std::size_t num_qubits;

    bool save_to_file(const std::string& path) {
        wave().decompress();
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        size_t n = wave().state.size();
        ofs.write(reinterpret_cast<const char*>(&n), sizeof(size_t));
        ofs.write(reinterpret_cast<const char*>(wave().state.data()),
                  n * sizeof(std::complex<double>));
        return true;
    }

    bool load_from_file(const std::string& path) {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) return false;
        size_t n;
        ifs.read(reinterpret_cast<char*>(&n), sizeof(size_t));
        std::vector<std::complex<double>> st(n);
        ifs.read(reinterpret_cast<char*>(st.data()), n * sizeof(std::complex<double>));
        wave().decompress();
        if (st.size() != wave().state.size()) return false;
        wave().state = std::move(st);
        return true;
    }
    std::chrono::steady_clock::time_point start_time;
    std::size_t op_count{0};
};

// TODO(good-first-issue): enhance QRegister with save/load helpers

struct CRegister {
    std::vector<int> bits;
    explicit CRegister(size_t n) : bits(n, 0) {}
};

class MemoryManager {
public:
    int create_qregister(size_t n);
    bool release_qregister(int id);
    int create_cregister(size_t n);
    bool release_cregister(int id);
    std::vector<int> create_qregisters(const std::vector<size_t>& sizes);
    void release_qregisters(const std::vector<int>& ids);
    std::vector<int> create_cregisters(const std::vector<size_t>& sizes);
    void release_cregisters(const std::vector<int>& ids);
    QRegister& qreg(int id);
    CRegister& creg(int id);
    // statistics helpers
    size_t qreg_allocs(int id);
    size_t creg_allocs(int id);

    // live memory statistics
    size_t memory_usage();

    // state import/export
    std::vector<std::complex<double>> export_state(int id);
    bool import_state(int id, const std::vector<std::complex<double>>& st);
  
    // resonance zone cache helpers
    bool save_resonance_zone(int id, const std::string& key);
    bool load_resonance_zone(int id, const std::string& key);
  
    bool save_state_to_file(int id, const std::string& path);
    bool load_state_from_file(int id, const std::string& path);
    bool checkpoint_if_needed(int id, std::size_t op_threshold,
                              double time_threshold_sec,
                              const std::string& file);

private:
    std::vector<std::unique_ptr<QRegister>> qregs;
    std::vector<std::unique_ptr<CRegister>> cregs;
    std::vector<size_t> qalloc_count;
    std::vector<size_t> calloc_count;
    std::vector<int> free_qids;
    std::vector<int> free_cids;
    std::unordered_map<std::string, std::vector<std::complex<double>>> resonance_cache;
    std::mutex mtx;
};



extern MemoryManager memory;
}

