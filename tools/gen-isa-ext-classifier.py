#!/usr/bin/env python3
"""
Generate ISA extension classifier from loongarch-opcodes data.

This script reads instruction opcode data from the loongarch-opcodes
submodule and generates efficient C code for detecting ISA extensions
(LSX, LASX, LBT, LVZ).
"""

import sys
from pathlib import Path
from collections import defaultdict


def parse_opcodes(file_path):
    """Parse opcode file and return list of (opcode, mnemonic) tuples."""
    opcodes = []
    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            parts = line.split()
            if len(parts) < 2:
                continue
            
            opcode_str = parts[0]
            mnemonic = parts[1]
            
            try:
                opcode = int(opcode_str, 16)
                opcodes.append((opcode, mnemonic))
            except ValueError:
                continue
    
    return opcodes


def build_mask_table(opcodes):
    """
    Build an efficient mask table by finding common high-order bits.
    Returns list of (mask, set of values) for checking.
    """
    # Strategy: Use high-order byte masking (0xff000000, 0xffc00000, etc.)
    # to create efficient range checks
    
    patterns = defaultdict(set)
    
    # Try common mask patterns
    common_masks = [
        0xff000000,  # Top 8 bits
        0xffc00000,  # Top 10 bits
        0xfff00000,  # Top 12 bits
        0xffff0000,  # Top 16 bits
        0xfffff000,  # Top 20 bits
        0xffffff00,  # Top 24 bits
        0xffffffff,  # Exact match
    ]
    
    for opcode, mnemonic in opcodes:
        # Find the most general mask that uniquely identifies this instruction
        # Start with the most specific and work backwards
        for mask in reversed(common_masks):
            value = opcode & mask
            patterns[mask].add(value)
    
    # Convert to sorted list of (mask, values)
    result = []
    for mask in sorted(common_masks, reverse=True):
        if mask in patterns and patterns[mask]:
            result.append((mask, sorted(patterns[mask])))
    
    return result


def generate_classifier(extension_name, mask_table):
    """Generate C code for checking if instruction matches extension."""
    lines = []
    lines.append(f"static bool is_{extension_name}_insn(uint32_t insn)")
    lines.append("{")
    
    if not mask_table:
        lines.append("    return false;")
        lines.append("}")
        return "\n".join(lines)
    
    # Generate switch-like structure with masks
    lines.append("    uint32_t masked;")
    lines.append("")
    
    for mask, values in mask_table:
        lines.append(f"    masked = insn & 0x{mask:08x};")
        
        # Group consecutive values into ranges
        ranges = []
        i = 0
        while i < len(values):
            start = values[i]
            end = start
            j = i + 1
            
            # Find consecutive values (accounting for mask granularity)
            step = 1 << (32 - bin(mask).count('1'))
            while j < len(values) and values[j] == end + step:
                end = values[j]
                j += 1
            
            if end > start:
                ranges.append(f"    if (masked >= 0x{start:08x} && masked <= 0x{end:08x}) return true;")
            else:
                ranges.append(f"    if (masked == 0x{start:08x}) return true;")
            
            i = j
        
        # If too many individual checks, just emit them
        if len(ranges) > 20:
            # Use a switch statement for readability
            lines.append(f"    switch (masked) {{")
            for value in values[:100]:  # Limit to avoid huge switches
                lines.append(f"        case 0x{value:08x}: return true;")
            if len(values) > 100:
                lines.append(f"        // ... and {len(values) - 100} more cases")
            lines.append(f"        default: break;")
            lines.append(f"    }}")
        else:
            lines.extend(ranges)
        
        lines.append("")
    
    lines.append("    return false;")
    lines.append("}")
    
    return "\n".join(lines)


def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <opcodes-dir> <output-file>", file=sys.stderr)
        sys.exit(1)
    
    opcodes_dir = Path(sys.argv[1])
    output_file = Path(sys.argv[2])
    
    # Read opcode files
    extensions = {
        'lsx': opcodes_dir / 'lsx.txt',
        'lasx': opcodes_dir / 'lasx.txt',
        'lbt': opcodes_dir / 'lbt.txt',
        'lvz': opcodes_dir / 'lvz.txt',
    }
    
    # Generate header
    lines = [
        "/* AUTO-GENERATED FILE - DO NOT EDIT */",
        "/* Generated from loongarch-opcodes data */",
        "",
        "#ifndef _shengloong_isa_ext_classifier_gen_h",
        "#define _shengloong_isa_ext_classifier_gen_h",
        "",
        "#include <stdbool.h>",
        "#include <stdint.h>",
        "",
    ]
    
    # Generate classifiers for each extension
    for ext_name, ext_file in extensions.items():
        if not ext_file.exists():
            print(f"Warning: {ext_file} not found, skipping", file=sys.stderr)
            continue
        
        opcodes = parse_opcodes(ext_file)
        print(f"Loaded {len(opcodes)} {ext_name.upper()} instructions", file=sys.stderr)
        
        mask_table = build_mask_table(opcodes)
        print(f"Generated {len(mask_table)} mask groups for {ext_name.upper()}", file=sys.stderr)
        
        classifier = generate_classifier(ext_name, mask_table)
        lines.append(classifier)
        lines.append("")
    
    lines.append("#endif  // _shengloong_isa_ext_classifier_gen_h")
    
    # Write output
    with open(output_file, 'w') as f:
        f.write("\n".join(lines))
    
    print(f"Generated {output_file}", file=sys.stderr)


if __name__ == '__main__':
    main()
