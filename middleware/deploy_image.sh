#!/usr/bin/env bash
set -euo pipefail

# AI Slop but it works

# Config
REMOTE_USER="root"
REMOTE_HOST="192.168.1.203"
REMOTE_PATH="/tmp"
IMAGE_NAME="bus-stop-middleware:latest"
TAR_NAME="bus-stop-middleware_latest.tar.gz"

# Build image
docker build -t "${IMAGE_NAME}" .

# Save and compress, stream to remote to avoid local disk usage
docker save "${IMAGE_NAME}" | gzip | ssh "${REMOTE_USER}@${REMOTE_HOST}" "cat > ${REMOTE_PATH}/${TAR_NAME}"

# Load image on remote (use sudo if remote docker needs it)
ssh "${REMOTE_USER}@${REMOTE_HOST}" "gunzip -c ${REMOTE_PATH}/${TAR_NAME} | docker load"

# Optional cleanup on remote (uncomment to remove tar after loading)
# ssh "${REMOTE_USER}@${REMOTE_HOST}" "rm -f ${REMOTE_PATH}/${TAR_NAME}"

# Local cleanup (no local tar created when streaming)
echo "Deployed ${IMAGE_NAME} to ${REMOTE_USER}@${REMOTE_HOST} and loaded into Docker."
