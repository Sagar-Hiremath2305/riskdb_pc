#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo "          PROVISIONING VM 2: RISKDB ACCELERATED ARCHITECTURE          "
echo "======================================================================"

# 1. Install prerequisites (Ubuntu/Debian)
sudo apt-get update -y
sudo apt-get install -y build-essential cmake libpq-dev libomp-dev

# 2. Build RiskDB Parallel Query Engine
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target riskdb_app riskdb -j$(nproc)

echo "======================================================================"
echo "[+] VM 2 Provisioning Complete."
echo "    Run RiskDB Engine over PostgreSQL (pointing to VM 1 IP):"
echo "    ./build/riskdb_app --host <VM1_IP> --dbname riskdb --user riskdb_user --password riskdb_password --threads 16"
echo "    Or run isolated in-engine benchmark:"
echo "    ./build/riskdb --benchmark 10000000 --threads 16"
echo "======================================================================"
