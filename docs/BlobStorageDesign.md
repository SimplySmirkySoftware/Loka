# Blob storage

Status: Normative

Owns: Shared byte ownership, nullable construction, sealing, and whole-file read failure semantics.

Does not own: Native image retirement or editor limits.

Code truth: `common/core/resource/Blob.hpp`, `common/platform/file/FileIO.hpp`,
`common/platform/file/FileRead.cpp`, and platform implementations.

Verification: Blob allocation pins in `tests/LokaAllocTests.cpp`, stdio pins in
`tests/FlowDslTests.cpp`, and Toolbox memory/source and native-image host tests.

## Storage and ownership

A Blob shares a Managed control block owning a noncopyable BlobRecord. The
record owns gate-allocated bytes and their single authoritative logical length.
Loading, progress, and one-way completion are private plain values, written
only through Blob. No State observation or implicit record creation exists.
Record allocation uses Blob/Record; bytes use Blob/Bytes; control blocks use
Managed/ControlBlock. Control-block refusal releases the unadopted record at
its original site. Last-owner release synchronously frees bytes and the record,
then Managed frees the control block. This is pure memory reclamation, without
callbacks, State writes, or a native queue flush.

R4 exception: Blob-owned byte storage also uses the gate for nullable allocation;
ownership and lifetime remain with BlobRecord.

The historical lifecycle-memory-model-rulings file is external to this repository;
this paragraph is the maintained in-repository exception to its R4 allocation scope.

## Construction and sealing

The next byte producer must Create, check validity, tryResize or tryAssign,
fill synchronously, then seal. Only Create allocates a record; it returns an
invalid Blob on refusal. Empty is allocation-free and identical to default
invalid. A successfully created zero-byte Blob is valid and distinct. Invalid
writes never allocate: boolean doors refuse, mutableData returns null, and
metadata setters and seal are no-ops.

tryResize sets the logical extent. Shrink allocates nothing; zero also releases
the old allocation. Growth allocates a replacement, copies the existing prefix,
then releases the old bytes. New tail bytes require initialization by the producer.
There is no retained capacity field. tryAssign copies before freeing, so self
and subrange assignment are safe. Null with zero length is accepted; null with
nonzero length refuses. Refusal preserves pointer, length, bytes, and metadata.
Old and new allocations coexist during successful replacement.

sealBytes sets completion once. Afterward, both storage mutation doors refuse,
mutableData returns null, and loading/progress setters do nothing. The wall is
always active, including release builds. Reads remain valid. An owning Blob copy
retains identity, not a snapshot of mutable storage.

Borrowed data()/mutableData() pointers expire on storage-changing success,
seal, or last-owner release. This is a caller contract: sealing cannot physically
revoke a previously returned raw pointer. LRPK readers close their borrowed bags
before their last Blob owner releases storage.

## Whole-file reads and publication

ReadBytes takes fresh, valid, unsealed scratch. It leaves success unsealed; the
producer finalizes metadata and seals at publication. Failure clears scratch,
closes the stream, and publishes no prefix. FileImageSource stages privately and
replaces caller output only on success. BlobLoader publishes Empty on failure.
Scrapbook checks construction and extent before opening a bag into its bytes,
so allocation refusal preserves the installed page and its reader borrow.

Known-length reads check caller ceilings before byte allocation. Unknown-length
stdio reads check overflow and the caller ceiling against actual filled plus
received bytes before geometrically growing scratch. Reserve extent is not a
semantic file length. EOF truncates scratch to the filled length. Growth can
need both old and new buffers at once; it makes no prediction about heap space.

READ_ALLOCATION_REFUSED means record, control-block, or byte allocation could
not be obtained. READ_CAPACITY_REFUSED means a caller's semantic ceiling was
exceeded. READ_SIZE_OVERFLOW means arithmetic cannot represent the extent.
TextDocument maps allocation to TEXT_DOCUMENT_ALLOCATION and capacity/overflow
to TOO_LARGE, preserving its editor ceiling. For text files, the raw read
allowance is `B + (L ? L − 1 : 0) + 3`: the admitted byte capacity, possible
CRLF expansion for the admitted line count, and a leading UTF-8 byte order mark.
Both additions refuse on overflow; normalized rows still obey the admitted
byte capacity. Neither allocation nor capacity refusal triggers another I/O
path. PlatformReadCapacity is removed; the optional
largest-contiguous-allocation query remains a measurement fact.

## Images and recovery

Toolbox shares only completed Blobs. An incomplete source is copied once into
a checked snapshot of the requested range and sealed. Any refusal releases all
new payload allocations and returns invalid Image.

SimpleViewer's #614 release-and-retry path is triggered only by allocation
refusal. Synchronous byte recovery before retry is a Toolbox-only guarantee,
and only when the last owner is released. It does not guarantee sufficient
space. Win32/macOS native release waits for the next completion under #1065;
a second refusal therefore shows "Not enough memory" and ends the session,
without waiting or looping. Caller capacity refusal is an ordinary read failure.

Allocation tests keep the fake backend installed over the complete allocation
and release region. An owning witness cannot establish last-owner reclamation;
use non-owning free counters for that observation.
