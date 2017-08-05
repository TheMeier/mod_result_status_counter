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

ab -n 10 http://localhost:8080/mrsc
ab -n 10 http://localhost:8080/

curl -o test.output -q http://localhost:8080/mrsc

grep '{ "200 OK": 10 }' test.output
grep '{ "404 Not Found": 10 }' test.output

curl -o test.output2 http://localhost:8080/mrsc?prometheus

grep 'http_requests_total{status="200 OK"}  11' test.output2
grep 'http_requests_total{status="404 Not Found"}  10' test.output2
