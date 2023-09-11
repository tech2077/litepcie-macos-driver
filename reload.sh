#!/bin/bash

set -e 

systemextensionsctl reset
sleep 1
./build/Debug/litepcie-manager.app/Contents/MacOS/litepcie-manager forceActivate
sleep 1
./build/Debug/litepcie-manager.app/Contents/MacOS/litepcie-manager forceActivate
