import { TSDemuxer } from './demuxer';
import { SYNC_BYTE, TS_PACKET_SIZE, PID_PAT, TABLE_ID_PAT, TABLE_ID_PMT } from './constants';

describe('TSDemuxer', () => {
  let demuxer: TSDemuxer;

  beforeEach(() => {
    demuxer = new TSDemuxer();
  });

  afterEach(() => {
    demuxer.removeAllListeners();
  });

  describe('Basic functionality', () => {
    it('should create a demuxer instance', () => {
      expect(demuxer).toBeInstanceOf(TSDemuxer);
    });

    it('should emit packet events', (done) => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 2);
      
      // First packet
      buffer[0] = SYNC_BYTE;
      buffer[1] = 0x40;
      buffer[2] = 0x00;
      buffer[3] = 0x10;
      
      // Second packet (needed for sync verification)
      buffer[TS_PACKET_SIZE] = SYNC_BYTE;
      buffer[TS_PACKET_SIZE + 1] = 0x40;
      buffer[TS_PACKET_SIZE + 2] = 0x01;
      buffer[TS_PACKET_SIZE + 3] = 0x10;

      let count = 0;
      demuxer.on('packet', (pkt) => {
        count++;
        expect(pkt).toBeDefined();
        expect(pkt.syncByte).toBe(SYNC_BYTE);
        if (count === 1) {
          done();
        }
      });

      demuxer.push(buffer);
    });

    it('should handle multiple packets', () => {
      const packets: any[] = [];
      demuxer.on('packet', (pkt) => {
        packets.push(pkt);
      });

      const buffer = Buffer.alloc(TS_PACKET_SIZE * 3);
      for (let i = 0; i < 3; i++) {
        const offset = i * TS_PACKET_SIZE;
        buffer[offset] = SYNC_BYTE;
        buffer[offset + 1] = 0x40;
        buffer[offset + 2] = i; // Different PIDs
        buffer[offset + 3] = 0x10;
      }

      demuxer.push(buffer);

      expect(packets.length).toBe(3);
      expect(packets[0].pid).toBe(0);
      expect(packets[1].pid).toBe(1);
      expect(packets[2].pid).toBe(2);
    });

    it('should resync on lost sync', () => {
      const packets: any[] = [];
      demuxer.on('packet', (pkt) => {
        packets.push(pkt);
      });

      // Create buffer with garbage followed by valid packet
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 3);
      buffer.fill(0xFF, 0, TS_PACKET_SIZE); // Garbage

      // Valid packet 1
      buffer[TS_PACKET_SIZE] = SYNC_BYTE;
      buffer[TS_PACKET_SIZE + 1] = 0x40;
      buffer[TS_PACKET_SIZE + 2] = 0x00;
      buffer[TS_PACKET_SIZE + 3] = 0x10;

      // Valid packet 2
      buffer[TS_PACKET_SIZE * 2] = SYNC_BYTE;
      buffer[TS_PACKET_SIZE * 2 + 1] = 0x40;
      buffer[TS_PACKET_SIZE * 2 + 2] = 0x01;
      buffer[TS_PACKET_SIZE * 2 + 3] = 0x10;

      demuxer.push(buffer);

      expect(packets.length).toBe(2);
    });
  });

  describe('PAT handling', () => {
    it('should emit PAT events', (done) => {
      const buffer = Buffer.alloc(TS_PACKET_SIZE * 2);
      
      // First packet - PAT
      buffer[0] = SYNC_BYTE;
      buffer[1] = 0x40; // payload_unit_start_indicator
      buffer[2] = PID_PAT; // PAT PID
      buffer[3] = 0x10;

      // PAT payload
      buffer[4] = 0x00; // pointer field
      buffer[5] = TABLE_ID_PAT;
      buffer[6] = 0xB0;
      buffer[7] = 0x0D; // section length
      buffer[8] = 0x00;
      buffer[9] = 0x01; // transport stream ID
      buffer[10] = 0xC1;
      buffer[11] = 0x00;
      buffer[12] = 0x00;
      buffer[13] = 0x00;
      buffer[14] = 0x01; // program number
      buffer[15] = 0xE0;
      buffer[16] = 0x10; // PMT PID
      
      // Second packet (for sync verification)
      buffer[TS_PACKET_SIZE] = SYNC_BYTE;
      buffer[TS_PACKET_SIZE + 1] = 0x40;
      buffer[TS_PACKET_SIZE + 2] = 0x01;
      buffer[TS_PACKET_SIZE + 3] = 0x10;

      demuxer.on('pat', (pat) => {
        expect(pat).toBeDefined();
        expect(pat.transportStreamId).toBe(1);
        expect(pat.programs).toHaveLength(1);
        expect(pat.programs[0].programNumber).toBe(1);
        expect(pat.programs[0].pid).toBe(0x10);
        done();
      });

      demuxer.push(buffer);
    });
  });

  describe('PMT handling', () => {
    it('should emit PMT events after PAT', (done) => {
      let patReceived = false;

      demuxer.on('pat', () => {
        patReceived = true;
      });

      demuxer.on('pmt', (pmt) => {
        expect(patReceived).toBe(true);
        expect(pmt).toBeDefined();
        expect(pmt.programNumber).toBe(1);
        done();
      });

      // First send PAT
      const patPacket = Buffer.alloc(TS_PACKET_SIZE);
      patPacket[0] = SYNC_BYTE;
      patPacket[1] = 0x40;
      patPacket[2] = PID_PAT;
      patPacket[3] = 0x10;
      patPacket[4] = 0x00;
      patPacket[5] = TABLE_ID_PAT;
      patPacket[6] = 0xB0;
      patPacket[7] = 0x0D;
      patPacket[8] = 0x00;
      patPacket[9] = 0x01;
      patPacket[10] = 0xC1;
      patPacket[11] = 0x00;
      patPacket[12] = 0x00;
      patPacket[13] = 0x00;
      patPacket[14] = 0x01;
      patPacket[15] = 0xE0;
      patPacket[16] = 0x10; // PMT PID = 0x10

      // Then send PMT
      const pmtPacket = Buffer.alloc(TS_PACKET_SIZE);
      pmtPacket[0] = SYNC_BYTE;
      pmtPacket[1] = 0x40; // payload_unit_start_indicator, PID high bits (0)
      pmtPacket[2] = 0x10; // PID low bits = 0x10
      pmtPacket[3] = 0x10; // adaptation_field_control = 01, continuity_counter = 0
      pmtPacket[4] = 0x00;
      pmtPacket[5] = TABLE_ID_PMT;
      pmtPacket[6] = 0xB0;
      pmtPacket[7] = 0x12;
      pmtPacket[8] = 0x00;
      pmtPacket[9] = 0x01;
      pmtPacket[10] = 0xC1;
      pmtPacket[11] = 0x00;
      pmtPacket[12] = 0x00;
      pmtPacket[13] = 0xE1;
      pmtPacket[14] = 0x00;
      pmtPacket[15] = 0xF0;
      pmtPacket[16] = 0x00;
      pmtPacket[17] = 0x1B; // H.264
      pmtPacket[18] = 0xE1;
      pmtPacket[19] = 0x00;
      
      // Add dummy packet for sync verification
      const dummyPacket = Buffer.alloc(TS_PACKET_SIZE);
      dummyPacket[0] = SYNC_BYTE;
      dummyPacket[1] = 0x40;
      dummyPacket[2] = 0xFF;
      dummyPacket[3] = 0x10;

      demuxer.push(Buffer.concat([patPacket, pmtPacket, dummyPacket]));
    });
  });

  describe('getPrograms', () => {
    it('should return empty array when no PAT received', () => {
      const programs = demuxer.getPrograms();
      expect(programs).toEqual([]);
    });

    it('should return program info after PAT/PMT', (done) => {
      demuxer.on('pmt', () => {
        const programs = demuxer.getPrograms();
        expect(programs).toHaveLength(1);
        expect(programs[0].programNumber).toBe(1);
        expect(programs[0].pmtPid).toBe(0x10);
        expect(programs[0].streams).toHaveLength(1);
        expect(programs[0].streams[0].streamType).toBe(0x1B);
        expect(programs[0].streams[0].streamTypeName).toBe('H.264 Video');
        done();
      });

      // Send PAT
      const patPacket = Buffer.alloc(TS_PACKET_SIZE);
      patPacket[0] = SYNC_BYTE;
      patPacket[1] = 0x40;
      patPacket[2] = PID_PAT;
      patPacket[3] = 0x10;
      patPacket[4] = 0x00;
      patPacket[5] = TABLE_ID_PAT;
      patPacket[6] = 0xB0;
      patPacket[7] = 0x0D;
      patPacket[8] = 0x00;
      patPacket[9] = 0x01;
      patPacket[10] = 0xC1;
      patPacket[11] = 0x00;
      patPacket[12] = 0x00;
      patPacket[13] = 0x00;
      patPacket[14] = 0x01;
      patPacket[15] = 0xE0;
      patPacket[16] = 0x10;

      // Send PMT
      const pmtPacket = Buffer.alloc(TS_PACKET_SIZE);
      pmtPacket[0] = SYNC_BYTE;
      pmtPacket[1] = 0x40; // payload_unit_start_indicator, PID high bits (0)
      pmtPacket[2] = 0x10; // PID low bits = 0x10
      pmtPacket[3] = 0x10; // adaptation_field_control = 01, continuity_counter = 0
      pmtPacket[4] = 0x00;
      pmtPacket[5] = TABLE_ID_PMT;
      pmtPacket[6] = 0xB0;
      pmtPacket[7] = 0x12;
      pmtPacket[8] = 0x00;
      pmtPacket[9] = 0x01;
      pmtPacket[10] = 0xC1;
      pmtPacket[11] = 0x00;
      pmtPacket[12] = 0x00;
      pmtPacket[13] = 0xE1;
      pmtPacket[14] = 0x00;
      pmtPacket[15] = 0xF0;
      pmtPacket[16] = 0x00;
      pmtPacket[17] = 0x1B;
      pmtPacket[18] = 0xE1;
      pmtPacket[19] = 0x00;
      
      // Add dummy packet for sync verification
      const dummyPacket = Buffer.alloc(TS_PACKET_SIZE);
      dummyPacket[0] = SYNC_BYTE;
      dummyPacket[1] = 0x40;
      dummyPacket[2] = 0xFF;
      dummyPacket[3] = 0x10;

      demuxer.push(Buffer.concat([patPacket, pmtPacket, dummyPacket]));
    });
  });

  describe('reset', () => {
    it('should clear all state', () => {
      demuxer.reset();
      const programs = demuxer.getPrograms();
      expect(programs).toEqual([]);
    });
  });
});
