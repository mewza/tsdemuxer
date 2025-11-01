# tsdemuxer

A VLC-style MPEG Transport Stream (MPEG-TS) demuxer implemented in TypeScript.

## Features

- **Complete TS packet parsing** - Parses 188-byte MPEG-TS packets including sync byte detection, headers, and payload extraction
- **PAT (Program Association Table) parsing** - Identifies programs and their PMT PIDs
- **PMT (Program Map Table) parsing** - Extracts stream information including video, audio, and other elementary streams
- **PES (Packetized Elementary Stream) parsing** - Parses PES packets with PTS/DTS timestamps
- **Adaptation field support** - Handles packets with adaptation fields including PCR/OPCR
- **Automatic sync recovery** - Recovers from sync loss and corrupted data
- **Event-based API** - Easy-to-use event emitter interface
- **Stream type detection** - Identifies H.264, H.265, MPEG-2, AAC, AC-3, and more
- **TypeScript** - Fully typed with TypeScript for better development experience

## Installation

```bash
npm install tsdemuxer
```

## Quick Start

```typescript
import { TSDemuxer } from 'tsdemuxer';
import * as fs from 'fs';

const demuxer = new TSDemuxer();

// Listen for Program Association Table
demuxer.on('pat', (pat) => {
  console.log('PAT:', pat);
});

// Listen for Program Map Table
demuxer.on('pmt', (pmt) => {
  console.log('PMT:', pmt);
});

// Listen for PES packets
demuxer.on('pes', ({ pid, pes }) => {
  console.log(`PES on PID ${pid}:`, pes);
});

// Read and parse a .ts file
const stream = fs.createReadStream('video.ts');
stream.on('data', (chunk) => {
  demuxer.push(chunk);
});

stream.on('end', () => {
  const programs = demuxer.getPrograms();
  console.log('Programs:', programs);
});
```

## API Reference

### TSDemuxer

The main demuxer class that parses MPEG-TS streams.

#### Methods

##### `push(data: Buffer): void`

Push data into the demuxer for parsing.

```typescript
demuxer.push(buffer);
```

##### `getPrograms(): ProgramInfo[]`

Get information about all programs in the stream.

```typescript
const programs = demuxer.getPrograms();
programs.forEach(program => {
  console.log(`Program ${program.programNumber}`);
  program.streams.forEach(stream => {
    console.log(`  - ${stream.streamTypeName} (PID: ${stream.pid})`);
  });
});
```

##### `reset(): void`

Reset the demuxer state.

```typescript
demuxer.reset();
```

#### Events

##### `packet`

Emitted for each parsed TS packet.

```typescript
demuxer.on('packet', (packet: TSPacket) => {
  console.log(`Packet PID: ${packet.pid}`);
});
```

##### `pat`

Emitted when a Program Association Table is parsed.

```typescript
demuxer.on('pat', (pat: PAT) => {
  console.log(`Transport Stream ID: ${pat.transportStreamId}`);
  pat.programs.forEach(p => {
    console.log(`Program ${p.programNumber} -> PMT PID ${p.pid}`);
  });
});
```

##### `pmt`

Emitted when a Program Map Table is parsed.

```typescript
demuxer.on('pmt', (pmt: PMT) => {
  console.log(`Program ${pmt.programNumber}`);
  console.log(`PCR PID: ${pmt.pcrPid}`);
  pmt.streams.forEach(s => {
    console.log(`Stream PID ${s.pid}, Type ${s.streamType}`);
  });
});
```

##### `pes`

Emitted when a PES packet is complete.

```typescript
demuxer.on('pes', ({ pid, pes }: { pid: number, pes: PESPacket }) => {
  console.log(`PES on PID ${pid}`);
  if (pes.pts) {
    console.log(`PTS: ${pes.pts}`);
  }
  console.log(`Data length: ${pes.data.length}`);
});
```

### Utility Functions

#### `parsePacket(buffer: Buffer, offset?: number): TSPacket | null`

Parse a single TS packet from a buffer.

```typescript
import { parsePacket } from 'tsdemuxer';

const packet = parsePacket(buffer, 0);
if (packet) {
  console.log(`PID: ${packet.pid}`);
}
```

#### `findSyncByte(buffer: Buffer, offset?: number): number`

Find the next sync byte in a buffer.

```typescript
import { findSyncByte } from 'tsdemuxer';

const syncPos = findSyncByte(buffer, 0);
if (syncPos !== -1) {
  console.log(`Sync found at position ${syncPos}`);
}
```

#### `parsePAT(buffer: Buffer): PAT | null`

Parse a PAT from payload data.

```typescript
import { parsePAT } from 'tsdemuxer';

const pat = parsePAT(payloadBuffer);
if (pat) {
  console.log(`Programs: ${pat.programs.length}`);
}
```

#### `parsePMT(buffer: Buffer): PMT | null`

Parse a PMT from payload data.

```typescript
import { parsePMT } from 'tsdemuxer';

const pmt = parsePMT(payloadBuffer);
if (pmt) {
  console.log(`Streams: ${pmt.streams.length}`);
}
```

#### `parsePES(buffer: Buffer): PESPacket | null`

Parse a PES packet.

```typescript
import { parsePES } from 'tsdemuxer';

const pes = parsePES(pesBuffer);
if (pes && pes.pts) {
  console.log(`PTS: ${pes.pts}`);
}
```

## Types

### TSPacket

```typescript
interface TSPacket {
  syncByte: number;
  transportErrorIndicator: boolean;
  payloadUnitStartIndicator: boolean;
  transportPriority: boolean;
  pid: number;
  scramblingControl: number;
  adaptationFieldControl: number;
  continuityCounter: number;
  adaptationField?: AdaptationField;
  payload?: Buffer;
}
```

### PAT

```typescript
interface PAT {
  transportStreamId: number;
  versionNumber: number;
  currentNextIndicator: boolean;
  programs: PATProgram[];
}

interface PATProgram {
  programNumber: number;
  pid: number; // PMT PID
}
```

### PMT

```typescript
interface PMT {
  programNumber: number;
  versionNumber: number;
  currentNextIndicator: boolean;
  pcrPid: number;
  programInfo: Buffer;
  streams: PMTStream[];
}

interface PMTStream {
  streamType: number;
  pid: number;
  descriptors: Buffer;
}
```

### PESPacket

```typescript
interface PESPacket {
  streamId: number;
  packetLength: number;
  pts?: bigint;
  dts?: bigint;
  data: Buffer;
}
```

### ProgramInfo

```typescript
interface ProgramInfo {
  programNumber: number;
  pmtPid: number;
  pcrPid?: number;
  streams: StreamInfo[];
}

interface StreamInfo {
  pid: number;
  streamType: number;
  streamTypeName: string;
}
```

## Supported Stream Types

- MPEG-1 Video (0x01)
- MPEG-2 Video (0x02)
- MPEG-1 Audio (0x03)
- MPEG-2 Audio (0x04)
- AAC Audio (0x0F)
- MPEG-4 Video (0x10)
- H.264/AVC Video (0x1B)
- H.265/HEVC Video (0x24)
- AC-3 Audio (0x81)

## Examples

See [src/example.ts](src/example.ts) for a complete example.

## Development

```bash
# Install dependencies
npm install

# Build
npm run build

# Run tests
npm test

# Run tests with coverage
npm run test:coverage
```

## License

ISC

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## References

- [ISO/IEC 13818-1](https://www.iso.org/standard/62074.html) - MPEG-2 Systems specification
- [VLC Media Player](https://www.videolan.org/vlc/) - Reference implementation
