Not a helpful readme, just some commands used while working on this

```
# typical test workflow:

# build and reload client app and driverkit driver
./build.sh
./build.sh && ./reload.sh

# run direct client app (and dump debug results to file)
./build/Debug//litepcie-client.app/Contents/MacOS/litepcie-client > test.txt

# build and run litepcielib util side
clang litepcie_util.c -o litepcie_util -I liblitepcie/ -I litepcie -lm -lliblitepcie -L build/Debug
./litepcie_util -c 0 -z dma_test

# few ways to view kernel level logs:
./log.sh
log stream --level info --predicate 'sender == "litex.litepcie.dext"'
log stream --level info | grep litex
```

to enable viewing private data in unified logs: https://georgegarside.com/blog/macos/sierra-console-private/