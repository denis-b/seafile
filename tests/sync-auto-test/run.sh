#!/bin/bash

export PYTHONPATH=$PYTHONPATH:/home/jye/seafile-relate/lib/python2.7/site-packages

case $1 in
    "test")
        nosetests -v -s
        sleep 10
        export ENCRYPTED_REPO=true
        nosetests -v -s;;
    "clean")
        rm -rf cli1 cli2 worktree1 worktree2
        pkill -f "ccnet --daemon -c $(pwd)/cli1"
        pkill -f "ccnet --daemon -c $(pwd)/cli2";
esac
