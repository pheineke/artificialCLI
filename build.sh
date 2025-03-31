#! /bin/bash

gcc -o geminiflash geminiflash.c -lcurl -lcjson
gcc -o geminipro geminipro.c -lcurl -lcjson
gcc -o geminiflash-test geminiflash-test.c -lcurl -lcjson

