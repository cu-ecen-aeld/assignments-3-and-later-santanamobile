#!/bin/sh
#
# Assignment 1: finder.sh
#

set -e

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

if [ ! -d $1 ]; then
    echo "First argument must be a directory"
    exit 1
fi

filesdir=$1
searchstr=$2

total_files=$(find "$filesdir" -type f | wc -l)
total_files_with_searchstr=$(grep -rl "$searchstr" "$filesdir" | wc -l)

# DEBUG
#echo "Directory: ${filesdir}"
#echo "String   : ${searchstr}"

echo "The number of files are ${total_files} and the number of matching lines are ${total_files_with_searchstr}"
