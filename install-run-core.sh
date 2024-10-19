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

while getopts 'toifd:h' OPTION; do
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
        h)
            echo "Usage: ./install-run-core.sh [-i] [-f <file>] [-t] [-d <debug>] [-o]"
            echo "Options:"
            echo "  -i: install dependencies"
            echo "  -f: set work file"
            echo "  -t: run test file"
            echo "  -d: run with debug keys"
            echo "  -o: console output"
            exit 0
            ;;
        ?)
            echo "idk what you mean"
            exit 1
            ;;
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
else
    # detect system packet manager
    declare -A osInfo;
    osInfo[/etc/arch-release]="pacman -S"
    osInfo[/etc/debian_version]="apt-get install"

    # install pacages
    for f in ${!osInfo[@]}
    do
        if [[ -f $f ]];then
            pack_manager=${osInfo[$f]}
        fi
    done
    if [ -z ${pack_manager+x} ]; then 
        echo "I have no idea, what this system is, sorry"
        exit 255
    fi

    echo "install c++ compiler"
    if [ "$pack_manager" == "pacman -S" ]; then
      sudo pacman -Syu base-devel
      sudo pacman -S restinio
    else
       sudo apt-get install build-essential
       sudo apt-get install librestinio-dev
    fi
    
    echo "install cmake"
    sudo $pack_manager cmake 
    echo "install jq"
    sudo $pack_manager jq
    echo "install pkg-config"
    sudo $pack_manager pkg-config
    echo "install nlohmann-json3-dev"
    sudo $pack_manager nlohmann-json3-dev
    echo "install libasio-dev"
    sudo $pack_manager libasio-dev
    echo "install rapidjson-dev"
    sudo $pack_manager rapidjson-dev
    echo "install libpqxx-dev"
    sudo $pack_manager libpqxx-dev
fi
