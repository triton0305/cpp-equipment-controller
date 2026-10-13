#!/usr/bin/env bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$ROOT/build/apps/controller/equipment_ctl"
