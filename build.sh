#!/bin/bash

set -e 

xcodebuild -alltargets -configuration Debug

codesign -s - -f --entitlements "litepcie/litepcie.entitlements" "build/Debug/litepcie-manager.app/Contents/Library/SystemExtensions/litex.litepcie.dext"
codesign -s - -f --entitlements "litepcie-manager/litepcie_manager.entitlements" "build/Debug/litepcie-manager.app"
codesign -s - -f --entitlements "litepcie-client/litepcie_client.entitlements" "build/Debug/litepcie-client.app"

clang litepcie_util.c -o litepcie_util -I liblitepcie/ -I litepcie -lm -lliblitepcie -L build/Debug

codesign -s - -f --entitlements "litepcie-client/litepcie_client.entitlements" litepcie_util
