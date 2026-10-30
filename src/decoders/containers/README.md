# Reusable container parsers

IFF-family chunks support AIFF/AIFC, 8SVX, 16SV, MAUD and related formats. RIFF-family handling supports WAV/BWF; Sony Wave64 is related but uses a distinct GUID-based layout. Container recognition is not itself a claim of codec support. Keep bounds checks and chunk padding rules explicit.
