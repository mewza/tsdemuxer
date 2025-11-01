/**
 * MPEG-TS Constants
 */

// Standard TS packet size
export const TS_PACKET_SIZE = 188;

// Sync byte that starts every TS packet
export const SYNC_BYTE = 0x47;

// Reserved PIDs
export const PID_PAT = 0x0000; // Program Association Table
export const PID_CAT = 0x0001; // Conditional Access Table
export const PID_TSDT = 0x0002; // Transport Stream Description Table
export const PID_NULL = 0x1FFF; // Null packets

// Table IDs
export const TABLE_ID_PAT = 0x00;
export const TABLE_ID_PMT = 0x02;

// Stream types (from ISO 13818-1)
export const STREAM_TYPE_VIDEO_MPEG1 = 0x01;
export const STREAM_TYPE_VIDEO_MPEG2 = 0x02;
export const STREAM_TYPE_AUDIO_MPEG1 = 0x03;
export const STREAM_TYPE_AUDIO_MPEG2 = 0x04;
export const STREAM_TYPE_PRIVATE_SECTION = 0x05;
export const STREAM_TYPE_PRIVATE_DATA = 0x06;
export const STREAM_TYPE_AUDIO_AAC = 0x0F;
export const STREAM_TYPE_VIDEO_MPEG4 = 0x10;
export const STREAM_TYPE_VIDEO_H264 = 0x1B;
export const STREAM_TYPE_VIDEO_H265 = 0x24;
export const STREAM_TYPE_AUDIO_AC3 = 0x81;
