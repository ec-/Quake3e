#!/usr/bin/env python
import os, string, subprocess, sys

# converts q3asm .map file to hash map file (.hmap)

def usage ():
    print "usage: %s <qvm .map file> <qvm file>" % sys.argv[0]
    print "    example:  %s cgame.map cgame.qvm > cgame-func.hmap" % sys.argv[0]
    sys.exit(1)

def main ():
    if len(sys.argv) < 2:
        usage()

    qvmMapFile = sys.argv[1]
    qvmFile = sys.argv[2]

    f = open(qvmMapFile)
    lines = f.readlines()
    f.close()
    names = {}
    for line in lines:
        words = string.split(line)
        if len(words) > 2:
            if words[0] == "0":
                addr = string.atoi(words[1], 16)
                n = words[2]
                # skip system calls and stack func
                if addr < 0x7fffffff and not n in ("_stackStart", "_stackEnd"):
                    names[addr] = n

    currentDir = os.path.dirname(os.path.realpath(__file__))
    parentDir = os.path.dirname(currentDir)
    qvmdis = os.path.join(parentDir, "qvmdis")

    procOut = subprocess.check_output([qvmdis, "--func-hash", qvmFile])

    hashes = {}

    lines = procOut.splitlines()
    for line in lines:
        words = string.split(line)
        addr = string.atoi(words[0], 16)
        funcHash = words[2]
        hashes[addr] = funcHash

    k = names.keys()
    k.sort ()

    for addr in k:
        print "0x%x %s %s" % (addr, names[addr], hashes[addr])

if __name__ == "__main__":
    main ()

