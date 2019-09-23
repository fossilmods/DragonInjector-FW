import lz4.block
import os
import sys

inputFile = sys.argv[1]
base = os.path.splitext(inputFile)[0]
uncomp_f = open(inputFile, "rb")
data = uncomp_f.read()
compressed = lz4.block.compress(data, 'high_compression', store_size=False)
fileOut =  base + "_lz4_compressed.bin"
comp_f = open(fileOut , "wb")
comp_f.write(compressed)
comp_f.close()