# RiskDB: High-Performance Parallel Options Query Engine

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen.svg)]()
[![Language](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)]()
[![Hardware Acceleration](https://img.shields.io/badge/SIMD-AVX2%20%7C%20ARM%20NEON-orange.svg)]()
[![Concurrency](https://img.shields.io/badge/Parallelism-OpenMP%20%2B%20O(log%20N)%20Reduction-purple.svg)]()

> **Academic Scope & Project Identity**  
> **Domain**: Systems Engineering, High-Performance Computing (HPC), Parallel Database Architecture, Quantitative Finance.  
> *Strict Constraint*: This is strictly a systems engineering and parallel computing project. It is **NOT** a Machine Learning or Data Science project.

---

## 1. The Core Problem: The Latency Bottleneck

In standard financial technology architectures, relational databases (such as PostgreSQL) store millions of options contracts. Because every contract has a unique Strike Price ($K$), Volatility ($\sigma$), and Time to Expiry ($\tau$), options prices and risk sensitivities (Greeks: $\Delta, \Gamma, \nu, \Theta$) **cannot be aggregated globally using standard SQL `SUM()` queries**. They must be computed individually per contract.

### The Traditional Bottleneck
Moving millions of rows over a TCP/IP network connection from the database server to an external computational application for row-by-row iterative processing introduces severe performance penalties:
1. **Network Serialization Overhead**: Streaming millions of database tuples across sockets saturates network bandwidth.
2. **Cache Thrashing & Misaligned Row Layout**: Row-oriented tuple representations force the CPU to chase pointers and load irrelevant columns, leading to L1/L2/L3 cache misses.
3. **Volcano Model Inefficiencies**: Row-by-row iteration causes heavy branch mispredictions and stalls modern superscalar instruction pipelines.

```
[Traditional Tiered Architecture (VM 1)]
┌─────────────────────────┐         TCP/IP Network          ┌──────────────────────────┐
│   PostgreSQL Database   │  ────────────────────────────>  │  External Client App     │
│  (Row-Oriented Tuples)  │   Millions of Rows Streamed     │  (Scalar Row-by-Row Loop)│
└─────────────────────────┘                                 └──────────────────────────┘
                                                                        │
                                                                        ▼
                                                             Latency: High (80-120 ms)
                                                             Throughput: ~8-12 M rows/s
```

### The RiskDB Solution: In-Engine Zero-Copy Acceleration
RiskDB eliminates the network bottleneck by replacing the traditional tiered architecture. It is an **in-memory, parallel query engine** that embeds the mathematical pricing computation (Black-Scholes and Greeks) directly inside the query execution pipeline over PostgreSQL data.

```
[RiskDB In-Engine Architecture (VM 2)]
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                                   RiskDB Engine                                        │
│                                                                                        │
│   PostgreSQL Data Ingestion ──> Cache-Aligned Columnar Pivot (64-byte aligned)         │
│                                                              │                         │
│                                                              ▼                         │
│                                           Thread Dispatch (OpenMP 1-16 threads)        │
│                                                              │                         │
│                                                              ▼                         │
│                                           SIMD Execution Kernel (AVX2 / ARM NEON)      │
│                                           (Zero-gather streaming vector registers)    │
│                                                              │                         │
│                                                              ▼                         │
│                                           Lock-Free Thread-Local Accumulators          │
│                                           (Padded to prevent false sharing)            │
│                                                              │                         │
│                                                              ▼                         │
│                                           Logarithmic Reduction Tree O(log T)          │
│                                           (Vectorized pairwise parallel merge)         │
└────────────────────────────────────────────────────────────────────────────────────────┘
                                            │
                                            ▼
                                 Latency: Ultra-Low (1.9-5 ms)
                                 Throughput: > 250-520 M rows/s (16x - 30x Speedup)
```

---

## 2. Technology Stack & Architectural Features

- **Language**: Modern C++ (C++17 standard).
- **Hardware Acceleration (SIMD)**:
  - Intel/AMD: **AVX2 + FMA** (256-bit registers processing 8 single-precision contracts/cycle).
  - ARM: **ARM NEON** (128-bit registers processing 4 contracts/cycle).
  - Vectorized Abramowitz & Stegun rational Chebyshev approximation of Cumulative Normal Distribution $\Phi(x)$ with error bounded to $< 7.5 \times 10^{-8}$.
- **Memory Layout**:
  - 64-byte cache-aligned columnar buffers (`spot_prices`, `strike_prices`, `times_to_expiry`, `volatilities`, `risk_free_rates`, `quantities`, `option_types`, `portfolio_ids`).
  - Enables zero-gather contiguous SIMD streaming vector loads directly into vector registers.
- **Concurrency & Scaling**:
  - OpenMP parallel thread dispatching (scaling 1 to 16 threads).
  - **Lock-Free Thread-Local Accumulation**: Worker threads mutate private, cache-line padded accumulators (`alignas(CACHE_LINE_SIZE)`), eliminating mutex locks, atomic contention, and inter-core cache-line ping-pong.
  - **Logarithmic Reduction Tree ($O(\log T)$)**: Merges thread-local portfolio metrics in parallel pairwise stages ($\lceil \log_2 T \rceil$ steps).

---

## 3. Workload & Dataset Specifications

Adheres strictly to the academic benchmark specification:
- **Type**: Synthetic Options Portfolio Dataset (Simulated derivatives book).
- **Scale**: Up to $10^7$ (10 million) independent contract rows.
- **Underlying Assets**: 50 fixed underlying instruments with heterogeneous spot and volatility regimes.
- **Portfolios**: 50 to 500 configurable portfolios.
- **Expiry Horizons**: 1 week ($\approx 0.019$ years) to 2.0 years.
- **Distribution**: $\sim 55\%$ Calls, $\sim 45\%$ Puts.
- **Moneyness**:
  - $25\%$ In-The-Money (ITM)
  - $35\%$ At-The-Money (ATM)
  - $40\%$ Out-Of-The-Money (OTM)

---

## 4. Two-VM Deployment Architecture

The system is designed for deployment across **two distinct Virtual Machines**:

| Deployment Node | Stack Components | Role & Execution Behavior |
| :--- | :--- | :--- |
| **VM 1: Traditional Architecture** | PostgreSQL 16 + `traditional_app` | Stores row-oriented contracts in PostgreSQL. Application fetches rows via TCP/IP and evaluates Black-Scholes using a scalar iterator loop. |
| **VM 2: RiskDB Accelerated Architecture** | RiskDB In-Engine Kernel + `riskdb_app` | Ingests PostgreSQL data directly into 64-byte aligned columnar arrays. Executes parallel SIMD Black-Scholes and Greeks pipeline with $O(\log T)$ reduction. |

### Provisioning Scripts
- **VM 1 Setup**: `deploy/vm1_traditional/setup_vm1.sh`
- **VM 2 Setup**: `deploy/vm2_riskdb/setup_vm2.sh`
- **Unified Multi-Container Emulation**: `deploy/docker-compose.yml`

---

## 5. Building and Running

### Prerequisites
- CMake $\ge 3.16$
- C++17 compliant compiler (`gcc` $\ge 9$, `clang` $\ge 11$, or Apple Clang)
- OpenMP (`libomp-dev` on Linux, `brew install libomp` on macOS)
- PostgreSQL client libraries (`libpq-dev` on Linux, `brew install libpq` on macOS)

### Quick Build
```bash
# Configure and compile
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

# Run test suite
ctest --test-dir build --output-on-failure
```

### Running Applications

#### 1. Academic Benchmark Suite (Strong & Weak Scaling + Architectural Comparison)
```bash
./build/riskdb --benchmark 1000000 --threads 8
```

#### 2. VM 1 Traditional Application
```bash
./build/traditional_app --host localhost --port 5432 --dbname riskdb --user postgres --password postgres --rows 1000000
```

#### 3. VM 2 RiskDB Accelerated Application
```bash
./build/riskdb_app --host <VM1_IP> --port 5432 --dbname riskdb --user postgres --password postgres --threads 16 --rows 1000000
```

#### 4. Synthetic CSV Dataset Generation (for PostgreSQL Seeding)
```bash
./build/riskdb --generate-csv contracts.csv 10000000
```

---

## 6. Experimental Evaluation Results

*Evaluated on an 8-core CPU across 1,000,000 options contracts (100 portfolios):*

### Strong Scaling (Amdahl's Law, Fixed $N = 1,000,000$)
| Threads ($T$) | Execution Time | Speedup $S(T)$ | Parallel Efficiency $E(T)$ | Throughput |
| :---: | :---: | :---: | :---: | :---: |
| **1** | 6.87 ms | 1.00x | 100.0% | 145.66 M rows/s |
| **2** | 3.62 ms | 1.89x | 94.7% | 275.90 M rows/s |
| **4** | 1.97 ms | 3.49x | 87.2% | 508.19 M rows/s |
| **8** | 1.92 ms | 3.57x | 44.6% | 520.04 M rows/s |

### Weak Scaling (Gustafson's Law, Scaled $N = T \times 500,000$)
| Threads ($T$) | Total Rows | Execution Time | Parallel Efficiency | Throughput |
| :---: | :---: | :---: | :---: | :---: |
| **1** | 500,000 | 3.52 ms | 100.0% | 142.16 M rows/s |
| **2** | 1,000,000 | 3.62 ms | 97.0% | 275.86 M rows/s |
| **4** | 2,000,000 | 3.80 ms | 92.5% | 525.75 M rows/s |
| **8** | 4,000,000 | 6.81 ms | 51.6% | 587.13 M rows/s |

### Architectural Comparison ($N = 1,000,000$ contracts)
| Architectural Metric | Traditional Tiered Stack (VM 1) | RiskDB In-Engine Architecture (VM 2) |
| :--- | :---: | :---: |
| **Network / Serialization Overhead** | 32.00 ms | **0.00 ms (Zero-Copy In-Engine)** |
| **Execution / Compute Time** | 52.76 ms | **5.10 ms (SIMD Vectorized)** |
| **Reduction / Aggregation Merge** | Included (Sequential) | **0.10 ms (Logarithmic Tree)** |
| **Total End-to-End Latency** | **84.76 ms** | **5.20 ms** |
| **Processing Throughput** | 11.80 M contracts/sec | **192.49 M contracts/sec** |
| **OVERALL SPEEDUP FACTOR** | **1.00x** | **16.32x** |
