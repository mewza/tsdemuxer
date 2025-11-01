import { PESPacket } from './types';

/**
 * Parse PES packet from buffer
 */
export function parsePES(buffer: Buffer): PESPacket | null {
  if (buffer.length < 6) {
    return null;
  }

  // Check for PES start code (0x000001)
  if (buffer[0] !== 0x00 || buffer[1] !== 0x00 || buffer[2] !== 0x01) {
    return null;
  }

  const streamId = buffer[3];
  const packetLength = (buffer[4] << 8) | buffer[5];

  let pos = 6;
  const packet: PESPacket = {
    streamId,
    packetLength,
    data: Buffer.alloc(0),
  };

  // For some stream IDs, there's no additional PES header
  if (
    streamId !== 0xBC && // program_stream_map
    streamId !== 0xBE && // padding_stream
    streamId !== 0xBF && // private_stream_2
    streamId !== 0xF0 && // ECM
    streamId !== 0xF1 && // EMM
    streamId !== 0xFF && // program_stream_directory
    streamId !== 0xF2 && // DSMCC_stream
    streamId !== 0xF8    // ITU-T Rec. H.222.1 type E
  ) {
    if (buffer.length < 9) {
      return null;
    }

    const byte6 = buffer[pos];
    const byte7 = buffer[pos + 1];
    const pesHeaderDataLength = buffer[pos + 2];

    // Check marker bits
    if ((byte6 & 0xC0) !== 0x80) {
      return null;
    }

    packet.scramblingControl = (byte6 >> 4) & 0x03;
    packet.priority = (byte6 & 0x08) !== 0;
    packet.dataAlignmentIndicator = (byte6 & 0x04) !== 0;
    packet.copyright = (byte6 & 0x02) !== 0;
    packet.originalOrCopy = (byte6 & 0x01) !== 0;

    const ptsDtsFlags = (byte7 >> 6) & 0x03;
    packet.ptsDtsFlags = ptsDtsFlags;

    pos += 3;

    // Parse PTS
    if (ptsDtsFlags === 0x02 || ptsDtsFlags === 0x03) {
      if (pos + 5 <= buffer.length) {
        packet.pts = parseTimestamp(buffer, pos);
        pos += 5;
      }
    }

    // Parse DTS
    if (ptsDtsFlags === 0x03) {
      if (pos + 5 <= buffer.length) {
        packet.dts = parseTimestamp(buffer, pos);
        pos += 5;
      }
    }

    // Skip remaining PES header data
    pos += pesHeaderDataLength - (ptsDtsFlags === 0x03 ? 10 : ptsDtsFlags === 0x02 ? 5 : 0);
  }

  // Extract payload data
  if (pos < buffer.length) {
    packet.data = buffer.slice(pos);
  }

  return packet;
}

/**
 * Parse PTS/DTS timestamp (33 bits)
 */
function parseTimestamp(buffer: Buffer, offset: number): bigint {
  const byte0 = BigInt(buffer[offset]);
  const byte1 = BigInt(buffer[offset + 1]);
  const byte2 = BigInt(buffer[offset + 2]);
  const byte3 = BigInt(buffer[offset + 3]);
  const byte4 = BigInt(buffer[offset + 4]);

  const timestamp = 
    ((byte0 & BigInt(0x0E)) << 29n) |
    (byte1 << 22n) |
    ((byte2 & BigInt(0xFE)) << 14n) |
    (byte3 << 7n) |
    ((byte4 & BigInt(0xFE)) >> 1n);

  return timestamp;
}

/**
 * Check if buffer starts with a PES start code
 */
export function isPESStart(buffer: Buffer): boolean {
  return buffer.length >= 3 && 
         buffer[0] === 0x00 && 
         buffer[1] === 0x00 && 
         buffer[2] === 0x01;
}
