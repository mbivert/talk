#!/bin/sh

wd=$1
out=$1/broadcast-unix

read nickline

nick=`echo $nickline | awk '$1 == "!iam" { print $2 }'`

if [ "$nick" == "" ]; then
	echo "error: expecting '!iam <nickname>'"
	exit
fi

if ! ./lockf -f "$wd/nicks/$nick"; then
	echo "error: nick already taken."
	exit
fi

(while true; do cat $wd/nicks/$nick; done) &
pid=$!

echo "* $nick has logged in" > $out

while true; do
	read line
	cmd=`echo $line | awk '{ print $1 }'`
	case "$cmd" in
	"!quit"|"!leave"|"")
		rm -f "$wd/nicks/$nick"
		echo "* $nick has logged out" > $out
		# kill parent?
		exit
		;;
	"!list")
		for n in $wd/nicks/*; do
			echo '*' `basename $n` is here
		done
		;;
	"!msg")
		to=`echo $line | awk '{ print $2 }'`
		if ./lockf $wd/nicks/$to; then
			echo '*' $to not here
			rm $wd/nicks/$to
		else
			echo - "<$nick>" $line | sed "s/!msg $to //g" > $wd/nicks/$to
		fi
		;;
	"!say")
		echo -n "<$nick> " > $out
		echo $line | sed 's/!say *//' > $out
		;;
	*)
		echo "<$nick>" $line > $out
		;;
	esac
done 

