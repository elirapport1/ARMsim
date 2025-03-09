#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <input_file>"
    exit 1
fi

INPUT_FILE=$1

# First, go to src and run make
cd src
make clean && make
cd ..

# Go back to src for the rest of the operations
cd src

# Create temporary files for simulator outputs
MY_SIM_OUT="my_sim_out.txt"
REF_SIM_OUT="ref_sim_out.txt"
SIM_DUMP="dump_sim"
REFSIM_DUMP="dump_refsim"
DIFF_FILE="dump_diff"

# Clear dump files if they exist
> $SIM_DUMP
> $REFSIM_DUMP
> $DIFF_FILE

# Function to run simulator for N cycles and capture state
run_sim_cycles() {
    local sim=$1
    local cycles=$2
    local outfile=$3
    local dumpfile=$4
    {
        echo "r $cycles"
        echo "rdump"
        echo "mdump 0x10000000 0x100000ff"
        echo "quit"
    } | $sim "../$INPUT_FILE" > "$outfile"
    
    # Append cycle information and dumps to the dump file
    echo "=== Cycle $cycles ===" >> "$dumpfile"
    cat "$outfile" >> "$dumpfile"
    echo -e "\n" >> "$dumpfile"
}

# Number of cycles to test (21 for step1-a-input.x)
TOTAL_CYCLES=641
DIFFERING_CYCLES=0

echo "Running cycle by cycle comparison..."
for ((i=1; i<=TOTAL_CYCLES; i++)); do
    # Run my simulator for i cycles
    run_sim_cycles "./sim" $i "$MY_SIM_OUT" "$SIM_DUMP"
    
    # Run reference simulator for i cycles
    if [ -f "./refsim" ]; then
        run_sim_cycles "./refsim" $i "$REF_SIM_OUT" "$REFSIM_DUMP"
    elif [ -f "../refsim" ]; then
        run_sim_cycles "../refsim" $i "$REF_SIM_OUT" "$REFSIM_DUMP"
    else
        echo "Error: refsim not found in ./src or parent directory"
        exit 1
    fi
    
    # Compare the outputs and save to diff file
    echo "=== Differences in cycle $i ===" >> "$DIFF_FILE"
    if ! diff "$MY_SIM_OUT" "$REF_SIM_OUT" >> "$DIFF_FILE"; then
        DIFFERING_CYCLES=$((DIFFERING_CYCLES + 1))
    fi
    echo "----------------------------------------" >> "$DIFF_FILE"
done

echo "Number of differing cycles: $DIFFERING_CYCLES/$TOTAL_CYCLES"

# Clean up temporary files
rm "$MY_SIM_OUT" "$REF_SIM_OUT"
# Note: sim_dump and refsim_dump files are kept for reference
