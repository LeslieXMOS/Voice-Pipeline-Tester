import subprocess
import numpy as np
import xscope_fileio
import argparse
from pathlib import Path
import os
import wave
import soundfile

# Target parameters
TARGET_SR = 16000
HOST_PATH = (Path(__file__).parent).absolute()
TEST_AUDIO_PATH = f"{HOST_PATH}/test_audio"

firmware_xe = (Path(__file__).parent / ".." / "bin" / "VoicePipelineTest.xe").absolute()
adapter_id = 0

# read test audio
audio_data_list = []
file_names = []

filename = "long.wav"

file_path = os.path.join(TEST_AUDIO_PATH, filename)
with wave.open(file_path, 'rb') as wav_file:
    # Extract metadata
    n_channels = wav_file.getnchannels()      # 1 for mono, 2 for stereo
    sample_width = wav_file.getsampwidth()    # 2 for 16-bit, 4 for 32-bit
    framerate = wav_file.getframerate()       # e.g., 16000
    n_frames = wav_file.getnframes()          # Total number of samples

    # Read raw bytes
    raw_data = wav_file.readframes(n_frames)

    # Determine the correct numpy dtype based on sample_width
    if sample_width == 2:
        dtype = np.int16
    elif sample_width == 4:
        dtype = np.int32
    else:
        assert False, f"Unsupported sample width: {sample_width}, skipping {filename}"

    # skip if framerate not 16kHz
    assert framerate == 16000, f"Unsupported framerate: {framerate}, skipping {filename}"

    # Convert buffer to numpy array
    audio_array = np.frombuffer(raw_data, dtype=dtype)

    assert n_channels == 6, "Expected 6 channels in the audio file"
    audio_array = audio_array.reshape(-1, n_channels)
    audio_array = audio_array[:, [2, 3, 4, 5, 0, 1]] #reorder to match ref ref mic mic proc proc needed by the voice pipeline

    # Convert to 32bit depth if it is 16bit
    if sample_width == 2:
        audio_array = audio_array.astype(np.int32) << 16

    # Save to temp folder
    audio_array.tofile(f"{HOST_PATH}/temp/audio.bin")

    # run example
    xscope_fileio.run_on_target(adapter_id, firmware_xe, use_xsim=False)

    # read back the result
    processed_audio = np.fromfile(f"{HOST_PATH}/temp/out.bin", dtype=np.int32).reshape(-1, 6)

    os.makedirs(f"{HOST_PATH}/result", exist_ok=True)
    soundfile.write(f"{HOST_PATH}/result/{filename[:-4]}.wav", processed_audio, TARGET_SR)


print("Test completed!")