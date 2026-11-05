#!/bin/bash
# RiskDB One-Click Startup Script
# Starts local RiskDB engine, opens WAN reverse tunnels, and starts Azure VM services.

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
cd "$DIR"

echo "======================================================================"
echo " Starting RiskDB Distributed Application Stack"
echo "======================================================================"

# 1. Start Local RiskDB In-Memory Engine Daemon
if ! pgrep -x "riskdb_app" > /dev/null; then
    echo "[*] 1/4 Launching local RiskDB In-Memory Engine (Port 9090)..."
    nohup ./build/riskdb_app --port 9090 --threads 8 --rows 1000000 > /tmp/riskdb_app.log 2>&1 &
    sleep 1
else
    echo "[+] 1/4 Local RiskDB In-Memory Engine is already running."
fi

# 2. Establish SSH Tunnels to Azure VMs
echo "[*] 2/4 Establishing WAN Reverse Tunnels to Azure VMs..."
pkill -f "5432:localhost:5432" 2>/dev/null || true
pkill -f "9090:localhost:9090" 2>/dev/null || true
sleep 1

ssh -N -R 5432:localhost:5432 -L 8081:localhost:8081 vm1 >/dev/null 2>&1 &
ssh -N -R 9090:localhost:9090 -L 8080:localhost:8080 vm2 >/dev/null 2>&1 &
sleep 2

# 3. Start VM 1 Traditional Service
echo "[*] 3/4 Starting Traditional DBMS Service on Azure VM 1 (20.244.34.9:8081)..."
ssh vm1 "sudo systemctl restart traditional-app.service"

# 4. Start VM 2 Computation App
echo "[*] 4/4 Starting RiskDB Computation Service on Azure VM 2 (40.81.233.84:8080)..."
ssh vm2 "sudo systemctl restart riskdb-computation.service"

echo "======================================================================"
echo "[+] All services active! Open in browser:"
echo "    - Dispatcher (VM 2): http://40.81.233.84:8080/system_dispatcher.html"
echo "    - RiskDB Client (VM 2): http://40.81.233.84:8080/client.html"
echo "    - Traditional Client (VM 1): http://20.244.34.9:8081/client.html"
echo "======================================================================"
