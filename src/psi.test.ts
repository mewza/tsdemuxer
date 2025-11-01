import { parsePAT, parsePMT } from './psi';
import { TABLE_ID_PAT, TABLE_ID_PMT } from './constants';

describe('PSI Parser', () => {
  describe('parsePAT', () => {
    it('should parse a valid PAT', () => {
      // Create a minimal PAT
      const buffer = Buffer.from([
        0x00, // pointer field
        TABLE_ID_PAT, // table ID
        0xB0, 0x0D, // section syntax indicator = 1, section length = 13
        0x00, 0x01, // transport stream ID = 1
        0xC1, // version = 0, current/next = 1
        0x00, // section number = 0
        0x00, // last section number = 0
        // Program 1
        0x00, 0x01, // program number = 1
        0xE0, 0x10, // PMT PID = 0x10
        // CRC32 (4 bytes)
        0x00, 0x00, 0x00, 0x00,
      ]);

      const pat = parsePAT(buffer);

      expect(pat).not.toBeNull();
      expect(pat?.transportStreamId).toBe(1);
      expect(pat?.versionNumber).toBe(0);
      expect(pat?.currentNextIndicator).toBe(true);
      expect(pat?.programs).toHaveLength(1);
      expect(pat?.programs[0].programNumber).toBe(1);
      expect(pat?.programs[0].pid).toBe(0x10);
    });

    it('should return null for invalid table ID', () => {
      const buffer = Buffer.from([
        0x00, // pointer field
        0xFF, // invalid table ID
        0xB0, 0x0D,
        0x00, 0x01,
        0xC1,
        0x00,
        0x00,
      ]);

      const pat = parsePAT(buffer);

      expect(pat).toBeNull();
    });

    it('should parse PAT with multiple programs', () => {
      const buffer = Buffer.from([
        0x00, // pointer field
        TABLE_ID_PAT,
        0xB0, 0x11, // section length = 17
        0x00, 0x01,
        0xC1,
        0x00,
        0x00,
        // Program 1
        0x00, 0x01,
        0xE0, 0x10,
        // Program 2
        0x00, 0x02,
        0xE0, 0x20,
        // CRC32
        0x00, 0x00, 0x00, 0x00,
      ]);

      const pat = parsePAT(buffer);

      expect(pat).not.toBeNull();
      expect(pat?.programs).toHaveLength(2);
      expect(pat?.programs[0].programNumber).toBe(1);
      expect(pat?.programs[0].pid).toBe(0x10);
      expect(pat?.programs[1].programNumber).toBe(2);
      expect(pat?.programs[1].pid).toBe(0x20);
    });
  });

  describe('parsePMT', () => {
    it('should parse a valid PMT', () => {
      const buffer = Buffer.from([
        0x00, // pointer field
        TABLE_ID_PMT,
        0xB0, 0x12, // section syntax indicator = 1, section length = 18
        0x00, 0x01, // program number = 1
        0xC1, // version = 0, current/next = 1
        0x00, // section number = 0
        0x00, // last section number = 0
        0xE1, 0x00, // PCR PID = 0x100
        0xF0, 0x00, // program info length = 0
        // Stream 1
        0x1B, // stream type = H.264
        0xE1, 0x00, // elementary PID = 0x100
        0xF0, 0x00, // ES info length = 0
        // CRC32
        0x00, 0x00, 0x00, 0x00,
      ]);

      const pmt = parsePMT(buffer);

      expect(pmt).not.toBeNull();
      expect(pmt?.programNumber).toBe(1);
      expect(pmt?.versionNumber).toBe(0);
      expect(pmt?.currentNextIndicator).toBe(true);
      expect(pmt?.pcrPid).toBe(0x100);
      expect(pmt?.streams).toHaveLength(1);
      expect(pmt?.streams[0].streamType).toBe(0x1B);
      expect(pmt?.streams[0].pid).toBe(0x100);
    });

    it('should return null for invalid table ID', () => {
      const buffer = Buffer.from([
        0x00,
        0xFF, // invalid table ID
        0xB0, 0x12,
        0x00, 0x01,
        0xC1,
        0x00,
        0x00,
      ]);

      const pmt = parsePMT(buffer);

      expect(pmt).toBeNull();
    });

    it('should parse PMT with multiple streams', () => {
      const buffer = Buffer.from([
        0x00,
        TABLE_ID_PMT,
        0xB0, 0x17, // section length = 23
        0x00, 0x01,
        0xC1,
        0x00,
        0x00,
        0xE1, 0x00, // PCR PID = 0x100
        0xF0, 0x00, // program info length = 0
        // Stream 1 - Video
        0x1B, // H.264
        0xE1, 0x00,
        0xF0, 0x00,
        // Stream 2 - Audio
        0x0F, // AAC
        0xE1, 0x01,
        0xF0, 0x00,
        // CRC32
        0x00, 0x00, 0x00, 0x00,
      ]);

      const pmt = parsePMT(buffer);

      expect(pmt).not.toBeNull();
      expect(pmt?.streams).toHaveLength(2);
      expect(pmt?.streams[0].streamType).toBe(0x1B);
      expect(pmt?.streams[0].pid).toBe(0x100);
      expect(pmt?.streams[1].streamType).toBe(0x0F);
      expect(pmt?.streams[1].pid).toBe(0x101);
    });
  });
});
