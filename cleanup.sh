#!/bin/sh

test -f Makefile && make clean

rm -f stamp-h1 *.h configure config.log config.status ltmain.sh
rm -f *.m4
rm -rf autom4te.cache/
rm -f m4/*.m4
rm -f build/m4/*.m4
test -f libtool && rm -r libtool
rm -f test-driver
rm -f build/ltmain.sh
rm -f build/test-driver

for dir in . src tools
do
    rm -f ${dir}/*.in
    rm -f ${dir}/Makefile
    rm -rf ${dir}/.deps
    rm -f ${dir}/*~
done

