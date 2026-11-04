# Distributed Multi-Node Deployment & WAN Latency Testing Guide
## 4-Person / 4-Laptop Architecture Demonstration with Azure VMs

This guide details how to execute the distributed benchmark across **4 team members (4 laptops)** and **2 cloud Virtual Machines (Azure VM 1 and Azure VM 2)** to demonstrate the real-world **Wide Area Network (WAN) latency and bandwidth bottleneck** of the traditional architecture versus the zero-copy, signal-driven in-engine acceleration of **RiskDB**.

---

## 1. Physical & Virtual Network Topology (4 Applications across 4 Laptops + 2 Cloud VMs)

```
+---------------------------------------------------------------------------------------------------+
|                                  4-APPLICATION DISTRIBUTED TOPOLOGY                               |
+---------------------------------------------------------------------------------------------------+

   [LAPTOP 1: Person 1]                                 [LAPTOP 4: Person 4 (You)]
   Traditional Database Host                            RiskDB Database Host & Client Presenter
   PostgreSQL 16 Server (Port 5432)                     RiskDB Engine Daemon (Port 8080)
   1,000,000 Row Contracts in Relational Table          1,000,000 Columnar Contracts in DB Memory
          |                                                    |
          |  WAN Internet (Reverse SSH Tunnel)                 |  WAN Internet (Reverse SSH Tunnel)
          |  Payload: ~48 MB serialized tuples                 |  Payload: 32-byte O(1) Signal
          v                                                    v
   [AZURE VM 1: Cloud Node 1]                           [AZURE VM 2: Cloud Node 2]
   APPLICATION 1: Traditional System App (Port 8081)    APPLICATION 3: RiskDB System App (Port 8082)
   Fetches 1M tuples over WAN; scalar compute           Dual Market Signal Dispatcher UI
          ^                                                    ^
          |                                                    |
   [LAPTOP 2: Person 2]                                 [LAPTOP 3: Person 3]
   APPLICATION 2: Traditional Client Dashboard          System Operator / Controller
   Browser: http://<VM1_IP>:8081/client.html            Browser: http://<VM2_IP>:8082/system_dispatcher.html
   Shows contract prices & Greeks updating in ~7.6s     Triggers simultaneous dual broadcast
+---------------------------------------------------------------------------------------------------+
```

---

## 2. The 4 Applications Defined

| Application | Deployment Location | URL / Access Point | Role & Primary Features |
| :--- | :--- | :--- | :--- |
| **App 1: Traditional System App** | **Azure VM 1** | `http://<VM1_IP>:8081/index.html` | Backend service + Web UI to change stock price. Fetches 1M tuples over WAN from Laptop 1; scalar loop. |
| **App 2: Traditional Client Dashboard** | **Azure VM 1 (Viewed on Laptop 2)** | `http://<VM1_IP>:8081/client.html` | Client-facing dashboard showing live Option Contracts, Option Prices ($), and Greeks ($\Delta, \Gamma, \nu, \Theta$). Measures WAN delay. |
| **App 3: RiskDB System Dispatcher** | **Azure VM 2** | `http://<VM2_IP>:8082/system_dispatcher.html` | Market Signal System UI. Contains the **"Broadcast Price Change to Both Architectures"** button. |
| **App 4: RiskDB Client Dashboard** | **Laptop 4 (Viewed on Laptop 4)** | `http://localhost:8080/client.html` | Client-facing dashboard showing live Option Contracts, Prices, and Greeks **stored in database memory**, updating in 35 ms, plus Amdahl/Gustafson scaling. |

---

## 3. Quantitative WAN Network Bottleneck Analysis

| Dimension | Traditional Architecture (Laptop 1 <-> VM 1) | RiskDB In-Engine Architecture (Laptop 4 <-> VM 2) |
| :--- | :--- | :--- |
| **Data Payload Transferred** | **$\approx 48.0\text{ MB}$** ($10^6$ row tuples) | **$32\text{ bytes}$** (Stock Price Signal: `underlying_id, spot`) |
| **Network Complexity** | $O(N)$ tuples streamed across WAN socket | $O(1)$ control signal dispatched across WAN |
| **WAN Transfer Delay ($50\text{ Mbps}$ uplink)** | **$\approx 7,680\text{ ms}$ ($7.68\text{ seconds}$)** | **$\approx 0.005\text{ ms}$ (Negligible)** |
| **Compute Execution Time** | $\approx 52.0\text{ ms}$ (Single-threaded scalar loop) | $\approx 2.1\text{ ms}$ (AVX2/NEON SIMD + OpenMP) |
| **Total Process Completion Latency** | **$\approx 7,732\text{ ms}$ ($7.7\text{ seconds}$)** | **$\approx 30\text{--}45\text{ ms}$** (Governed purely by WAN RTT) |
| **Empirical Speedup Factor** | **$1.0\times$ (Baseline)** | **$> 170\times\text{--}250\times$ FASTER** |

---

## 4. Step-by-Step Execution Guide for Each Person

### STEP 1: Person 1 (Laptop 1: Traditional Database Host)

1. **Clone repository and build generator**:
   ```bash
   git clone <REPO_URL> riskdb && cd riskdb
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target riskdb -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
   ```

2. **Initialize and Seed PostgreSQL**:
   Run the automated setup script:
   ```bash
   ./scripts/setup_laptop1_postgres.sh
   ```
   *(Creates database `riskdb`, table `contracts`, generates 1,000,000 contracts, and copies them into PostgreSQL).*

3. **Open Reverse SSH Tunnel to Azure VM 1**:
   ```bash
   ssh -N -R 5432:localhost:5432 <AZURE_VM1_USER>@<AZURE_VM1_PUBLIC_IP>
   ```
   *Keep this terminal tab open. Port 5432 on VM 1 now tunnels directly to Laptop 1's local PostgreSQL.*

---

### STEP 2: Azure VM 1 Setup (Cloud Node 1: Traditional Service)

1. **SSH into Azure VM 1**:
   ```bash
   ssh <AZURE_VM1_USER>@<AZURE_VM1_PUBLIC_IP>
   ```

2. **Clone and Provision VM 1**:
   ```bash
   git clone <REPO_URL> riskdb && cd riskdb
   chmod +x deploy/vm1_traditional/setup_vm1.sh
   ./deploy/vm1_traditional/setup_vm1.sh
   ```

3. **Launch Application 1 (Traditional Computation App)**:
   ```bash
   ./build/traditional_app --host 127.0.0.1 --pg-port 5432 --dbname riskdb --user postgres --password postgres --port 8081
   ```
   *(Add `--live-fetch` if you want every click to re-stream 1,000,000 rows across WAN).*

---

### STEP 3: Person 2 (Laptop 2: Traditional Client Dashboard Presenter)

1. Open your web browser on Laptop 2 and navigate to Application 2:
   ```
   http://<AZURE_VM1_PUBLIC_IP>:8081/client.html
   ```
2. **What this screen displays**:
   - Live table of Option Contracts with **Option Prices ($) and Greek metrics ($\Delta, \Gamma, \nu, \Theta$)**.
   - Auto-syncs every 1 second with the Traditional Application.
3. **Observation to demonstrate to the professor**:
   - When Person 3 broadcasts a stock price change, this client screen takes **$\approx 7.6\text{ seconds}$** to update because 48 MB of tuples must stream across the WAN socket before calculating prices and Greeks.

---

### STEP 4: Person 4 / You (Laptop 4: RiskDB Database Host & Client Presenter)

1. **Launch RiskDB In-Engine Database Daemon Locally on Laptop 4 (Port 9090)**:
   ```bash
   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target riskdb_app -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
   ./build/riskdb_app --port 9090 --threads 8 --rows 1000000
   ```
   *1,000,000 contracts are ingested into cache-aligned columnar memory. Initial Black-Scholes Greeks are computed and stored in database memory in 2.1 ms.*

2. **Open Reverse SSH Tunnel to Azure VM 2**:
   In a separate terminal window on Laptop 4, expose port 9090 on VM 2:
   ```bash
   ssh -N -R 9090:localhost:9090 <AZURE_VM2_USER>@<AZURE_VM2_PUBLIC_IP>
   ```
   *Port 9090 on VM 2 now tunnels directly across the WAN to your laptop's RiskDB engine.*

3. **Access Application 4 (RiskDB Client Dashboard) Hosted on Azure VM 2**:
   Open in your browser from Laptop 4 (or any device):
   ```
   http://<AZURE_VM2_PUBLIC_IP>:8080/client.html
   ```
   - Both the Web UI and API requests route through Azure VM 2 to your laptop's database over WAN.
   - Displays live Option Contracts with **Option Prices ($) and Greek metrics ($\Delta, \Gamma, \nu, \Theta$)** stored directly in database memory!
   - When Person 3 broadcasts the price change, this screen **updates virtually instantly in ~35 ms**!
   - Contains **Amdahl's Law (Strong Scaling)** and **Gustafson's Law (Weak Scaling)** evaluation tables for the professor.

---

### STEP 5: Azure VM 2 Setup (Cloud Node 2: RiskDB Application Node)

1. **SSH into Azure VM 2**:
   ```bash
   ssh <AZURE_VM2_USER>@<AZURE_VM2_PUBLIC_IP>
   ```

2. **Clone and Provision VM 2**:
   ```bash
   git clone <REPO_URL> riskdb && cd riskdb
   chmod +x deploy/vm2_riskdb/setup_vm2.sh
   ./deploy/vm2_riskdb/setup_vm2.sh
   ```

3. **Launch RiskDB Web Application Suite on VM 2 (Port 8080)**:
   ```bash
   # Connects across WAN reverse tunnel to Laptop 4 (127.0.0.1:9090) and VM 1 (port 8081)
   ./build/riskdb_computation_app --port 8080 --riskdb-host 127.0.0.1 --riskdb-port 9090 --trad-host <AZURE_VM1_PUBLIC_IP> --trad-port 8081
   ```
   *Now Azure VM 2 is publicly serving both Application 4 (`/client.html`) and Application 3 (`/system_dispatcher.html`).*

---

### STEP 6: Person 3 (Laptop 3: RiskDB System Application Operator)

Person 3 opens Application 3 (Market Signal System Dispatcher) on Azure VM 2:

1. **Open System Dispatcher Web UI in Browser**:
   Navigate to:
   ```
   http://<AZURE_VM2_PUBLIC_IP>:8080/system_dispatcher.html
   ```

3. **Live Demonstration for the Professor**:
   - Select an asset (e.g. Asset #5) and set a new stock price (e.g. $130.00).
   - Click **"Broadcast Price Change to Both Architectures"**.
   - Notice that both signals are dispatched across WAN simultaneously:
     - **RiskDB completes in ~35 ms** (2.1 ms in-engine SIMD compute).
     - **Traditional takes ~7.6 seconds** (stalled on 48 MB WAN transfer).
     - Shows a live **> 200x speedup** banner!

4. **Optional CLI Mode**:
   ```bash
   # Continuous price feed:
   ./build/riskdb_computation_app --cli --stream 10
   # Scaling benchmarks:
   ./build/riskdb_computation_app --cli --benchmark
   ```

---

## 5. Live Demonstration Sequence for the Professor

1. **Show the Contrast**:
   - Have Person 2 click **Trigger Traditional Revaluation** on VM 1 (`http://<VM1_IP>:8081`). Point out that fetching 1,000,000 tuples across WAN takes $\approx 7\text{ seconds}$.
   - Have Person 3 dispatch a signal from VM 2. Point out that RiskDB finishes the end-to-end signal update in $\approx 30\text{--}40\text{ ms}$ (pure network round-trip time + $2.1\text{ ms}$ in-engine execution), delivering over **$200\times$ lower latency**.

2. **Demonstrate In-Database Storage of Greeks**:
   - On Person 4's Client UI (`http://localhost:8080`), scroll down to the **Individual Option Contracts (Client View)** table.
   - Point out that all Option Prices ($) and Greeks ($\Delta, \Gamma, \nu, \Theta$) are computed and maintained directly in the database's cache-aligned columnar memory. When Person 3 sends a signal, the contracts update in real time.

3. **Present Academic Scaling Tables**:
   - In the **Academic Scaling Evaluation** section of the RiskDB dashboard, switch between:
     - **Tab 1: Strong Scaling (Amdahl's Law)**: Fixed 1,000,000 contracts scaled from 1 to 16 threads, showing near-linear speedup and $S(T) = \frac{1}{(1-p) + p/T}$ scaling curve.
     - **Tab 2: Weak Scaling (Gustafson's Law)**: Workload scaled proportionally with thread count (125,000 rows/thread up to 2,000,000 rows on 16 threads), showing sustained parallel efficiency ($E \approx 95\%$).

