#!/usr/bin/env python3
"""Wrapper to call mkenums with fhead/ftail from files instead of command line."""
import sys
import subprocess
import os

args = sys.argv[1:]
# Find --fhead-file and --ftail-file and replace with --fhead/--ftail content
new_args = []
i = 0
while i < len(args):
    if args[i] == '--fhead-file' and i + 1 < len(args):
        with open(args[i+1]) as f:
            new_args.extend(['--fhead', f.read()])
        i += 2
    elif args[i] == '--ftail-file' and i + 1 < len(args):
        with open(args[i+1]) as f:
            new_args.extend(['--ftail', f.read()])
        i += 2
    else:
        new_args.append(args[i])
        i += 1

result = subprocess.run(new_args, capture_output=True, text=True)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
sys.exit(result.returncode)
