#!/bin/bash 

test_file="test.cpp"
work_file="server.cpp"
build_files=""
run_flag=true
logging=true

# Read and parse BuildConfig.json
config_file="./BuildConfig.json"

if [ -f "$config_file" ]; then
    test_file=$(jq -r '.test_file' "$config_file")
    work_file=$(jq -r '.work_file' "$config_file")
    for i in $(jq -c '.build_files[]' "$config_file"); do
        build_files+="$i "
    done 
    build_files=$(echo "$build_files" | tr -d '"')
    build_files=$(echo "$build_files" | tr '\n' ' ')
else
    echo "Config file not found!"
    exit 1
fi

echo "test file $test_file"
echo "work file $work_file"
echo "build files: $build_files"

while getopts 'toifd:' OPTION; do
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
            work_file="$test_file"
            ;;
        d)
            echo "running with debug keys"
            export DEBUG="t"
            ;;
        o)
            echo "console output"
            logging=false
            ;;
        ?)
            echo "idk what you mean"
    esac
done

if [ $run_flag = true ]; then
    export MAIN_FILE="$work_file"
    export BUILD_FILES="$build_files"
    echo "running $work_file"
    cd src
    if [ $logging = true ]; then
        cmake . &>> ./launch_logs/build_"$(date '+%d-%m-%Y_%H:%M:%S')".log 2>&1
        cmake --build . 2>> ./launch_logs/build_"$(date '+%d-%m-%Y_%H:%M:%S')".log
        ./server_starter &> ./launch_logs/run_"$(date '+%d-%m-%Y_%H:%M:%S')".log
    else
        cmake . 
        cmake --build .
        ./server_starter
    fi
fi