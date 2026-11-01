#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo "          PROVISIONING VM 1: TRADITIONAL TIERED ARCHITECTURE          "
echo "======================================================================"

# 1. Install prerequisites (Ubuntu/Debian)
sudo apt-get update -y
sudo apt-get install -y build-essential cmake libpq-dev postgresql postgresql-contrib libomp-dev

# 2. Configure PostgreSQL
sudo systemctl enable postgresql
sudo systemctl start postgresql

sudo -u postgres psql -c "CREATE USER riskdb_user WITH PASSWORD 'riskdb_password' SUPERUSER;" || true
sudo -u postgres psql -c "CREATE DATABASE riskdb OWNER riskdb_user;" || true

# Allow external connection from VM 2
PG_CONF=$(sudo -u postgres psql -t -P format=unaligned -c "SHOW config_file;")
PG_HBA=$(sudo -u postgres psql -t -P format=unaligned -c "SHOW hba_file;")

echo "listen_addresses = '*'" | sudo tee -a "$PG_CONF"
echo "host    all             all             0.0.0.0/0               md5" | sudo tee -a "$PG_HBA"
sudo systemctl restart postgresql

# 3. Initialize Schema
sudo -u postgres psql -d riskdb -f init.sql

# 4. Build Traditional App
mkdir -p build && cd build
cmake ..
cmake --build . --target traditional_app riskdb -j$(nproc)

echo "[+] Seeding database with 1,000,000 options contracts..."
./riskdb --generate-csv /tmp/contracts.csv 1000000
sudo -u postgres psql -d riskdb -c "\copy contracts FROM '/tmp/contracts.csv' WITH (FORMAT csv, HEADER true);"

echo "======================================================================"
echo "[+] VM 1 Provisioning Complete."
echo "    Run Traditional App: ./build/traditional_app --host localhost --dbname riskdb --user riskdb_user --password riskdb_password"
echo "======================================================================"
