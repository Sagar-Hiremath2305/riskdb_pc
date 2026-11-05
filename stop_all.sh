#!/bin/bash
# RiskDB Clean Shutdown Script
# Terminates all remote Azure VM services, local background tunnels, and the RiskDB engine daemon.

echo "======================================================================"
echo " Stopping RiskDB Distributed Application Stack"
echo "======================================================================"

echo "[*] 1/3 Stopping RiskDB Computation App on Azure VM 2 (40.81.233.84)..."
ssh vm2 "sudo systemctl stop riskdb-computation.service && killall riskdb_computation_app 2>/dev/null || true; sudo fuser -k 9090/tcp 2>/dev/null || true"

echo "[*] 2/3 Stopping Traditional DBMS Service on Azure VM 1 (20.244.34.9)..."
ssh vm1 "sudo systemctl stop traditional-app.service && killall traditional_app 2>/dev/null || true; sudo fuser -k 5432/tcp 2>/dev/null || true"

echo "[*] 3/3 Terminating local Mac daemons and SSH reverse tunnels..."
killall riskdb_app 2>/dev/null || true
pkill -f "ServerAliveInterval=10" 2>/dev/null || true
pkill -f "5432:localhost:5432" 2>/dev/null || true
pkill -f "9090:localhost:9090" 2>/dev/null || true

echo "======================================================================"
echo "[+] All distributed services and tunnels have been stopped cleanly."
echo "======================================================================"
