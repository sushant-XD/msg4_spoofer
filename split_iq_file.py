#!/usr/bin/env python3
"""
Split a .fc32 IQ file at a specific slot number.

Usage:
    python split_iq_file.py input.fc32 slot_number [sample_rate]

Example:
    python split_iq_file.py capture.fc32 2000000
    python split_iq_file.py capture.fc32 2000000 23.04e6

Output:
    input_part1.fc32 - samples from start to slot_number
    input_part2.fc32 - samples from slot_number to end
"""

import numpy as np
import sys
import os

def split_iq_file(input_file, split_slot, sample_rate=23.04e6):
    """
    Split IQ file at specified slot number.
    
    Args:
        input_file: Path to input .fc32 file
        split_slot: Slot number where to split
        sample_rate: Sample rate in Hz (default: 23.04 MHz)
    """
    
    # Calculate samples per slot (1ms for 15kHz SCS)
    slot_duration = 1e-3  # 1 millisecond
    samples_per_slot = int(sample_rate * slot_duration)
    
    # Calculate split point in samples
    split_sample = split_slot * samples_per_slot
    
    print(f"Input file: {input_file}")
    print(f"Sample rate: {sample_rate/1e6:.2f} MHz")
    print(f"Samples per slot: {samples_per_slot}")
    print(f"Split at slot: {split_slot}")
    print(f"Split at sample: {split_sample}")
    
    # Get input file size
    file_size = os.path.getsize(input_file)
    bytes_per_sample = 8  # complex64 = 2 * float32 = 2 * 4 bytes
    total_samples = file_size // bytes_per_sample
    total_slots = total_samples // samples_per_slot
    
    print(f"Total samples in file: {total_samples:,}")
    print(f"Total slots in file: {total_slots:,}")
    
    if split_sample >= total_samples:
        print(f"ERROR: Split slot {split_slot} is beyond file end (max slot: {total_slots})")
        return False
    
    # Generate output filenames
    base_name = os.path.splitext(input_file)[0]
    output_file1 = f"{base_name}_part1.fc32"
    output_file2 = f"{base_name}_part2.fc32"
    
    print(f"\nSplitting...")
    print(f"Part 1: {output_file1} (slots 0 to {split_slot-1})")
    print(f"Part 2: {output_file2} (slots {split_slot} to {total_slots-1})")
    
    # Read and split the file
    # Using memmap for memory efficiency with large files
    try:
        # Memory-map the input file
        data = np.memmap(input_file, dtype=np.complex64, mode='r')
        
        # Write part 1
        print(f"\nWriting part 1 ({split_sample:,} samples)...")
        part1 = data[:split_sample]
        part1.tofile(output_file1)
        
        # Write part 2
        remaining_samples = len(data) - split_sample
        print(f"Writing part 2 ({remaining_samples:,} samples)...")
        part2 = data[split_sample:]
        part2.tofile(output_file2)
        
        # Verify
        size1 = os.path.getsize(output_file1)
        size2 = os.path.getsize(output_file2)
        print(f"\nDone!")
        print(f"Part 1 size: {size1/1e9:.2f} GB ({size1//bytes_per_sample:,} samples)")
        print(f"Part 2 size: {size2/1e9:.2f} GB ({size2//bytes_per_sample:,} samples)")
        print(f"Total: {(size1+size2)/1e9:.2f} GB (original: {file_size/1e9:.2f} GB)")
        
        return True
        
    except Exception as e:
        print(f"ERROR: {e}")
        return False

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    
    input_file = sys.argv[1]
    split_slot = int(sys.argv[2])
    
    # Optional sample rate parameter
    sample_rate = 23.04e6  # Default
    if len(sys.argv) >= 4:
        sample_rate = float(sys.argv[3])
    
    # Check if input file exists
    if not os.path.exists(input_file):
        print(f"ERROR: File not found: {input_file}")
        sys.exit(1)
    
    # Split the file
    success = split_iq_file(input_file, split_slot, sample_rate)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()
