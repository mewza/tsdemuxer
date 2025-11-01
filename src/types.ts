/**
 * TypeScript interfaces for MPEG-TS demuxer
 */

/**
 * TS Packet structure
 */
export interface TSPacket {
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

/**
 * Adaptation Field structure
 */
export interface AdaptationField {
  length: number;
  discontinuityIndicator: boolean;
  randomAccessIndicator: boolean;
  elementaryStreamPriorityIndicator: boolean;
  pcrFlag: boolean;
  opcrFlag: boolean;
  splicingPointFlag: boolean;
  transportPrivateDataFlag: boolean;
  adaptationFieldExtensionFlag: boolean;
  pcr?: bigint;
  opcr?: bigint;
}

/**
 * Program Association Table (PAT) entry
 */
export interface PATProgram {
  programNumber: number;
  pid: number; // PMT PID for this program
}

/**
 * Program Association Table (PAT)
 */
export interface PAT {
  transportStreamId: number;
  versionNumber: number;
  currentNextIndicator: boolean;
  programs: PATProgram[];
}

/**
 * Program Map Table (PMT) stream info
 */
export interface PMTStream {
  streamType: number;
  pid: number;
  descriptors: Buffer;
}

/**
 * Program Map Table (PMT)
 */
export interface PMT {
  programNumber: number;
  versionNumber: number;
  currentNextIndicator: boolean;
  pcrPid: number;
  programInfo: Buffer;
  streams: PMTStream[];
}

/**
 * Packetized Elementary Stream (PES) packet
 */
export interface PESPacket {
  streamId: number;
  packetLength: number;
  scramblingControl?: number;
  priority?: boolean;
  dataAlignmentIndicator?: boolean;
  copyright?: boolean;
  originalOrCopy?: boolean;
  ptsDtsFlags?: number;
  pts?: bigint;
  dts?: bigint;
  data: Buffer;
}

/**
 * Stream information
 */
export interface StreamInfo {
  pid: number;
  streamType: number;
  streamTypeName: string;
}

/**
 * Program information
 */
export interface ProgramInfo {
  programNumber: number;
  pmtPid: number;
  pcrPid?: number;
  streams: StreamInfo[];
}
