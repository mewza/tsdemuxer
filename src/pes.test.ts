import { parsePES, isPESStart } from './pes';

describe('PES Parser', () => {
  describe('parsePES', () => {
    it('should parse a basic PES packet', () => {
      const buffer = Buffer.from([
        0x00, 0x00, 0x01, // start code
        0xE0, // stream ID (video)
        0x00, 0x0A, // packet length = 10
        0x80, // marker bits
        0x00, // no PTS/DTS
        0x00, // PES header data length = 0
        // Payload data
        0x01, 0x02, 0x03,
      ]);

      const pes = parsePES(buffer);

      expect(pes).not.toBeNull();
      expect(pes?.streamId).toBe(0xE0);
      expect(pes?.packetLength).toBe(10);
      expect(pes?.data.length).toBe(3);
      expect(pes?.data[0]).toBe(0x01);
    });

    it('should return null for invalid start code', () => {
      const buffer = Buffer.from([
        0x00, 0x00, 0x02, // invalid start code
        0xE0,
        0x00, 0x00,
      ]);

      const pes = parsePES(buffer);

      expect(pes).toBeNull();
    });

    it('should parse PES packet with PTS', () => {
      const buffer = Buffer.from([
        0x00, 0x00, 0x01,
        0xE0,
        0x00, 0x0D, // packet length = 13
        0x80,
        0x80, // PTS flag set
        0x05, // PES header data length = 5
        // PTS (5 bytes) - simplified
        0x21, 0x00, 0x01, 0x00, 0x01,
        // Payload
        0x01, 0x02,
      ]);

      const pes = parsePES(buffer);

      expect(pes).not.toBeNull();
      expect(pes?.streamId).toBe(0xE0);
      expect(pes?.pts).toBeDefined();
      expect(pes?.data.length).toBe(2);
    });

    it('should parse PES packet with both PTS and DTS', () => {
      const buffer = Buffer.from([
        0x00, 0x00, 0x01,
        0xE0,
        0x00, 0x12, // packet length = 18
        0x80,
        0xC0, // PTS and DTS flags set
        0x0A, // PES header data length = 10
        // PTS (5 bytes)
        0x31, 0x00, 0x01, 0x00, 0x01,
        // DTS (5 bytes)
        0x11, 0x00, 0x01, 0x00, 0x01,
        // Payload
        0x01, 0x02,
      ]);

      const pes = parsePES(buffer);

      expect(pes).not.toBeNull();
      expect(pes?.streamId).toBe(0xE0);
      expect(pes?.pts).toBeDefined();
      expect(pes?.dts).toBeDefined();
      expect(pes?.data.length).toBe(2);
    });

    it('should handle PES packets without optional headers', () => {
      // For stream IDs like padding_stream (0xBE)
      const buffer = Buffer.from([
        0x00, 0x00, 0x01,
        0xBE, // padding stream
        0x00, 0x05, // packet length = 5
        // Payload (no PES header for this stream type)
        0x01, 0x02, 0x03, 0x04, 0x05,
      ]);

      const pes = parsePES(buffer);

      expect(pes).not.toBeNull();
      expect(pes?.streamId).toBe(0xBE);
      expect(pes?.data.length).toBe(5);
    });
  });

  describe('isPESStart', () => {
    it('should return true for valid PES start code', () => {
      const buffer = Buffer.from([0x00, 0x00, 0x01, 0xE0]);
      expect(isPESStart(buffer)).toBe(true);
    });

    it('should return false for invalid start code', () => {
      const buffer = Buffer.from([0x00, 0x00, 0x02, 0xE0]);
      expect(isPESStart(buffer)).toBe(false);
    });

    it('should return false for buffer too short', () => {
      const buffer = Buffer.from([0x00, 0x00]);
      expect(isPESStart(buffer)).toBe(false);
    });
  });
});
