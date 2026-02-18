# Compression Research for Wormhole P2P Storage

## 1. Executive Summary

**Can we compress chunks losslessly?** Yes. All candidates are mathematically lossless — bit-for-bit identical on decompression.

**How much time does it add?** Negligible. zstd L3 compresses a 256KB chunk in ~1.3ms and decompresses in ~0.15ms. An entropy pre-check (~10 microseconds) skips incompressible data entirely.

**Does it work on all file types?** Generic compression (zstd) works on any byte sequence, but already-compressed media (JPEG, MP4, MP3, ZIP) gets ~0% benefit. Media-specific lossless recompression (JPEG-XL, Lepton, FLAC) can save 20%+ on certain formats but requires whole-file access, which is architecturally incompatible with our chunk-first pipeline.

**Recommendation:** Implement **zstd level 3** with entropy-based skip detection at the `ChunkStore` layer. This is a clear win — 15-45% storage savings depending on workload, zero cost on incompressible data, no protocol changes, ~300 LOC. Media-specific compression is a separate future consideration requiring architectural changes.

---

## 2. Why Compression Matters for P2P Storage

Every chunk in Wormhole is replicated 3x across peers. Compression savings multiply across every replica:

| Metric | Without Compression | With ~35% Compression |
|--------|--------------------|-----------------------|
| 10 GB quota, 3x replication | ~3.33 GB effective unique data | ~5.13 GB effective unique data |
| Improvement | — | **+54% usable storage** |

Beyond storage, compressed chunks reduce bandwidth for every peer-to-peer transfer — store operations, replication, health-check recovery, and DHT-coordinated chunk retrieval all move less data over the wire.

With a 10 GB default quota (`max_storage_gb` in config) and the RS(4,2) erasure coding generating 50% additional parity data, compression directly extends how much real user data the network can hold.

---

## 3. Answering the Key Questions

### Q1: Is it 100% reversible?

**Yes.** All algorithms evaluated are mathematically lossless. Decompressed output is bit-for-bit identical to the original input. This is a fundamental property of the LZ77/LZ78/entropy coding families — they exploit statistical redundancy without discarding data.

Wormhole's integrity model remains intact:
- Blake3 hash is computed on **raw data before compression** (content addressing unchanged)
- On retrieval, data is decompressed then verified against the stored Blake3 hash
- Dedup works identically — same raw content produces the same hash regardless of compression
- If decompression ever fails or produces wrong data, the Blake3 verification catches it

### Q2: How much time does it add?

Benchmarks for a single 256KB chunk (zstd level 3, single-threaded, modern x86):

| Operation | Time | Context |
|-----------|------|---------|
| zstd compress (256KB) | ~1.3 ms | Per chunk on store |
| zstd decompress (256KB) | ~0.15 ms | Per chunk on retrieve |
| Blake3 hash (256KB) | ~0.3 ms | Already done per chunk |
| Entropy pre-check (4KB sample) | ~0.01 ms | Skip incompressible data |

For a 1 GB file (4,096 chunks):
- **Store:** +5.3 seconds total (~1.3ms/chunk), or **+4.5s net** after subtracting reduced disk I/O
- **Retrieve:** +0.6 seconds total, often **faster overall** because reading smaller files from disk is quicker

For incompressible data (JPEG, MP4, encrypted): the entropy check costs ~0.01ms per chunk, then skips compression. Effectively zero overhead.

### Q3: Does it work on all file types?

Generic lossless compression (zstd) operates on raw bytes and accepts any input. However, data that's already compressed or encrypted has maximum entropy and cannot be compressed further:

| Data Type | Generic (zstd) Benefit | Why |
|-----------|----------------------|-----|
| Text, source code, logs | 70-90% | Highly redundant byte patterns |
| JSON, XML, CSV | 75-88% | Repeated structural tokens |
| Executables, DLLs | 33-50% | Structured binary with padding |
| Database files | 50-75% | Repeated schemas, NULL padding |
| JPEG, PNG, MP4, MP3 | ~0% | Already compressed by codec |
| ZIP, 7z, GZIP | ~0% | Already compressed by archiver |
| Encrypted data | ~0% | Designed to be indistinguishable from random |
| RS parity shards | ~0% | Maximum entropy by construction |

Media-specific lossless compression exists (covered in Section 8) but requires whole-file access — architecturally incompatible with chunk-level storage.

---

## 4. Algorithm Comparison

Six candidates evaluated for 256KB chunk compression:

| Algorithm | Compress Speed | Decompress Speed | Ratio (Silesia) | License | Notes |
|-----------|---------------|-------------------|-----------------|---------|-------|
| **zstd L3** | ~200 MB/s | ~1.7 GB/s | 3.15x | BSD | **Recommended.** Best ratio at this speed class. Trained dictionaries possible. |
| LZ4 (default) | ~800 MB/s | ~4.0 GB/s | 2.10x | BSD | Speed king. Good fallback if CPU is constrained. |
| Snappy | ~580 MB/s | ~1.8 GB/s | 2.09x | BSD | LZ4 is strictly better (faster + same ratio). No reason to choose. |
| zlib L6 | ~50 MB/s | ~400 MB/s | 3.05x | zlib | Legacy. 4x slower to compress than zstd for slightly worse ratio. |
| Brotli L4 | ~40 MB/s | ~450 MB/s | 3.18x | MIT | Designed for HTTP. Marginal ratio gain doesn't justify 5x slower compress. |
| LZO | ~650 MB/s | ~850 MB/s | 2.05x | **GPL** | License incompatible with BSD/MIT projects. LZ4 is faster anyway. |

**Source:** lzbench synthetic benchmarks on Silesia corpus, single-threaded x86-64.

### Why zstd?

1. **Best ratio-to-speed tradeoff**: 3.15x compression at 200 MB/s compress / 1.7 GB/s decompress
2. **Asymmetric design**: Decompression is 8x faster than compression — ideal for storage (compress once on store, decompress many times on retrieve)
3. **Level tuning**: L1 (fast, ~2.8x) through L19 (slow, ~3.5x) — configurable per deployment
4. **Small compiled size**: ~300KB static library, vendorable like Blake3
5. **BSD license**: Compatible with the project
6. **Battle-tested**: Used by Facebook, Linux kernel (btrfs, squashfs), FreeBSD, Android

### LZ4 as an alternative

If profiling shows zstd L3's 1.3ms per chunk is too expensive on target hardware (unlikely on any modern CPU), LZ4 at 0.3ms per chunk is a viable fallback with ~30% less compression ratio.

---

## 5. The Pre-Compressed Data Problem

Attempting to compress already-compressed data wastes CPU for zero benefit. Wormhole should detect and skip these chunks.

### Shannon Entropy Detection

Sample the first 1-4KB of each chunk and compute byte-frequency entropy:

```
H = -sum(p(x) * log2(p(x))) for each byte value x
```

- Maximum entropy: 8.0 bits/byte (perfectly random)
- Threshold: **> 7.5 bits/byte** → skip compression
- Cost: ~10 microseconds per chunk (negligible)

This catches:
- JPEG, PNG, MP4, MP3, AAC, OGG (codec-compressed)
- ZIP, 7z, GZIP, ZSTD (archive-compressed)
- Encrypted data (AES, ChaCha20)
- RS parity shards (GF(2^8) arithmetic produces max-entropy output)

### Implementation sketch

```c
static BOOLEAN ShouldCompress(const uint8_t *data, uint32_t size)
{
    // Sample first 4KB (or full chunk if smaller)
    uint32_t sample = (size < 4096) ? size : 4096;
    uint32_t freq[256] = {0};

    for (uint32_t i = 0; i < sample; i++)
        freq[data[i]]++;

    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] == 0) continue;
        double p = (double)freq[i] / sample;
        entropy -= p * log2(p);
    }

    return (entropy < 7.5);  // Skip if near-random
}
```

### False positive/negative rates

- False positives (compress when shouldn't): Rare. High-entropy compressible data is unusual. Worst case: wasted ~1ms CPU, chunk stored uncompressed after zstd returns it at original size.
- False negatives (skip when could compress): Possible for structured binary with high local entropy but global redundancy. Marginal loss — these chunks typically compress only 5-10% anyway.

---

## 6. Space Savings Estimates

### By file type (zstd L3)

| File Type | Compression Ratio | Space Savings | Example |
|-----------|-------------------|---------------|---------|
| Text / source code | 4-5x | 75-80% | 256KB → ~55KB |
| JSON / XML / CSV | 4-8x | 75-88% | 256KB → ~40KB |
| Log files | 5-10x | 80-90% | 256KB → ~30KB |
| Database files (SQLite, etc.) | 2-4x | 50-75% | 256KB → ~90KB |
| Executables / DLLs | 1.5-2x | 33-50% | 256KB → ~150KB |
| Office docs (raw OLE) | 2-3x | 50-67% | 256KB → ~110KB |
| .docx/.xlsx (ZIP internally) | ~1x | ~0% | Already ZIP-compressed |
| JPEG / PNG / WebP | ~1x | ~0% | Already codec-compressed |
| MP4 / MKV / AVI (H.264/265) | ~1x | ~0% | Already codec-compressed |
| MP3 / AAC / OGG / FLAC | ~1x | ~0% | Already compressed |
| ZIP / 7z / tar.gz | ~1x | ~0% | Already archive-compressed |
| Encrypted data | ~1x | ~0% | Maximum entropy by design |

### Realistic workload scenarios

| Workload | Estimated Overall Savings | Notes |
|----------|--------------------------|-------|
| Developer workstation | 35-45% | Source code, build artifacts, docs, some binaries |
| Document archive | 30-50% | PDFs (partially compressed), Word/Excel, text files |
| Mixed personal files | 15-25% | Photos, videos, documents, downloads |
| Photo/video archive | 3-5% | Almost entirely pre-compressed media |
| Database backups | 50-65% | SQL dumps, SQLite files, CSV exports |
| Server log archive | 70-85% | Highly repetitive text |

### Impact on Wormhole's effective capacity

With default 10 GB quota and 3x replication:

| Workload | Without Compression | With Compression | Improvement |
|----------|--------------------|--------------------|-------------|
| Developer | 3.33 GB usable | 5.0-5.7 GB usable | +50-71% |
| Mixed personal | 3.33 GB usable | 3.9-4.3 GB usable | +17-29% |
| Photo/video | 3.33 GB usable | 3.4-3.5 GB usable | +2-5% |
| Database backup | 3.33 GB usable | 5.5-6.7 GB usable | +65-100% |

---

## 7. How It Fits Wormhole's Architecture (Generic Compression)

### Insertion point: `ChunkStore_Put()` and `ChunkStore_Get()`

Compression is applied transparently inside the chunk store layer. No other component needs to change.

**`ChunkStore_Put()` — compress before write:**
```
1. Receive raw chunk data + Blake3 hash (caller already hashed it)
2. Check ChunkStore_Has() — skip if duplicate (unchanged)
3. Run entropy check → if high entropy, store raw with 1-byte header [0x00]
4. Compress with zstd → if compressed < raw, store with header [0x01][compressed]
5. If compressed >= raw (expansion), store raw with header [0x00]
6. fwrite() to ~/.wormhole/store/XX/YYYYYY...
```

**`ChunkStore_Get()` — decompress after read:**
```
1. fread() from disk
2. Check first byte: 0x00 = raw, 0x01 = zstd-compressed
3. If compressed, decompress to caller's buffer
4. Return raw data (caller verifies Blake3 hash as before)
```

### What stays the same

| Component | Impact | Why |
|-----------|--------|-----|
| **Blake3 hashing** | None | Hash computed on raw data before `ChunkStore_Put()` |
| **Content addressing / dedup** | None | Same raw data → same hash → `ChunkStore_Has()` skips |
| **Erasure coding** | None | `ErasureCoding_Encode()` calls `ChunkStore_Get()` which returns decompressed data. Parity shards pass through `ChunkStore_Put()` and get entropy-skipped (max entropy). |
| **Proof-of-storage** | None | `Proof_Compute()` uses `Blake3(seed \|\| chunk_data)` on raw data. The daemon reads via `ChunkStore_Get()` (decompressed) before computing proofs. Cached proofs in `~/.wormhole/proofs/` are unaffected. |
| **Health checks** | None | `Health_RecoverChunk()` uses `ChunkStore_Get()`/`ChunkStore_Put()` — transparent |
| **Transfer protocol** | None | Chunks travel over QUIC in raw form (compress only at storage layer) |
| **Manifest format** | None | Manifests reference raw chunk hashes and sizes |
| **LRU eviction** | Works better | `ChunkStore_GetTotalSize()` scans actual file sizes on disk. Compressed chunks are smaller on disk, so the quota stretches further — eviction logic needs no changes. |
| **Storage ledger / incentives** | None | `Ledger_ShouldAcceptStorage()` makes accept/reject decisions based on peer ratios, not chunk sizes |
| **Replica metadata** | None | Stored in `~/.wormhole/replicas/`, separate from chunk data |
| **IPC (store/get commands)** | None | IPC passes raw data; `ChunkStore_Put()`/`Get()` handles compression internally |
| **DHT store/lookup** | None | DHT stores chunk hash → peer locations, not chunk data |

### Store format versioning

A 1-byte header prefix on each stored chunk file distinguishes formats:
- `0x00` — raw (uncompressed)
- `0x01` — zstd compressed

This allows:
- Existing chunk stores to work without migration (files without the prefix can be detected by size or treated as raw during a one-time migration)
- Future algorithm changes (e.g., `0x02` for LZ4) without breaking backward compatibility

---

## 8. Media-Specific Compression Deep Dive

### The fundamental problem

Media-specific lossless compression algorithms (JPEG-XL, Lepton, FLAC, FFV1) achieve significant savings on their target formats, but they all require **whole-file access** — they parse headers, frame structures, color spaces, and codec-specific data that span the entire file.

Wormhole's pipeline is: **file → chunk into 256KB pieces → hash each chunk → store individually**. An arbitrary 256KB chunk from the middle of a JPEG has no JPEG headers, no Huffman tables, no frame boundaries. Format-aware compression cannot operate on it.

This is a fundamental architectural mismatch, not a bug to fix.

### What exists for each media type

#### Photos / Images

| Format | Tool | Savings | Reversible? | Whole-File Required? |
|--------|------|---------|-------------|---------------------|
| JPEG | JPEG-XL (lossless recompression) | ~20% | Yes, bit-for-bit to original JPEG | Yes |
| JPEG | Lepton (Dropbox) | ~22% | Yes, bit-for-bit | Yes |
| JPEG | Brunsli / PackJPG | 22-24% | Yes, bit-for-bit | Yes |
| PNG | Zopfli re-compression | ~5% | Not reversible to original PNG (different DEFLATE stream) |  Yes |
| RAW (CR2, NEF, DNG) | N/A | Marginal | N/A | Already use internal compression |

JPEG-XL's lossless JPEG recompression is the most promising — it reconstructs the entropy coding more efficiently without touching pixel data. Lepton (developed by Dropbox, now maintained by Microsoft as a Rust port called `lepton-rs`) achieves similar results through arithmetic re-encoding of DCT coefficients.

All of these operate on complete JPEG files with intact headers.

#### Video

| Format | Tool | Savings | Practical? |
|--------|------|---------|-----------|
| Uncompressed (YUV, DPX) | FFV1 | ~3x | Yes, but rare format |
| H.264/H.265 MP4 | N/A | ~0% | Already compressed |
| ProRes / DNxHR | FFV1 | 20-40% | Re-encoding takes hours for large files |

Video re-encoding is impractical for P2P storage:
- A 4K 1-hour ProRes file takes 30-60 minutes to re-encode to FFV1 lossless
- Most user video is already H.264/H.265 (0% compressible)
- The compute cost vastly outweighs storage savings

#### Audio

| Format | Tool | Savings | Notes |
|--------|------|---------|-------|
| WAV / AIFF (uncompressed) | FLAC | 50-70% | Excellent, but WAV is increasingly rare |
| FLAC | N/A | ~0% | Already losslessly compressed |
| MP3 / AAC / OGG | N/A | ~0% | Lossy-compressed, cannot recover original |

Only uncompressed audio (WAV, AIFF) benefits meaningfully, and these formats are uncommon in typical user workflows.

#### Application / Creative Files

| Format | Internal Compression | Additional Savings |
|--------|---------------------|-------------------|
| PSD (Photoshop) | RLE | ~33% with zstd on raw layers |
| .blend (Blender) | zlib | ~5-10% |
| .ai / .indd (Adobe) | Mixed | ~10-15% |
| .docx / .xlsx / .pptx | ZIP (DEFLATE) | ~0% |

Most modern application formats already use internal compression. The gains from re-compressing are marginal and format-specific.

### What major platforms actually do

| Platform | Approach | Media Handling |
|----------|----------|---------------|
| **Dropbox** | Compress whole files before chunking | Lepton for JPEG (~22%), zlib for others |
| **Google Drive** | Server-side, opaque | Undisclosed internal compression |
| **IPFS** | No protocol-level compression | Nodes may compress local blockstore independently |
| **BitTorrent** | No compression | Pieces are raw; clients don't compress |
| **Backblaze B2** | No compression | Stores raw objects |

**Key insight:** Major P2P systems (IPFS, BitTorrent) either don't compress at all, or (like Dropbox) compress whole files before chunking. Nobody does format-aware compression on individual chunks.

### Architectural options for Wormhole

#### Option A: Compress-Before-Chunk (Dropbox approach)

```
File → detect format → format-aware compress → chunk → hash → store
```

- **Pro:** Full media compression benefits (20% on JPEG, 50-70% on WAV)
- **Con:** Blake3 hashes are now of compressed data, not original file data
- **Con:** Dedup breaks across compressed/uncompressed versions of the same file
- **Con:** Receiver must know to decompress after reassembly
- **Con:** Requires format detection, multiple codec dependencies, error handling per format
- **Con:** Manifest must track: "this file was pre-compressed with X before chunking"

#### Option B: Storage-Layer Transparency (Recommended for Phase 1)

```
File → chunk → hash on raw data → store with zstd on disk → decompress on retrieve
```

- **Pro:** Transparent, backward compatible, no protocol changes
- **Pro:** All existing code (erasure coding, proofs, health checks) works unchanged
- **Pro:** Simple implementation (~300 LOC)
- **Con:** Only generic compression works — no media-specific gains on pre-compressed formats

This is what Section 7 describes. It is the clear first step.

#### Option C: Dual-Path Hybrid (Future consideration)

```
Default: Option B (chunk-level zstd)
For supported formats: Option A (whole-file compress → chunk → store)
Manifest v4 metadata: { pre_compression: "jpeg-xl" | "flac" | null }
```

- Most complex but maximizes savings across all file types
- Requires manifest v4 with pre-compression metadata
- Each supported format needs a codec dependency and encode/decode path
- Would be a significant engineering effort — months of work

### The honest answer on media

- Generic lossless compression (zstd) gives **~0% benefit** on already-compressed media (JPEG, MP4, MP3)
- Media-specific lossless compression (JPEG-XL, Lepton, FLAC) **can save 20%+** but requires whole-file access
- Our chunk-based architecture makes media-specific compression a **future optimization**, not a Phase 1 feature
- The good news: media that IS uncompressed (WAV, BMP, raw video, PSD layers) benefits enormously from generic zstd
- Most users' media collections are dominated by pre-compressed formats where no compression approach helps at the chunk level

---

## 9. Risks and Trade-offs

### Backward compatibility

Existing chunk stores contain raw data with no header byte. Options:
1. **Migration on first access:** If file size equals exactly a valid chunk size and first byte isn't a known header, treat as raw. Low risk since the 1-byte header approach is unambiguous.
2. **Store version file:** Write `~/.wormhole/store/VERSION` with format version. New stores get version 2 (compressed). Old stores without VERSION file are version 1 (all raw).
3. **Config flag:** `compression_enabled = 1` (default on for new installs). Existing users can enable when ready.

Recommendation: Option 2 + 3 combined. VERSION file for detection, config flag for user control.

### CPU overhead on constrained devices

zstd L3 at 200 MB/s uses meaningful CPU on low-power devices (Raspberry Pi, older NAS boxes). Mitigations:
- Config: `compression_level = 0` to disable, `1` for LZ4-fast, `3` for zstd default
- Entropy skip ensures zero overhead on incompressible chunks
- Decompression at 1.7 GB/s is never the bottleneck (disk I/O is slower)

### Dependency size

- zstd single-file amalgamation: ~300KB compiled
- Can be vendored into `deps/zstd/` following the same pattern as Blake3 (`deps/blake3/`) and Reed-Solomon (`deps/reed_solomon/`)
- No external runtime dependencies

### Compression ratio variability

Chunks from the same file may compress differently (e.g., a text file's first chunk compresses 80% but a chunk containing an embedded image compresses 5%). This is fine:
- `ChunkStore_GetTotalSize()` already scans actual file sizes on disk
- LRU eviction uses actual `file_size` from disk (see `EVICTION_CANDIDATE` struct in `chunk_store.c`)
- Quota enforcement uses real disk usage, not estimated sizes

### Data integrity under corruption

If a compressed chunk file is corrupted on disk:
- zstd decompression will fail (returns error code)
- `ChunkStore_Get()` returns FALSE
- Health monitoring detects the missing chunk
- EC recovery (`ErasureCoding_ReconstructChunk()`) rebuilds it from parity
- This is the same recovery path as a completely missing chunk — no new failure modes

---

## 10. Recommendation and Next Steps

### Phase 1: Generic chunk-level compression (clear win)

**Implement zstd L3 with entropy-based skip detection inside `ChunkStore_Put()`/`ChunkStore_Get()`.**

Scope:
- New file: `src/compression.c` / `src/compression.h` (~150 LOC)
  - `Compression_Compress(data, size, out, out_size)` — zstd compress with entropy pre-check
  - `Compression_Decompress(data, size, out, out_size)` — zstd decompress
  - `Compression_ShouldSkip(data, size)` — Shannon entropy check
- Modify: `src/chunk_store.c` (~100 LOC changes)
  - `ChunkStore_Put()`: compress before `fwrite()`, prepend 1-byte format header
  - `ChunkStore_Get()`: read header, decompress if needed before returning to caller
- New dependency: `deps/zstd/` — vendored single-file build (like Blake3 pattern)
- Config additions: `compression_enabled` (default 1), `compression_level` (default 3)
- Build: Add zstd sources to `build.bat`
- Tests: `test_compression.c` — compress/decompress roundtrip, entropy skip, incompressible data, empty input, header format

**Expected savings:** 15-45% depending on workload, with zero overhead on incompressible data.

### Phase 2: Media-specific compression (future, if warranted)

If media savings become important for the user base:
1. Research compress-before-chunk for JPEG-XL lossless JPEG recompression
2. Design manifest v4 metadata for tracking pre-compression
3. Prototype with JPEG-only (largest potential impact: ~20% savings on JPEG-heavy workloads)
4. Evaluate whether the architectural complexity is justified by actual user workload data

### Not recommended

| Approach | Reason |
|----------|--------|
| Video re-encoding (FFV1, H.265 lossless) | Too slow (hours per file), too complex, most video already compressed |
| Per-chunk media compression | Architecturally impossible — format codecs need whole-file access |
| Transfer-layer compression (compress on wire) | Protocol change for marginal benefit; storage compression already reduces transfer sizes indirectly via smaller chunks on disk that peers serve |
| Dictionary-trained zstd | Significant complexity (need representative training corpus per file type), marginal gain (~5-10% on small chunks), not worth it for 256KB chunks where the dictionary overhead amortizes poorly |

---

## 11. Sources

### Compression Algorithms

- **zstd**: [facebook/zstd](https://github.com/facebook/zstd) — BSD licensed, includes single-file amalgamation for embedding. Benchmarks in repo and at [lzbench](https://github.com/inikep/lzbench).
- **LZ4**: [lz4/lz4](https://github.com/lz4/lz4) — BSD licensed. Fastest decompression in class.
- **lzbench**: [inikep/lzbench](https://github.com/inikep/lzbench) — Standardized compression benchmark suite. Silesia corpus results used for algorithm comparison table.
- **Squash Compression Benchmark**: Comprehensive multi-algorithm benchmarks across diverse datasets.

### Media-Specific Compression

- **JPEG-XL**: [libjxl/libjxl](https://github.com/libjxl/libjxl) — JPEG lossless recompression achieves ~20% savings. ISO/IEC 18181 standard.
- **Lepton**: [dropbox/lepton](https://github.com/dropbox/lepton) — Dropbox's JPEG compressor, ~22% savings. Deprecated in C++ but Microsoft maintains [AzureAD/lepton-rs](https://github.com/nickel-org/rust-lepton) (Rust port).
- **Dropbox Engineering Blog**: "Lepton image compression: saving 22% losslessly from images at 15MB/s" (2016). Details on JPEG entropy re-encoding approach.
- **Brunsli**: [google/brunsli](https://github.com/google/brunsli) — Google's JPEG recompressor, ~22% savings. Incorporated into JPEG-XL.
- **PackJPG**: Lossless JPEG recompression, ~24% savings. Academic project.

### Video and Audio

- **FFV1**: Library of Congress recommended format for video preservation. Lossless, ~3x compression vs uncompressed. Part of Matroska container specification.
- **FLAC**: [xiph.org/flac](https://xiph.org/flac/) — Free Lossless Audio Codec. 50-70% compression on PCM audio. Widely supported.

### P2P and Storage Systems

- **IPFS Documentation**: [docs.ipfs.tech](https://docs.ipfs.tech/) — No protocol-level compression; nodes manage local storage independently.
- **BitTorrent Protocol Specification (BEP 3)**: No compression — pieces are raw byte ranges of the original file.
- **Backblaze B2**: Stores objects as-is with no server-side compression.

### Information Theory

- **Shannon Entropy**: Shannon, C.E. "A Mathematical Theory of Communication" (1948). Entropy formula used for incompressibility detection.
- Practical entropy threshold of 7.5 bits/byte for skip detection is well-established in compression literature and used by tools like `ent` and `binwalk`.
