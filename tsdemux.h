/*
 **  tsdemux.h - vlc style .ts format demuxer in a single .h C++ class
 **  © 2009-2024 Dmitry Boldyrev All rights reserved.
 */
#pragma once

//#define TS_DEBUG

#ifdef TS_DEBUG
#define TS_LOG(...) fprintf(stderr, "[TS] " __VA_ARGS__)
#else
#define TS_LOG(...)
#endif

// VLC-Style TS Demuxer Constants
#define VLC_TS_PACKET_SIZE          188
#define VLC_TS_SYNC_BYTE            0x47
#define VLC_TS_MAX_PID              0x1FFF
#define VLC_TS_NULL_PID             0x1FFF
#define VLC_TS_PAT_PID              0x0000
#define VLC_TS_CAT_PID              0x0001
#define VLC_TS_NIT_PID              0x0010
#define VLC_TS_SDT_PID              0x0011
#define VLC_TS_EIT_PID              0x0012
#define VLC_TS_RST_PID              0x0013
#define VLC_TS_TDT_PID              0x0014
#define VLC_TS_TOT_PID              0x0014

// VLC-Style Stream Types
#define VLC_STREAM_TYPE_VIDEO_MPEG1     0x01
#define VLC_STREAM_TYPE_VIDEO_MPEG2     0x02
#define VLC_STREAM_TYPE_AUDIO_MPEG1     0x03
#define VLC_STREAM_TYPE_AUDIO_MPEG2     0x04
#define VLC_STREAM_TYPE_PRIVATE_PES     0x06
#define VLC_STREAM_TYPE_AUDIO_AAC       0x0f
#define VLC_STREAM_TYPE_VIDEO_MPEG4     0x10
#define VLC_STREAM_TYPE_AUDIO_AAC_LATM  0x11
#define VLC_STREAM_TYPE_VIDEO_H264      0x1b
#define VLC_STREAM_TYPE_VIDEO_HEVC      0x24
#define VLC_STREAM_TYPE_VIDEO_CAVS      0x42
#define VLC_STREAM_TYPE_VIDEO_VC1       0xea
#define VLC_STREAM_TYPE_VIDEO_DIRAC     0xd1

// VLC-Style PES Stream IDs
#define VLC_PES_STREAM_ID_VIDEO_FIRST   0xe0
#define VLC_PES_STREAM_ID_VIDEO_LAST    0xef
#define VLC_PES_STREAM_ID_AUDIO_FIRST   0xc0
#define VLC_PES_STREAM_ID_AUDIO_LAST    0xdf

// Forward declarations
class VLCTSProgram;
class VLCTSStream;
class VLCTSPacket;

// VLC-Style TS Header Structure
struct VLCTSHeader {
    uint16_t pid;
    uint8_t  continuity_counter;
    uint8_t  scrambling_control;
    bool     transport_error;
    bool     payload_unit_start;
    bool     transport_priority;
    bool     has_adaptation;
    bool     has_payload;
    
    VLCTSHeader() : pid(0), continuity_counter(0), scrambling_control(0),
                    transport_error(false), payload_unit_start(false),
                    transport_priority(false), has_adaptation(false),
                    has_payload(false) {}
};

class SPSParser {
private:
    const uint8_t* mData;
    size_t mSize;
    size_t mBitPos;
    
public:
    struct VideoInfo {
        uint32_t width;
        uint32_t height;
        uint32_t fps_num;
        uint32_t fps_den;
        uint8_t profile;
        uint8_t level;
        bool valid;
        
        VideoInfo() : width(0), height(0), fps_num(0), fps_den(0),
        profile(0), level(0), valid(false) {}
    };
    
    SPSParser(const uint8_t* data, size_t size) : mData(data), mSize(size), mBitPos(0) {}
    
    VideoInfo parseVideoInfo() {
        VideoInfo info;
        
        if (mSize < 4) {
            TS_LOG("SPS: Too small (%zu bytes)", mSize);
            return info;
        }
        
        // Skip NAL header (first byte)
        mBitPos = 8;
        
        try {
            // Parse SPS header
            info.profile = readBits(8);           // profile_idc
            uint8_t constraints = readBits(8);    // constraint flags
            info.level = readBits(8);             // level_idc
            
            uint32_t seq_parameter_set_id = readUEG(); // seq_parameter_set_id
            
            TS_LOG("SPS: Profile=%d, Level=%d, ID=%d", info.profile, info.level, seq_parameter_set_id);
            
            // Handle different profiles
            if (info.profile == 100 || info.profile == 110 || info.profile == 122 ||
                info.profile == 244 || info.profile == 44 || info.profile == 83 ||
                info.profile == 86 || info.profile == 118 || info.profile == 128) {
                
                uint32_t chroma_format_idc = readUEG();
                if (chroma_format_idc == 3) {
                    readBits(1); // separate_colour_plane_flag
                }
                
                readUEG(); // bit_depth_luma_minus8
                readUEG(); // bit_depth_chroma_minus8
                readBits(1); // qpprime_y_zero_transform_bypass_flag
                
                bool seq_scaling_matrix_present = readBits(1);
                if (seq_scaling_matrix_present) {
                    // Skip scaling matrices (complex parsing)
                    for (int i = 0; i < ((chroma_format_idc != 3) ? 8 : 12); i++) {
                        if (readBits(1)) { // seq_scaling_list_present_flag
                            skipScalingList(i < 6 ? 16 : 64);
                        }
                    }
                }
            }
            
            readUEG(); // log2_max_frame_num_minus4
            uint32_t pic_order_cnt_type = readUEG();
            
            if (pic_order_cnt_type == 0) {
                readUEG(); // log2_max_pic_order_cnt_lsb_minus4
            } else if (pic_order_cnt_type == 1) {
                readBits(1); // delta_pic_order_always_zero_flag
                readSEG(); // offset_for_non_ref_pic
                readSEG(); // offset_for_top_to_bottom_field
                uint32_t num_ref_frames = readUEG();
                for (uint32_t i = 0; i < num_ref_frames; i++) {
                    readSEG(); // offset_for_ref_frame
                }
            }
            
            readUEG(); // max_num_ref_frames
            readBits(1); // gaps_in_frame_num_value_allowed_flag
            
            // HERE'S THE IMPORTANT PART: Width and Height
            uint32_t pic_width_in_mbs_minus1 = readUEG();
            uint32_t pic_height_in_map_units_minus1 = readUEG();
            
            bool frame_mbs_only_flag = readBits(1);
            
            if (!frame_mbs_only_flag) {
                readBits(1); // mb_adaptive_frame_field_flag
            }
            
            readBits(1); // direct_8x8_inference_flag
            
            // Calculate raw dimensions
            uint32_t raw_width = (pic_width_in_mbs_minus1 + 1) * 16;
            uint32_t raw_height = (pic_height_in_map_units_minus1 + 1) * 16;
            
            if (!frame_mbs_only_flag) {
                raw_height *= 2;
            }
            
            TS_LOG("SPS: Raw dimensions: %dx%d", raw_width, raw_height);
            
            // Handle cropping
            bool frame_cropping_flag = readBits(1);
            uint32_t crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
            
            if (frame_cropping_flag) {
                crop_left = readUEG();
                crop_right = readUEG();
                crop_top = readUEG();
                crop_bottom = readUEG();
                
                TS_LOG("SPS: Cropping: left=%d, right=%d, top=%d, bottom=%d",
                    crop_left, crop_right, crop_top, crop_bottom);
            }
            
            // Calculate final dimensions (accounting for cropping)
            uint32_t crop_unit_x = 2; // For 4:2:0 chroma format
            uint32_t crop_unit_y = 2;
            
            if (!frame_mbs_only_flag) {
                crop_unit_y = 4;
            }
            
            info.width = raw_width - (crop_left + crop_right) * crop_unit_x;
            info.height = raw_height - (crop_top + crop_bottom) * crop_unit_y;
            
            TS_LOG("SPS: ✅ Final dimensions: %dx%d", info.width, info.height);
            
            // Try to get timing info
            bool vui_parameters_present = readBits(1);
            if (vui_parameters_present) {
                parseVUIForTiming(info);
            }
            
            info.valid = true;
            
        } catch (const std::exception& e) {
            TS_LOG("SPS: Parse error: %s", e.what());
            info.valid = false;
        }
        
        return info;
    }
    
    
private:
    uint32_t readBits(int numBits) {
        if (mBitPos + numBits > mSize * 8) {
            throw std::runtime_error("SPS: Not enough bits");
        }
        
        uint32_t result = 0;
        for (int i = 0; i < numBits; i++) {
            size_t bytePos = mBitPos / 8;
            size_t bitPos = 7 - (mBitPos % 8);
            
            if (mData[bytePos] & (1 << bitPos)) {
                result |= (1 << (numBits - 1 - i));
            }
            
            mBitPos++;
        }
        
        return result;
    }
    
    uint32_t readUEG() {
        // Unsigned Exp-Golomb coding
        int leadingZeros = 0;
        
        while (readBits(1) == 0) {
            leadingZeros++;
            if (leadingZeros > 32) {
                throw std::runtime_error("SPS: Invalid UEG");
            }
        }
        
        if (leadingZeros == 0) {
            return 0;
        }
        
        uint32_t result = readBits(leadingZeros);
        return result + (1 << leadingZeros) - 1;
    }
    
    int32_t readSEG() {
        // Signed Exp-Golomb coding
        uint32_t val = readUEG();
        if (val & 1) {
            return (val + 1) / 2;
        } else {
            return -((int32_t)val / 2);
        }
    }
    
    void skipScalingList(int size) {
        // Simplified scaling list skipping
        for (int i = 0; i < size; i++) {
            if (readBits(1)) { // delta_scale present
                readSEG(); // delta_scale
            }
        }
    }
    
    void parseVUIForTiming(VideoInfo& info) {
        try {
            bool aspect_ratio_info_present = readBits(1);
            if (aspect_ratio_info_present) {
                uint8_t aspect_ratio_idc = readBits(8);
                if (aspect_ratio_idc == 255) { // Extended_SAR
                    readBits(16); // sar_width
                    readBits(16); // sar_height
                }
            }
            
            bool overscan_info_present = readBits(1);
            if (overscan_info_present) {
                readBits(1); // overscan_appropriate_flag
            }
            
            bool video_signal_type_present = readBits(1);
            if (video_signal_type_present) {
                readBits(3); // video_format
                readBits(1); // video_full_range_flag
                bool colour_description_present = readBits(1);
                if (colour_description_present) {
                    readBits(8); // colour_primaries
                    readBits(8); // transfer_characteristics
                    readBits(8); // matrix_coefficients
                }
            }
            
            bool chroma_loc_info_present = readBits(1);
            if (chroma_loc_info_present) {
                readUEG(); // chroma_sample_loc_type_top_field
                readUEG(); // chroma_sample_loc_type_bottom_field
            }
            
            bool timing_info_present = readBits(1);
            if (timing_info_present) {
                uint32_t num_units_in_tick = readBits(32);
                uint32_t time_scale = readBits(32);
                bool fixed_frame_rate = readBits(1);
                
                TS_LOG("SPS: Raw timing: time_scale=%u, num_units_in_tick=%u", time_scale, num_units_in_tick);
                
                if (num_units_in_tick > 0 && time_scale > 0) {
                    // The issue is these values are often encoded incorrectly
                    // Common pattern: time_scale=16777216, num_units_in_tick=192
                    // This gives 43690 fps which is wrong
                    
                    double calculated_fps = (double)time_scale / (2.0 * num_units_in_tick);
                    TS_LOG("SPS: Calculated frame rate: %.2f fps", calculated_fps);
                    
                    // Check if the calculated frame rate is reasonable
                    if (calculated_fps >= 15.0 && calculated_fps <= 120.0) {
                        // Use calculated rate
                        info.fps_num = num_units_in_tick;
                        info.fps_den = time_scale / 2;
                        TS_LOG("SPS: ✅ Using calculated frame rate: %.2f fps", calculated_fps);
                    } else {
                        // Try alternative calculation (without /2)
                        double alt_fps = (double)time_scale / (double)num_units_in_tick;
                        TS_LOG("SPS: Alternative calculation: %.2f fps", alt_fps);
                        
                        if (alt_fps >= 15.0 && alt_fps <= 120.0) {
                            info.fps_num = num_units_in_tick;
                            info.fps_den = time_scale;
                            TS_LOG("SPS: ✅ Using alternative frame rate: %.2f fps", alt_fps);
                        } else {
                            // Fall back to common frame rates based on time_scale patterns
                            if (time_scale == 16777216) {
                                // Common pattern - likely 30fps
                                info.fps_num = 1;
                                info.fps_den = 30;
                                TS_LOG("SPS: ✅ Using 30fps for time_scale=16777216 pattern");
                            } else if (time_scale == 90000) {
                                // MPEG-2 TS timescale - likely 29.97fps
                                info.fps_num = 1001;
                                info.fps_den = 30000;
                                TS_LOG("SPS: ✅ Using 29.97fps for MPEG-2 TS pattern");
                            } else {
                                // Default to 30fps
                                info.fps_num = 1;
                                info.fps_den = 30;
                                TS_LOG("SPS: ❌ Using 30fps default for invalid timing");
                            }
                        }
                    }
                } else {
                    info.fps_num = 1;
                    info.fps_den = 30;
                    TS_LOG("SPS: ❌ Invalid timing values, using 30fps default");
                }
            } else {
                info.fps_num = 1;
                info.fps_den = 30;
                TS_LOG("SPS: No timing info, using 30fps default");
            }
            
        } catch (const std::exception& e) {
            TS_LOG("SPS: VUI parse error: %s", e.what());
            info.fps_num = 1;
            info.fps_den = 30;
        }
    }
};

// VLC-Style Adaptation Field
struct VLCTSAdaptationField {
    uint8_t length;
    bool    discontinuity;
    bool    random_access;
    bool    elementary_stream_priority;
    bool    pcr_flag;
    bool    opcr_flag;
    bool    splicing_point_flag;
    bool    transport_private_data_flag;
    bool    adaptation_field_extension_flag;
    
    uint64_t pcr_base;
    uint16_t pcr_extension;
    uint64_t opcr_base;
    uint16_t opcr_extension;
    
    VLCTSAdaptationField() : length(0), discontinuity(false), random_access(false),
                            elementary_stream_priority(false), pcr_flag(false),
                            opcr_flag(false), splicing_point_flag(false),
                            transport_private_data_flag(false),
                            adaptation_field_extension_flag(false),
                            pcr_base(0), pcr_extension(0), opcr_base(0), opcr_extension(0) {}
};

// VLC-Style PES Header
struct VLCPESHeader {
    uint8_t  stream_id;
    uint16_t packet_length;
    uint8_t  scrambling_control;
    uint8_t  priority;
    uint8_t  data_alignment;
    uint8_t  copyright;
    uint8_t  original_or_copy;
    uint8_t  pts_dts_flags;
    uint8_t  escr_flag;
    uint8_t  es_rate_flag;
    uint8_t  dsm_trick_mode_flag;
    uint8_t  additional_copy_info_flag;
    uint8_t  crc_flag;
    uint8_t  extension_flag;
    uint8_t  header_data_length;
    
    uint64_t pts;
    uint64_t dts;
    
    VLCPESHeader() : stream_id(0), packet_length(0), scrambling_control(0),
                     priority(0), data_alignment(0), copyright(0),
                     original_or_copy(0), pts_dts_flags(0), escr_flag(0),
                     es_rate_flag(0), dsm_trick_mode_flag(0),
                     additional_copy_info_flag(0), crc_flag(0),
                     extension_flag(0), header_data_length(0),
                     pts(0), dts(0) {}
};

// VLC-Style TS Stream
class VLCTSStream {
public:
    uint16_t pid;
    uint8_t  stream_type;
    uint8_t  stream_id;
    uint8_t  last_cc;
    bool     cc_valid;
    
    // PES assembly
    std::vector<uint8_t> pes_buffer;
    bool pes_header_parsed;
    VLCPESHeader pes_header;
    size_t pes_bytes_needed;
    
    // Timing
    uint64_t last_pcr;
    uint64_t last_pts;
    uint64_t last_dts;
    
    // Stats
    uint64_t packets_received;
    uint64_t continuity_errors;
    uint64_t scrambled_packets;
    
    VLCTSStream(uint16_t p, uint8_t st) : pid(p), stream_type(st), stream_id(0),
                                          last_cc(0), cc_valid(false),
                                          pes_header_parsed(false), pes_bytes_needed(0),
                                          last_pcr(0), last_pts(0), last_dts(0),
                                          packets_received(0), continuity_errors(0),
                                          scrambled_packets(0) {
        pes_buffer.reserve(65536);
    }
    
    bool isVideo() const {
        return stream_type == VLC_STREAM_TYPE_VIDEO_H264 ||
               stream_type == VLC_STREAM_TYPE_VIDEO_HEVC ||
               stream_type == VLC_STREAM_TYPE_VIDEO_MPEG2 ||
               stream_type == VLC_STREAM_TYPE_VIDEO_MPEG4;
    }
    
    bool isAudio() const {
        return stream_type == VLC_STREAM_TYPE_AUDIO_AAC ||
               stream_type == VLC_STREAM_TYPE_AUDIO_AAC_LATM ||
               stream_type == VLC_STREAM_TYPE_AUDIO_MPEG1 ||
               stream_type == VLC_STREAM_TYPE_AUDIO_MPEG2;
    }
    
    void resetPES() {
        pes_buffer.clear();
        pes_header_parsed = false;
        pes_bytes_needed = 0;
    }
};

struct CachedSPSInfo {
    bool valid = false;
    uint32_t width = 640;
    uint32_t height = 480;
    double frameDuration = 1.0/30.0; // Default 30fps
    uint32_t profile = 0;
    uint32_t level = 0;
    std::vector<uint8_t> spsData;
    
    void updateFromSPS(const uint8_t* data, size_t size) {
        if (!data || size < 4) return;
        
        SPSParser parser(data, size);
        SPSParser::VideoInfo spsInfo = parser.parseVideoInfo();
        
        if (spsInfo.valid) {
            valid = true;
            width = spsInfo.width;
            height = spsInfo.height;
            profile = spsInfo.profile;
            level = spsInfo.level;
            
            // Calculate frame duration from FPS
            if (spsInfo.fps_num > 0 && spsInfo.fps_den > 0) {
                double extractedFPS = (double)spsInfo.fps_den / (double)spsInfo.fps_num;
                
                // Validate FPS is reasonable (5-120 fps range)
                if (extractedFPS >= 5.0 && extractedFPS <= 120.0) {
                    frameDuration = 1.0 / extractedFPS;
                    TS_LOG("✅ SPS FPS extracted: %.2f fps (duration=%.4f)", extractedFPS, frameDuration);
                } else {
                    TS_LOG("⚠️ SPS FPS out of range: %.2f fps, using 30fps default", extractedFPS);
                    frameDuration = 1.0/30.0;
                }
            } else {
                TS_LOG("⚠️ SPS contains no valid FPS info, using 30fps default");
                frameDuration = 1.0/30.0;
            }
            
            // Cache the SPS data
            spsData.assign(data, data + size);
            
            TS_LOG("✅ SPS cached: %ux%u, profile=%u, level=%u, %.2f fps",
                width, height, profile, level, 1.0/frameDuration);
        } else {
            TS_LOG("❌ Failed to parse SPS data");
        }
    }
    
    double getFPS() const {
        return valid ? (1.0 / frameDuration) : 30.0;
    }
};

// VLC-Style TS Program
class VLCTSProgram {
public:
    // Cached SPS information to avoid re-parsing
   
    uint16_t program_number;
    uint16_t pmt_pid;
    uint16_t pcr_pid;
    
    std::map<uint16_t, std::unique_ptr<VLCTSStream>> streams;
    
    // Timing
    uint64_t pcr_base;
    uint32_t pcr_extension;
    bool     pcr_valid;
    
    VLCTSProgram(uint16_t pn, uint16_t pp) : program_number(pn), pmt_pid(pp),
                                             pcr_pid(0), pcr_base(0),
                                             pcr_extension(0), pcr_valid(false) {}
    
    VLCTSStream* getStream(uint16_t pid) {
        auto it = streams.find(pid);
        return (it != streams.end()) ? it->second.get() : nullptr;
    }
    
    void addStream(uint16_t pid, uint8_t stream_type) {
        streams[pid] = std::make_unique<VLCTSStream>(pid, stream_type);
        //TS_LOG("VLC TS: Added stream PID 0x%04X, type 0x%02X", pid, stream_type);
    }
    
    void removeStream(uint16_t pid) {
        streams.erase(pid);
        //TS_LOG("VLC TS: Removed stream PID 0x%04X", pid);
    }
};

struct NALUnit {
    size_t offset;
    size_t size;
    uint8_t type;
    bool isKeyFrame;
    
    NALUnit() : offset(0), size(0), type(0), isKeyFrame(false) {}
};



static bool convertAnnexBToAVCC(const uint8_t* annexBData, size_t annexBSize, std::vector<uint8_t>& avccData) {
    if (!annexBData || annexBSize < 1) {
        //TS_LOG("❌ Invalid Annex B data: ptr=%p, size=%zu", annexBData, annexBSize);
        return false;
    }
    
    avccData.clear();
    avccData.reserve(annexBSize + 100); // Reserve some extra space for length headers
    
    size_t pos = 0;
    int nalCount = 0;
    
    // Check if this is a single raw NAL unit (common for P-frames)
    if (annexBSize > 0) {
        uint8_t firstByte = annexBData[0];
        uint8_t nalType = firstByte & 0x1F;
        
        // If first byte looks like a NAL header and no start codes found
        if (nalType >= 1 && nalType <= 31) {
            bool hasStartCodes = false;
            
            // Quick check for start codes
            for (size_t i = 0; i < std::min(annexBSize, (size_t)32) - 3; i++) {
                if (annexBData[i] == 0x00 && annexBData[i+1] == 0x00) {
                    if ((annexBData[i+2] == 0x01) ||
                        (i+3 < annexBSize && annexBData[i+2] == 0x00 && annexBData[i+3] == 0x01)) {
                        hasStartCodes = true;
                        break;
                    }
                }
            }
            
            if (!hasStartCodes) {
                //TS_LOG("🔧 Detected raw NAL unit: type=%d, converting to AVCC", nalType);
                
                // Convert single raw NAL unit to AVCC
                uint32_t nalSizeBE = htonl((uint32_t)annexBSize);  // Convert to big endian
                avccData.insert(avccData.end(), (uint8_t*)&nalSizeBE, (uint8_t*)&nalSizeBE + 4);
                avccData.insert(avccData.end(), annexBData, annexBData + annexBSize);
                
                //TS_LOG("✅ Converted raw NAL unit: 1 NAL, %zu→%zu bytes", annexBSize, avccData.size());
                return true;
            }
        }
    }
    
    // Original start code processing
    while (pos < annexBSize) {
        // Find start code
        size_t startCodeSize = 0;
        size_t nalStart = pos;
        
        // Check for 4-byte start code (00 00 00 01)
        if (pos + 4 <= annexBSize &&
            annexBData[pos] == 0x00 && annexBData[pos+1] == 0x00 &&
            annexBData[pos+2] == 0x00 && annexBData[pos+3] == 0x01) {
            startCodeSize = 4;
            nalStart = pos + 4;
        }
        // Check for 3-byte start code (00 00 01)
        else if (pos + 3 <= annexBSize &&
                 annexBData[pos] == 0x00 && annexBData[pos+1] == 0x00 &&
                 annexBData[pos+2] == 0x01) {
            startCodeSize = 3;
            nalStart = pos + 3;
        }
        else {
            //TS_LOG("❌ No start code found at position %zu, scanning ahead...", pos);
            
            // Enhanced error recovery - scan ahead for next start code
            size_t nextStartCode = SIZE_MAX;
            for (size_t scan = pos + 1; scan < annexBSize - 3; scan++) {
                if ((annexBData[scan] == 0x00 && annexBData[scan+1] == 0x00 &&
                     annexBData[scan+2] == 0x01) ||
                    (scan + 4 < annexBSize && annexBData[scan] == 0x00 &&
                     annexBData[scan+1] == 0x00 && annexBData[scan+2] == 0x00 &&
                     annexBData[scan+3] == 0x01)) {
                    nextStartCode = scan;
                    break;
                }
            }
            
            if (nextStartCode == SIZE_MAX) {
                //TS_LOG("❌ No start codes found in remaining data");
                break;
            }
            
            //TS_LOG("🔧 Sync recovery: skipping %zu bytes", nextStartCode - pos);
            pos = nextStartCode;
            continue;
        }
        
        // Validate NAL start position
        if (nalStart >= annexBSize) {
            //TS_LOG("❌ NAL start beyond buffer bounds: %zu >= %zu", nalStart, annexBSize);
            break;
        }
        
        // Find end of this NAL unit
        size_t nalEnd = annexBSize;
        for (size_t i = nalStart + 1; i < annexBSize - 2; i++) {
            if (annexBData[i] == 0x00 && annexBData[i+1] == 0x00) {
                if (i + 3 < annexBSize && annexBData[i+2] == 0x01) {
                    nalEnd = i;
                    break;
                } else if (i + 4 < annexBSize && annexBData[i+2] == 0x00 &&
                          annexBData[i+3] == 0x01) {
                    nalEnd = i;
                    break;
                }
            }
        }
        
        size_t nalSize = nalEnd - nalStart;
        if (nalSize == 0 || nalSize > 1024 * 1024) { // 1MB limit
            //TS_LOG("❌ Invalid NAL size: %zu", nalSize);
            pos = nalEnd;
            continue;
        }
        
        uint8_t nalType = annexBData[nalStart] & 0x1F;
        
        // Validate NAL type
        if (nalType == 0 || nalType > 31) {
            //TS_LOG("❌ Invalid NAL type: %d", nalType);
            pos = nalEnd;
            continue;
        }
        
        //TS_LOG("📦 AVCC NAL #%d: type=%d, size=%zu (removed %zu-byte start code)",
        //    nalCount + 1, nalType, nalSize, startCodeSize);
        
        // Write 4-byte length header (big endian)
        uint32_t nalSizeBE = htonl((uint32_t)nalSize);
        avccData.insert(avccData.end(), (uint8_t*)&nalSizeBE, (uint8_t*)&nalSizeBE + 4);
        
        // Write NAL unit data (without start code)
        avccData.insert(avccData.end(), &annexBData[nalStart], &annexBData[nalStart] + nalSize);
        
        nalCount++;
        pos = nalEnd;
    }
    
    if (nalCount == 0) {
        //TS_LOG("❌ No valid NAL units found");
        return false;
    }
    
    //TS_LOG("✅ Converted %d NALs, %zu→%zu bytes (saved %zu bytes)",
    //    nalCount, annexBSize, avccData.size(), annexBSize - avccData.size());
    
    return true;
}


class VLCTSDemuxer {
public:
    
    CachedSPSInfo mCachedSPS;
    
private:
    // Core TS demuxing state
    std::map<uint16_t, std::unique_ptr<VLCTSProgram>> programs;
    std::map<uint16_t, uint8_t> continuity_counters;
    
    
    // Add data mode tracking to class
    enum DataMode {
        DATA_MODE_UNKNOWN,
        DATA_MODE_PES,
        DATA_MODE_RAW_H264
    };
    
    // Add these as class members:
    std::map<uint16_t, DataMode> mDataMode;
    
    std::map<uint16_t, std::vector<uint8_t>> mPESBuffers;     // Buffer per PID
    std::map<uint16_t, uint32_t> mPESPacketCounts;            // Packet count per PID
    std::map<uint16_t, bool> mPESHeaderParsed;                // Header parsed per PID
    std::map<uint16_t, size_t> mPESExpectedSize;              // Expected size per PID
    
    // Core statistics
    uint64_t total_packets;
    uint64_t sync_errors;
    uint64_t continuity_errors;
    uint64_t transport_errors;
    
    // Timing
    uint64_t current_pcr;
    bool     pcr_valid;
    std::chrono::steady_clock::time_point start_time;
    
    // Core frame processing
    std::vector<uint8_t> currentFrame;
    bool frameStarted;
    uint32_t frameSequence;
    bool currentFrameIsKeyframe;
    double currentFrameTimestamp;
    std::vector<uint8_t> currentSPS;
    std::vector<uint8_t> currentPPS;
    
    // YouTube-specific enhancements
    std::vector<uint8_t> mSegmentBuffer;
    size_t mMaxSegmentBufferSize = 4 * 1024 * 1024; // 4MB buffer
    
    // YouTube error tracking
    int mConsecutiveErrors = 0;
    int mSyncLossCount = 0;
    std::chrono::steady_clock::time_point mLastResetTime;
    
    bool mInSegmentTransition = false;
    
    // YouTube sync configuration
    int mCurrentSyncLosses = 0;
    std::map<uint16_t, bool> mPIDDiscontinuityFlags;
    
    static const size_t TS_PACKET_SIZE = 188;
    static const uint8_t TS_SYNC_BYTE = 0x47;
    
    std::map<uint16_t, std::vector<uint8_t>> mFrameBuffers;     // Complete frame being assembled
    std::map<uint16_t, bool> mFrameInProgress;                  // Is a frame currently being assembled?
    std::map<uint16_t, double> mFrameTimestamp;                 // Timestamp for current frame
    std::map<uint16_t, bool> mFrameIsKeyframe;
    
    // Timestamp normalization state
    struct TimestampNormalizer {
        bool initialized = false;
        uint64_t basePTS = 0;
        uint64_t baseDTS = 0;
        double baseTime = 0.0;
        uint32_t frameCounter = 0;
        
        // Wraparound detection (33-bit PTS can wrap)
        uint64_t lastPTS = 0;
        uint64_t lastDTS = 0;
        uint64_t ptsWrapOffset = 0;
        uint64_t dtsWrapOffset = 0;
        
        static const uint64_t PTS_WRAP_THRESHOLD = 0x100000000ULL; // 33-bit wrap point
        static const uint64_t PTS_WRAP_DETECT = 0x80000000ULL;     // Half wrap point for detection
        
        void reset() {
            initialized = false;
            basePTS = baseDTS = 0;
            baseTime = 0.0;
            frameCounter = 0;
            lastPTS = lastDTS = 0;
            ptsWrapOffset = dtsWrapOffset = 0;
            TS_LOG("🔄 Timestamp normalizer reset");
        }
        
        void initializeBaseline(uint64_t firstPTS, uint64_t firstDTS, double playbackStartTime) {
            basePTS = firstPTS;
            baseDTS = firstDTS;
            baseTime = playbackStartTime;
            frameCounter = 0;
            lastPTS = firstPTS;
            lastDTS = firstDTS;
            initialized = true;
            
            TS_LOG("✅ Timestamp baseline initialized: PTS=%llu, DTS=%llu, baseTime=%.3f",
                   basePTS, baseDTS, baseTime);
        }
        
        std::pair<double, double> normalize(uint64_t rawPTS, uint64_t rawDTS, double frameDuration) {
            if (!initialized) {
                // Initialize with first frame
                double currentTime = CFAbsoluteTimeGetCurrent();
                initializeBaseline(rawPTS, rawDTS, currentTime);
                return std::make_pair(0.0, 0.0); // First frame at time 0
            }
            
            // Handle wraparound detection for PTS
            uint64_t adjustedPTS = rawPTS;
            if (rawPTS != 0) {
                if (rawPTS < lastPTS && (lastPTS - rawPTS) > PTS_WRAP_DETECT) {
                    ptsWrapOffset += PTS_WRAP_THRESHOLD;
                    TS_LOG("🔄 PTS wraparound detected: old=%llu, new=%llu, offset=%llu",
                           lastPTS, rawPTS, ptsWrapOffset);
                }
                adjustedPTS = rawPTS + ptsWrapOffset;
                lastPTS = rawPTS;
            }
            
            // Handle wraparound detection for DTS
            uint64_t adjustedDTS = rawDTS;
            if (rawDTS != 0) {
                if (rawDTS < lastDTS && (lastDTS - rawDTS) > PTS_WRAP_DETECT) {
                    dtsWrapOffset += PTS_WRAP_THRESHOLD;
                    TS_LOG("🔄 DTS wraparound detected: old=%llu, new=%llu, offset=%llu",
                           lastDTS, rawDTS, dtsWrapOffset);
                }
                adjustedDTS = rawDTS + dtsWrapOffset;
                lastDTS = rawDTS;
            }
            
            // Convert to normalized timestamps (relative to baseline)
            double normalizedCTS = 0.0;
            double normalizedDTS = 0.0;
            
            if (adjustedPTS != 0) {
                normalizedCTS = (double)(adjustedPTS - basePTS) / 90000.0;
            } else {
                // Fallback: frame-based timing
                normalizedCTS = frameCounter * frameDuration;
            }
            
            if (adjustedDTS != 0) {
                normalizedDTS = (double)(adjustedDTS - baseDTS) / 90000.0;
            } else {
                normalizedDTS = normalizedCTS; // DTS = CTS for no B-frames
            }
            
            // Ensure normalized timestamps are non-negative
            if (normalizedCTS < 0.0) {
                TS_LOG("⚠️ Negative CTS detected: %.3f, using frame-based fallback", normalizedCTS);
                normalizedCTS = frameCounter * frameDuration;
            }
            
            if (normalizedDTS < 0.0) {
                normalizedDTS = normalizedCTS;
            }
            
            frameCounter++;
            
            //  TS_LOG("🕐 Normalized timing: raw PTS=%llu→CTS=%.3f, raw DTS=%llu→DTS=%.3f (frame %u)", \
            rawPTS, normalizedCTS, rawDTS, normalizedDTS, frameCounter);
            
            return std::make_pair(normalizedCTS, normalizedDTS);
        }
        
        // Handle segment discontinuities (common in HLS)
        void handleDiscontinuity(uint64_t newPTS, uint64_t newDTS) {
            if (initialized) {
                TS_LOG("🔄 Timestamp discontinuity detected - reinitializing baseline");
                double currentPlaybackTime = frameCounter * (1.0/30.0); // Maintain smooth playback time
                initializeBaseline(newPTS, newDTS, baseTime + currentPlaybackTime);
            }
        }
    };
    
    // Timing statistics tracking
    struct TimingStats {
        uint32_t totalFrames = 0;
        uint32_t normalizedFrames = 0;
        uint32_t fallbackFrames = 0;
        uint32_t discontinuities = 0;
        double avgFrameInterval = 0.0;
        
        void recordFrame(bool wasNormalized) {
            totalFrames++;
            if (wasNormalized) {
                normalizedFrames++;
            } else {
                fallbackFrames++;
            }
        }
        
        void recordDiscontinuity() {
            discontinuities++;
        }
        
        void printStats() {
            if (totalFrames > 0) {
                TS_LOG("📊 Timing Stats: %u frames (%u normalized, %u fallback, %u discontinuities)",
                       totalFrames, normalizedFrames, fallbackFrames, discontinuities);
                TS_LOG("📊 Normalization success rate: %.1f%%",
                       (float)normalizedFrames / totalFrames * 100.0f);
            }
        }
    };
    
    TimestampNormalizer mTimestampNormalizer;
    TimingStats mTimingStats;
    uint32_t nextSequenceNumber = 1;
    
    // Callbacks
    std::function<void(uint16_t pid, const uint8_t* data, size_t size, VLCPESHeader& header)> audio_callback;
    std::function<void(uint16_t pid, const uint8_t* data, size_t size, VLCPESHeader& header)> video_callback;
    
public:
    VLCTSDemuxer() : total_packets(0), sync_errors(0), continuity_errors(0),
    transport_errors(0), current_pcr(0), pcr_valid(false) {
        start_time = std::chrono::steady_clock::now();
        mSegmentBuffer.reserve(mMaxSegmentBufferSize);
        frameStarted = false;
        frameSequence = 0;
        currentFrameIsKeyframe = false;
        currentFrameTimestamp = 0.0;
    }
    
    // Callback setup
    void setAudioCallback(std::function<void(uint16_t, const uint8_t*, size_t, VLCPESHeader&)> cb) {
        audio_callback = cb;
    }
    
    void setVideoCallback(std::function<void(uint16_t, const uint8_t*, size_t, VLCPESHeader&)> cb) {
        video_callback = cb;
    }
    
    VLCTSStream* tryAutoDetectStream(uint16_t pid, const uint8_t* payload, size_t size) {
        if (!payload || size < 9) return nullptr;
        
        // STRICT: Only detect on valid PES packet starts
        if (payload[0] != 0x00 || payload[1] != 0x00 || payload[2] != 0x01) {
            TS_LOG("🔍 PID 0x%04X: Not a PES packet start (%02X %02X %02X)",
                   pid, payload[0], payload[1], payload[2]);
            return nullptr;
        }
        
        uint8_t streamId = payload[3];
        TS_LOG("🔍 Auto-detecting PID 0x%04X: PES stream ID 0x%02X", pid, streamId);
        
        // Create program if needed
        if (programs.find(1) == programs.end()) {
            programs[1] = std::make_unique<VLCTSProgram>(1, 0x1000);
            TS_LOG("✅ Created default program for auto-detection");
        }
        
        // Video stream IDs (0xE0-0xEF)
        if (streamId >= 0xE0 && streamId <= 0xEF) {
            TS_LOG("🎬 Auto-detected VIDEO stream on PID 0x%04X (stream_id=0x%02X)", pid, streamId);
            programs[1]->addStream(pid, VLC_STREAM_TYPE_VIDEO_H264);
            return programs[1]->getStream(pid);
        }
        // Audio stream IDs (0xC0-0xDF)
        else if (streamId >= 0xC0 && streamId <= 0xDF) {
            TS_LOG("🔊 Auto-detected AUDIO stream on PID 0x%04X (stream_id=0x%02X)", pid, streamId);
            programs[1]->addStream(pid, VLC_STREAM_TYPE_AUDIO_AAC);
            return programs[1]->getStream(pid);
        }
        // NEW: Also check for private streams that might contain audio
        else if (streamId == 0xBD) {
            TS_LOG("🔊 Auto-detected PRIVATE AUDIO stream on PID 0x%04X (stream_id=0x%02X)", pid, streamId);
            programs[1]->addStream(pid, VLC_STREAM_TYPE_AUDIO_AAC);
            return programs[1]->getStream(pid);
        }
        else {
            TS_LOG("❓ Unknown stream ID 0x%02X on PID 0x%04X", streamId, pid);
        }
        
        return nullptr;
    }
    
    bool demux(const uint8_t* data, size_t size) {
        if (!data || size == 0) {
            TS_LOG("❌ VLCTSDemuxer::demux: Invalid input");
            return false;
        }
        
        // Safe buffer management
        const size_t MAX_BUFFER_SIZE = 2 * 1024 * 1024; // 2MB limit
        if (mSegmentBuffer.size() + size > MAX_BUFFER_SIZE) {
            try {
                size_t clearAmount = mSegmentBuffer.size() / 2;
                if (clearAmount < mSegmentBuffer.size()) {
                    mSegmentBuffer.erase(mSegmentBuffer.begin(), mSegmentBuffer.begin() + clearAmount);
                } else {
                    mSegmentBuffer.clear();
                }
            } catch (const std::exception& e) {
                mSegmentBuffer.clear();
                TS_LOG("mSegmentBuffer.erase 1: %s", e.what());
            }
        }
        
        // Add new data with error handling
        try {
            mSegmentBuffer.insert(mSegmentBuffer.end(), data, data + size);
        } catch (const std::exception& e) {
            return false;
        }
        
        int packetsProcessed = 0;
        
        // Process packets with YouTube-enhanced sync handling
        while (mSegmentBuffer.size() >= TS_PACKET_SIZE) {
            // Check for sync byte with YouTube tolerance
            if (mSegmentBuffer[0] != TS_SYNC_BYTE) {
                // YouTube-style sync recovery
                bool foundSync = false;
                size_t searchLimit = std::min(mSegmentBuffer.size(), (size_t)(TS_PACKET_SIZE * 2));
                
                for (size_t i = 1; i < searchLimit; i++) {
                    if (mSegmentBuffer[i] == TS_SYNC_BYTE) {
                        // Double-check with YouTube tolerance
                        if (i + TS_PACKET_SIZE < mSegmentBuffer.size() &&
                            mSegmentBuffer[i + TS_PACKET_SIZE] == TS_SYNC_BYTE) {
                            
                            try {
                                mSegmentBuffer.erase(mSegmentBuffer.begin(), mSegmentBuffer.begin() + i);
                                foundSync = true;
                                break;
                            } catch (const std::exception& e) {
                                TS_LOG("mSegmentBuffer.erase 2: %s", e.what());
                                mSegmentBuffer.clear();
                                return false;
                            }
                        }
                    }
                }
                
                if (!foundSync) {
                    mSegmentBuffer.clear();
                    break;
                }
            }
            
            // Process one packet with enhanced error handling
            bool packetOK = false;
            try {
                if (mSegmentBuffer.size() >= TS_PACKET_SIZE) {
                    packetOK = processPacketWithYouTubeEnhancements(mSegmentBuffer.data());
                }
            } catch (const std::exception& e) {
                packetOK = false;
            }
            
            // Remove processed packet
            if (mSegmentBuffer.size() >= TS_PACKET_SIZE) {
                try {
                    mSegmentBuffer.erase(mSegmentBuffer.begin(), mSegmentBuffer.begin() + TS_PACKET_SIZE);
                    packetsProcessed++;
                } catch (const std::exception& e) {
                    TS_LOG("mSegmentBuffer.erase 3: %s", e.what());
                    mSegmentBuffer.clear();
                    break;
                }
            } else {
                mSegmentBuffer.clear();
                break;
            }
            
            // Safety: Prevent infinite loops
            if (packetsProcessed > 50) {
                break;
            }
        }
        
        return packetsProcessed > 0;
    }
    
private:
    
    bool shouldProcessFrame(size_t frameSize, uint16_t pid) {
        // Process frame if:
        // 1. It's getting large (likely complete)
        // 2. We've accumulated reasonable amount of data
        
        if (frameSize >= 4096) { // 4KB - likely a complete frame
            return true;
        }
        
        // For smaller frames, use packet count or timing heuristics
        static std::map<uint16_t, std::chrono::steady_clock::time_point> lastProcessTime;
        auto now = std::chrono::steady_clock::now();
        
        if (lastProcessTime.find(pid) == lastProcessTime.end()) {
            lastProcessTime[pid] = now;
            return false;
        }
        
        // Process if it's been more than 50ms since last frame
        auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProcessTime[pid]);
        if (timeSinceLastFrame.count() >= 50 && frameSize >= 1024) {
            lastProcessTime[pid] = now;
            return true;
        }
        
        return false;
    }
    
    bool processPacketWithYouTubeEnhancements(const uint8_t* packet) {
        VLCTSHeader header;
        
        // TS_LOG("processPacketWithYouTubeEnhancements");
        // Parse TS header
        if (!parseHeader(packet, header)) {
            sync_errors++;
            return false;
        }
        
        total_packets++;
        
        // Skip null packets
        if (header.pid == VLC_TS_NULL_PID)
            return true;
        
        // YouTube-specific: Handle discontinuity flags
        if (mPIDDiscontinuityFlags[header.pid]) {
            continuity_counters[header.pid] = header.continuity_counter;
            mPIDDiscontinuityFlags[header.pid] = false;
            mInSegmentTransition = false;
            
            // Reset timestamp normalizer on major discontinuities
            TS_LOG("🔄 Discontinuity detected on PID 0x%04X - resetting timestamp normalizer", header.pid);
            mTimestampNormalizer.reset();
            mTimingStats.recordDiscontinuity();
        }
        
        // YouTube-enhanced continuity checking
        if (header.has_payload && header.pid != VLC_TS_NULL_PID)
            checkYouTubeContinuity(header);
        
        // Parse adaptation field and process payload
        const uint8_t* payload = packet + 4;
        size_t payload_size = TS_PACKET_SIZE - 4;
        
        if (header.has_adaptation) {
            VLCTSAdaptationField adaptation;
            payload = parseAdaptationField(payload, payload_size, adaptation);
            
            // Handle discontinuity indicator
            if (adaptation.discontinuity) {
                TS_LOG("🔄 Adaptation field discontinuity on PID 0x%04X", header.pid);
                mPIDDiscontinuityFlags[header.pid] = true;
            }
        }
        
        // Process payload
        if (header.has_payload && payload_size > 0)
            return processPayload(header, payload, payload_size);
        
        return true;
    }
    
    // YouTube-enhanced continuity checking
    bool checkYouTubeContinuity(const VLCTSHeader& header) {
        auto it = continuity_counters.find(header.pid);
        if (it == continuity_counters.end()) {
            continuity_counters[header.pid] = header.continuity_counter;
            return true;
        }
        
        uint8_t expected = (it->second + 1) & 0x0F;
        if (header.continuity_counter != expected) {
            uint8_t gap = (header.continuity_counter - expected) & 0x0F;
            
            // YouTube tolerance: Allow larger gaps
            if (gap <= 5) {  // Allow up to 5 packet gap
                it->second = header.continuity_counter;
                return true;
            }
            
            // Large gap - reset CC (YouTube streams often have gaps)
            it->second = header.continuity_counter;
            return true; // Don't fail
        }
        
        it->second = header.continuity_counter;
        return true;
    }
    
    // Core TS header parsing
    bool parseHeader(const uint8_t* packet, VLCTSHeader& header) {
        if (packet[0] != TS_SYNC_BYTE) {
            TS_LOG("❌ Invalid sync byte: 0x%02X", packet[0]);
            return false;
        }
        
        header.transport_error = (packet[1] & 0x80) != 0;
        header.payload_unit_start = (packet[1] & 0x40) != 0;
        header.transport_priority = (packet[1] & 0x20) != 0;
        header.pid = ((packet[1] & 0x1F) << 8) | packet[2];
        
        header.scrambling_control = (packet[3] & 0xC0) >> 6;
        header.has_adaptation = (packet[3] & 0x20) != 0;
        header.has_payload = (packet[3] & 0x10) != 0;
        header.continuity_counter = packet[3] & 0x0F;
        
        // DEBUG: Log header details for video stream
        if (header.pid == 0x0100) {
            TS_LOG("🔍 TS Header PID=0x%04X: payload_start=%d, has_payload=%d, cc=%d",
                   header.pid, header.payload_unit_start, header.has_payload, header.continuity_counter);
        }
        
        return true;
    }
    
    // Adaptation field parsing
    const uint8_t* parseAdaptationField(const uint8_t* data, size_t& remaining_size,
                                        VLCTSAdaptationField& adaptation) {
        if (remaining_size < 1) return data;
        
        adaptation.length = data[0];
        if (adaptation.length == 0 || adaptation.length >= remaining_size) {
            remaining_size -= 1;
            return data + 1;
        }
        
        if (remaining_size < adaptation.length + 1) {
            remaining_size = 0;
            return data + remaining_size;
        }
        
        const uint8_t* af_data = data + 1;
        
        if (adaptation.length > 0) {
            uint8_t flags = af_data[0];
            adaptation.discontinuity = (flags & 0x80) != 0;
            adaptation.random_access = (flags & 0x40) != 0;
            adaptation.elementary_stream_priority = (flags & 0x20) != 0;
            adaptation.pcr_flag = (flags & 0x10) != 0;
            adaptation.opcr_flag = (flags & 0x08) != 0;
            adaptation.splicing_point_flag = (flags & 0x04) != 0;
            adaptation.transport_private_data_flag = (flags & 0x02) != 0;
            adaptation.adaptation_field_extension_flag = (flags & 0x01) != 0;
            
            size_t offset = 1;
            
            // Parse PCR
            if (adaptation.pcr_flag && adaptation.length >= 7) {
                adaptation.pcr_base = ((uint64_t)af_data[offset] << 25) |
                ((uint64_t)af_data[offset + 1] << 17) |
                ((uint64_t)af_data[offset + 2] << 9) |
                ((uint64_t)af_data[offset + 3] << 1) |
                ((af_data[offset + 4] & 0x80) >> 7);
                adaptation.pcr_extension = ((af_data[offset + 4] & 0x01) << 8) |
                af_data[offset + 5];
                offset += 6;
            }
        }
        
        remaining_size -= (adaptation.length + 1);
        return data + adaptation.length + 1;
    }
    
    bool isAVCCFormat(const uint8_t* data, size_t size) {
        if (size < 5) return false;
        
        uint32_t nalLength = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        
        // Reasonable length check
        if (nalLength > 0 && nalLength < size - 4 && nalLength < 1024*1024) {
            uint8_t nalType = data[4] & 0x1F;
            
            // Valid H.264 NAL type range
            if (nalType >= 1 && nalType <= 31) {
                TS_LOG("✅ Detected AVCC format: length=%u, NAL type=%d", nalLength, nalType);
                return true;
            }
        }
        
        return false;
    }
    
    bool processPayload(const VLCTSHeader& header, const uint8_t* payload, size_t size) {
        TS_LOG("processPayload PID=0x%04X, size=%zu, payload_start=%d",
               header.pid, size, header.payload_unit_start);
        
        // PRIORITY 1: Handle PAT
        if (header.pid == VLC_TS_PAT_PID) {
            TS_LOG("📋 Processing PAT (Program Association Table)");
            return processPAT(payload, size);
        }
        
        // PRIORITY 2: Check for PMT on known PIDs
        for (auto& prog_pair : programs) {
            auto& program = prog_pair.second;
            if (program->pmt_pid == header.pid) {
                TS_LOG("📋 Processing PMT for program %u on PID 0x%04X",
                       program->program_number, header.pid);
                return processPMT(payload, size, program.get());
            }
        }
        
        // PRIORITY 3: Handle known streams from PMT
        VLCTSStream* stream = findStreamForPID(header.pid);
        if (stream) {
            TS_LOG("✅ Found known stream for PID 0x%04X, type=0x%02X",
                   header.pid, stream->stream_type);
            return processPES(header, payload, size, stream);
        }
        
        // PRIORITY 4: Auto-detect streams ONLY on payload start
        if (header.payload_unit_start && size >= 9) {
            stream = tryAutoDetectStream(header.pid, payload, size);
            if (stream) {
                TS_LOG("🎯 Auto-detected stream for PID 0x%04X", header.pid);
                return processPES(header, payload, size, stream);
            }
        }
        
        // NEW: Enhanced audio detection - check common audio PIDs
        if (header.payload_unit_start && size >= 9 && !stream) {
            // Check for PES header first
            if (payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                uint8_t streamId = payload[3];
                
                // Audio stream IDs (0xC0-0xDF, 0xBD for private)
                if ((streamId >= 0xC0 && streamId <= 0xDF) || streamId == 0xBD) {
                    TS_LOG("🔍 POTENTIAL AUDIO found on unlisted PID 0x%04X (stream_id=0x%02X)",
                           header.pid, streamId);
                    
                    // Create program if needed
                    if (programs.find(1) == programs.end()) {
                        programs[1] = std::make_unique<VLCTSProgram>(1, 0x1000);
                    }
                    
                    // Add as audio stream
                    programs[1]->addStream(header.pid, VLC_STREAM_TYPE_AUDIO_AAC);
                    stream = programs[1]->getStream(header.pid);
                    
                    if (stream) {
                        TS_LOG("✅ Added unlisted audio stream on PID 0x%04X", header.pid);
                        return processPES(header, payload, size, stream);
                    }
                }
            }
        }
        
        // Log unhandled PIDs (less verbose for continuation packets)
        static std::set<uint16_t> loggedPIDs;
        if (loggedPIDs.find(header.pid) == loggedPIDs.end()) {
            if (header.payload_unit_start) {
                TS_LOG("🔍 Unhandled PID 0x%04X with %zu bytes payload (PAYLOAD START)",
                       header.pid, size);
                if (size >= 8) {
                    TS_LOG("🔍 Payload preview: %02X %02X %02X %02X %02X %02X %02X %02X",
                           payload[0], payload[1], payload[2], payload[3],
                           payload[4], payload[5], payload[6], payload[7]);
                }
            }
            loggedPIDs.insert(header.pid);
        }
        
        return true;
    }
    
    bool processPAT(const uint8_t* data, size_t size) {
        TS_LOG("📋 Processing PAT: %zu bytes", size);
        
        if (size < 8) {
            TS_LOG("❌ PAT too small: %zu bytes", size);
            return false;
        }
        
        // Log raw data for debugging
        if (size >= 8) {
            TS_LOG("🔍 PAT raw data: %02X %02X %02X %02X %02X %02X %02X %02X",
                   data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
        }
        
        // Handle pointer field (first byte indicates offset to actual table)
        const uint8_t* patData = data;
        size_t remainingSize = size;
        
        uint8_t pointerField = patData[0];
        TS_LOG("🔧 PAT pointer field: %u", pointerField);
        
        // Skip pointer field and any stuffing bytes
        patData += 1 + pointerField;
        remainingSize -= 1 + pointerField;
        
        if (remainingSize < 8) {
            TS_LOG("❌ PAT too small after pointer: %zu bytes", remainingSize);
            return false;
        }
        
        // Check table ID (should be 0x00 for PAT)
        if (patData[0] != 0x00) {
            TS_LOG("❌ Invalid PAT table ID: 0x%02X (expected 0x00)", patData[0]);
            return false;
        }
        
        // Parse PAT header properly
        bool section_syntax = (patData[1] & 0x80) != 0;
        bool zero_bit = (patData[1] & 0x40) != 0;  // Should be 0
        uint16_t section_length = ((patData[1] & 0x0F) << 8) | patData[2];
        
        TS_LOG("🔍 PAT section: syntax=%d, zero=%d, length=%u, remaining=%zu",
               section_syntax, zero_bit, section_length, remainingSize);
        
        // Validate section length against remaining data
        if (section_length > remainingSize - 3) {
            TS_LOG("❌ PAT section length %u exceeds remaining data %zu",
                   section_length, remainingSize - 3);
            return false;
        }
        
        // More validation
        if (section_length < 5) {
            TS_LOG("❌ PAT section too short: %u bytes", section_length);
            return false;
        }
        
        uint16_t transport_stream_id = (patData[3] << 8) | patData[4];
        uint8_t version = (patData[5] & 0x3E) >> 1;
        bool current_next = (patData[5] & 0x01) != 0;
        uint8_t section_number = patData[6];
        uint8_t last_section = patData[7];
        
        TS_LOG("✅ PAT: TSID=%u, version=%u, current=%d, section=%u/%u",
               transport_stream_id, version, current_next, section_number, last_section);
        
        if (!current_next) {
            TS_LOG("⚠️ PAT not current, skipping");
            return true;
        }
        
        // Calculate program data size (exclude 5-byte header and 4-byte CRC)
        size_t program_data_size = section_length - 9;
        const uint8_t* program_data = patData + 8;
        
        TS_LOG("🔍 Parsing %zu bytes of program data", program_data_size);
        
        int programCount = 0;
        for (size_t i = 0; i + 4 <= program_data_size; i += 4) {
            uint16_t program_number = (program_data[i] << 8) | program_data[i + 1];
            uint16_t pmt_pid = ((program_data[i + 2] & 0x1F) << 8) | program_data[i + 3];
            
            if (program_number == 0) {
                TS_LOG("🌐 Network PID: 0x%04X", pmt_pid);
            } else {
                programCount++;
                TS_LOG("📺 Program %u -> PMT PID 0x%04X", program_number, pmt_pid);
                
                if (programs.find(program_number) == programs.end()) {
                    programs[program_number] = std::make_unique<VLCTSProgram>(program_number, pmt_pid);
                    TS_LOG("✅ Created program %u with PMT PID 0x%04X", program_number, pmt_pid);
                }
            }
        }
        
        TS_LOG("✅ PAT processed successfully: %d programs found", programCount);
        return true;
    }
    
    
    bool processPMT(const uint8_t* data, size_t size, VLCTSProgram* program) {
        TS_LOG("📋 Processing PMT for program %u: %zu bytes", program->program_number, size);
        
        if (size < 12) {
            TS_LOG("❌ PMT too small: %zu bytes", size);
            return false;
        }
        
        // Handle pointer field
        const uint8_t* pmtData = data;
        size_t dataOffset = 0;
        
        if (pmtData[0] != 0x00) {
            dataOffset = pmtData[0] + 1;
            TS_LOG("🔧 Skipping PMT pointer field: %d bytes", pmtData[0]);
            
            if (dataOffset >= size) {
                TS_LOG("❌ PMT pointer field too large: %zu >= %zu", dataOffset, size);
                return false;
            }
        } else {
            dataOffset = 1; // Skip the 0x00 pointer
        }
        
        pmtData += dataOffset;
        size -= dataOffset;
        
        if (size < 12) {
            TS_LOG("❌ PMT too small after pointer: %zu bytes", size);
            return false;
        }
        
        // Check table ID
        if (pmtData[0] != 0x02) {
            TS_LOG("❌ Invalid PMT table ID: 0x%02X (expected 0x02)", pmtData[0]);
            return false;
        }
        
        // Parse PMT header
        bool section_syntax = (pmtData[1] & 0x80) != 0;
        uint16_t section_length = ((pmtData[1] & 0x0F) << 8) | pmtData[2];
        uint16_t program_number = (pmtData[3] << 8) | pmtData[4];
        uint8_t version = (pmtData[5] & 0x3E) >> 1;
        bool current_next = (pmtData[5] & 0x01) != 0;
        uint8_t section_number = pmtData[6];
        uint8_t last_section = pmtData[7];
        program->pcr_pid = ((pmtData[8] & 0x1F) << 8) | pmtData[9];
        uint16_t program_info_length = ((pmtData[10] & 0x0F) << 8) | pmtData[11];
        
        TS_LOG("✅ PMT header: program=%u, version=%u, current=%d, PCR_PID=0x%04X, prog_info=%u",
               program_number, version, current_next, program->pcr_pid, program_info_length);
        
        if (section_length > size - 3) {
            TS_LOG("❌ PMT section length invalid: %u > %zu", section_length, size - 3);
            return false;
        }
        
        if (!current_next) {
            TS_LOG("⚠️ PMT not current, skipping");
            return true;
        }
        
        if (program_number != program->program_number) {
            TS_LOG("❌ PMT program number mismatch: %u != %u", program_number, program->program_number);
            return false;
        }
        
        // Skip program descriptors
        if (program_info_length > 0) {
            TS_LOG("🔧 Skipping %u bytes of program descriptors", program_info_length);
        }
        
        // Parse elementary streams
        const uint8_t* streams_data = pmtData + 12 + program_info_length;
        size_t streams_size = section_length - 13 - program_info_length; // Exclude CRC
        
        TS_LOG("🔍 Parsing %zu bytes of elementary stream data", streams_size);
        
        int streamCount = 0;
        for (size_t i = 0; i + 5 <= streams_size; ) {
            uint8_t stream_type = streams_data[i];
            uint16_t elementary_pid = ((streams_data[i + 1] & 0x1F) << 8) | streams_data[i + 2];
            uint16_t es_info_length = ((streams_data[i + 3] & 0x0F) << 8) | streams_data[i + 4];
            
            streamCount++;
            
            // Identify stream type
            const char* streamTypeName = getStreamTypeName(stream_type);
            
            TS_LOG("📺 Elementary Stream #%d: PID=0x%04X, Type=0x%02X (%s), ES_info=%u",
                   streamCount, elementary_pid, stream_type, streamTypeName, es_info_length);
            
            // Add stream to program
            program->addStream(elementary_pid, stream_type);
            
            // Skip ES descriptors
            if (es_info_length > 0) {
                TS_LOG("🔧 Skipping %u bytes of ES descriptors for PID 0x%04X",
                       es_info_length, elementary_pid);
            }
            
            i += 5 + es_info_length;
        }
        
        TS_LOG("✅ PMT processed successfully: %d elementary streams added to program %u",
               streamCount, program->program_number);
        
        return true;
    }
    
    const char* getStreamTypeName(uint8_t stream_type) {
        switch (stream_type) {
            case 0x01: return "MPEG-1 Video";
            case 0x02: return "MPEG-2 Video";
            case 0x03: return "MPEG-1 Audio";
            case 0x04: return "MPEG-2 Audio";
            case 0x06: return "Private PES";
            case 0x0F: return "AAC Audio";
            case 0x11: return "AAC-LATM Audio";
            case 0x1B: return "H.264 Video";
            case 0x24: return "HEVC Video";
            case 0x42: return "CAVS Video";
            case 0xD1: return "Dirac Video";
            case 0xEA: return "VC-1 Video";
            default:   return "Unknown";
        }
    }
    VLCTSStream* findStreamForPID(uint16_t pid) {
        for (auto& prog_pair : programs) {
            VLCTSStream* stream = prog_pair.second->getStream(pid);
            if (stream) return stream;
        }
        return nullptr;
    }
    
    bool processPES(const VLCTSHeader& header, const uint8_t* payload, size_t size, VLCTSStream* stream) {
        if (!stream) return false;
        
        uint16_t pid = header.pid;
        bool payloadStart = header.payload_unit_start;
        
        TS_LOG("processPES PID=0x%04X, size=%zu, payloadStart=%d", pid, size, payloadStart);
        
        if (payloadStart) {
            TS_LOG("🆕 NEW PES packet start on PID 0x%04X", pid);
            
            // Complete any frame in progress first
            if (mFrameInProgress[pid] && !mFrameBuffers[pid].empty()) {
                TS_LOG("📦 Completing previous frame: %zu bytes", mFrameBuffers[pid].size());
                processCompleteFrame(mFrameBuffers[pid].data(), mFrameBuffers[pid].size(),
                                     pid, mFrameTimestamp[pid], mFrameIsKeyframe[pid]);
                
                // Clear for new frame
                mFrameBuffers[pid].clear();
                mFrameInProgress[pid] = false;
            }
            
            // Parse the new PES packet
            if (size >= 9 && payload[0] == 0x00 && payload[1] == 0x00 && payload[2] == 0x01) {
                VLCPESHeader pesHeader;
                parsePESHeaderInfo(payload, size, pesHeader);
                
                const uint8_t* h264Data = nullptr;
                size_t h264Size = 0;
                if (parsePESHeader(payload, size, &h264Data, &h264Size)) {
                    TS_LOG("✅ New PES frame: %zu bytes H.264 data, PTS=%llu", h264Size, pesHeader.pts);
                    
                    // Start new frame - but check if it's complete first
                    bool isComplete = frameAppearsComplete(h264Data, h264Size);
                    bool isKeyframe = checkIfKeyframe(h264Data, h264Size);
                    
                    double timestamp = pesHeader.pts != 0 ? (double)pesHeader.pts / 90000.0 : getCurrentTimestamp();
                    
                    if (isComplete) {
                        // Complete frame - process immediately
                        TS_LOG("✅ Complete frame in single PES packet, processing immediately");
                        processCompleteFrame(h264Data, h264Size, pid, timestamp, isKeyframe);
                        
                        // No need to buffer this frame
                        mFrameInProgress[pid] = false;
                    } else {
                        // Incomplete frame - start buffering for continuation packets
                        TS_LOG("🔄 Incomplete frame, waiting for continuation packets");
                        mFrameBuffers[pid].assign(h264Data, h264Data + h264Size);
                        mFrameInProgress[pid] = true;
                        mFrameTimestamp[pid] = timestamp;
                        mFrameIsKeyframe[pid] = isKeyframe;
                    }
                    
                    return true;
                } else {
                    TS_LOG("❌ Failed to parse PES header");
                    return false;
                }
            } else {
                TS_LOG("❌ Invalid PES packet format");
                return false;
            }
        } else {
            // CONTINUATION PACKET - append to current frame (if one is in progress)
            TS_LOG("➕ Continuation packet for PID 0x%04X: %zu bytes", pid, size);
            
            if (mFrameInProgress[pid]) {
                // Append to current frame
                size_t oldSize = mFrameBuffers[pid].size();
                mFrameBuffers[pid].insert(mFrameBuffers[pid].end(), payload, payload + size);
                
                TS_LOG("📈 Extending current frame: %zu -> %zu bytes", oldSize, mFrameBuffers[pid].size());
                
                // Check if we should process the extended frame
                if (shouldProcessExtendedFrame(mFrameBuffers[pid].size(), pid)) {
                    TS_LOG("✅ Extended frame ready: %zu bytes", mFrameBuffers[pid].size());
                    
                    processCompleteFrame(mFrameBuffers[pid].data(), mFrameBuffers[pid].size(),
                                         pid, mFrameTimestamp[pid], mFrameIsKeyframe[pid]);
                    
                    mFrameBuffers[pid].clear();
                    mFrameInProgress[pid] = false;
                }
            } else {
                // No frame in progress - this is orphaned continuation data
                TS_LOG("⚠️ Orphaned continuation packet (no frame in progress) - discarding");
                // Don't process orphaned continuation data as it has no context
            }
        }
        
        return true;
    }
    
    void submitAVCCToVideoRingBufferWithTiming(const uint8_t* avccData, size_t avccSize, uint16_t pid, double cts, double dts) {
        if (!SceneDelegate.videoRingBuffer) {
            TS_LOG("❌ No video ring buffer available");
            return;
        }
        
        TS_LOG("🎬 Submitting AVCC data: %zu bytes, CTS=%.3f, DTS=%.3f", avccSize, cts, dts);
        
        bool isKeyframe = false;
        bool foundNewSPS = false;
        
        // Analyze AVCC data for keyframes and SPS
        size_t pos = 0;
        int nalCount = 0;
        
        while (pos + 4 < avccSize) {
            uint32_t nalLength = (avccData[pos] << 24) | (avccData[pos+1] << 16) |
            (avccData[pos+2] << 8) | avccData[pos+3];
            
            if (nalLength == 0 || nalLength > avccSize - pos - 4 || nalLength > 1024*1024) {
                TS_LOG("⚠️ Invalid NAL length: %u at position %zu", nalLength, pos);
                break;
            }
            
            const uint8_t* nalData = avccData + pos + 4;
            uint8_t nalType = nalData[0] & 0x1F;
            nalCount++;
            
            TS_LOG("🧬 NAL #%d: type=%d, length=%u", nalCount, nalType, nalLength);
            
            // Check for keyframe (IDR slice or SPS)
            if (nalType == 5 || nalType == 7) {
                isKeyframe = true;
            }
            
            // Cache SPS data when found (NAL type 7)
            if (nalType == 7 && nalLength >= 4) {
                // Check if this is a new SPS
                if (!mCachedSPS.valid ||
                    mCachedSPS.spsData.size() != nalLength ||
                    memcmp(mCachedSPS.spsData.data(), nalData, nalLength) != 0) {
                    
                    TS_LOG("🔧 New SPS detected in AVCC data, updating cache...");
                    mCachedSPS.updateFromSPS(nalData, nalLength);
                    foundNewSPS = true;
                }
            }
            
            pos += 4 + nalLength;
        }
        
        // Get video parameters from cached SPS
        uint32_t videoWidth = mCachedSPS.valid ? mCachedSPS.width : 640;
        uint32_t videoHeight = mCachedSPS.valid ? mCachedSPS.height : 480;
        double extractedFPS = mCachedSPS.valid ? mCachedSPS.getFPS() : 30.0;
        double frameDuration = mCachedSPS.valid ? mCachedSPS.frameDuration : (1.0/30.0);
        
        if (!mCachedSPS.valid) {
            TS_LOG("⚠️ No SPS cached yet, using defaults: 640x480 @ 30fps");
        } else if (foundNewSPS) {
            TS_LOG("✅ Using updated SPS: %ux%u @ %.2f fps", videoWidth, videoHeight, extractedFPS);
        }
        
        // Create frame info
        VT_FrameInfo frameInfo;
        memset(&frameInfo, 0, sizeof(frameInfo));
        frameInfo.magic = VT_MAGIC;
        frameInfo.sequence = nextSequenceNumber++;
        frameInfo.isKeyFrame = isKeyframe;
        frameInfo.cts = cts;
        frameInfo.dts = dts;
        frameInfo.duration = frameDuration;
        frameInfo.fps = extractedFPS;
        frameInfo.width = videoWidth;
        frameInfo.height = videoHeight;
        frameInfo.timeScale = 90000;
        frameInfo.spSize = 0;
        frameInfo.ppSize = 0;
        frameInfo.size = sizeof(VT_FrameInfo) + avccSize;
        
        // Pack AVCC data directly into ring buffer
        uint8_t* frameBuffer = (uint8_t*)malloc(frameInfo.size);
        if (!frameBuffer) {
            TS_LOG("❌ Failed to allocate frame buffer");
            return;
        }
        
        memcpy(frameBuffer, &frameInfo, sizeof(VT_FrameInfo));
        memcpy(frameBuffer + sizeof(VT_FrameInfo), avccData, avccSize);
        
        // Wait for space and write
        while (SceneDelegate.videoRingBuffer->FreeSpace() < frameInfo.size) {
            pthread_yield();
        }
        
        SceneDelegate.videoRingBuffer->WriteData(frameBuffer, frameInfo.size);
        
        TS_LOG("✅ AVCC frame queued: seq=%u, %zu bytes, keyframe=%s, %ux%u @ %.2f fps",
               frameInfo.sequence, avccSize, isKeyframe ? "YES" : "NO",
               videoWidth, videoHeight, extractedFPS);
        
        free(frameBuffer);
    }
    
    void processAVCCData(const uint8_t* avccData, size_t avccSize, uint16_t pid) {
        TS_LOG("🎬 Processing AVCC H.264 data: PID=0x%04X, %zu bytes", pid, avccSize);
        
        if (!avccData || avccSize < 5) {
            TS_LOG("❌ Invalid AVCC data");
            return;
        }
        
        // Extract timing info (normalize timestamps if available)
        double cts = 0.0;
        double dts = 0.0;
        
        // If we have cached PTS/DTS from PES header parsing, use timestamp normalizer
        VLCTSStream* stream = findStreamForPID(pid);
        if (stream && (stream->last_pts != 0 || stream->last_dts != 0)) {
            double frameDuration = mCachedSPS.valid ? mCachedSPS.frameDuration : (1.0/30.0);
            auto normalizedTime = mTimestampNormalizer.normalize(stream->last_pts, stream->last_dts, frameDuration);
            cts = normalizedTime.first;
            dts = normalizedTime.second;
            
            TS_LOG("🕐 Using normalized timing: CTS=%.3f, DTS=%.3f", cts, dts);
        } else {
            // Fallback to frame-based timing
            cts = getCurrentTimestamp();
            dts = cts;
            
            TS_LOG("🕐 Using fallback timing: CTS=%.3f", cts);
        }
        
        // Queue AVCC data directly to video ring buffer
        submitAVCCToVideoRingBufferWithTiming(avccData, avccSize, pid, cts, dts);
        
        // OPTIONAL: Call video callback for compatibility with existing code
        if (video_callback) {
            VLCPESHeader dummyHeader;
            memset(&dummyHeader, 0, sizeof(dummyHeader));
            dummyHeader.stream_id = 0xE0; // Video stream
            dummyHeader.pts = stream ? stream->last_pts : 0;
            dummyHeader.dts = stream ? stream->last_dts : 0;
            
            TS_LOG("📹 Calling video callback with AVCC data");
            video_callback(pid, avccData, avccSize, dummyHeader);
        }
        
        TS_LOG("✅ AVCC H.264 frame processed and queued");
    }
    void submitH264ToVideoRingBufferWithTiming(const uint8_t* h264Data, size_t h264Size,
                                             uint16_t pid, double cts, double dts) {
        if (!SceneDelegate.videoRingBuffer || !h264Data || h264Size < 4) {
            TS_LOG("❌ Invalid input for H.264 submission");
            return;
        }
        
        TS_LOG("🎬 Submitting H.264 data: %zu bytes, CTS=%.3f, DTS=%.3f", h264Size, cts, dts);
        
        bool isKeyframe = false;
        bool foundNewSPS = false;
        std::vector<uint8_t> avccData;
        
        // Convert to AVCC format if needed
        if (isAVCCFormat(h264Data, h264Size)) {
            TS_LOG("✅ Data already in AVCC format");
            avccData.assign(h264Data, h264Data + h264Size);
            analyzeAVCCData(h264Data, h264Size, isKeyframe, foundNewSPS);
        } else {
            TS_LOG("🔧 Converting Annex B to AVCC format");
            if (!convertAnnexBToAVCC(h264Data, h264Size, avccData)) {
                TS_LOG("❌ Failed to convert H.264 to AVCC format");
                return;
            }
            analyzeH264Data(h264Data, h264Size);
            
            // Re-analyze in AVCC format for keyframe detection
            analyzeAVCCData(avccData.data(), avccData.size(), isKeyframe, foundNewSPS);
        }
        
        // Get video parameters from cached SPS
        uint32_t videoWidth = mCachedSPS.valid ? mCachedSPS.width : 640;
        uint32_t videoHeight = mCachedSPS.valid ? mCachedSPS.height : 480;
        double extractedFPS = mCachedSPS.valid ? mCachedSPS.getFPS() : 30.0;
        double frameDuration = mCachedSPS.valid ? mCachedSPS.frameDuration : (1.0/30.0);
        
        // Create frame info
        VT_FrameInfo frameInfo;
        memset(&frameInfo, 0, sizeof(frameInfo));
        frameInfo.magic = VT_MAGIC;
        frameInfo.sequence = nextSequenceNumber++;
        frameInfo.isKeyFrame = isKeyframe;
        frameInfo.cts = cts;
        frameInfo.dts = dts;
        frameInfo.duration = frameDuration;
        frameInfo.fps = extractedFPS;
        frameInfo.width = videoWidth;
        frameInfo.height = videoHeight;
        frameInfo.timeScale = 90000;
        frameInfo.size = (uint32_t)(sizeof(VT_FrameInfo) + avccData.size());
        
        // Allocate and pack frame buffer
        uint8_t* frameBuffer = (uint8_t*)malloc(frameInfo.size);
        if (!frameBuffer) {
            TS_LOG("❌ Failed to allocate frame buffer");
            return;
        }
        
        memcpy(frameBuffer, &frameInfo, sizeof(VT_FrameInfo));
        memcpy(frameBuffer + sizeof(VT_FrameInfo), avccData.data(), avccData.size());
        
        // Submit to ring buffer
        while (SceneDelegate.videoRingBuffer->FreeSpace() < frameInfo.size) {
            pthread_yield();
        }
        
        SceneDelegate.videoRingBuffer->WriteData(frameBuffer, frameInfo.size);
        
        TS_LOG("✅ H.264 frame queued: seq=%u, %zu bytes, keyframe=%s, %ux%u @ %.2f fps",
               frameInfo.sequence, avccData.size(), isKeyframe ? "YES" : "NO",
               videoWidth, videoHeight, extractedFPS);
        
        free(frameBuffer);
    }
    
    void processH264FrameWithTiming(const uint8_t* h264Data, size_t h264Size,
                                   uint16_t pid, uint64_t pts, uint64_t dts) {
        if (!h264Data || h264Size < 4) {
            TS_LOG("❌ Invalid H.264 frame data");
            return;
        }
        
        // Convert timestamps to seconds
        double cts = pts != 0 ? (double)pts / 90000.0 : getCurrentTimestamp();
        double dt = dts != 0 ? (double)dts / 90000.0 : cts;
        
        TS_LOG("🎬 Processing H.264 frame: PID=0x%04X, %zu bytes, PTS=%llu, DTS=%llu",
               pid, h264Size, pts, dts);
        
        // Submit to ring buffer
        submitH264ToVideoRingBufferWithTiming(h264Data, h264Size, pid, cts, dt);
        
        // Optional: Call video callback for compatibility
        if (video_callback) {
            VLCPESHeader header;
            memset(&header, 0, sizeof(header));
            header.stream_id = 0xE0;
            header.pts = pts;
            header.dts = dts;
            
            video_callback(pid, h264Data, h264Size, header);
        }
    }
    
    void analyzeAVCCData(const uint8_t* avccData, size_t avccSize, bool& isKeyframe, bool& foundNewSPS) {
        isKeyframe = false;
        foundNewSPS = false;
        
        size_t pos = 0;
        while (pos + 4 < avccSize) {
            uint32_t nalLength = (avccData[pos] << 24) | (avccData[pos+1] << 16) |
            (avccData[pos+2] << 8) | avccData[pos+3];
            
            if (nalLength == 0 || nalLength > avccSize - pos - 4) break;
            
            const uint8_t* nalData = avccData + pos + 4;
            uint8_t nalType = nalData[0] & 0x1F;
            
            if (nalType == 5 || nalType == 7) isKeyframe = true;
            
            if (nalType == 7) { // SPS
                if (!mCachedSPS.valid ||
                    mCachedSPS.spsData.size() != nalLength ||
                    memcmp(mCachedSPS.spsData.data(), nalData, nalLength) != 0) {
                    
                    mCachedSPS.updateFromSPS(nalData, nalLength);
                    foundNewSPS = true;
                }
            }
            
            pos += 4 + nalLength;
        }
    }
    
    void processAccumulatedData(const uint8_t* data, size_t size, uint16_t pid) {
        TS_LOG("🎯 processAccumulatedData PID=0x%04X, size=%zu", pid, size);
        
        if (!data || size < 4) {
            TS_LOG("❌ Invalid accumulated data: %zu bytes", size);
            return;
        }
        if (pid == 0x0100) {
            TS_LOG("🔍 DETAILED DEBUG for PID 0x0100:");
            
            // Show first 64 bytes in hex
            TS_LOG("🔍 First 64 bytes:");
            for (int i = 0; i < std::min((int)size, 64); i += 16) {
                char hexLine[200] = {0};
                char* p = hexLine;
                p += sprintf(p, "[%04X] ", i);
                
                for (int j = 0; j < 16 && i + j < size; j++) {
                    p += sprintf(p, "%02X ", data[i + j]);
                }
                TS_LOG("%s", hexLine);
            }
            
            // Search for ANY valid patterns in the entire buffer
            TS_LOG("🔍 Scanning entire buffer for patterns...");
            
            // 1. Look for PES headers anywhere
            bool foundPES = false;
            for (size_t i = 0; i < size - 3; i++) {
                if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01) {
                    uint8_t streamId = (i + 3 < size) ? data[i + 3] : 0;
                    TS_LOG("🔍 Found PES header at offset %zu: stream_id=0x%02X", i, streamId);
                    foundPES = true;
                    if (foundPES) break; // Just find first one
                }
            }
            
            // 2. Look for H.264 start codes anywhere
            bool foundH264 = false;
            for (size_t i = 0; i < size - 4; i++) {
                if (data[i] == 0x00 && data[i+1] == 0x00) {
                    if (data[i+2] == 0x01 && i+3 < size) {
                        uint8_t nalType = data[i+3] & 0x1F;
                        if (nalType >= 1 && nalType <= 31) {
                            TS_LOG("🔍 Found H.264 3-byte start code at offset %zu: NAL type %d", i, nalType);
                            foundH264 = true;
                        }
                    } else if (data[i+2] == 0x00 && i+4 < size && data[i+3] == 0x01) {
                        uint8_t nalType = data[i+4] & 0x1F;
                        if (nalType >= 1 && nalType <= 31) {
                            TS_LOG("🔍 Found H.264 4-byte start code at offset %zu: NAL type %d", i, nalType);
                            foundH264 = true;
                        }
                    }
                }
            }
            
            // 3. Look for AVCC patterns (reasonable lengths)
            bool foundAVCC = false;
            for (size_t i = 0; i < size - 5; i++) {
                uint32_t len = (data[i] << 24) | (data[i+1] << 16) | (data[i+2] << 8) | data[i+3];
                if (len > 0 && len < 65536 && len < size - i - 4) { // Reasonable length
                    uint8_t nalType = data[i+4] & 0x1F;
                    if (nalType >= 1 && nalType <= 31) {
                        TS_LOG("🔍 Found AVCC pattern at offset %zu: length=%u, NAL type %d", i, len, nalType);
                        foundAVCC = true;
                        break;
                    }
                }
            }
            
            // 4. Check data entropy
            int uniqueBytes = 0;
            bool seen[256] = {false};
            for (size_t i = 0; i < std::min(size, (size_t)256); i++) {
                if (!seen[data[i]]) {
                    seen[data[i]] = true;
                    uniqueBytes++;
                }
            }
            
            TS_LOG("🔍 Data analysis summary:");
            TS_LOG("    - PES headers found: %s", foundPES ? "YES" : "NO");
            TS_LOG("    - H.264 start codes found: %s", foundH264 ? "YES" : "NO");
            TS_LOG("    - AVCC patterns found: %s", foundAVCC ? "YES" : "NO");
            TS_LOG("    - Data entropy: %d/256 unique bytes", uniqueBytes);
            
            if (!foundPES && !foundH264 && !foundAVCC) {
                TS_LOG("❌ NO VALID PATTERNS FOUND - Data may be corrupted or encrypted");
                
                // Last resort: try to process anyway if entropy is reasonable
                if (uniqueBytes > 50) {
                    TS_LOG("🔧 Reasonable entropy detected, attempting raw processing...");
                    // processRawVideoData(data, size, pid);
                    return;
                }
            }
        }
        // STRATEGY 1: Check if data starts with PES header
        if (data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
            TS_LOG("✅ Processing as PES packet");
            processCompletePESPacket(data, size, pid);
            return;
        }
        
        if (isAVCCFormat(data, size)) {
            TS_LOG("✅ Processing as AVCC H.264 data");
            processAVCCData(data, size, pid);
            return;
        }
        
        // STRATEGY 2: Check if data starts with H.264 NAL units
        if (isH264Data(data, size)) {
            TS_LOG("✅ Processing as raw H.264 data");
            processRawH264Data(data, size, pid);
            return;
        }
        
        // STRATEGY 3: Check if data contains H.264 somewhere in the middle
        for (size_t i = 0; i < std::min(size, (size_t)64); i++) {
            if (i + 4 <= size && data[i] == 0x00 && data[i+1] == 0x00 &&
                (data[i+2] == 0x01 || (data[i+2] == 0x00 && data[i+3] == 0x01))) {
                
                TS_LOG("✅ Found H.264 start code at offset %zu, processing as raw H.264", i);
                processRawH264Data(data + i, size - i, pid);
                return;
            }
        }
        
        TS_LOG("❌ Unable to identify data format - first bytes: %02X %02X %02X %02X",
               data[0], data[1], data[2], data[3]);
    }
    
    bool isH264Data(const uint8_t* data, size_t size) {
        if (!data || size < 4) return false;
        
        // Check for H.264 start codes (00 00 01 or 00 00 00 01)
        for (size_t i = 0; i < std::min(size, (size_t)16); i++) {
            if (data[i] == 0x00 && i + 3 < size && data[i+1] == 0x00) {
                if (data[i+2] == 0x01) {
                    // Found 3-byte start code
                    if (i + 3 < size) {
                        uint8_t nalType = data[i+3] & 0x1F;
                        if (nalType >= 1 && nalType <= 31) {
                            TS_LOG("🔍 Found H.264 NAL type %d at offset %zu", nalType, i);
                            return true;
                        }
                    }
                } else if (data[i+2] == 0x00 && i + 4 < size && data[i+3] == 0x01) {
                    // Found 4-byte start code
                    if (i + 4 < size) {
                        uint8_t nalType = data[i+4] & 0x1F;
                        if (nalType >= 1 && nalType <= 31) {
                            TS_LOG("🔍 Found H.264 NAL type %d at offset %zu", nalType, i);
                            return true;
                        }
                    }
                }
            }
        }
        
        return false;
    }
    
    void processRawH264Data(const uint8_t* h264Data, size_t h264Size, uint16_t pid) {
        TS_LOG("🎬 Processing raw H.264 data: PID=0x%04X, %zu bytes", pid, h264Size);
        
        // Create a dummy PES header for callback compatibility
        VLCPESHeader dummyHeader;
        memset(&dummyHeader, 0, sizeof(dummyHeader));
        dummyHeader.stream_id = 0xE0; // Video stream
        dummyHeader.pts = 0; // No timing info for continuation data
        dummyHeader.dts = 0;
        
        // Analyze H.264 content
        analyzeH264Data(h264Data, h264Size);
        
        // Trigger video callback with raw H.264 data
        if (video_callback) {
            TS_LOG("📹 Calling video callback for raw H.264 data");
            video_callback(pid, h264Data, h264Size, dummyHeader);
        } else {
            TS_LOG("❌ No video callback set for raw H.264 data");
        }
    }
    
    void processCompletePESPacket(const uint8_t* pesData, size_t pesSize, uint16_t pid) {
        TS_LOG("🎯 processCompletePESPacket PID=0x%04X, size=%zu", pid, pesSize);
        
        if (pesSize < 9) {
            TS_LOG("❌ PES packet too small: %zu bytes", pesSize);
            return;
        }
        
        if (pesData[0] != 0x00 || pesData[1] != 0x00 || pesData[2] != 0x01) {
            TS_LOG("❌ Invalid PES start code: %02X %02X %02X", pesData[0], pesData[1], pesData[2]);
            return;
        }
        
        uint8_t streamId = pesData[3];
        TS_LOG("📺 PES stream ID: 0x%02X", streamId);
        
        // Video stream (0xE0-0xEF)
        if (streamId >= 0xE0 && streamId <= 0xEF) {
            TS_LOG("🎬 Processing VIDEO PES packet: PID=0x%04X, streamId=0x%02X", pid, streamId);
            
            const uint8_t* h264Data = nullptr;
            size_t h264Size = 0;
            VLCPESHeader pesHeader;
            
            if (parsePESHeader(pesData, pesSize, &h264Data, &h264Size)) {
                parsePESHeaderInfo(pesData, pesSize, pesHeader);
                
                TS_LOG("✅ Extracted H.264 data: %zu bytes, PTS=%llu", h264Size, pesHeader.pts);
                
                // ENHANCED: Analyze H.264 data for frame boundaries
                analyzeH264Data(h264Data, h264Size);
                
                // Trigger video callback
                if (video_callback) {
                    TS_LOG("📹 Calling video callback");
                    video_callback(pid, h264Data, h264Size, pesHeader);
                } else {
                    TS_LOG("❌ No video callback set");
                }
            } else {
                TS_LOG("❌ Failed to parse PES header for video");
            }
        }
        // Audio stream (0xC0-0xDF, 0xBD)
        else if ((streamId >= 0xC0 && streamId <= 0xDF) || streamId == 0xBD) {
            TS_LOG("🔊 Processing AUDIO PES packet: PID=0x%04X, streamId=0x%02X", pid, streamId);
            
            if (audio_callback) {
                VLCPESHeader header;
                parsePESHeaderInfo(pesData, pesSize, header);
                
                TS_LOG("✅ Calling audio callback with %zu bytes", pesSize);
                audio_callback(pid, pesData, pesSize, header);
            } else {
                TS_LOG("❌ No audio callback set");
            }
        } else {
            TS_LOG("❓ Unknown stream ID: 0x%02X", streamId);
        }
    }
    
    void analyzeH264Data(const uint8_t* h264Data, size_t h264Size) {
        if (!h264Data || h264Size < 4) return;
        
        int nalUnits = 0;
        int keyframes = 0;
        int pframes = 0;
        
        // Count NAL units and frame types
        for (size_t i = 0; i < h264Size - 4; i++) {
            if (h264Data[i] == 0x00 && h264Data[i+1] == 0x00 &&
                (h264Data[i+2] == 0x01 || (h264Data[i+2] == 0x00 && h264Data[i+3] == 0x01))) {
                
                size_t nalStart = (h264Data[i+2] == 0x01) ? i + 3 : i + 4;
                if (nalStart < h264Size) {
                    uint8_t nalType = h264Data[nalStart] & 0x1F;
                    nalUnits++;
                    
                    if (nalType == 5) keyframes++;
                    else if (nalType == 1) pframes++;
                }
            }
        }
        
        TS_LOG("🧬 H.264 Analysis: %d NAL units, %d keyframes, %d P-frames",
               nalUnits, keyframes, pframes);
    }
    
    void cleanupOversizedBuffers() {
        for (auto& buffer_pair : mPESBuffers) {
            uint16_t pid = buffer_pair.first;
            auto& buffer = buffer_pair.second;
            
            if (buffer.size() > 32768) { // 32KB limit
                TS_LOG("🧹 Cleaning oversized buffer for PID 0x%04X: %zu bytes", pid, buffer.size());
                buffer.clear();
                mPESPacketCounts[pid] = 0;
                mPESHeaderParsed[pid] = false;
                mPESExpectedSize[pid] = 0;
            }
        }
    }
    // Helper methods
    bool parsePESHeader(const uint8_t* pesData, size_t pesSize,
                        const uint8_t** h264Data, size_t* h264Size) {
        if (pesSize < 9) return false;
        
        uint8_t pesHeaderDataLength = pesData[8];
        size_t h264Offset = 9 + pesHeaderDataLength;
        
        if (h264Offset >= pesSize) return false;
        
        *h264Data = pesData + h264Offset;
        *h264Size = pesSize - h264Offset;
        
        return true;
    }
    
    // Enhanced PES header parsing with full timing extraction
    void parsePESHeaderInfo(const uint8_t* data, size_t size, VLCPESHeader& header) {
        // Initialize header
        memset(&header, 0, sizeof(header));
        
        if (size < 6) return;
        
        header.stream_id = data[3];
        header.packet_length = (data[4] << 8) | data[5];
        
        if (size < 9) return;
        
        uint8_t flags1 = data[6];
        uint8_t flags2 = data[7];
        header.header_data_length = data[8];
        
        header.scrambling_control = (flags1 & 0x30) >> 4;
        header.priority = (flags1 & 0x08) != 0;
        header.data_alignment = (flags1 & 0x04) != 0;
        header.copyright = (flags1 & 0x02) != 0;
        header.original_or_copy = (flags1 & 0x01) != 0;
        
        header.pts_dts_flags = (flags2 & 0xC0) >> 6;
        header.escr_flag = (flags2 & 0x20) != 0;
        header.es_rate_flag = (flags2 & 0x10) != 0;
        header.dsm_trick_mode_flag = (flags2 & 0x08) != 0;
        header.additional_copy_info_flag = (flags2 & 0x04) != 0;
        header.crc_flag = (flags2 & 0x02) != 0;
        header.extension_flag = (flags2 & 0x01) != 0;
        
        // Parse PTS/DTS with enhanced extraction
        const uint8_t* pts_dts_data = data + 9;
        size_t remaining = size - 9;
        
        // Extract PTS (Presentation Time Stamp / CTS)
        if ((header.pts_dts_flags == 0x02 || header.pts_dts_flags == 0x03) && remaining >= 5) {
            // PTS present - extract 33-bit timestamp
            header.pts = ((uint64_t)(pts_dts_data[0] & 0x0E) << 29) |
            ((uint64_t)pts_dts_data[1] << 22) |
            ((uint64_t)(pts_dts_data[2] & 0xFE) << 14) |
            ((uint64_t)pts_dts_data[3] << 7) |
            ((uint64_t)(pts_dts_data[4] & 0xFE) >> 1);
            
            pts_dts_data += 5;
            remaining -= 5;
            
            //TS_LOG("🕐 Extracted PTS: %llu (%.3f seconds)", header.pts, (double)header.pts / 90000.0);
        }
        
        // Extract DTS (Decode Time Stamp) if present
        if (header.pts_dts_flags == 0x03 && remaining >= 5) {
            // DTS present - extract 33-bit timestamp
            header.dts = ((uint64_t)(pts_dts_data[0] & 0x0E) << 29) |
            ((uint64_t)pts_dts_data[1] << 22) |
            ((uint64_t)(pts_dts_data[2] & 0xFE) << 14) |
            ((uint64_t)pts_dts_data[3] << 7) |
            ((uint64_t)(pts_dts_data[4] & 0xFE) >> 1);
            
            // TS_LOG("🕐 Extracted DTS: %llu (%.3f seconds)", header.dts, (double)header.dts / 90000.0);
        } else if (header.pts_dts_flags == 0x02) {
            // Only PTS present, DTS = PTS for frames without B-frames
            header.dts = header.pts;
        }
        
        // Handle case where no timing info is present
        if (header.pts_dts_flags == 0x00) {
            TS_LOG("⚠️ No PTS/DTS in PES header - will use fallback timing");
            header.pts = 0;
            header.dts = 0;
        }
        
        // Log timing extraction results
        if (header.pts != 0 || header.dts != 0) {
            double pts_seconds = (double)header.pts / 90000.0;
            double dts_seconds = (double)header.dts / 90000.0;
            //  TS_LOG("✅ Timing extracted - PTS: %.3fs, DTS: %.3fs (flags=0x%02X)", \
            pts_seconds, dts_seconds, header.pts_dts_flags);
        }
    }
    
    double getCurrentTimestamp() {
        static double baseTimestamp = 0.0;
        static int frameCount = 0;
        
        if (baseTimestamp == 0.0) {
            baseTimestamp = CFAbsoluteTimeGetCurrent();
        }
        
        double timestamp = baseTimestamp + (frameCount * (1.0 / 30.0));
        frameCount++;
        
        return timestamp;
    }
    
    
    void validateH264Data(const uint8_t* data, size_t size, const char* source) {
        if (!data || size < 4) {
            return;
        }
        
        // Check for H.264 patterns
        checkH264Patterns(data, size);
        
        // Check for encryption/scrambling
        checkDataEntropy(data, size);
    }
    
    void checkH264Patterns(const uint8_t* data, size_t size) {
        bool foundStartCode = false;
        int startCodeCount = 0;
        
        // Look for start codes (00 00 00 01 or 00 00 01)
        for (size_t i = 0; i < size - 3; i++) {
            if (data[i] == 0x00 && data[i+1] == 0x00) {
                if (data[i+2] == 0x00 && i+3 < size && data[i+3] == 0x01) {
                    foundStartCode = true;
                    startCodeCount++;
                    
                    // Check NAL type after start code
                    if (i+4 < size) {
                        uint8_t nalType = data[i+4] & 0x1F;
                    }
                    i += 3; // Skip ahead
                } else if (data[i+2] == 0x01) {
                    foundStartCode = true;
                    startCodeCount++;
                    
                    if (i+3 < size) {
                        uint8_t nalType = data[i+3] & 0x1F;
                    }
                    i += 2; // Skip ahead
                }
            }
        }
        
        if (!foundStartCode) {
            checkAVCCFormat(data, size);
        }
    }
    
    void checkAVCCFormat(const uint8_t* data, size_t size) {
        if (size < 4) return;
        
        // Check if first 4 bytes could be a length header (big endian)
        uint32_t possibleLength = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        
        if (possibleLength > 0 && possibleLength < size - 4) {
            if (possibleLength + 4 <= size) {
                uint8_t nalType = data[4] & 0x1F;
            }
        }
    }
    
    void checkDataEntropy(const uint8_t* data, size_t size) {
        int histogram[256] = {0};
        
        // Count byte frequencies
        for (size_t i = 0; i < size; i++) {
            histogram[data[i]]++;
        }
        
        // Calculate simple entropy measure
        int uniqueBytes = 0;
        int maxCount = 0;
        for (int i = 0; i < 256; i++) {
            if (histogram[i] > 0) {
                uniqueBytes++;
                if (histogram[i] > maxCount) {
                    maxCount = histogram[i];
                }
            }
        }
        
        float uniformity = (float)maxCount / size;
        
        if (uniqueBytes > 200 && uniformity < 0.1) {
            // High entropy - possibly encrypted/compressed
        } else if (uniformity > 0.5) {
            // Low entropy - patterns/padding
        }
    }
    
    const char* nalTypeString(uint8_t nalType) {
        switch (nalType) {
            case 1: return "Non-IDR slice";
            case 2: return "Slice partition A";
            case 3: return "Slice partition B";
            case 4: return "Slice partition C";
            case 5: return "IDR slice";
            case 6: return "SEI";
            case 7: return "SPS";
            case 8: return "PPS";
            case 9: return "Access unit delimiter";
            case 10: return "End of sequence";
            case 11: return "End of stream";
            case 12: return "Filler data";
            default: return "Unknown/Reserved";
        }
    }
    
    void validateCompletePESPacket(const uint8_t* pesData, size_t pesSize, uint16_t pid) {
        if (pesSize < 9) return;
        
        // Check if this is a PES packet
        if (pesData[0] == 0x00 && pesData[1] == 0x00 && pesData[2] == 0x01) {
            uint8_t streamId = pesData[3];
            
            if (streamId >= 0xE0 && streamId <= 0xEF) {
                // Parse PES header to extract H.264 data
                const uint8_t* h264Data = nullptr;
                size_t h264Size = 0;
                
                if (parsePESHeader(pesData, pesSize, &h264Data, &h264Size)) {
                    validateH264Data(h264Data, h264Size, "Complete H.264 Frame");
                }
            }
        }
    }
    
public:
    
    // H.264 Pattern Detection and Validation Methods
    bool findH264Patterns(const uint8_t* data, size_t size) {
        if (!data || size < 8) return false;
        
        int nalUnitsFound = 0;
        
        for (size_t i = 0; i < size - 4; i++) {
            if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01) {
                nalUnitsFound++;
            } else if (i + 3 < size && data[i] == 0x00 && data[i+1] == 0x00 &&
                       data[i+2] == 0x00 && data[i+3] == 0x01) {
                nalUnitsFound++;
            }
            
            if (nalUnitsFound >= 2) {
                return true;
            }
        }
        
        return nalUnitsFound > 0;
    }
    
    bool extractNALUnitsFromRawData(const uint8_t* data, size_t size, std::vector<NALUnit>& nalUnits) {
        nalUnits.clear();
        
        size_t pos = 0;
        while (pos < size - 4) {
            size_t startCodeSize = 0;
            
            if (pos + 4 <= size && data[pos] == 0x00 && data[pos+1] == 0x00 &&
                data[pos+2] == 0x00 && data[pos+3] == 0x01) {
                startCodeSize = 4;
            } else if (pos + 3 <= size && data[pos] == 0x00 && data[pos+1] == 0x00 &&
                       data[pos+2] == 0x01) {
                startCodeSize = 3;
            } else {
                pos++;
                continue;
            }
            
            size_t nalStart = pos + startCodeSize;
            if (nalStart >= size) break;
            
            // Find next start code
            size_t nalEnd = size;
            for (size_t i = nalStart + 1; i < size - 3; i++) {
                if ((data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x01) ||
                    (i + 3 < size && data[i] == 0x00 && data[i+1] == 0x00 &&
                     data[i+2] == 0x00 && data[i+3] == 0x01)) {
                    nalEnd = i;
                    break;
                }
            }
            
            if (nalEnd > nalStart) {
                NALUnit nal;
                nal.offset = nalStart;
                nal.size = nalEnd - nalStart;
                nal.type = data[nalStart] & 0x1F;
                nal.isKeyFrame = (nal.type == 5);
                
                nalUnits.push_back(nal);
            }
            
            pos = nalEnd;
        }
        
        return !nalUnits.empty();
    }
    
    void reset() {
        programs.clear();
        continuity_counters.clear();
        mSegmentBuffer.clear();
        mPESBuffers.clear();
        mPESPacketCounts.clear();
        mPESHeaderParsed.clear();
        mPESExpectedSize.clear();
        
        total_packets = 0;
        sync_errors = 0;
        continuity_errors = 0;
        transport_errors = 0;
        current_pcr = 0;
        pcr_valid = false;
        
        // Reset YouTube-specific state
        mInSegmentTransition = false;
        mCurrentSyncLosses = 0;
        mPIDDiscontinuityFlags.clear();
        
        // Reset cached SPS info
        mCachedSPS.valid = false;
        mCachedSPS.spsData.clear();
        
        // Reset timestamp normalizer and timing stats
        mTimestampNormalizer.reset();
        mTimingStats = TimingStats(); // Reset to default values
        nextSequenceNumber = 1;
        
        start_time = std::chrono::steady_clock::now();
        
        TS_LOG("✅ VLC TS Demuxer fully reset - ready for new stream");
    }
    
    void printStats() {
        TS_LOG("Combined VLC TS Stats:");
        TS_LOG("  Total packets: %llu", total_packets);
        TS_LOG("  Sync errors: %llu", sync_errors);
        TS_LOG("  Continuity errors: %llu", continuity_errors);
        TS_LOG("  Transport errors: %llu", transport_errors);
        TS_LOG("  Programs: %zu", programs.size());
        TS_LOG("  YouTube sync losses: %d", mCurrentSyncLosses);
        
        // Enhanced stats with SPS info
        if (mCachedSPS.valid) {
            TS_LOG("  Video stream: %ux%u @ %.2f fps (profile=%u, level=%u)",
                   mCachedSPS.width, mCachedSPS.height, mCachedSPS.getFPS(),
                   mCachedSPS.profile, mCachedSPS.level);
        } else {
            TS_LOG("  Video stream: No SPS data cached");
        }
        
        // Timing statistics
        mTimingStats.printStats();
        
        for (auto& prog_pair : programs) {
            auto& program = prog_pair.second;
            TS_LOG("  Program %d: %zu streams", program->program_number, program->streams.size());
        }
    }
private:
    bool shouldProcessExtendedFrame(size_t frameSize, uint16_t pid) {
        // For extended frames, use more conservative thresholds
        
        // 1. Large frames are likely complete
        if (frameSize >= 8192) { // 8KB - definitely large enough
            return true;
        }
        
        // 2. Time-based processing (avoid holding frames too long)
        static std::map<uint16_t, std::chrono::steady_clock::time_point> frameStartTime;
        auto now = std::chrono::steady_clock::now();
        
        if (frameStartTime.find(pid) == frameStartTime.end()) {
            frameStartTime[pid] = now;
            return false;
        }
        
        // Process if frame has been accumulating for more than 100ms
        auto frameAge = std::chrono::duration_cast<std::chrono::milliseconds>(now - frameStartTime[pid]);
        if (frameAge.count() >= 100 && frameSize >= 2048) { // 100ms + reasonable size
            frameStartTime[pid] = now; // Reset timer
            return true;
        }
        
        // 3. Very large frames should definitely be processed
        if (frameSize >= 16384) { // 16KB - emergency processing
            frameStartTime[pid] = now; // Reset timer
            return true;
        }
        
        return false;
    }
    
    void processCompleteFrame(const uint8_t* frameData, size_t frameSize,
                              uint16_t pid, double timestamp, bool isKeyframe) {
        TS_LOG("🎬 Processing complete frame: PID=0x%04X, %zu bytes, keyframe=%s, timestamp=%.3f",
               pid, frameSize, isKeyframe ? "YES" : "NO", timestamp);
        
        if (!frameData || frameSize == 0) {
            TS_LOG("❌ Invalid frame data");
            return;
        }
        
        // Create PES header for callback compatibility
        VLCPESHeader header;
        memset(&header, 0, sizeof(header));
        header.stream_id = 0xE0;
        header.pts = (uint64_t)(timestamp * 90000.0);
        header.dts = header.pts;
        
        // Call video callback with complete frame
        if (video_callback) {
            TS_LOG("📹 Calling video callback with complete frame");
            video_callback(pid, frameData, frameSize, header);
        } else {
            TS_LOG("❌ No video callback set");
        }
    }
    bool checkIfKeyframe(const uint8_t* data, size_t size) {
        if (!data || size < 4) return false;
        
        for (size_t i = 0; i < size - 4; i++) {
            if (data[i] == 0x00 && data[i+1] == 0x00) {
                if (data[i+2] == 0x01 || (data[i+2] == 0x00 && i+3 < size && data[i+3] == 0x01)) {
                    size_t nalStart = (data[i+2] == 0x01) ? i + 3 : i + 4;
                    if (nalStart < size) {
                        uint8_t nalType = data[nalStart] & 0x1F;
                        if (nalType == 5 || nalType == 7) { // IDR slice or SPS
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }
    
    bool frameAppearsComplete(const uint8_t* data, size_t size) {
        if (!data || size < 10) return false;
        
        int nalCount = 0;
        bool hasIDR = false;
        bool hasSPS = false;
        bool hasPPS = false;
        bool hasAUD = false;
        
        // Count NAL units and identify types
        for (size_t i = 0; i < size - 4; i++) {
            if (data[i] == 0x00 && data[i+1] == 0x00) {
                if (data[i+2] == 0x01 || (data[i+2] == 0x00 && i+3 < size && data[i+3] == 0x01)) {
                    nalCount++;
                    
                    size_t nalStart = (data[i+2] == 0x01) ? i + 3 : i + 4;
                    if (nalStart < size) {
                        uint8_t nalType = data[nalStart] & 0x1F;
                        switch(nalType) {
                            case 5: hasIDR = true; break;
                            case 7: hasSPS = true; break;
                            case 8: hasPPS = true; break;
                            case 9: hasAUD = true; break;
                        }
                    }
                }
            }
        }
        
        // A complete keyframe should have: AUD + SPS + PPS + IDR slice
        // A complete P-frame might just have: AUD + P-slice
        bool isCompleteKeyframe = hasAUD && hasSPS && hasPPS && hasIDR;
        bool isCompletePFrame = hasAUD && nalCount >= 2; // AUD + slice
        
        bool complete = isCompleteKeyframe || isCompletePFrame;
        
        TS_LOG("🔍 Frame completeness: %d NALs (AUD=%s, SPS=%s, PPS=%s, IDR=%s) -> %s",
               nalCount, hasAUD ? "Y" : "N", hasSPS ? "Y" : "N",
               hasPPS ? "Y" : "N", hasIDR ? "Y" : "N",
               complete ? "COMPLETE" : "INCOMPLETE");
        
        return complete;
    }
    
    void handleNextPESPacket(uint16_t pid) {
        // This is called when a new PES packet starts
        // Complete any frame still in progress before processing new PES
        if (mFrameInProgress[pid] && !mFrameBuffers[pid].empty()) {
            TS_LOG("🔚 Forcing completion of frame due to new PES: %zu bytes", mFrameBuffers[pid].size());
            
            processCompleteFrame(mFrameBuffers[pid].data(), mFrameBuffers[pid].size(),
                                 pid, mFrameTimestamp[pid], mFrameIsKeyframe[pid]);
            
            mFrameBuffers[pid].clear();
            mFrameInProgress[pid] = false;
        }
    }
};
