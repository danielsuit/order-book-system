#include "julia_bridge.h"
#include "../util/logger.h"

#include <cstring>
#include <filesystem>

#if CONVERGENCE_QUANTUM_ENABLED

#include <julia.h>

// Julia requires this macro to be defined before calling jl_init
JULIA_DEFINE_FAST_TLS

JuliaBridge& JuliaBridge::instance() {
    static JuliaBridge bridge;
    return bridge;
}

JuliaBridge::~JuliaBridge() {
    shutdown();
}

bool JuliaBridge::initialize(const std::string& julia_scripts_dir) {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    if (initialized_.load()) {
        return true;
    }

    LOG_INFO("Initializing Julia runtime...");

    // Initialize Julia
    jl_init();

    // Check for initialization errors
    if (jl_exception_occurred()) {
        LOG_ERROR("Failed to initialize Julia runtime");
        return false;
    }

    // Load the quantum modules
    std::string init_path = julia_scripts_dir + "/init.jl";
    if (!std::filesystem::exists(init_path)) {
        LOG_ERROR("Julia init script not found: " + init_path);
        jl_atexit_hook(0);
        return false;
    }

    std::string load_cmd = "include(\"" + init_path + "\")";
    jl_eval_string(load_cmd.c_str());

    if (jl_exception_occurred()) {
        jl_value_t* ex = jl_exception_occurred();
        const char* msg = jl_typeof_str(ex);
        LOG_ERROR(std::string("Julia init failed: ") + msg);
        jl_atexit_hook(0);
        return false;
    }

    // Cache function handles for performance
    fn_peer_index_ = jl_eval_string("bridge_quantum_peer_index");
    fn_jitter_ = jl_eval_string("bridge_quantum_jitter");
    fn_uint32_ = jl_eval_string("bridge_quantum_uint32");
    fn_schedule_ = jl_eval_string("bridge_quantum_schedule");
    fn_optimize_ = jl_eval_string("bridge_optimize_matching");

    if (!fn_peer_index_ || !fn_jitter_ || !fn_uint32_ ||
        !fn_schedule_ || !fn_optimize_) {
        LOG_ERROR("Failed to resolve Julia bridge functions");
        jl_atexit_hook(0);
        return false;
    }

    initialized_.store(true);
    LOG_INFO("Julia quantum computing bridge initialized");
    return true;
}

void JuliaBridge::shutdown() {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    if (!initialized_.load()) return;

    LOG_INFO("Shutting down Julia runtime...");
    jl_atexit_hook(0);
    initialized_.store(false);
}

jl_value_t* JuliaBridge::call_julia(const std::string& func_name,
                                     jl_value_t** args, int nargs) {
    jl_value_t* func = jl_eval_string(func_name.c_str());
    if (!func) return nullptr;

    jl_value_t* result = nullptr;
    if (nargs == 0) {
        result = jl_call0(reinterpret_cast<jl_function_t*>(func));
    } else if (nargs == 1) {
        result = jl_call1(reinterpret_cast<jl_function_t*>(func), args[0]);
    } else if (nargs == 2) {
        result = jl_call2(reinterpret_cast<jl_function_t*>(func), args[0], args[1]);
    } else {
        result = jl_call(reinterpret_cast<jl_function_t*>(func), args, nargs);
    }

    if (jl_exception_occurred()) {
        jl_value_t* ex = jl_exception_occurred();
        LOG_ERROR(std::string("Julia call '") + func_name + "' failed: " +
                  jl_typeof_str(ex));
        return nullptr;
    }

    return result;
}

jl_value_t* JuliaBridge::to_julia_float64_array(const std::vector<double>& vec) {
    jl_value_t* array_type = jl_apply_array_type(
        reinterpret_cast<jl_value_t*>(jl_float64_type), 1);
    jl_array_t* arr = jl_alloc_array_1d(array_type, vec.size());
    double* data = reinterpret_cast<double*>(jl_array_data(arr));
    std::memcpy(data, vec.data(), vec.size() * sizeof(double));
    return reinterpret_cast<jl_value_t*>(arr);
}

jl_value_t* JuliaBridge::to_julia_int64_array(const std::vector<int64_t>& vec) {
    jl_value_t* array_type = jl_apply_array_type(
        reinterpret_cast<jl_value_t*>(jl_int64_type), 1);
    jl_array_t* arr = jl_alloc_array_1d(array_type, vec.size());
    int64_t* data = reinterpret_cast<int64_t*>(jl_array_data(arr));
    std::memcpy(data, vec.data(), vec.size() * sizeof(int64_t));
    return reinterpret_cast<jl_value_t*>(arr);
}

uint32_t JuliaBridge::quantum_peer_index(int num_peers) {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    if (!initialized_.load()) return 0;

    jl_value_t* arg = jl_box_int64(num_peers);
    jl_value_t* result = jl_call1(
        reinterpret_cast<jl_function_t*>(fn_peer_index_), arg);

    if (jl_exception_occurred() || !result) {
        LOG_ERROR("quantum_peer_index failed");
        return 0;
    }

    return static_cast<uint32_t>(jl_unbox_int64(result));
}

double JuliaBridge::quantum_jitter(double base_interval) {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    if (!initialized_.load()) return 0.0;

    jl_value_t* arg = jl_box_float64(base_interval);
    jl_value_t* result = jl_call1(
        reinterpret_cast<jl_function_t*>(fn_jitter_), arg);

    if (jl_exception_occurred() || !result) {
        LOG_ERROR("quantum_jitter failed");
        return 0.0;
    }

    return jl_unbox_float64(result);
}

uint32_t JuliaBridge::quantum_uint32() {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    if (!initialized_.load()) return 0;

    jl_value_t* result = jl_call0(
        reinterpret_cast<jl_function_t*>(fn_uint32_));

    if (jl_exception_occurred() || !result) {
        LOG_ERROR("quantum_uint32 failed");
        return 0;
    }

    return static_cast<uint32_t>(jl_unbox_uint32(result));
}

std::vector<JuliaBridge::GossipPairQ> JuliaBridge::quantum_schedule(
        int num_nodes, int num_rounds) {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    std::vector<GossipPairQ> result_vec;

    if (!initialized_.load()) return result_vec;

    jl_value_t* arg1 = jl_box_int64(num_nodes);
    jl_value_t* arg2 = jl_box_int64(num_rounds);
    jl_value_t* args[] = {arg1, arg2};

    jl_value_t* result = jl_call(
        reinterpret_cast<jl_function_t*>(fn_schedule_), args, 2);

    if (jl_exception_occurred() || !result) {
        LOG_ERROR("quantum_schedule failed");
        return result_vec;
    }

    // Result is a (num_rounds x 2) Matrix{Int}
    jl_array_t* matrix = reinterpret_cast<jl_array_t*>(result);
    int64_t* data = reinterpret_cast<int64_t*>(jl_array_data(matrix));
    size_t nrows = jl_array_dim(matrix, 0);

    result_vec.reserve(nrows);
    for (size_t i = 0; i < nrows; ++i) {
        GossipPairQ pair;
        // Julia matrices are column-major: data[i] = col1[i], data[nrows+i] = col2[i]
        pair.sender = static_cast<uint32_t>(data[i]);
        pair.receiver = static_cast<uint32_t>(data[nrows + i]);
        result_vec.push_back(pair);
    }

    return result_vec;
}

std::vector<QuantumMatch> JuliaBridge::optimize_matching(
        const std::vector<double>& bids_prices,
        const std::vector<int64_t>& bids_quantities,
        const std::vector<double>& asks_prices,
        const std::vector<int64_t>& asks_quantities) {
    std::lock_guard<std::mutex> lock(julia_mutex_);

    std::vector<QuantumMatch> matches;

    if (!initialized_.load()) return matches;
    if (bids_prices.empty() || asks_prices.empty()) return matches;

    // Convert C++ vectors to Julia arrays
    JL_GC_PUSH4(nullptr, nullptr, nullptr, nullptr);

    jl_value_t* jl_bp = to_julia_float64_array(bids_prices);
    jl_value_t* jl_bq = to_julia_int64_array(bids_quantities);
    jl_value_t* jl_ap = to_julia_float64_array(asks_prices);
    jl_value_t* jl_aq = to_julia_int64_array(asks_quantities);

    jl_value_t* args[] = {jl_bp, jl_bq, jl_ap, jl_aq};

    jl_value_t* result = jl_call(
        reinterpret_cast<jl_function_t*>(fn_optimize_), args, 4);

    JL_GC_POP();

    if (jl_exception_occurred() || !result) {
        LOG_ERROR("optimize_matching failed");
        return matches;
    }

    // Result is a (N x 4) Matrix{Int64}
    jl_array_t* matrix = reinterpret_cast<jl_array_t*>(result);
    int64_t* data = reinterpret_cast<int64_t*>(jl_array_data(matrix));
    size_t nrows = jl_array_dim(matrix, 0);

    if (nrows == 0) return matches;

    matches.reserve(nrows);
    for (size_t i = 0; i < nrows; ++i) {
        QuantumMatch m;
        m.bid_idx = static_cast<int>(data[i]);                   // col 1
        m.ask_idx = static_cast<int>(data[nrows + i]);           // col 2
        m.quantity = data[2 * nrows + i];                         // col 3
        m.exec_price = static_cast<double>(data[3 * nrows + i]) / 10000.0;  // col 4
        matches.push_back(m);
    }

    return matches;
}

#else
// ============================================================================
// Stub implementation when Julia/quantum is not available at compile time
// ============================================================================

JuliaBridge& JuliaBridge::instance() {
    static JuliaBridge bridge;
    return bridge;
}

JuliaBridge::~JuliaBridge() {}

bool JuliaBridge::initialize(const std::string&) {
    LOG_WARN("Quantum computing disabled at compile time (Julia not found)");
    return false;
}

void JuliaBridge::shutdown() {}

uint32_t JuliaBridge::quantum_peer_index(int) { return 0; }
double JuliaBridge::quantum_jitter(double) { return 0.0; }
uint32_t JuliaBridge::quantum_uint32() { return 0; }

std::vector<JuliaBridge::GossipPairQ> JuliaBridge::quantum_schedule(int, int) {
    return {};
}

std::vector<QuantumMatch> JuliaBridge::optimize_matching(
        const std::vector<double>&, const std::vector<int64_t>&,
        const std::vector<double>&, const std::vector<int64_t>&) {
    return {};
}

#endif // CONVERGENCE_QUANTUM_ENABLED
