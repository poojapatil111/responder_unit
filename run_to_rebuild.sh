#!/bin/bash

set -e

source /opt/qt6-sdk/environment-setup-cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi

cd ~/responder_unit/build-arm

make -j$(nproc)

scp untitled root@192.168.1.25:/root/

ssh root@192.168.1.25 "pkill untitled || true; chmod +x /root/untitled; nohup /root/untitled >/tmp/untitled.log 2>&1 &"
