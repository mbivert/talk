#!/bin/sh

nc localhost $2 &
pout=$!
nc localhost $1

kill $pout &>/dev/null
