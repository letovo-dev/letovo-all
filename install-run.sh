#!/bin/bash 

test_file="test.cpp"
work_file="server.cpp"
run_flag=true

while getopts 'tif:' OPTION; do
    case "$OPTION" in
        i) 
            ehco "installing"
            run_flag=false
            ;;
        f)
            echo "set work file to $OPTARG"
            work_file="$OPTARG"
            ;;
        t)
            echo "running test file"
            work_file="$OPTARG"
            ;;
        ?)
            echo "idk what you mean"
    esac
done

if [ $run_flag = true ]; then
    export BUILD_FILE="$work_file"
    echo "running $work_file"
    cd src
    cmake .
    cmake --build .
    ./server_starter
fi