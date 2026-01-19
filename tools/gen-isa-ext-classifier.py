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
    Build an efficient mask table by grouping opcodes with common prefixes.
    Returns list of (mask, set of values) for checking.
    """
    # Strategy: Group opcodes by common high-order bits
    # For each mask level, only include opcodes that aren't covered by
    # more specific masks
    
    common_masks = [
        0xffffffff,  # Exact match
        0xffffff00,  # Top 24 bits
        0xfffff000,  # Top 20 bits
        0xffff0000,  # Top 16 bits
        0xfff00000,  # Top 12 bits
        0xffc00000,  # Top 10 bits
        0xff000000,  # Top 8 bits
    ]
    
    # Collect all unique (mask, value) pairs
    all_patterns = set()
    for opcode, mnemonic in opcodes:
        for mask in common_masks:
            value = opcode & mask
            all_patterns.add((mask, value))
    
    # Group by mask and return only the unique values for each mask
    result = []
    for mask in common_masks:
        values = sorted({value for m, value in all_patterns if m == mask})
        if values:
            result.append((mask, values))
    
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
        
        # Always use switch for consistency and compiler optimization
        lines.append(f"    switch (masked) {{")
        for value in values:
            lines.append(f"        case 0x{value:08x}: return true;")
        lines.append(f"        default: break;")
        lines.append(f"    }}")
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
