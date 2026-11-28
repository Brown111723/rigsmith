# Keysmith banks

`KeysmithBanks.bin` is built into Rigsmith / Keysmith (see `keys::banks()` in `Source/Engine/Synth.cpp`).

- **Wavetables**: 2,147 single-cycle waveforms from the **Adventure Kid Waveforms (AKWF)** collection by
  Kristoffer Ekstrand, each family made into one morphable table (up to 64 frames). Public domain (CC0 1.0):
  https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE
- **Instruments** (for the Sampler): one pitched note each of 24 orchestral and keyboard instruments from the
  **Versilian Community Sample Library (VSCO 2 CE)** by Versilian Studios, trimmed to about 3 seconds, mono,
  32 kHz FLAC, the root note measured from the audio. Public domain (CC0 1.0):
  https://github.com/sgossner/VSCO-2-CE

Format (little endian): `KSB1`, u32 count, then per entry u8 kind (0 table, 1 sample), u16+UTF-8 category,
u16+UTF-8 name; a table: u16 frames, u16 frame length, int16 samples; a sample: u8 root note, u32 size, FLAC bytes.
