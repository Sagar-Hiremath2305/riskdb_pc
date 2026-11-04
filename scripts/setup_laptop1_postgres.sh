#!/usr/bin/env bash
set -euo pipefail

echo "======================================================================"
echo "          LAPTOP 1: TRADITIONAL POSTGRESQL DATABASE SETUP             "
echo "======================================================================"

DB_NAME="riskdb"
CONTRACT_ROWS=1000000

# 1. Create database if it does not exist
echo "[+] Checking PostgreSQL database '${DB_NAME}'..."
if ! psql -lqt | cut -d \| -f 1 | grep -qw "${DB_NAME}"; then
    echo "[+] Creating database '${DB_NAME}'..."
    createdb "${DB_NAME}"
fi

# 2. Apply Schema
echo "[+] Initializing schema from deploy/vm1_traditional/init.sql..."
psql -d "${DB_NAME}" -f deploy/vm1_traditional/init.sql

# 3. Check existing row count
EXISTING_COUNT=$(psql -d "${DB_NAME}" -t -A -c "SELECT COUNT(*) FROM contracts;" || echo "0")
echo "[+] Current contract count in database: ${EXISTING_COUNT}"

if [ "${EXISTING_COUNT}" -lt "${CONTRACT_ROWS}" ]; then
    echo "[+] Generating ${CONTRACT_ROWS} synthetic options contracts..."
    mkdir -p /tmp/riskdb_data
    ./build/riskdb --generate-csv /tmp/riskdb_data/contracts.csv "${CONTRACT_ROWS}"
    
    echo "[+] Bulk copying contracts into PostgreSQL table 'contracts'..."
    psql -d "${DB_NAME}" -c "\copy contracts FROM '/tmp/riskdb_data/contracts.csv' WITH (FORMAT csv, HEADER true);"
    rm -f /tmp/riskdb_data/contracts.csv
    echo "[+] PostgreSQL database populated with ${CONTRACT_ROWS} contracts!"
else
    echo "[+] Database already contains ${EXISTING_COUNT} contracts. Skipping seed."
fi

echo ""
echo "======================================================================"
echo "[+] Laptop 1 Setup Complete!"
echo "    To forward port 5432 to Azure VM 1, run:"
echo "    ssh -N -R 5432:localhost:5432 <AZURE_USER>@<VM1_PUBLIC_IP>"
echo "======================================================================"
