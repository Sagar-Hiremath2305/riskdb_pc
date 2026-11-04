#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo "          PROVISIONING VM 1: TRADITIONAL TIERED ARCHITECTURE          "
echo "======================================================================"

# 1. Install prerequisites (Ubuntu/Debian)
sudo apt-get update -y
sudo apt-get install -y build-essential cmake libpq-dev postgresql-client libomp-dev

# 2. Build Traditional Application
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target traditional_app riskdb -j$(nproc)
cd ..

echo "======================================================================"
echo "[+] VM 1 Provisioning Complete."
echo ""
echo "Step A (From Laptop 1): Laptop 1 opens reverse SSH tunnel:"
echo "   ssh -N -R 5432:localhost:5432 <AZURE_USER>@<VM1_PUBLIC_IP>"
echo ""
echo "Step B (On VM 1): Launch Application 1 connecting to Laptop 1's PostgreSQL:"
echo "   ./build/traditional_app --host 127.0.0.1 --pg-port 5432 --dbname riskdb --user postgres --password postgres --port 8081"
echo ""
echo "Step C (From Laptop 2): Presenter views Traditional Dashboard:"
echo "   http://<VM1_PUBLIC_IP>:8081"
echo "======================================================================"

