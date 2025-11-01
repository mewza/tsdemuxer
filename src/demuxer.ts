import { EventEmitter } from 'events';
import { parsePacket, findSyncByte } from './packet';
import { parsePAT, parsePMT } from './psi';
import { parsePES } from './pes';
import { TSPacket, PAT, PMT, PESPacket, ProgramInfo, StreamInfo } from './types';
import { 
  TS_PACKET_SIZE, 
  PID_PAT, 
  STREAM_TYPE_VIDEO_MPEG1,
  STREAM_TYPE_VIDEO_MPEG2,
  STREAM_TYPE_AUDIO_MPEG1,
  STREAM_TYPE_AUDIO_MPEG2,
  STREAM_TYPE_AUDIO_AAC,
  STREAM_TYPE_VIDEO_MPEG4,
  STREAM_TYPE_VIDEO_H264,
  STREAM_TYPE_VIDEO_H265,
  STREAM_TYPE_AUDIO_AC3,
} from './constants';

/**
 * Main MPEG-TS Demuxer class
 */
export class TSDemuxer extends EventEmitter {
  private pat: PAT | null = null;
  private pmts: Map<number, PMT> = new Map();
  private pesBuffers: Map<number, Buffer> = new Map();
  private buffer: Buffer = Buffer.alloc(0);
  private synced: boolean = false;

  constructor() {
    super();
  }

  /**
   * Push data into the demuxer
   */
  public push(data: Buffer): void {
    this.buffer = Buffer.concat([this.buffer, data]);
    this.processBuffer();
  }

  /**
   * Process buffered data
   */
  private processBuffer(): void {
    while (this.buffer.length >= TS_PACKET_SIZE) {
      // Find sync if not already synced
      if (!this.synced) {
        const syncPos = findSyncByte(this.buffer);
        if (syncPos === -1) {
          // No sync found, clear buffer except last few bytes
          this.buffer = this.buffer.slice(-TS_PACKET_SIZE);
          return;
        }
        if (syncPos > 0) {
          this.buffer = this.buffer.slice(syncPos);
        }
        this.synced = true;
      }

      // Try to parse packet
      const packet = parsePacket(this.buffer, 0);
      if (!packet) {
        // Lost sync, try to resync
        this.synced = false;
        this.buffer = this.buffer.slice(1);
        continue;
      }

      // Process packet
      this.processPacket(packet);

      // Remove processed packet
      this.buffer = this.buffer.slice(TS_PACKET_SIZE);
    }
  }

  /**
   * Process a single TS packet
   */
  private processPacket(packet: TSPacket): void {
    // Emit raw packet
    this.emit('packet', packet);

    // Handle PAT
    if (packet.pid === PID_PAT && packet.payload) {
      this.handlePAT(packet);
      return;
    }

    // Handle PMT
    if (this.pat) {
      for (const program of this.pat.programs) {
        if (program.programNumber !== 0 && packet.pid === program.pid && packet.payload) {
          this.handlePMT(packet, program.programNumber);
          return;
        }
      }
    }

    // Handle PES
    if (packet.payload) {
      this.handlePES(packet);
    }
  }

  /**
   * Handle PAT packet
   */
  private handlePAT(packet: TSPacket): void {
    if (!packet.payload) return;

    const pat = parsePAT(packet.payload);
    if (pat) {
      this.pat = pat;
      this.emit('pat', pat);
    }
  }

  /**
   * Handle PMT packet
   */
  private handlePMT(packet: TSPacket, programNumber: number): void {
    if (!packet.payload) return;

    const pmt = parsePMT(packet.payload);
    if (pmt) {
      this.pmts.set(programNumber, pmt);
      this.emit('pmt', pmt);
    }
  }

  /**
   * Handle PES packet
   */
  private handlePES(packet: TSPacket): void {
    if (!packet.payload) return;

    const pid = packet.pid;

    // Check if this PID is a known elementary stream
    let isElementaryStream = false;
    for (const pmt of this.pmts.values()) {
      if (pmt.streams.some(s => s.pid === pid)) {
        isElementaryStream = true;
        break;
      }
    }

    if (!isElementaryStream) {
      return;
    }

    // Start of new PES packet
    if (packet.payloadUnitStartIndicator) {
      // Process previous PES if exists
      const prevBuffer = this.pesBuffers.get(pid);
      if (prevBuffer && prevBuffer.length > 0) {
        const pes = parsePES(prevBuffer);
        if (pes) {
          this.emit('pes', { pid, pes });
        }
      }

      // Start new PES buffer
      this.pesBuffers.set(pid, Buffer.from(packet.payload));
    } else {
      // Continue existing PES packet
      const existingBuffer = this.pesBuffers.get(pid) || Buffer.alloc(0);
      this.pesBuffers.set(pid, Buffer.concat([existingBuffer, packet.payload]));
    }
  }

  /**
   * Get program information
   */
  public getPrograms(): ProgramInfo[] {
    if (!this.pat) {
      return [];
    }

    const programs: ProgramInfo[] = [];

    for (const patProgram of this.pat.programs) {
      if (patProgram.programNumber === 0) continue; // Skip network PID

      const pmt = this.pmts.get(patProgram.programNumber);
      const streams: StreamInfo[] = [];

      if (pmt) {
        for (const stream of pmt.streams) {
          streams.push({
            pid: stream.pid,
            streamType: stream.streamType,
            streamTypeName: getStreamTypeName(stream.streamType),
          });
        }
      }

      programs.push({
        programNumber: patProgram.programNumber,
        pmtPid: patProgram.pid,
        pcrPid: pmt?.pcrPid,
        streams,
      });
    }

    return programs;
  }

  /**
   * Reset demuxer state
   */
  public reset(): void {
    this.pat = null;
    this.pmts.clear();
    this.pesBuffers.clear();
    this.buffer = Buffer.alloc(0);
    this.synced = false;
  }
}

/**
 * Get human-readable stream type name
 */
function getStreamTypeName(streamType: number): string {
  switch (streamType) {
    case STREAM_TYPE_VIDEO_MPEG1: return 'MPEG-1 Video';
    case STREAM_TYPE_VIDEO_MPEG2: return 'MPEG-2 Video';
    case STREAM_TYPE_AUDIO_MPEG1: return 'MPEG-1 Audio';
    case STREAM_TYPE_AUDIO_MPEG2: return 'MPEG-2 Audio';
    case STREAM_TYPE_AUDIO_AAC: return 'AAC Audio';
    case STREAM_TYPE_VIDEO_MPEG4: return 'MPEG-4 Video';
    case STREAM_TYPE_VIDEO_H264: return 'H.264 Video';
    case STREAM_TYPE_VIDEO_H265: return 'H.265 Video';
    case STREAM_TYPE_AUDIO_AC3: return 'AC-3 Audio';
    default: return `Unknown (0x${streamType.toString(16).toUpperCase()})`;
  }
}
