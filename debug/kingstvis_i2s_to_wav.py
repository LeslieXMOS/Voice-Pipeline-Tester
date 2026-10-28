import sys
import numpy as np
import scipy.io.wavfile as wavfile

dump_fname = sys.argv[1]
samp_freq = int(sys.argv[2])
num_bit = int(sys.argv[3])
out_fname = sys.argv[4]
sign_mask = 0x80000000
if num_bit <= 16:
    sign_mask = 0x8000
if num_bit <= 8:
    sign_mask = 0x80
val_mask = sign_mask-1


print(f"Start the parse {dump_fname} at {samp_freq}Hz with {num_bit}bits")

audio_data = [[],[]]
y = []
x = []
cnt = 0
signal_data = []

ty = []

with open(dump_fname) as data:
    for line in data.readlines():
        if 'Time' not in line:
            parts = line.split(',')
            val = int(parts[2], 0)
            if val & sign_mask:
                val = (val & val_mask)-val_mask
            else:
                val = val & val_mask
            if parts[1] == '1':
                audio_data[0].append(val)
            elif parts[1] == '2':
                audio_data[1].append(val)
    if sign_mask == 0x80000000:
        audio_data = np.int32(audio_data)
    elif sign_mask == 0x8000:
        audio_data = np.int16(audio_data)
    elif sign_mask == 0x80:
        audio_data = np.int8(audio_data)
    audio_data = audio_data.transpose()
    wavfile.write(out_fname, samp_freq, audio_data)