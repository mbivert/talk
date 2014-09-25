#!/bin/sh

trap : SIGTERM SIGINT

conf=./config
[ "$1" != "" ] && conf=$1

source $conf
echo --- BEGIN CONFIG
sed -n '/^#/! s/^/--- /p' $conf
echo --- END CONFIG

if [ "$wd" == "" ]; then
	echo 'bad workdir.'
	exit
fi

mkdir -p $wd/nicks/
rm -f $wd/nicks/*

rm -f $bfifo
mkfifo $bfifo

./serve -p $sport ./talk.sh $wd $bfifo &
sid=$!

./broadcast -p $dport $bfifo &
bid=$!

wait $bid $sid

# we're here on SIGTERM/SIGINT too
pkill -g $$
# kill -- -$$
rm -f $wd/nicks/*
rm -f $bfifo
