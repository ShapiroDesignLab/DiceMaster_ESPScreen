#ifndef DICE_EXAMPLES_H
#define DICE_EXAMPLES_H

#include "media.h"
#include "protocol.h"
#include "screen.h"  // Include the actual Screen class definition
#include "jpg.hs/umlogo_sq240.h"
#include "constants.h"

#include "jpg.hs/revolving_umlogo_12/rev_00.h"
#include "jpg.hs/revolving_umlogo_12/rev_02.h"
#include "jpg.hs/revolving_umlogo_12/rev_04.h"
#include "jpg.hs/revolving_umlogo_12/rev_06.h"
#include "jpg.hs/revolving_umlogo_12/rev_08.h"
#include "jpg.hs/revolving_umlogo_12/rev_10.h"
#include "jpg.hs/revolving_umlogo_12/rev_12.h"
#include "jpg.hs/revolving_umlogo_12/rev_14.h"
#include "jpg.hs/revolving_umlogo_12/rev_16.h"
#include "jpg.hs/revolving_umlogo_12/rev_18.h"
#include "jpg.hs/revolving_umlogo_12/rev_20.h"
#include "jpg.hs/revolving_umlogo_12/rev_22.h"

namespace dice {

// ===================== COLOR CONSTANTS =====================

// ===================== DEMO IMAGE ARRAYS =====================

// Array of 12-frame revolving logo images
const uint8_t* revolving_umlogo_12_array[12] = {
    rev_00, rev_02, rev_04, rev_06, rev_08, rev_10,
    rev_12, rev_14, rev_16, rev_18, rev_20, rev_22
};

const size_t revolving_umlogo_12_sizes[12] = {
    rev_00_SIZE, rev_02_SIZE, rev_04_SIZE, rev_06_SIZE,
    rev_08_SIZE, rev_10_SIZE, rev_12_SIZE, rev_14_SIZE,
    rev_16_SIZE, rev_18_SIZE, rev_20_SIZE, rev_22_SIZE
};

const size_t revolving_umlogo_12_count = 12;

// ===================== FORWARD DECLARATIONS =====================

// Forward declarations for functions used in demo sequences
namespace dice {
    String get_board_status();
    bool test_protocol_encode_decode();
    bool test_image_protocol_encode_decode();
}

// ===================== DEMO FUNCTIONS =====================

/**
 * Helper function to create an error text group
 */
MediaContainer* print_error(const char* error_msg) {
    TextGroup* group = new TextGroup(2000, DICE_RED, DICE_WHITE);
    group->add_member(new Text("ERROR:", 0, FontID::TF, 200, 200));
    group->add_member(new Text(error_msg, 0, FontID::TF, 150, 250));
    return group;
}

/**
 * Helper function to create a success text group
 */
MediaContainer* print_success(const char* success_msg) {
    TextGroup* group = new TextGroup(2000, DICE_GREEN, DICE_BLACK);
    group->add_member(new Text("SUCCESS:", 0, FontID::TF, 180, 200));
    group->add_member(new Text(success_msg, 0, FontID::TF, 130, 250));
    return group;
}

/**
 * Creates a multi-language text demonstration showing "Psychic" in 8 languages
 */
MediaContainer* get_demo_textgroup() {
    TextGroup* group = new TextGroup(0, DICE_DARKGREY, DICE_WHITE);
    group->add_member(new Text("Psíquico", 0, FontID::TF, 40, 40));
    group->add_member(new Text("Hellseher", 0, FontID::TF, 280, 40));
    group->add_member(new Text("экстрасенс", 0, FontID::CYRILLIC, 40, 160));
    group->add_member(new Text("Psychique", 0, FontID::TF, 280, 160));
    group->add_member(new Text("Psychic", 0, FontID::TF, 40, 280));
    group->add_member(new Text("मानसिक", 0, FontID::DEVANAGARI, 280, 280));
    group->add_member(new Text("靈媒", 0, FontID::CHINESE, 40, 400));
    group->add_member(new Text("نفسية", 0, FontID::ARABIC, 280, 400));
    return group;
}

/**
 * Creates a demo showing different font capabilities
 */
MediaContainer* get_demo_fonts() {
    TextGroup* group = new TextGroup(1000, DICE_BLACK, DICE_WHITE);
    group->add_member(new Text("English - Regular", 0, FontID::TF, 20, 60));
    group->add_member(new Text("العربية - Arabic", 0, FontID::ARABIC, 20, 120));
    group->add_member(new Text("中文 - Chinese", 0, FontID::CHINESE, 20, 180));
    group->add_member(new Text("Русский - Cyrillic", 0, FontID::CYRILLIC, 20, 240));
    group->add_member(new Text("हिन्दी - Devanagari", 0, FontID::DEVANAGARI, 20, 300));
    group->add_member(new Text("Font Demo Complete", 0, FontID::TF, 140, 400));
    return group;
}

/**
 * Creates a demo TextGroup with rotation
 */
MediaContainer* get_demo_textgroup_rotated(Rotation rot) {
    TextGroup* group = new TextGroup(2000, DICE_BLUE, DICE_WHITE, rot);
    
    // Use consistent coordinates - rotation will be handled by the screen renderer
    // This creates a vertical layout that should work for all rotations
    group->add_member(new Text("ROTATED", 0, FontID::TF, 180, 180));
    group->add_member(new Text("TEXT", 0, FontID::TF, 180, 230));
    group->add_member(new Text("LINE 3", 0, FontID::TF, 180, 280));
    
    // Add rotation indicator
    String rotText = "ROT " + String(static_cast<uint8_t>(rot) * 90) + "°";
    group->add_member(new Text(rotText.c_str(), 0, FontID::TF, 180, 330));
    
    return group;
}

/**
 * Creates a demo image with rotation
 */
MediaContainer* get_demo_image_rotated(Rotation rot) {
    try {
        Image* img = new Image(100 + static_cast<uint8_t>(rot), ImageFormat::JPEG, 
                              ImageResolution::SQ240, umlogo_sq240_SIZE, 1500, 1, rot); // 1 chunk, Shorter duration for tests
        img->add_chunk(umlogo_sq240, umlogo_sq240_SIZE);
        return img;
    } catch (...) {
        return print_error("Failed to create rotated image");
    }
}

/**
 * Creates a single frame from the 12-frame revolving logo sequence
 */
MediaContainer* get_demo_revolving_frame(uint8_t frame_index) {
    if (frame_index >= revolving_umlogo_12_count) {
        frame_index = 0; // Wrap around
    }
    
    const uint8_t* img_data = revolving_umlogo_12_array[frame_index];
    size_t img_size = revolving_umlogo_12_sizes[frame_index];
    
    try {
        Image* img = new Image(frame_index, ImageFormat::JPEG, ImageResolution::SQ240, img_size, 100, 1, Rotation::ROT_0);
        img->add_chunk(img_data, img_size);
        return img;
    } catch (...) {
        return print_error("Failed to create revolving frame");
    }
}

/**
 * Creates the startup logo image
 */
MediaContainer* get_demo_startup_logo() {
    try {
        Image* img = new Image(255, ImageFormat::JPEG, ImageResolution::SQ240, umlogo_sq240_SIZE, 2000, 1, Rotation::ROT_0);
        img->add_chunk(umlogo_sq240, umlogo_sq240_SIZE);
        return img;
    } catch (...) {
        return print_error("Failed to create startup logo");
    }
}

/**
 * Creates a color test pattern showing different background colors
 */
MediaContainer* get_demo_colors() {
    // Create multiple separate TextGroups with different colors
    // Note: This returns only the first one - for a full demo, call this multiple times
    // or modify the screen system to handle color transitions
    
    static int color_index = 0;
    
    switch (color_index % 6) {
        case 0: {
            color_index++;
            TextGroup* red_group = new TextGroup(800, DICE_RED, DICE_WHITE);
            red_group->add_member(new Text("RED ZONE", 0, FontID::TF, 180, 200));
            red_group->add_member(new Text("Background: Red", 0, FontID::TF, 140, 280));
            return red_group;
        }
        case 1: {
            color_index++;
            TextGroup* green_group = new TextGroup(800, DICE_GREEN, DICE_BLACK);
            green_group->add_member(new Text("GREEN ZONE", 0, FontID::TF, 160, 200));
            green_group->add_member(new Text("Background: Green", 0, FontID::TF, 120, 280));
            return green_group;
        }
        case 2: {
            color_index++;
            TextGroup* blue_group = new TextGroup(800, DICE_BLUE, DICE_WHITE);
            blue_group->add_member(new Text("BLUE ZONE", 0, FontID::TF, 170, 200));
            blue_group->add_member(new Text("Background: Blue", 0, FontID::TF, 130, 280));
            return blue_group;
        }
        case 3: {
            color_index++;
            TextGroup* yellow_group = new TextGroup(800, DICE_YELLOW, DICE_BLACK);
            yellow_group->add_member(new Text("YELLOW ZONE", 0, FontID::TF, 150, 200));
            yellow_group->add_member(new Text("Background: Yellow", 0, FontID::TF, 110, 280));
            return yellow_group;
        }
        case 4: {
            color_index++;
            TextGroup* cyan_group = new TextGroup(800, DICE_CYAN, DICE_BLACK);
            cyan_group->add_member(new Text("CYAN ZONE", 0, FontID::TF, 175, 200));
            cyan_group->add_member(new Text("Background: Cyan", 0, FontID::TF, 125, 280));
            return cyan_group;
        }
        case 5: {
            color_index++;
            TextGroup* magenta_group = new TextGroup(800, DICE_MAGENTA, DICE_WHITE);
            magenta_group->add_member(new Text("MAGENTA ZONE", 0, FontID::TF, 145, 200));
            magenta_group->add_member(new Text("Background: Magenta", 0, FontID::TF, 105, 280));
            return magenta_group;
        }
        default: {
            color_index = 0;
            TextGroup* white_group = new TextGroup(800, DICE_WHITE, DICE_BLACK);
            white_group->add_member(new Text("COLOR TEST", 0, FontID::TF, 160, 200));
            white_group->add_member(new Text("Complete!", 0, FontID::TF, 180, 280));
            return white_group;
        }
    }
}

// ===================== MESSAGE CREATION HELPERS =====================

/**
 * Creates test SPI messages for image transfer
 * Returns array of message buffers that simulate SPI communication
 */
uint8_t** make_test_img_message(const uint8_t* img_data, size_t img_size, 
                                uint8_t img_id, size_t chunk_size, uint8_t msg_id_start) {
    
    size_t num_chunks = (img_size + chunk_size - 1) / chunk_size;
    uint8_t** messages = new uint8_t*[num_chunks + 2]; // start + chunks + end
    
    // Create IMAGE_TRANSFER_START message
    messages[0] = new uint8_t[12]; // 5 header + 7 payload
    messages[0][0] = 0x7E; // SOF_MARKER
    messages[0][1] = static_cast<uint8_t>(MessageType::IMAGE_TRANSFER_START);
    messages[0][2] = msg_id_start;
    messages[0][3] = 0x00; messages[0][4] = 0x07; // payload length = 7
    messages[0][5] = img_id;
    messages[0][6] = (static_cast<uint8_t>(ImageFormat::JPEG) << 4) | static_cast<uint8_t>(ImageResolution::SQ240);
    messages[0][7] = 100; // delay
    messages[0][8] = (img_size >> 16) & 0xFF; // total size (24-bit BE)
    messages[0][9] = (img_size >> 8) & 0xFF;
    messages[0][10] = img_size & 0xFF;
    messages[0][11] = num_chunks;
    
    // Create IMAGE_CHUNK messages
    for (size_t i = 0; i < num_chunks; i++) {
        size_t offset = i * chunk_size;
        size_t current_chunk_size = std::min(chunk_size, img_size - offset);
        size_t msg_size = 5 + 7 + current_chunk_size; // header + chunk header + data
        
        messages[i + 1] = new uint8_t[msg_size];
        messages[i + 1][0] = 0x7E; // SOF_MARKER
        messages[i + 1][1] = static_cast<uint8_t>(MessageType::IMAGE_CHUNK);
        messages[i + 1][2] = msg_id_start + 1 + i;
        messages[i + 1][3] = ((7 + current_chunk_size) >> 8) & 0xFF; // payload length
        messages[i + 1][4] = (7 + current_chunk_size) & 0xFF;
        messages[i + 1][5] = img_id;
        messages[i + 1][6] = i; // chunk ID
        messages[i + 1][7] = (offset >> 16) & 0xFF; // offset (24-bit BE)
        messages[i + 1][8] = (offset >> 8) & 0xFF;
        messages[i + 1][9] = offset & 0xFF;
        messages[i + 1][10] = (current_chunk_size >> 8) & 0xFF; // chunk size
        messages[i + 1][11] = current_chunk_size & 0xFF;
        
        // Copy image data
        memcpy(&messages[i + 1][12], img_data + offset, current_chunk_size);
    }
    
    // Create IMAGE_TRANSFER_END message
    messages[num_chunks + 1] = new uint8_t[6]; // 5 header + 1 payload
    messages[num_chunks + 1][0] = 0x7E; // SOF_MARKER
    messages[num_chunks + 1][1] = static_cast<uint8_t>(MessageType::IMAGE_TRANSFER_END);
    messages[num_chunks + 1][2] = msg_id_start + num_chunks + 1;
    messages[num_chunks + 1][3] = 0x00; messages[num_chunks + 1][4] = 0x01; // payload length = 1
    messages[num_chunks + 1][5] = img_id;
    
    return messages;
}

/**
 * Creates a test text batch message
 */
uint8_t* make_test_text_message(const char* text, uint16_t x, uint16_t y, 
                               FontID font, uint8_t msg_id) {
    size_t text_len = strlen(text);
    size_t msg_size = 5 + 5 + 7 + text_len; // header + text_batch_header + text_item + text
    
    uint8_t* message = new uint8_t[msg_size];
    message[0] = 0x7E; // SOF_MARKER
    message[1] = static_cast<uint8_t>(MessageType::TEXT_BATCH);
    message[2] = msg_id;
    message[3] = ((5 + 7 + text_len) >> 8) & 0xFF; // payload length
    message[4] = (5 + 7 + text_len) & 0xFF;
    
    // Text batch header
    message[5] = (DICE_DARKGREY >> 8) & 0xFF; // bg color
    message[6] = DICE_DARKGREY & 0xFF;
    message[7] = (DICE_WHITE >> 8) & 0xFF; // font color  
    message[8] = DICE_WHITE & 0xFF;
    message[9] = 1; // number of items
    
    // Text item
    message[10] = (x >> 8) & 0xFF; // x position
    message[11] = x & 0xFF;
    message[12] = (y >> 8) & 0xFF; // y position
    message[13] = y & 0xFF;
    message[14] = static_cast<uint8_t>(font);
    message[15] = 0; // color (unused in current implementation)
    message[16] = text_len;
    
    // Text content
    memcpy(&message[17], text, text_len);
    
    return message;
}

// ===================== DEMO SEQUENCES =====================

/**
 * Creates a demo showing protocol ping functionality
 */
String get_board_status() {
    size_t freeHeap = ESP.getFreeHeap();
    size_t freePsram = ESP.getFreePsram();
    
    if (freeHeap < 50000) {
        return "Low Memory: " + String(freeHeap) + " bytes";
    } else if (freePsram < 1000000) {
        return "Warning: PSRAM " + String(freePsram) + " bytes";
    } else {
        return "OK: " + String(freeHeap) + "/" + String(freePsram) + " bytes";
    }
}

/**
 * Encodes a demo TextGroup into SPI protocol format and then decodes it back
 * Uses heap allocation to avoid stack overflow
 */
bool test_protocol_encode_decode() {
    // Allocate messages on heap to avoid stack overflow (Message structs are >1KB)
    Message* testMsg = (Message*)malloc(sizeof(Message));
    Message* decodedMsg = (Message*)malloc(sizeof(Message));
    
    if (!testMsg || !decodedMsg) {
        Serial.println("[TEST] Failed to allocate protocol test messages on heap");
        if (testMsg) free(testMsg);
        if (decodedMsg) free(decodedMsg);
        return false;
    }
    
    // Create a test message with rotation
    testMsg->hdr.marker = ::SOF_MARKER;
    testMsg->hdr.type = MessageType::TEXT_BATCH;
    testMsg->hdr.id = 1;
    
    // Create test payload with rotation
    testMsg->payload.tag = TAG_TEXT_BATCH;
    TextBatch& tb = testMsg->payload.u.textBatch;
    tb.itemCount = 2;
    tb.rotation = static_cast<uint8_t>(Rotation::ROT_180);  // Test 180° rotation
    
    // First text item
    tb.items[0].x = 100;
    tb.items[0].y = 200;
    tb.items[0].font = static_cast<uint8_t>(FontID::TF);
    tb.items[0].color = 0xFF;
    tb.items[0].len = 4;
    strcpy(tb.items[0].text, "Test");
    
    // Second text item
    tb.items[1].x = 150;
    tb.items[1].y = 250;
    tb.items[1].font = static_cast<uint8_t>(FontID::CHINESE);
    tb.items[1].color = 0x00;
    tb.items[1].len = 6;
    strcpy(tb.items[1].text, "中文");
    
    // Encode the message
    uint8_t buffer[512];
    size_t encodedSize = encode(buffer, sizeof(buffer), *testMsg);
    
    if (encodedSize == 0) {
        Serial.println("[TEST] Encode failed");
        free(testMsg);
        free(decodedMsg);
        return false;
    }
    
    Serial.println("[TEST] Encoded " + String(encodedSize) + " bytes");
    
    // Decode the message
    ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
    
    if (result != ErrorCode::SUCCESS) {
        Serial.println("[TEST] Decode failed: " + String(static_cast<uint8_t>(result)));
        free(testMsg);
        free(decodedMsg);
        return false;
    }
    
    // Verify the decoded data including rotation
    bool success = true;
    success &= (decodedMsg->hdr.type == MessageType::TEXT_BATCH);
    success &= (decodedMsg->payload.tag == TAG_TEXT_BATCH);
    success &= (decodedMsg->payload.u.textBatch.itemCount == 2);
    success &= (decodedMsg->payload.u.textBatch.rotation == static_cast<uint8_t>(Rotation::ROT_180));
    success &= (strcmp(decodedMsg->payload.u.textBatch.items[0].text, "Test") == 0);
    success &= (strcmp(decodedMsg->payload.u.textBatch.items[1].text, "中文") == 0);
    
    // Log detailed results
    if (success) {
        Serial.println("[TEST] Protocol verification PASSED:");
        Serial.println("  - Message type: " + String(static_cast<uint8_t>(decodedMsg->hdr.type)));
        Serial.println("  - Text items: " + String(decodedMsg->payload.u.textBatch.itemCount));
        Serial.println("  - Rotation: " + String(decodedMsg->payload.u.textBatch.rotation * 90) + "°");
    } else {
        Serial.println("[TEST] Protocol verification FAILED");
    }
    
    free(testMsg);
    free(decodedMsg);
    return success;
}

/**
 * Demo function that cycles through revolving logo frames
 * Call this repeatedly to create animation effect
 */
MediaContainer* demo_revolving_animation(int& frame_counter) {
    MediaContainer* frame = get_demo_revolving_frame(frame_counter % revolving_umlogo_12_count);
    frame_counter++;
    return frame;
}

/**
 * Creates a rotated UM logo for demo
 */
MediaContainer* get_demo_rotated_logo(Rotation rotation) {
    try {
        Image* img = new Image(100 + static_cast<uint8_t>(rotation), ImageFormat::JPEG, 
                              ImageResolution::SQ240, umlogo_sq240_SIZE, 1000, 1, rotation);
        img->add_chunk(umlogo_sq240, umlogo_sq240_SIZE);
        return img;
    } catch (...) {
        return print_error("Failed to create rotated logo");
    }
}

/**
 * Creates rotated text for demo
 */
MediaContainer* get_demo_rotated_text(Rotation rotation) {
    TextGroup* group = new TextGroup(1000, DICE_BLACK, DICE_WHITE, rotation);
    
    // Text positioned for center display
    String rotText = "ROTATION " + String(static_cast<uint8_t>(rotation) * 90) + "°";
    group->add_member(new Text(rotText.c_str(), 0, FontID::TF, 200, 200));
    group->add_member(new Text("UNIVERSITY OF", 0, FontID::TF, 160, 240));
    group->add_member(new Text("MICHIGAN", 0, FontID::TF, 180, 280));
    
    return group;
}

/**
 * Main demo sequence manager - handles cycling through different demo phases
 */
void run_demo_sequence(Screen* screen, int& revolving_counter) {
    static unsigned long last_demo_change = 0;
    static int demo_phase = 0;
    static bool demo_suite_run = false;
    unsigned long now = millis();
    
    // Run demo tests once at startup
    if (!demo_suite_run) {
        // You could call a demo version of tests here if needed
        demo_suite_run = true;
        last_demo_change = now;
    }

    // Change demo every 3 seconds
    if (now - last_demo_change > 3000) {
        switch (demo_phase) {
            case 0:
                Serial.println("Demo: Multi-language text");
                screen->enqueue(get_demo_textgroup());
                break;

            case 1:
                Serial.println("Demo: Font showcase");
                screen->enqueue(get_demo_fonts());
                break;

            case 2:
                Serial.println("Demo: Revolving logo animation");
                // Show several frames in sequence
                for (int i = 0; i < 12; i++) {
                    MediaContainer* frame = get_demo_revolving_frame(revolving_counter % revolving_umlogo_12_count);
                    if (frame) {
                        // Wait for decode
                        while (frame->get_status() != MediaStatus::READY && 
                                frame->get_status() != MediaStatus::EXPIRED) {
                            delay(1);
                        }
                        screen->enqueue(frame);
                        screen->update();
                        delay(83); // Fast animation
                        revolving_counter++;
                    }
                }
                break;

            case 3:
                Serial.println("Demo: Color showcase");
                screen->enqueue(get_demo_colors());
                break;

            case 4:
                Serial.println("Demo: Rotation test - Images");
                // Show UM logo in all 4 rotations, 1 second each
                for (int rot = 0; rot < 4; rot++) {
                    Rotation rotation = static_cast<Rotation>(rot);
                    MediaContainer* rotated_logo = get_demo_rotated_logo(rotation);
                    if (rotated_logo) {
                        screen->enqueue(rotated_logo);
                        screen->update();
                        delay(1000); // 1 second each
                    }
                }
                break;
                
            case 5:
                Serial.println("Demo: Rotation test - Text");
                // Show text in all 4 rotations, 1 second each
                for (int rot = 0; rot < 4; rot++) {
                    Rotation rotation = static_cast<Rotation>(rot);
                    MediaContainer* rotated_text = get_demo_rotated_text(rotation);
                    if (rotated_text) {
                        screen->enqueue(rotated_text);
                        screen->update();
                        delay(1000); // 1 second each
                    }
                }
                break;
                
            case 6:
                Serial.println("Demo: Protocol ACK/ERROR test");
                // if (test_ack_error_protocol()) {
                //     MediaContainer* success_msg = print_success("ACK/ERROR Protocol Test PASSED");
                //     screen->enqueue(success_msg);
                // } else {
                //     MediaContainer* error_msg = print_error("ACK/ERROR Protocol Test FAILED");
                //     screen->enqueue(error_msg);
                // }
                // break;
                
            default:
                demo_phase = -1; // Reset
                Serial.println("Demo cycle complete, restarting...");
                break;
        }
        
        demo_phase++;
        last_demo_change = now;
    }
}

/**
 * Test image protocol encoding/decoding with rotation
 */
bool test_image_protocol_encode_decode() {
    // Allocate messages on heap to avoid stack overflow
    Message* testMsg = (Message*)malloc(sizeof(Message));
    Message* decodedMsg = (Message*)malloc(sizeof(Message));
    
    if (!testMsg || !decodedMsg) {
        Serial.println("[TEST] Failed to allocate image protocol test messages on heap");
        if (testMsg) free(testMsg);
        if (decodedMsg) free(decodedMsg);
        return false;
    }
    
    // Create image start message with rotation
    testMsg->hdr.marker = ::SOF_MARKER;
    testMsg->hdr.type = MessageType::IMAGE_TRANSFER_START;
    testMsg->hdr.id = 2;
    
    testMsg->payload.tag = TAG_IMAGE_START;
    ImageStart& is = testMsg->payload.u.imageStart;
    is.imgId = 42;
    is.fmtRes = 0x12;  // Format 1, Resolution 2
    is.delayMs = 100;
    is.totalSize = 8954;
    is.numChunks = 3;
    is.rotation = static_cast<uint8_t>(Rotation::ROT_270);  // Test 270° rotation
    
    // Encode the message
    uint8_t buffer[256];
    size_t encodedSize = encode(buffer, sizeof(buffer), *testMsg);
    
    if (encodedSize == 0) {
        Serial.println("[TEST] Image encode failed");
        free(testMsg);
        free(decodedMsg);
        return false;
    }
    
    Serial.println("[TEST] Image encoded " + String(encodedSize) + " bytes");
    
    // Decode the message
    ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
    
    if (result != ErrorCode::SUCCESS) {
        Serial.println("[TEST] Image decode failed: " + String(static_cast<uint8_t>(result)));
        free(testMsg);
        free(decodedMsg);
        return false;
    }
    
    // Verify the decoded data including rotation
    bool success = true;
    success &= (decodedMsg->hdr.type == MessageType::IMAGE_TRANSFER_START);
    success &= (decodedMsg->payload.tag == TAG_IMAGE_START);
    success &= (decodedMsg->payload.u.imageStart.imgId == 42);
    success &= (decodedMsg->payload.u.imageStart.rotation == static_cast<uint8_t>(Rotation::ROT_270));
    success &= (decodedMsg->payload.u.imageStart.totalSize == 8954);
    success &= (decodedMsg->payload.u.imageStart.numChunks == 3);
    
    if (success) {
        Serial.println("[TEST] Image protocol verification PASSED:");
        Serial.println("  - Image ID: " + String(decodedMsg->payload.u.imageStart.imgId));
        Serial.println("  - Rotation: " + String(decodedMsg->payload.u.imageStart.rotation * 90) + "°");
        Serial.println("  - Total size: " + String(decodedMsg->payload.u.imageStart.totalSize));
    } else {
        Serial.println("[TEST] Image protocol verification FAILED");
    }
    
    free(testMsg);
    free(decodedMsg);
    return success;
}

// ===================== ACK/ERROR MESSAGE HELPERS =====================

/**
 * Creates an ACK message with the specified status
 * @param status The ErrorCode status to include in the ACK
 * @param msg_id The message ID for the ACK
 * @return Encoded ACK message buffer (caller must free)
 */
uint8_t* create_ack_message(ErrorCode status, uint8_t msg_id) {
    Message ackMsg;
    ackMsg.hdr.marker = ::SOF_MARKER;
    ackMsg.hdr.type = MessageType::ACK;
    ackMsg.hdr.id = msg_id;
    
    ackMsg.payload.tag = TAG_ACK;
    ackMsg.payload.u.ack.status = status;
    
    // Allocate buffer for encoded message
    uint8_t* buffer = new uint8_t[256]; // Should be enough for ACK
    size_t encodedSize = encode(buffer, 256, ackMsg);
    
    if (encodedSize == 0) {
        delete[] buffer;
        return nullptr;
    }
    
    return buffer;
}

/**
 * Creates an ERROR message with the specified error code and message
 * @param error_code The ErrorCode to include
 * @param error_text The error message text
 * @param msg_id The message ID for the ERROR
 * @return Encoded ERROR message buffer (caller must free)
 */
uint8_t* create_error_message(ErrorCode error_code, const char* error_text, uint8_t msg_id) {
    Message errorMsg;
    errorMsg.hdr.marker = ::SOF_MARKER;
    errorMsg.hdr.type = MessageType::ERROR;
    errorMsg.hdr.id = msg_id;
    
    errorMsg.payload.tag = TAG_ERROR;
    errorMsg.payload.u.error.code = error_code;
    errorMsg.payload.u.error.len = strlen(error_text);
    strncpy(errorMsg.payload.u.error.text, error_text, sizeof(errorMsg.payload.u.error.text) - 1);
    errorMsg.payload.u.error.text[sizeof(errorMsg.payload.u.error.text) - 1] = '\0'; // Ensure null termination
    
    // Allocate buffer for encoded message
    uint8_t* buffer = new uint8_t[512]; // Should be enough for ERROR with text
    size_t encodedSize = encode(buffer, 512, errorMsg);
    
    if (encodedSize == 0) {
        delete[] buffer;
        return nullptr;
    }
    
    return buffer;
}

/**
 * Sends an ACK message response
 * @param status The ErrorCode status
 * @param msg_id The message ID
 * @return true if successful, false otherwise
 */
bool send_ack_response(ErrorCode status, uint8_t msg_id) {
    uint8_t* ackBuffer = create_ack_message(status, msg_id);
    if (!ackBuffer) {
        Serial.println("[ERROR] Failed to create ACK message");
        return false;
    }
    
    // TODO: Implement actual SPI sending logic here
    // For now, just log the ACK
    Serial.println("[ACK] Sending ACK response - Status: " + String(static_cast<uint8_t>(status)) + ", ID: " + String(msg_id));
    
    delete[] ackBuffer;
    return true;
}

/**
 * Sends an ERROR message response
 * @param error_code The ErrorCode
 * @param error_text The error message text
 * @param msg_id The message ID
 * @return true if successful, false otherwise
 */
bool send_error_response(ErrorCode error_code, const char* error_text, uint8_t msg_id) {
    uint8_t* errorBuffer = create_error_message(error_code, error_text, msg_id);
    if (!errorBuffer) {
        Serial.println("[ERROR] Failed to create ERROR message");
        return false;
    }
    
    // TODO: Implement actual SPI sending logic here
    // For now, just log the ERROR
    Serial.println("[ERROR] Sending ERROR response - Code: " + String(static_cast<uint8_t>(error_code)) + ", ID: " + String(msg_id));
    Serial.println("[ERROR] Message: " + String(error_text));
    
    delete[] errorBuffer;
    return true;
}

/**
 * Test function to verify ACK and ERROR message encoding/decoding
 */
bool test_ack_error_protocol() {
    Serial.println("[TEST] Testing ACK/ERROR protocol encoding/decoding...");
    
    // Test ACK message
    {
        uint8_t* ackBuffer = create_ack_message(ErrorCode::SUCCESS, 42);
        if (!ackBuffer) {
            Serial.println("[TEST] Failed to create ACK message");
            return false;
        }
        
        // Decode the ACK message
        Message decodedMsg;
        ErrorCode result = decode(ackBuffer, 256, decodedMsg);
        
        if (result != ErrorCode::SUCCESS) {
            Serial.println("[TEST] Failed to decode ACK message");
            delete[] ackBuffer;
            return false;
        }
        
        if (decodedMsg.hdr.type != MessageType::ACK || 
            decodedMsg.payload.u.ack.status != ErrorCode::SUCCESS) {
            Serial.println("[TEST] ACK message decode verification failed");
            delete[] ackBuffer;
            return false;
        }
        
        delete[] ackBuffer;
        Serial.println("[TEST] ACK message test PASSED");
    }
    
    // Test ERROR message
    {
        const char* testError = "Test error message";
        uint8_t* errorBuffer = create_error_message(ErrorCode::OUT_OF_MEMORY, testError, 43);
        if (!errorBuffer) {
            Serial.println("[TEST] Failed to create ERROR message");
            return false;
        }
        
        // Decode the ERROR message
        Message decodedMsg;
        ErrorCode result = decode(errorBuffer, 512, decodedMsg);
        
        if (result != ErrorCode::SUCCESS) {
            Serial.println("[TEST] Failed to decode ERROR message");
            delete[] errorBuffer;
            return false;
        }
        
        if (decodedMsg.hdr.type != MessageType::ERROR || 
            decodedMsg.payload.u.error.code != ErrorCode::OUT_OF_MEMORY ||
            strcmp(decodedMsg.payload.u.error.text, testError) != 0) {
            Serial.println("[TEST] ERROR message decode verification failed");
            delete[] errorBuffer;
            return false;
        }
        
        delete[] errorBuffer;
        Serial.println("[TEST] ERROR message test PASSED");
    }
    
    Serial.println("[TEST] ACK/ERROR protocol test completed successfully");
    return true;
}

// ===================== FUNCTION DECLARATIONS =====================

// Core demo functions
MediaContainer* get_demo_textgroup();
MediaContainer* get_demo_fonts();
MediaContainer* get_demo_textgroup_rotated(Rotation rot);
MediaContainer* get_demo_image_rotated(Rotation rot);
MediaContainer* get_demo_revolving_frame(uint8_t frame_index);
MediaContainer* get_demo_startup_logo();
MediaContainer* get_demo_colors();
MediaContainer* get_demo_rotated_logo(Rotation rotation);
MediaContainer* get_demo_rotated_text(Rotation rotation);

// Demo sequence management
void run_demo_sequence(Screen* screen, int& revolving_counter);

// Utility functions (already defined above)
String get_board_status();
bool test_protocol_encode_decode();
bool test_image_protocol_encode_decode();
bool test_ack_error_protocol();

// ACK/ERROR message helpers
uint8_t* create_ack_message(ErrorCode status, uint8_t msg_id);
uint8_t* create_error_message(ErrorCode error_code, const char* error_text, uint8_t msg_id);
bool send_ack_response(ErrorCode status, uint8_t msg_id);
bool send_error_response(ErrorCode error_code, const char* error_text, uint8_t msg_id);

// Helper message functions
MediaContainer* print_error(const char* error_msg);
MediaContainer* print_success(const char* success_msg);

// Message creation helpers
uint8_t** make_test_img_message(const uint8_t* img_data, size_t img_size, 
                                uint8_t img_id, size_t chunk_size, uint8_t msg_id_start);
uint8_t* make_test_text_message(const char* text, uint16_t x, uint16_t y, 
                               FontID font, uint8_t msg_id);

// Animation demo
MediaContainer* demo_revolving_animation(int& frame_counter);

/**
 * Creates a comprehensive rotation test with visual markers
 */
MediaContainer* get_rotation_test_pattern(Rotation rot) {
    TextGroup* group = new TextGroup(3000, DICE_BLACK, DICE_WHITE, rot);
    
    // Add corner markers to visualize rotation
    group->add_member(new Text("TL", 0, FontID::TF, 20, 40));     // Top Left
    group->add_member(new Text("TR", 0, FontID::TF, 440, 40));    // Top Right  
    group->add_member(new Text("BL", 0, FontID::TF, 20, 440));    // Bottom Left
    group->add_member(new Text("BR", 0, FontID::TF, 440, 440));   // Bottom Right
    
    // Center text showing rotation
    String rotText = "ROT " + String(static_cast<uint8_t>(rot) * 90) + "°";
    group->add_member(new Text(rotText.c_str(), 0, FontID::TF, 200, 240));
    
    // Add direction indicators
    group->add_member(new Text("^UP", 0, FontID::TF, 220, 100));     // Up direction
    group->add_member(new Text("DOWN", 0, FontID::TF, 200, 380));    // Down direction
    
    return group;
}

} // namespace dice

#endif // DICE_EXAMPLES_H
