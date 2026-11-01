# Two-VM Deployment Guide: Traditional vs RiskDB

This document outlines the step-by-step procedure to deploy the two benchmark environments across two separate Virtual Machines (or cloud instances, such as AWS EC2, GCP Compute Engine, Azure, Proxmox, or local hypervisors).

```
                      +-----------------------------+
                      |       Client / Runner       |
                      +--------------+--------------+
                                     |
               +---------------------+---------------------+
               |                                           |
               v                                           v
    +----------------------+                   +----------------------+
    |         VM 1         |                   |         VM 2         |
    | Traditional Database |  TCP/IP Port 5432 |  RiskDB Accelerated  |
    |      PostgreSQL      |<------------------|     Query Engine     |
    |          +           |                   |          +           |
    |   traditional_app    |                   |      riskdb_app      |
    +----------------------+                   +----------------------+
```

---

## 1. Virtual Machine Sizing & Prerequisites

For reproducible academic benchmarks:
- **VM 1 (Traditional Architecture)**:
  - 4 to 8 vCPUs (x86_64 or ARM64)
  - 16 GB RAM
  - Ubuntu 22.04 LTS / 24.04 LTS
  - Static IP: e.g., `192.168.1.101` (or private cloud IP)
- **VM 2 (RiskDB Accelerated Architecture)**:
  - 8 to 16 vCPUs (x86_64 with AVX2 support or ARM64 with NEON)
  - 16 to 32 GB RAM
  - Ubuntu 22.04 LTS / 24.04 LTS
  - Static IP: e.g., `192.168.1.102`

---

## 2. Setting Up VM 1 (PostgreSQL & Traditional Stack)

### Step 2.1: Clone and Provision VM 1
SSH into VM 1:
```bash
ssh user@192.168.1.101
git clone <repository_url> riskdb
cd riskdb/deploy/vm1_traditional
chmod +x setup_vm1.sh
./setup_vm1.sh
```

The script automatically:
1. Installs PostgreSQL 16, `build-essential`, `cmake`, `libpq-dev`, `libomp-dev`.
2. Configures PostgreSQL to listen on `0.0.0.0` and grants access to VM 2.
3. Initializes the database schema (`contracts` table and indexes).
4. Compiles the binaries: `traditional_app` and `riskdb`.
5. Seeds the database with 1,000,000 contracts via bulk `COPY`.

### Step 2.2: Verify PostgreSQL Status
```bash
sudo systemctl status postgresql
psql -h localhost -U riskdb_user -d riskdb -c "SELECT COUNT(*) FROM contracts;"
```

### Step 2.3: Run the VM 1 Traditional Application
```bash
cd ../../build
./traditional_app --host localhost --port 5432 --dbname riskdb --user riskdb_user --password riskdb_password
```

---

## 3. Setting Up VM 2 (RiskDB Accelerated Parallel Engine)

### Step 3.1: Clone and Provision VM 2
SSH into VM 2:
```bash
ssh user@192.168.1.102
git clone <repository_url> riskdb
cd riskdb/deploy/vm2_riskdb
chmod +x setup_vm2.sh
./setup_vm2.sh
```

The script automatically:
1. Installs compiler toolchains, CMake, `libpq-dev`, and OpenMP runtime.
2. Compiles the SIMD-vectorized binary targets with `-O3 -march=native -mavx2 -mfma -fopenmp`.

### Step 3.2: Run RiskDB over PostgreSQL (VM 1 Data Source)
Execute the accelerated engine, targeting the PostgreSQL instance hosted on VM 1:
```bash
cd ../../build
./riskdb_app --host 192.168.1.101 --port 5432 --dbname riskdb --user riskdb_user --password riskdb_password --threads 16
```

---

## 4. Local Multi-VM Testing via Docker Compose

If physical or cloud VMs are not immediately accessible, you can run an exact multi-host architectural simulation using Docker:

```bash
cd deploy
docker compose up --build
```

This starts:
1. `postgres-vm1`: The PostgreSQL server.
2. `traditional-app-vm1`: The traditional tiered client application.
3. `riskdb-engine-vm2`: The parallel SIMD + OpenMP query engine over PostgreSQL.
