# lbe-1420
LBE-1420 GPS locked clock source config

## Prerequisites ##

gcc g++


## Build Instructions Using GCC ##

For the frequency setting utility:
gcc -o lbe-1420-set-freq lbe-1420-set-freq.cpp lbe-1420-settings.cpp -lstdc++

For the status utility:
gcc -o lbe-1420-status lbe-1420-status.cpp -lstdc++
