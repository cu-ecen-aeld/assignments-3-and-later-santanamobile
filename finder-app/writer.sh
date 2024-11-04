#!/bin/sh
#
# Assignment 1: writer.sh
#

# set -e

if [ $# != 2 ]; then
    echo "Incorrect number of arguments, must be equal 2"
    exit 1
fi

if [ -z $1 ]; then
    echo "First argument cannot be null"
    exit 1
fi

if [ -z $2 ]; then
    echo "Second argument cannot be null"
    exit 1
fi

writefile=$1
writestr=$2

folder_name=$(dirname "$writefile")

if [ ! -d "${folder_name}" ]; then
    mkdir -p "${folder_name}"
    if [ $? -ne 0 ]; then
        echo "Cannot create directory: '${folder_name}'."
        exit 1
    fi
fi

echo "$writestr" > "$writefile"
if [ $? -ne 0 ]; then
    echo "Cannot create file: '${writefile}'."
    exit 1
fi
