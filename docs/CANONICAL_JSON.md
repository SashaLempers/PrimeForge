# Canonical JSON v1 contract

PrimeForge canonical JSON is a restricted deterministic subset of RFC 8259. The schema identifier for the future implementation will include a version; these rules define version 1.

## Encoding

- The byte encoding is valid UTF-8 without a byte-order mark.
- Object keys and string values must be valid Unicode normalized to NFC before serialization.
- Object keys are unique after NFC normalization.
- Keys are sorted lexicographically by their normalized UTF-8 byte sequences.
- Canonical documents contain no insignificant whitespace and no trailing newline.
- JSON Lines files append exactly one LF byte after each independently canonical object; an object's content hash excludes that framing LF.

## Values

- Floating-point numbers are forbidden.
- JSON numeric tokens are restricted to integers in the exact interoperable range `[-9007199254740991, 9007199254740991]`.
- Mathematical integers outside that range are decimal strings.
- A canonical non-negative decimal string is `0` or `[1-9][0-9]*`.
- A canonical signed decimal string is `0`, a positive canonical string, or `-[1-9][0-9]*`.
- A leading `+`, leading zero, decimal point, exponent, `-0`, `NaN`, and infinity are forbidden.
- The literals are exactly `true`, `false`, and `null`.

## Strings and escaping

- Quotation mark, reverse solidus, and U+0000 through U+001F are escaped.
- The short escapes `\b`, `\t`, `\n`, `\f`, and `\r` are used for their corresponding controls; other controls use lowercase `\u00xx`.
- Other Unicode scalar values are emitted directly as UTF-8, not escaped.
- Lone surrogate code points are invalid.

## Hashing

The logical value is validated, normalized, ordered, and serialized according to this document. SHA-256 is calculated over the resulting exact bytes. A newline, BOM, alternate key order, non-minimal escape, or non-canonical decimal representation changes or invalidates the byte representation. Windows and Linux implementations must share cross-platform golden vectors before work-unit identifiers depend on this format.

Stage 1 defined this contract and the injectable provider interface. Stage 7 adds an internal portable SHA-256 backend and a schema-specific work-unit serializer. Stage 8 adds a schema-specific canonical family serializer and the independent golden hash `82b8d4092dc444268cf0fb4376ccec502a1f3197aebcead837176d887e1c31cb`. Both serializers currently accept only ASCII user strings, a strict subset that is already valid UTF-8/NFC; they reject non-ASCII rather than pretending to normalize Unicode. No general-purpose JSON parser or external cryptographic dependency has been added.
