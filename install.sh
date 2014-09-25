#!/bin/sh

which curl &>/dev/null || (echo 'curl(1) not found' && exit )
which cc   &>/dev/null || (echo 'cc(1) not found' && exit )

url=https://raw.githubusercontent.com/m-b-/utils/master/

files="serve.c lockf.c broadcast.c"

for f in $files; do
	if [ -f "$f" ]; then
		echo $f 'already here; keep it? (Y/n)'
		read yn
		if echo $yn | grep '[Nn][Oo]?'; then
			continue
		fi
	fi
	curl -s $url/$f > $f
	grep pthread $f >/dev/null && CFLAGS="-lpthread" || CFLAGS=""
	grep DBG $f >/dev/null && CFLAGS="$CFLAGS -DDBG"
	cc -Wall -Wextra -O2 $CFLAGS $f -o `basename $f .c`
done

# cc -lcurses ctalk.c
