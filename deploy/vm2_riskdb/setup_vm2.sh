#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo "          PROVISIONING VM 2: RISKDB MARKET SIGNAL SYSTEM NODE         "
echo "======================================================================"

# 1. Install prerequisites (Ubuntu/Debian)
sudo apt-get update -y
sudo apt-get install -y build-essential cmake libomp-dev

# 2. Build RiskDB Market Signal Client & Engine (Clean any stale local/Mac cache)
rm -rf build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target riskdb_computation_app riskdb_app riskdb -j$(nproc)
cd ..

echo "======================================================================"
echo "[+] VM 2 Provisioning Complete."
echo ""
echo "Step A (From Laptop 4 / You): Run database locally on port 9090 and open reverse tunnel:"
echo "   # 1. On Laptop 4, run RiskDB in-memory database daemon:"
echo "   ./build/riskdb_app --port 9090 --threads 8 --rows 1000000"
echo ""
echo "   # 2. In a second terminal on Laptop 4, forward port 9090 to VM 2:"
echo "   ssh -N -R 9090:localhost:9090 <AZURE_USER>@<VM2_PUBLIC_IP>"
echo ""
echo "Step B (On VM 2): Launch RiskDB Applications (Port 8080):"
echo "   ./build/riskdb_computation_app --port 8080 --riskdb-host 127.0.0.1 --riskdb-port 9090 --trad-host <VM1_PUBLIC_IP> --trad-port 8081"
echo ""
echo "Access from any laptop or browser over the public internet:"
echo "   1. Application 4 (RiskDB Client Dashboard):"
echo "      http://<VM2_PUBLIC_IP>:8080/client.html"
echo ""
echo "   2. Application 3 (Market Signal System App & Dual Dispatcher):"
echo "      http://<VM2_PUBLIC_IP>:8080/system_dispatcher.html"
echo "======================================================================"

