# Retro decoder fixtures

Store tiny, legally redistributable synthetic test files here as formats are implemented. Use generated fixtures when possible, document provenance and licensing for historical samples, and avoid committing full copyrighted songs.

Required coverage per decoder: valid mono/stereo samples; boundary/truncation cases; malformed size/overflow; endian and signedness; frequency and sample count; reference PCM checksums. Use separate test sources under `tests/` and wire into `make test` when implementing a decoder.
