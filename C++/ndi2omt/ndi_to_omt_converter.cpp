/*
 * NDI HX2/3 to OMT Converter
 * Receives NDI HX2/3 compressed H.264 streams and forwards to OMT
 * 
 * Usage:
 * ./ndi_to_omt_converter -s "NDI Source Name" -o "OMT Stream Name"
 * 
 * Compile on macOS:
 * g++ -std=c++11 -O2 -Wall -I"/Library/NDI Advanced SDK for Apple/include" \
 *     -o ndi_to_omt_converter ndi_to_omt_converter.cpp \
 *     -L"/Library/NDI Advanced SDK for Apple/lib/macOS" \
 *     -lndi_advanced -lomt -lpthread
 */

#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <mutex>
#include <queue>
#include <map>

// NDI Advanced SDK
#include <Processing.NDI.Advanced.h>
#include <Processing.NDI.Lib.h>

// OMT SDK
#include "libomt.h"

std::atomic<bool> running(true);

void signal_handler(int) {
    std::cout << "\nShutdown signal received..." << std::endl;
    running = false;
}

class NDIToOMTConverter {
private:
    // NDI Components
    NDIlib_recv_instance_t ndi_receiver;
    NDIlib_find_instance_t ndi_finder;
    
    // OMT Components
    omt_send_t* omt_sender;
    
    // Stream info
    std::string ndi_source_name;
    std::string omt_stream_name;
    
    // Statistics
    std::atomic<int> frames_received{0};
    std::atomic<int> frames_sent{0};
    std::atomic<int> bytes_received{0};
    std::atomic<int> bytes_sent{0};
    std::atomic<int> connections{0};
    std::atomic<int> keyframes_sent{0};
    std::atomic<int> pframes_sent{0};
    std::atomic<int> frames_dropped{0};
    
    // Stream properties
    int current_width = 0;
    int current_height = 0;
    int current_fps_n = 30;
    int current_fps_d = 1;
    
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point last_stats_time;

public:
    NDIToOMTConverter(const std::string& ndi_source, const std::string& omt_stream)
        : ndi_receiver(nullptr), ndi_finder(nullptr), omt_sender(nullptr),
          ndi_source_name(ndi_source), omt_stream_name(omt_stream) {
        
        start_time = std::chrono::high_resolution_clock::now();
        last_stats_time = start_time;
    }
    
    ~NDIToOMTConverter() {
        cleanup();
    }
    
    bool initialize() {
        std::cout << "NDI HX2/3 to OMT Converter" << std::endl;
        std::cout << "============================" << std::endl;
        
        // Initialize NDI
        if (!NDIlib_initialize()) {
            std::cerr << "Failed to initialize NDI" << std::endl;
            return false;
        }
        
        std::cout << "NDI SDK initialized successfully" << std::endl;
        
        // Create NDI finder
        NDIlib_find_create_t find_desc = {};
        find_desc.show_local_sources = true;
        find_desc.p_groups = nullptr;
        find_desc.p_extra_ips = nullptr;
        
        ndi_finder = NDIlib_find_create_v2(&find_desc);
        if (!ndi_finder) {
            std::cerr << "Failed to create NDI finder" << std::endl;
            return false;
        }
        
        // Find NDI sources
        if (!find_ndi_source()) {
            return false;
        }
        
        // Initialize OMT sender
        if (!init_omt_sender()) {
            return false;
        }
        
        std::cout << "Converter initialized successfully!" << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        
        return true;
    }
    
    bool find_ndi_source() {
        std::cout << "Searching for NDI sources..." << std::endl;
        
        // Wait for sources to be discovered
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        
        uint32_t no_sources = 0;
        const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(ndi_finder, &no_sources);
        
        if (no_sources == 0) {
            std::cerr << "No NDI sources found" << std::endl;
            return false;
        }
        
        std::cout << "Found " << no_sources << " NDI sources:" << std::endl;
        for (uint32_t i = 0; i < no_sources; i++) {
            std::cout << "  [" << i << "] " << p_sources[i].p_ndi_name << std::endl;
        }
        
        // Find the requested source
        const NDIlib_source_t* selected_source = nullptr;
        
        if (ndi_source_name.empty()) {
            // Use first source if none specified
            selected_source = &p_sources[0];
            ndi_source_name = selected_source->p_ndi_name;
            std::cout << "No source specified, using: " << ndi_source_name << std::endl;
        } else {
            // Find source by name
            for (uint32_t i = 0; i < no_sources; i++) {
                std::string source_name = p_sources[i].p_ndi_name;
                if (source_name.find(ndi_source_name) != std::string::npos) {
                    selected_source = &p_sources[i];
                    break;
                }
            }
            
            if (!selected_source) {
                std::cerr << "NDI source '" << ndi_source_name << "' not found" << std::endl;
                return false;
            }
        }
        
        // Create NDI receiver with compressed H.264 frame support
        NDIlib_recv_create_v3_t recv_desc = {};
        recv_desc.source_to_connect_to = *selected_source;
        recv_desc.color_format = (NDIlib_recv_color_format_e)NDIlib_recv_color_format_compressed_v3;  // Request compressed H.264 frames
        recv_desc.bandwidth = NDIlib_recv_bandwidth_highest;
        recv_desc.allow_video_fields = false;
        recv_desc.p_ndi_recv_name = "OMT Converter";
        
        ndi_receiver = NDIlib_recv_create_v3(&recv_desc);
        if (!ndi_receiver) {
            std::cerr << "Failed to create NDI receiver" << std::endl;
            return false;
        }
        
        std::cout << "NDI receiver created with compressed frame support (v3)" << std::endl;
        
        std::cout << "Connected to NDI source: " << selected_source->p_ndi_name << std::endl;
        
        return true;
    }
    
    bool init_omt_sender() {
        // Create OMT sender
        omt_sender = omt_send_create(omt_stream_name.c_str(), OMTQuality_High);
        if (!omt_sender) {
            std::cerr << "Failed to create OMT sender" << std::endl;
            return false;
        }
        
        // Set sender information
        OMTSenderInfo info = {};
        strcpy(info.ProductName, "NDI to OMT Converter");
        strcpy(info.Manufacturer, "OMT Bridge");
        strcpy(info.Version, "1.0");
        omt_send_setsenderinformation(omt_sender, &info);
        
        std::cout << "OMT sender created: " << omt_stream_name << std::endl;
        
        return true;
    }
    
    void run() {
        std::cout << "Starting conversion loop..." << std::endl;
        
        // NDI frame structures
        NDIlib_video_frame_v2_t video_frame;
        NDIlib_audio_frame_v3_t audio_frame;
        NDIlib_metadata_frame_t metadata_frame;
        
        // OMT frame structure
        OMTMediaFrame omt_frame = {};
        omt_frame.Type = OMTFrameType_Video;
        omt_frame.Codec = OMTCodec_VMX1;  // Use VMX1 as H.264 marker
        omt_frame.ColorSpace = OMTColorSpace_BT709;
        omt_frame.Flags = OMTVideoFlags_None;
        omt_frame.Timestamp = -1;  // Auto timestamp
        
        auto last_connection_check = std::chrono::high_resolution_clock::now();
        bool warned_about_compression = false;
        
        while (running) {
            // Check for new frames with timeout
            NDIlib_frame_type_e frame_type = NDIlib_recv_capture_v3(
                ndi_receiver, &video_frame, &audio_frame, &metadata_frame, 100);
            
            switch (frame_type) {
                case NDIlib_frame_type_video: {
                    // Check if we're getting compressed or uncompressed data
                    if (!warned_about_compression) {
                        std::cout << "Received video frame:" << std::endl;
                        std::cout << "  FourCC: " << (char*)&video_frame.FourCC << " (" << video_frame.FourCC << ")" << std::endl;
                        std::cout << "  Resolution: " << video_frame.xres << "x" << video_frame.yres << std::endl;
                        std::cout << "  Data size: " << video_frame.data_size_in_bytes << " bytes" << std::endl;
                        std::cout << "  Line stride: " << video_frame.line_stride_in_bytes << std::endl;
                        
                        // Check for compressed H.264 format
                        if (video_frame.FourCC == (uint32_t)NDIlib_compressed_FourCC_type_H264) {
                            std::cout << "✅ Receiving compressed H.264 data!" << std::endl;
                        } else if (video_frame.FourCC == NDIlib_FourCC_type_UYVY || 
                                  video_frame.FourCC == NDIlib_FourCC_type_BGRX || 
                                  video_frame.FourCC == NDIlib_FourCC_type_BGRA) {
                            std::cout << "⚠️  Still receiving uncompressed data. NDI source might not be HX or receiver config needs adjustment." << std::endl;
                        } else {
                            std::cout << "📦 Received format: " << (char*)&video_frame.FourCC << " - attempting to parse..." << std::endl;
                        }
                        warned_about_compression = true;
                    }
                    
                    handle_video_frame(video_frame, omt_frame);
                    NDIlib_recv_free_video_v2(ndi_receiver, &video_frame);
                    break;
                }
                
                case NDIlib_frame_type_audio: {
                    // For now, we're only handling video
                    NDIlib_recv_free_audio_v3(ndi_receiver, &audio_frame);
                    break;
                }
                
                case NDIlib_frame_type_metadata: {
                    // Handle metadata if needed
                    NDIlib_recv_free_metadata(ndi_receiver, &metadata_frame);
                    break;
                }
                
                case NDIlib_frame_type_none:
                    // No frame available, continue
                    break;
                
                case NDIlib_frame_type_status_change: {
                    // Check connection status
                    NDIlib_recv_performance_t perf;
                    NDIlib_recv_get_performance(ndi_receiver, &perf, nullptr);
                    std::cout << "NDI connection status changed" << std::endl;
                    break;
                }
                
                default:
                    break;
            }
            
            // Update connection count periodically
            auto now = std::chrono::high_resolution_clock::now();
            if (now - last_connection_check >= std::chrono::seconds(1)) {
                connections = omt_send_connections(omt_sender);
                last_connection_check = now;
                print_statistics();
            }
        }
        
        std::cout << "Conversion loop ended" << std::endl;
    }
    
    void handle_video_frame(const NDIlib_video_frame_v2_t& ndi_frame, OMTMediaFrame& omt_frame) {
        frames_received++;
        
        // Update stream properties if changed
        if (current_width != ndi_frame.xres || current_height != ndi_frame.yres ||
            current_fps_n != ndi_frame.frame_rate_N || current_fps_d != ndi_frame.frame_rate_D) {
            
            current_width = ndi_frame.xres;
            current_height = ndi_frame.yres;
            current_fps_n = ndi_frame.frame_rate_N;
            current_fps_d = ndi_frame.frame_rate_D;
            
            std::cout << "Stream format: " << current_width << "x" << current_height 
                      << " @ " << (float)current_fps_n / current_fps_d << " fps" << std::endl;
        }
        
        // Check frame format and compression status
        std::cout << "Frame format: " << (int)ndi_frame.FourCC 
                  << ", line_stride: " << ndi_frame.line_stride_in_bytes 
                  << ", data_size: " << ndi_frame.data_size_in_bytes << std::endl;
        
        // NDI HX streams can be detected by checking the FourCC or other properties
        // Let's try to handle this as compressed data first
        if (handle_compressed_frame(ndi_frame, omt_frame)) {
            return;
        }
        
        // If compressed handling failed, this might be uncompressed
        std::cout << "Warning: Could not extract compressed H.264 from NDI HX stream" << std::endl;
    }
    
    bool handle_compressed_frame(const NDIlib_video_frame_v2_t& ndi_frame, OMTMediaFrame& omt_frame) {
        if (!ndi_frame.p_data || ndi_frame.data_size_in_bytes == 0) {
            return false;
        }
        
        // Check if this is a compressed H.264 frame
        if (ndi_frame.FourCC == (uint32_t)NDIlib_compressed_FourCC_type_H264) {
            std::cout << "✅ Processing compressed H.264 frame..." << std::endl;
            
            // The data starts with NDIlib_compressed_packet_t structure
            if (ndi_frame.data_size_in_bytes < (int)sizeof(NDIlib_compressed_packet_t)) {
                std::cout << "❌ Frame too small to contain compressed packet header" << std::endl;
                return false;
            }
            
            // Cast to compressed packet structure
            const NDIlib_compressed_packet_t* packet = (const NDIlib_compressed_packet_t*)ndi_frame.p_data;
            
            std::cout << "  Packet version: " << packet->version << std::endl;
            std::cout << "  FourCC: " << (char*)&packet->fourCC << std::endl;
            std::cout << "  Flags: " << packet->flags << std::endl;
            std::cout << "  Total size: " << ndi_frame.data_size_in_bytes << " bytes" << std::endl;
            
            // Verify this is H.264
            if (packet->fourCC != NDIlib_compressed_FourCC_type_H264) {
                std::cout << "❌ Packet is not H.264 format" << std::endl;
                return false;
            }
            
            // Calculate H.264 data pointer and size
            const uint8_t* h264_data = (const uint8_t*)ndi_frame.p_data + sizeof(NDIlib_compressed_packet_t);
            size_t h264_size = ndi_frame.data_size_in_bytes - (int)sizeof(NDIlib_compressed_packet_t);
            
            std::cout << "  H.264 data size: " << h264_size << " bytes" << std::endl;
            
            // Check if this is a keyframe
            bool is_keyframe = (packet->flags & NDIlib_compressed_packet_flags_keyframe) != 0;
            
            // Verify H.264 start codes and get frame type
            bool has_start_codes = false;
            std::string frame_type = "Unknown";
            
            for (size_t i = 0; i < std::min(h264_size, (size_t)32); i++) {
                if (i >= 3 && h264_data[i-3] == 0x00 && h264_data[i-2] == 0x00 && 
                    h264_data[i-1] == 0x00 && h264_data[i] == 0x01) {
                    has_start_codes = true;
                    std::cout << "  Found H.264 start code at offset " << (i-3) << std::endl;
                    
                    // Analyze NAL unit type if we have enough data
                    if (i + 1 < h264_size) {
                        uint8_t nal_type = h264_data[i + 1] & 0x1F;
                        if (nal_type == 5) frame_type = "IDR (I-frame)";
                        else if (nal_type == 1) frame_type = "P-frame";
                        else if (nal_type == 7) frame_type = "SPS";
                        else if (nal_type == 8) frame_type = "PPS";
                        else frame_type = "NAL type " + std::to_string(nal_type);
                    }
                    break;
                }
                if (i >= 2 && h264_data[i-2] == 0x00 && h264_data[i-1] == 0x00 && h264_data[i] == 0x01) {
                    has_start_codes = true;
                    std::cout << "  Found H.264 start code at offset " << (i-2) << std::endl;
                    
                    // Analyze NAL unit type if we have enough data
                    if (i + 1 < h264_size) {
                        uint8_t nal_type = h264_data[i + 1] & 0x1F;
                        if (nal_type == 5) frame_type = "IDR (I-frame)";
                        else if (nal_type == 1) frame_type = "P-frame";
                        else if (nal_type == 7) frame_type = "SPS";
                        else if (nal_type == 8) frame_type = "PPS";
                        else frame_type = "NAL type " + std::to_string(nal_type);
                    }
                    break;
                }
            }
            
            // Additional H.264 analysis for verification
            std::cout << "  Frame analysis:" << std::endl;
            std::cout << "    NDI flags indicate keyframe: " << (is_keyframe ? "YES" : "NO") << std::endl;
            std::cout << "    H.264 NAL analysis: " << frame_type << std::endl;
            
            if (!has_start_codes) {
                std::cout << "⚠️  No H.264 start codes found - data might not be valid H.264" << std::endl;
                // Print first few bytes for debugging
                std::cout << "  First 16 bytes: ";
                for (size_t i = 0; i < std::min((size_t)16, h264_size); i++) {
                    printf("%02x ", h264_data[i]);
                }
                std::cout << std::endl;
            }
            
            // Send the H.264 data to OMT
            bool sent_successfully = send_compressed_to_omt(h264_data, h264_size, is_keyframe, omt_frame);
            (void)sent_successfully;  // Suppress unused variable warning
            
            // Always return true if we successfully extracted H.264 data
            // (even if OMT send failed - that's a different issue)
            return true;
        }
        
        // Not a compressed H.264 frame - fall back to pixel analysis
        std::cout << "⚠️  Frame is not compressed H.264 format (FourCC: " << (char*)&ndi_frame.FourCC << ")" << std::endl;
        return false;
    }
    
    bool send_compressed_to_omt(const void* h264_data, size_t data_size, 
                               bool is_keyframe, OMTMediaFrame& omt_frame) {
        
        // Set up OMT frame for compressed H.264 data
        omt_frame.Width = current_width;
        omt_frame.Height = current_height;
        omt_frame.FrameRateN = current_fps_n;
        omt_frame.FrameRateD = current_fps_d;
        omt_frame.AspectRatio = (float)current_width / current_height;
        
        // Set compressed data
        omt_frame.Data = (uint8_t*)h264_data;
        omt_frame.DataLength = data_size;
        omt_frame.CompressedData = nullptr;
        omt_frame.CompressedLength = 0;
        omt_frame.Stride = 0;  // Not used for compressed
        
        // Set frame flags - this is critical for decoder
        if (is_keyframe) {
            omt_frame.Flags = OMTVideoFlags_None;  // Keyframe
            keyframes_sent++;
            std::cout << "🔑 Sending I-frame (" << data_size << " bytes) - Total I-frames: " << keyframes_sent << std::endl;
        } else {
            omt_frame.Flags = OMTVideoFlags_None;  // P-frame (same flag?)
            pframes_sent++;
            std::cout << "📽️  Sending P-frame (" << data_size << " bytes) - Total P-frames: " << pframes_sent << std::endl;
        }
        
        // Show first few bytes of H.264 data for verification
        const uint8_t* data = (const uint8_t*)h264_data;
        std::cout << "   H.264 data starts: ";
        for (int i = 0; i < std::min(8, (int)data_size); i++) {
            printf("%02x ", data[i]);
        }
        std::cout << std::endl;
        
        // Send to OMT
        int bytes_sent_result = omt_send(omt_sender, &omt_frame);
        
        // Check OMT API return value - need to understand what success looks like
        if (bytes_sent_result >= 0) {  // Changed from > 0 to >= 0
            frames_sent++;
            bytes_sent += data_size;
            bytes_received += data_size;
            if (bytes_sent_result == 0) {
                std::cout << "   ⚠️  OMT send returned 0 (may indicate no clients connected)" << std::endl;
            } else {
                std::cout << "   ✅ Successfully sent to OMT (returned: " << bytes_sent_result << ")" << std::endl;
            }
            return true;
        } else {
            frames_dropped++;
            std::cout << "   ❌ Failed to send frame to OMT (error: " << bytes_sent_result << ")" << std::endl;
            
            // Add more diagnostics
            int conn_count = omt_send_connections(omt_sender);
            std::cout << "      Current OMT connections: " << conn_count << std::endl;
            
            if (conn_count == 0) {
                std::cout << "      💡 No clients connected - frames will be dropped" << std::endl;
            }
            
            return false;
        }
    }
    
    void print_statistics() {
        auto now = std::chrono::high_resolution_clock::now();
        if (now - last_stats_time >= std::chrono::seconds(2)) {  // More frequent updates
            auto elapsed = now - start_time;
            auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            
            if (seconds > 0) {
                float avg_fps_received = (float)frames_received / seconds;
                float avg_fps_sent = (float)frames_sent / seconds;
                float mbps_received = ((float)bytes_received * 8) / (seconds * 1000000);
                float mbps_sent = ((float)bytes_sent * 8) / (seconds * 1000000);
                
                std::cout << "\n=== FRAME STATISTICS ===" << std::endl;
                std::cout << "  Runtime: " << seconds << " seconds" << std::endl;
                std::cout << "  Total frames: " << frames_received << " received, " 
                          << frames_sent << " sent, " << frames_dropped << " dropped" << std::endl;
                std::cout << "  Frame types: " << keyframes_sent << " I-frames, " 
                          << pframes_sent << " P-frames" << std::endl;
                std::cout << "  I/P ratio: " << (pframes_sent > 0 ? (float)keyframes_sent / pframes_sent : 0) 
                          << " (lower = more P-frames)" << std::endl;
                std::cout << "  Success rate: " << (frames_received > 0 ? (100.0f * frames_sent / frames_received) : 0) << "%" << std::endl;
                std::cout << "  FPS: " << avg_fps_received << " in, " 
                          << avg_fps_sent << " out" << std::endl;
                std::cout << "  Bitrate: " << mbps_received << " Mbps in, " 
                          << mbps_sent << " Mbps out" << std::endl;
                std::cout << "  OMT Connections: " << connections << std::endl;
                std::cout << "  Format: " << current_width << "x" << current_height 
                          << " @ " << (float)current_fps_n / current_fps_d << " fps" << std::endl;
                std::cout << "========================\n" << std::endl;
                
                // Warn if we're only getting keyframes
                if (frames_sent > 10 && pframes_sent == 0) {
                    std::cout << "⚠️  WARNING: Only receiving I-frames, no P-frames detected!" << std::endl;
                    std::cout << "   This could indicate:" << std::endl;
                    std::cout << "   1. NDI source is sending only keyframes" << std::endl;
                    std::cout << "   2. P-frame detection logic has an issue" << std::endl;
                    std::cout << "   3. NDI Advanced SDK is filtering P-frames\n" << std::endl;
                }
                
                // Warn if many frames are being dropped
                float drop_rate = (float)frames_dropped / frames_received;
                if (frames_received > 10 && drop_rate > 0.1) {
                    std::cout << "⚠️  WARNING: High frame drop rate (" << (drop_rate * 100) << "%)!" << std::endl;
                    std::cout << "   Dropped frames: " << frames_dropped << " / " << frames_received << std::endl;
                    std::cout << "   This could indicate:" << std::endl;
                    std::cout << "   1. No OMT clients connected" << std::endl;
                    std::cout << "   2. OMT buffer overflow" << std::endl;
                    std::cout << "   3. Network congestion\n" << std::endl;
                }
            }
            
            last_stats_time = now;
        }
    }
    
    void cleanup() {
        running = false;
        
        std::cout << "Cleaning up..." << std::endl;
        
        if (ndi_receiver) {
            NDIlib_recv_destroy(ndi_receiver);
            ndi_receiver = nullptr;
        }
        
        if (ndi_finder) {
            NDIlib_find_destroy(ndi_finder);
            ndi_finder = nullptr;
        }
        
        if (omt_sender) {
            omt_send_destroy(omt_sender);
            omt_sender = nullptr;
        }
        
        NDIlib_destroy();
        
        std::cout << "Cleanup complete" << std::endl;
    }
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -s <source>    NDI source name (partial match)" << std::endl;
    std::cout << "  -o <output>    OMT stream name (default: NDItoOMT)" << std::endl;
    std::cout << "  -l             List available NDI sources and exit" << std::endl;
    std::cout << "  --help         Show this help" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " -s \"Camera 1\" -o \"LiveStream\"" << std::endl;
    std::cout << "  " << program_name << " -l" << std::endl;
}

void list_ndi_sources() {
    if (!NDIlib_initialize()) {
        std::cerr << "Failed to initialize NDI" << std::endl;
        return;
    }
    
    NDIlib_find_create_t find_desc = {};
    find_desc.show_local_sources = true;
    
    NDIlib_find_instance_t finder = NDIlib_find_create_v2(&find_desc);
    if (!finder) {
        std::cerr << "Failed to create NDI finder" << std::endl;
        NDIlib_destroy();
        return;
    }
    
    std::cout << "Searching for NDI sources..." << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    
    uint32_t no_sources = 0;
    const NDIlib_source_t* p_sources = NDIlib_find_get_current_sources(finder, &no_sources);
    
    if (no_sources == 0) {
        std::cout << "No NDI sources found" << std::endl;
    } else {
        std::cout << "Available NDI sources:" << std::endl;
        for (uint32_t i = 0; i < no_sources; i++) {
            std::cout << "  " << p_sources[i].p_ndi_name << std::endl;
        }
    }
    
    NDIlib_find_destroy(finder);
    NDIlib_destroy();
}

int main(int argc, char* argv[]) {
    std::string ndi_source = "";
    std::string omt_stream = "NDItoOMT";
    bool list_sources = false;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-s" && i + 1 < argc) {
            ndi_source = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            omt_stream = argv[++i];
        } else if (arg == "-l") {
            list_sources = true;
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    if (list_sources) {
        list_ndi_sources();
        return 0;
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create and run converter
    NDIToOMTConverter converter(ndi_source, omt_stream);
    
    if (!converter.initialize()) {
        std::cerr << "Failed to initialize converter" << std::endl;
        return 1;
    }
    
    converter.run();
    
    return 0;
}