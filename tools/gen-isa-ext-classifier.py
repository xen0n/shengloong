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
    Build exact opcode matches for precise classification.
    Returns list of exact opcodes to match.
    """
    # Simply return all unique opcodes - we want exact matching
    # to avoid any false positives
    return sorted(set(opcode for opcode, _ in opcodes))


def generate_classifier(extension_name, opcodes):
    """Generate C code for checking if instruction matches extension."""
    lines = []
    lines.append(f"static bool is_{extension_name}_insn(uint32_t insn)")
    lines.append("{")
    
    if not opcodes:
        lines.append("    return false;")
        lines.append("}")
        return "\n".join(lines)
    
    # Generate a single switch statement with all exact opcodes
    lines.append("    switch (insn) {")
    for opcode in opcodes:
        lines.append(f"        case 0x{opcode:08x}: return true;")
    lines.append("        default: return false;")
    lines.append("    }")
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
        
        opcode_list = build_mask_table(opcodes)
        print(f"Generating classifier with {len(opcode_list)} unique opcodes for {ext_name.upper()}", file=sys.stderr)
        
        classifier = generate_classifier(ext_name, opcode_list)
        lines.append(classifier)
        lines.append("")
    
    lines.append("#endif  // _shengloong_isa_ext_classifier_gen_h")
    
    # Write output
    with open(output_file, 'w') as f:
        f.write("\n".join(lines))
    
    print(f"Generated {output_file}", file=sys.stderr)


if __name__ == '__main__':
    main()
