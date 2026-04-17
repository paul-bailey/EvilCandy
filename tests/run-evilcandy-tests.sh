#!/bin/sh

# Automake TESTS wrapper for the EvilCandy scripts in tests/.

set -u

srcdir=${srcdir:-.}
evilcandy=${EVILCANDY:-./evilcandy}

case $evilcandy in
    /*) ;;
    *) evilcandy=$(pwd)/$evilcandy ;;
esac

case $srcdir in
    /*) top_srcdir=$srcdir ;;
    *) top_srcdir=$(pwd)/$srcdir ;;
esac

if [ ! -x "$evilcandy" ]; then
    echo "$0: cannot execute $evilcandy" >&2
    exit 1
fi

cd "$top_srcdir" || exit 1

if [ "$#" -eq 0 ]; then
    set -- tests/*.evc
fi

found=no
status=0

for script
do
    if [ ! -f "$script" ]; then
        continue
    fi

    found=yes
    echo "Running $script"

    "$evilcandy" "$script"
    result=$?

    if [ "$result" -ne 0 ]; then
        status=$result
    fi
done

if [ "$found" = no ]; then
    echo "$0: no EvilCandy test scripts found" >&2
    exit 1
fi

exit "$status"
