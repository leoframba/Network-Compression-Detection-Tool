#!/usr/bin/env bash
# Build the project and run unit tests / a tiny local smoke check.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "==> Building cooperative binaries"
make client server

echo "==> Running unit tests"
make test

echo "==> Smoke: show CLI help"
./bin/ncd-client --help >/dev/null
./bin/ncd-server --help >/dev/null

echo "==> Localhost cooperative smoke test"
python3 - <<'PY'
import json, subprocess, time, os, signal, sys
cfg_path = "configs/smoke-ephemeral.json"
cfg = {
  "serverIp": "127.0.0.1",
  "sourcePort": 54321,
  "destPortUdp": 28765,
  "destPortTcpHead": 28000,
  "destPortTcpTail": 28080,
  "TcpPortNum": 28100,
  "UdpPayloadSize": 64,
  "InterTime": 2,
  "UdpPackCount": 30,
  "Ttl": 64,
  "ThresholdMs": 100,
  "InterPacketUs": 200,
  "Trials": 1,
  "DataThreshold": 0.0
}
with open(cfg_path, "w") as f:
    json.dump(cfg, f, indent=2)
server = subprocess.Popen(["./bin/ncd-server", "--config", cfg_path, "--listen-port", "28100"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(0.5)
client = subprocess.run(["./bin/ncd-client", "--config", cfg_path],
                        capture_output=True, text=True, timeout=60)
server.send_signal(signal.SIGTERM)
server.wait(timeout=3)
print(client.stdout.strip())
if client.returncode != 0 or "No compression detected" not in client.stdout:
    print("Smoke test failed", file=sys.stderr)
    sys.exit(1)
print("Localhost smoke test OK (no compression on loopback, as expected).")
PY

if [[ "$(uname -s)" == "Linux" ]]; then
  echo "==> Attempting standalone probe build on Linux"
  make probe || echo "warning: install libpcap-dev to build ncd-probe"
else
  if make probe >/dev/null 2>&1; then
    echo "==> ncd-probe built successfully"
  else
    echo "==> Skipping ncd-probe (install libpcap to enable)"
  fi
fi

echo
echo "Demo complete."
echo "Next steps:"
echo "  1. Edit configs/example.json (set serverIp)"
echo "  2. On server:  ./bin/ncd-server --config configs/example.json"
echo "  3. On client:  ./bin/ncd-client --config configs/example.json --verbose"
echo "  4. Optional:   sudo ./bin/ncd-probe --config configs/example.json --interface <nic>"
