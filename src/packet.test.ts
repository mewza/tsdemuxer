import { parsePacket, findSyncByte } from './packet';
import { SYNC_BYTE, TS_PACKET_SIZE } from './constants';

describe('Packet Parser', () => {
  describe('parsePacket', () => {
    it('should parse a basic TS packet', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer[0] = SYNC_BYTE; // Sync byte
      buffer[1] = 0x40; // payload_unit_start_indicator = 1, PID high bits = 0
      buffer[2] = 0x00; // PID low bits = 0
      buffer[3] = 0x10; // adaptation_field_control = 01 (payload only), continuity_counter = 0

      const packet = parsePacket(buffer, 0);

      expect(packet).not.toBeNull();
      expect(packet?.syncByte).toBe(SYNC_BYTE);
      expect(packet?.pid).toBe(0);
      expect(packet?.payloadUnitStartIndicator).toBe(true);
      expect(packet?.adaptationFieldControl).toBe(1);
      expect(packet?.continuityCounter).toBe(0);
    });

    it('should return null for invalid sync byte', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer[0] = 0x00; // Invalid sync byte

      const packet = parsePacket(buffer, 0);

      expect(packet).toBeNull();
    });

    it('should parse PID correctly', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer[0] = SYNC_BYTE;
      buffer[1] = 0x41; // PID high bits = 0x01
      buffer[2] = 0xFF; // PID low bits = 0xFF
      buffer[3] = 0x10; // adaptation_field_control = 01, continuity_counter = 0

      const packet = parsePacket(buffer, 0);

      expect(packet).not.toBeNull();
      expect(packet?.pid).toBe(0x01FF);
    });

    it('should parse packet with adaptation field', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer[0] = SYNC_BYTE;
      buffer[1] = 0x40;
      buffer[2] = 0x00;
      buffer[3] = 0x30; // adaptation_field_control = 11 (both adaptation and payload)
      buffer[4] = 0x07; // adaptation field length
      buffer[5] = 0x10; // PCR flag set

      const packet = parsePacket(buffer, 0);

      expect(packet).not.toBeNull();
      expect(packet?.adaptationFieldControl).toBe(3);
      expect(packet?.adaptationField).toBeDefined();
      expect(packet?.adaptationField?.length).toBe(7);
    });

    it('should extract payload', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer[0] = SYNC_BYTE;
      buffer[1] = 0x40;
      buffer[2] = 0x00;
      buffer[3] = 0x10; // payload only
      buffer.fill(0xAA, 4); // Fill payload with 0xAA

      const packet = parsePacket(buffer, 0);

      expect(packet).not.toBeNull();
      expect(packet?.payload).toBeDefined();
      expect(packet?.payload?.length).toBe(TS_PACKET_SIZE - 4);
      expect(packet?.payload?.[0]).toBe(0xAA);
    });
  });

  describe('findSyncByte', () => {
    it('should find sync byte at the start', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 2);
      buffer[0] = SYNC_BYTE;
      buffer[TS_PACKET_SIZE] = SYNC_BYTE;

      const pos = findSyncByte(buffer, 0);

      expect(pos).toBe(0);
    });

    it('should find sync byte in the middle', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 3);
      buffer[10] = SYNC_BYTE;
      buffer[10 + TS_PACKET_SIZE] = SYNC_BYTE;

      const pos = findSyncByte(buffer, 0);

      expect(pos).toBe(10);
    });

    it('should return -1 if no valid sync byte found', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE);
      buffer.fill(0x00);

      const pos = findSyncByte(buffer, 0);

      expect(pos).toBe(-1);
    });

    it('should verify sync byte pattern', () => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 2);
      buffer[5] = SYNC_BYTE; // False sync
      buffer[100] = SYNC_BYTE;
      buffer[100 + TS_PACKET_SIZE] = SYNC_BYTE; // Valid sync

      const pos = findSyncByte(buffer, 0);

      expect(pos).toBe(100);
    });
  });
});
