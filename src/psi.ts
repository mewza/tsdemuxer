import { PAT, PMT, PATProgram, PMTStream } from './types';
import { TABLE_ID_PAT, TABLE_ID_PMT, PID_PAT } from './constants';

/**
 * Parse Program Association Table (PAT)
 */
export function parsePAT(buffer: Buffer): PAT | null {
  if (buffer.length < 8) {
    return null;
  }

  // Skip pointer field if present (first byte after payload_unit_start_indicator)
  let offset = 0;
  const pointerField = buffer[offset];
  offset += 1 + pointerField;

  if (offset >= buffer.length) {
    return null;
  }

  const tableId = buffer[offset];
  if (tableId !== TABLE_ID_PAT) {
    return null;
  }

  const byte1 = buffer[offset + 1];
  const byte2 = buffer[offset + 2];
  const sectionSyntaxIndicator = (byte1 & 0x80) !== 0;
  const sectionLength = ((byte1 & 0x0F) << 8) | byte2;

  if (!sectionSyntaxIndicator || offset + 3 + sectionLength > buffer.length) {
    return null;
  }

  const transportStreamId = (buffer[offset + 3] << 8) | buffer[offset + 4];
  const versionByte = buffer[offset + 5];
  const versionNumber = (versionByte >> 1) & 0x1F;
  const currentNextIndicator = (versionByte & 0x01) !== 0;

  const programs: PATProgram[] = [];
  let pos = offset + 8;
  const endPos = offset + 3 + sectionLength - 4; // Exclude CRC

  while (pos + 4 <= endPos) {
    const programNumber = (buffer[pos] << 8) | buffer[pos + 1];
    const pid = ((buffer[pos + 2] & 0x1F) << 8) | buffer[pos + 3];
    programs.push({ programNumber, pid });
    pos += 4;
  }

  return {
    transportStreamId,
    versionNumber,
    currentNextIndicator,
    programs,
  };
}

/**
 * Parse Program Map Table (PMT)
 */
export function parsePMT(buffer: Buffer): PMT | null {
  if (buffer.length < 12) {
    return null;
  }

  // Skip pointer field
  let offset = 0;
  const pointerField = buffer[offset];
  offset += 1 + pointerField;

  if (offset >= buffer.length) {
    return null;
  }

  const tableId = buffer[offset];
  if (tableId !== TABLE_ID_PMT) {
    return null;
  }

  const byte1 = buffer[offset + 1];
  const byte2 = buffer[offset + 2];
  const sectionSyntaxIndicator = (byte1 & 0x80) !== 0;
  const sectionLength = ((byte1 & 0x0F) << 8) | byte2;

  if (!sectionSyntaxIndicator || offset + 3 + sectionLength > buffer.length) {
    return null;
  }

  const programNumber = (buffer[offset + 3] << 8) | buffer[offset + 4];
  const versionByte = buffer[offset + 5];
  const versionNumber = (versionByte >> 1) & 0x1F;
  const currentNextIndicator = (versionByte & 0x01) !== 0;

  const pcrPid = ((buffer[offset + 8] & 0x1F) << 8) | buffer[offset + 9];
  const programInfoLength = ((buffer[offset + 10] & 0x0F) << 8) | buffer[offset + 11];

  let pos = offset + 12;
  const programInfo = buffer.slice(pos, pos + programInfoLength);
  pos += programInfoLength;

  const streams: PMTStream[] = [];
  const endPos = offset + 3 + sectionLength - 4; // Exclude CRC

  while (pos + 5 <= endPos) {
    const streamType = buffer[pos];
    const elementaryPid = ((buffer[pos + 1] & 0x1F) << 8) | buffer[pos + 2];
    const esInfoLength = ((buffer[pos + 3] & 0x0F) << 8) | buffer[pos + 4];
    pos += 5;

    const descriptors = buffer.slice(pos, pos + esInfoLength);
    pos += esInfoLength;

    streams.push({
      streamType,
      pid: elementaryPid,
      descriptors,
    });
  }

  return {
    programNumber,
    versionNumber,
    currentNextIndicator,
    pcrPid,
    programInfo,
    streams,
  };
}

/**
 * Calculate CRC32 for MPEG-TS PSI tables
 */
export function calculateCRC32(buffer: Buffer): number {
  let crc = 0xFFFFFFFF;

  for (let i = 0; i < buffer.length; i++) {
    crc ^= buffer[i] << 24;
    for (let j = 0; j < 8; j++) {
      if (crc & 0x80000000) {
        crc = (crc << 1) ^ 0x04C11DB7;
      } else {
        crc = crc << 1;
      }
    }
  }

  return crc >>> 0;
}
