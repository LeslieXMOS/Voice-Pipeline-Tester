'''
Use this script to convert xscope vcd to wav file

1. Add a probe to xscope, having something like this
    <xSCOPEconfig ioMode="basic" enabled="true">
    <Probe name="CH0" type="CONTINUOUS" datatype="INT" units="mV" enabled="true"/>
    </xSCOPEconfig>
2. Ouput the audio data with
    xscope_int(probe_name, data);
3. Compile program
4. xrun --xscope --xscope-file xscope.vcd your_app.xe
5. python vcd_to_wav.py xscope.vcd 48000 32 0
'''


import io
from vcd.reader import TokenKind, tokenize
import numpy as np
import scipy.io.wavfile as wavfile
import sys

vcd_fname = sys.argv[1]
samp_freq = int(sys.argv[2])
num_bit = int(sys.argv[3])
channel_idx = sys.argv[4]
out_fname = sys.argv[5]

if num_bit == 32:
    threshold = 2**33
    mask = 2**64
elif num_bit == 16:
    threshold = 2**17
    mask = 2**32
elif num_bit == 8:
    threshold = 2**7
    mask = 2**16
else:
    assert False, "unsupport num of bits"

vcd_file = open(vcd_fname, "rb")
vcd_file.readline()
vcd_file.readline()
vcd_file.readline()
tokens = tokenize(vcd_file)

data = []

for token in tokens:
    if token.kind == TokenKind.CHANGE_VECTOR:
        if token.data.id_code == channel_idx:
            v = token.data.value
            if v > threshold:
                v -= mask
            data.append(v)

data = np.asarray(data, dtype=np.int32)
wavfile.write(out_fname, samp_freq, data)
print(f"Converted {data.shape} samples to xscope.wav with sample rate {samp_freq}Hz")