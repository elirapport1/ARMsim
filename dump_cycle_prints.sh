#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <input_file>"
    exit 1
fi

INPUT_FILE=$1
OUTPUT_FILE="dump_cycles_sim"

# First, go to src and run make
cd src
make clean && make
cd ..

# Go back to src for the rest of the operations
cd src

# Clear output file if it exists
> $OUTPUT_FILE

echo "Running simulator until completion..."

# Run the simulator with input file and capture all output
{
    # Run the simulation to completion
    echo "go"
    # Exit the simulator
    echo "quit"
} | ./sim "../$INPUT_FILE" > $OUTPUT_FILE

echo "Simulation completed. Print statements captured in $OUTPUT_FILE"

# Move the output file to parent directory
mv $OUTPUT_FILE ../$OUTPUT_FILE
cd ..

echo "Output file located at ./$OUTPUT_FILE" 