import { TSPacket, AdaptationField } from './types';
import { TS_PACKET_SIZE, SYNC_BYTE } from './constants';

/**
 * Parse a single TS packet from a buffer
 */
export function parsePacket(buffer: Buffer, offset: number = 0): TSPacket | null {
  if (buffer.length - offset < TS_PACKET_SIZE) {
    return null;
  }

  // Check sync byte
  const syncByte = buffer[offset];
  if (syncByte !== SYNC_BYTE) {
    return null;
  }

  // Parse header (first 4 bytes)
  const byte1 = buffer[offset + 1];
  const byte2 = buffer[offset + 2];
  const byte3 = buffer[offset + 3];

  const transportErrorIndicator = (byte1 & 0x80) !== 0;
  const payloadUnitStartIndicator = (byte1 & 0x40) !== 0;
  const transportPriority = (byte1 & 0x20) !== 0;
  const pid = ((byte1 & 0x1F) << 8) | byte2;

  const scramblingControl = (byte3 >> 6) & 0x03;
  const adaptationFieldControl = (byte3 >> 4) & 0x03;
  const continuityCounter = byte3 & 0x0F;

  const packet: TSPacket = {
    syncByte,
    transportErrorIndicator,
    payloadUnitStartIndicator,
    transportPriority,
    pid,
    scramblingControl,
    adaptationFieldControl,
    continuityCounter,
  };

  let pos = offset + 4;

  // Parse adaptation field if present
  if (adaptationFieldControl === 0x02 || adaptationFieldControl === 0x03) {
    const adaptationField = parseAdaptationField(buffer, pos);
    if (adaptationField) {
      packet.adaptationField = adaptationField;
      pos += adaptationField.length + 1;
    }
  }

  // Extract payload if present
  if (adaptationFieldControl === 0x01 || adaptationFieldControl === 0x03) {
    const payloadStart = pos;
    const payloadEnd = offset + TS_PACKET_SIZE;
    if (payloadStart < payloadEnd) {
      packet.payload = buffer.slice(payloadStart, payloadEnd);
    }
  }

  return packet;
}

/**
 * Parse adaptation field
 */
function parseAdaptationField(buffer: Buffer, offset: number): AdaptationField | null {
  const length = buffer[offset];
  if (length === 0) {
    return { length: 0, discontinuityIndicator: false, randomAccessIndicator: false,
             elementaryStreamPriorityIndicator: false, pcrFlag: false, opcrFlag: false,
             splicingPointFlag: false, transportPrivateDataFlag: false,
             adaptationFieldExtensionFlag: false };
  }

  const flags = buffer[offset + 1];
  const discontinuityIndicator = (flags & 0x80) !== 0;
  const randomAccessIndicator = (flags & 0x40) !== 0;
  const elementaryStreamPriorityIndicator = (flags & 0x20) !== 0;
  const pcrFlag = (flags & 0x10) !== 0;
  const opcrFlag = (flags & 0x08) !== 0;
  const splicingPointFlag = (flags & 0x04) !== 0;
  const transportPrivateDataFlag = (flags & 0x02) !== 0;
  const adaptationFieldExtensionFlag = (flags & 0x01) !== 0;

  const field: AdaptationField = {
    length,
    discontinuityIndicator,
    randomAccessIndicator,
    elementaryStreamPriorityIndicator,
    pcrFlag,
    opcrFlag,
    splicingPointFlag,
    transportPrivateDataFlag,
    adaptationFieldExtensionFlag,
  };

  let pos = offset + 2;

  // Parse PCR if present
  if (pcrFlag && pos + 6 <= offset + length + 1) {
    field.pcr = parsePCR(buffer, pos);
    pos += 6;
  }

  // Parse OPCR if present
  if (opcrFlag && pos + 6 <= offset + length + 1) {
    field.opcr = parsePCR(buffer, pos);
    pos += 6;
  }

  return field;
}

/**
 * Parse Program Clock Reference (PCR)
 */
function parsePCR(buffer: Buffer, offset: number): bigint {
  const byte0 = BigInt(buffer[offset]);
  const byte1 = BigInt(buffer[offset + 1]);
  const byte2 = BigInt(buffer[offset + 2]);
  const byte3 = BigInt(buffer[offset + 3]);
  const byte4 = BigInt(buffer[offset + 4]);
  const byte5 = BigInt(buffer[offset + 5]);

  const base = (byte0 << 25n) | (byte1 << 17n) | (byte2 << 9n) | (byte3 << 1n) | (byte4 >> 7n);
  const extension = ((byte4 & BigInt(0x01)) << 8n) | byte5;

  return base * 300n + extension;
}

/**
 * Find the next sync byte in a buffer
 */
export function findSyncByte(buffer: Buffer, offset: number = 0): number {
  for (let i = offset; i < buffer.length; i++) {
    if (buffer[i] === SYNC_BYTE) {
      // Verify this is a real sync byte by checking if the next packet also has sync byte
      if (i + TS_PACKET_SIZE < buffer.length && buffer[i + TS_PACKET_SIZE] === SYNC_BYTE) {
        return i;
      }
    }
  }
  return -1;
}
