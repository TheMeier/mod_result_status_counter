#!/bin/bash
set -e
pushd test

bindir=`apxs -q sbindir`
progname=`apxs -q progname`
libexecdir=`apxs -q libexecdir`

COMMAND="${bindir}/${progname}"
TESTCONFIG="${PWD}/test.conf"
TESTLOG="./test.log"
rm -f modules

function finish {
    kill `cat test.pid`
    rm -f modules
}
trap finish EXIT
ln  -s ${libexecdir} ./modules


$COMMAND -f $TESTCONFIG & # > $TESTLOG 2>&1 &
PID=$!


sleep 2

rm -f test.output*

ab -n 10 http://localhost:8080/mrsc
ab -n 10 http://localhost:8080/

curl -o test.output -q http://localhost:8080/mrsc

cat test.output
grep 'http_requests_count_total{status="200 OK"}  10' test.output
grep 'http_requests_count_total{status="404 Not Found"}  10' test.output

