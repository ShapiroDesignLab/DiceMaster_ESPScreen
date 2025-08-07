#ifndef DICE_TESTS_H
#define DICE_TESTS_H

#include "examples.h"
#include "screen.h"
#include "spi.h"
#include "media.h"
#include "protocol.h"
#include "jpg.hs/logo.h"

namespace dice {

/**
 * Comprehensive test suite for the DiceMaster screen module
 */
class TestSuite {
private:
    Screen* screen;
    SPIDriver* spi;
    int revolving_frame_counter = 0;
    
public:
    TestSuite(Screen* scr, SPIDriver* spi_driver) : screen(scr), spi(spi_driver) {}
    
    /**
     * Test basic text rendering capabilities
     */
    void test_text_rendering() {
        Serial.println("=== Testing Text Rendering ===");
        
        // Test multi-language support
        MediaContainer* multilang = get_demo_textgroup();
        screen->enqueue(multilang);
        screen->update();
        delay(3000);
        
        // Test font capabilities  
        MediaContainer* fonts = get_demo_fonts();
        screen->enqueue(fonts);
        screen->update();
        delay(3000);
        
        Serial.println("Text rendering tests completed");
    }
    
    /**
     * Test image decoding and display
     */
    void test_image_display() {
        Serial.println("=== Testing Image Display ===");
        
        // Test startup logo
        Serial.println("[TEST] Creating startup logo...");
        MediaContainer* logo = get_demo_startup_logo();
        if (logo) {
            Serial.println("[TEST] Waiting for logo decode...");
            // Wait for decoding to complete
            while (logo->get_status() != MediaStatus::READY && 
                   logo->get_status() != MediaStatus::EXPIRED) {
                delay(10);
            }
            Serial.println("[TEST] Logo status: " + String(static_cast<int>(logo->get_status())));
            screen->enqueue(logo);
            screen->update();
            delay(2000);
            Serial.println("[TEST] Logo display completed");
        }
        
        // Add some delay and memory check between images
        delay(500);
        Serial.println("[MEMORY] Free PSRAM between images: " + String(ESP.getFreePsram()));
        
        // Test single revolving frame
        Serial.println("[TEST] Creating revolving frame...");
        MediaContainer* frame = get_demo_revolving_frame(0);
        if (frame) {
            Serial.println("[TEST] Waiting for frame decode...");
            while (frame->get_status() != MediaStatus::READY && 
                   frame->get_status() != MediaStatus::EXPIRED) {
                delay(10);
            }
            Serial.println("[TEST] Frame status: " + String(static_cast<int>(frame->get_status())));
            screen->enqueue(frame);
            screen->update();
            delay(1000);
            Serial.println("[TEST] Frame display completed");
        }
        
        Serial.println("Image display tests completed");
    }
    
    /**
     * Test revolving logo animation
     */
    void test_revolving_animation() {
        Serial.println("=== Testing Revolving Animation ===");
        
        // Cycle through all 12 frames twice
        for (int cycle = 0; cycle < 2; cycle++) {
            for (int frame = 0; frame < revolving_umlogo_12_count; frame++) {
                MediaContainer* frame_img = get_demo_revolving_frame(frame);
                if (frame_img) {
                    // Wait for decoding
                    while (frame_img->get_status() != MediaStatus::READY && 
                           frame_img->get_status() != MediaStatus::EXPIRED) {
                        delay(1);
                    }
                    screen->enqueue(frame_img);
                    screen->update();
                    delay(83); // ~12 FPS (1000ms/12frames = 83ms)
                }
            }
        }
        
        Serial.println("Revolving animation tests completed");
    }
    
    /**
     * Test rotation capabilities
     */
    void test_rotation() {
        Serial.println("=== Testing Rotation ===");
        
        // Test text rotation through all 4 orientations
        for (int i = 0; i < 4; i++) {
            Rotation rot = static_cast<Rotation>(i);
            Serial.println("Testing text rotation: " + String(i * 90) + " degrees");
            
            MediaContainer* rotated_text = get_demo_textgroup_rotated(rot);
            screen->enqueue(rotated_text);
            screen->update();
            delay(2000);
        }
        
        // Test image rotation through all 4 orientations
        for (int i = 0; i < 4; i++) {
            Rotation rot = static_cast<Rotation>(i);
            Serial.println("Testing image rotation: " + String(i * 90) + " degrees");
            Serial.println("[TEST] Creating image with rotation enum value: " + String(static_cast<uint8_t>(rot)));
            
            MediaContainer* rotated_img = get_demo_image_rotated(rot);
            if (rotated_img) {
                // Wait for decoding
                while (rotated_img->get_status() != MediaStatus::READY && 
                       rotated_img->get_status() != MediaStatus::EXPIRED) {
                    delay(10);
                }
                Serial.println("[TEST] Image ready, rotation value: " + String(static_cast<uint8_t>(rotated_img->get_rotation())));
                screen->enqueue(rotated_img);
                screen->update();
                delay(2000); // Wait for image to display and start expiring
                
                // Add extra delay to ensure the current image expires before next test
                delay(500);
            }
        }
        
        Serial.println("Rotation tests completed");
    }
    
    /**
     * Comprehensive protocol test - tests all available features
     * Includes: text, image, gif, rotation, color, and all message types
     * 
     * TIMING APPROACH: All protocol delayMs fields are set to 0 for immediate,
     * event-driven display. Test code uses delay() to simulate intervals between
     * messages from the master device, eliminating display latency.
     */
    void test_protocol() {
        Serial.println("=== COMPREHENSIVE PROTOCOL TESTING ===");
        
        // Test 1: Text Protocol with Multiple Languages and Fonts
        test_text_protocol();
        delay(3000); // Wait for content to expire
        
        // Test 2: Image Protocol with Rotation
        test_image_protocol();
        delay(3000);
        
        // Test 3: Animation (GIF-like) Protocol
        test_animation_protocol();
        delay(3000);
        
        // Test 4: Rotation Protocol (2 orientations: 0°, 180°)
        test_rotation_protocol();
        delay(3000);
        
        // Test 5: Color Protocol
        test_color_protocol();
        delay(3000);
        
        // Test 6: All Protocol Message Types (Encode/Decode) - This is the critical test
        test_all_message_types();
        delay(3000);
        
        Serial.println("=== COMPREHENSIVE PROTOCOL TESTING COMPLETE ===");
    }

private:
    /**
     * Test text protocol: encode -> decode -> display (2 examples)
     * CRITICAL: Message struct is 3160 bytes - too large for stack!
     */
    void test_text_protocol() {
        Serial.println("--- Testing Text Protocol ---");
        
        // Wait for any previous content to fully expire
        delay(500);
        
        // SOLUTION: Use heap allocation for large Message structures
        Serial.println("[DEBUG] Message size: " + String(sizeof(Message)) + " bytes");
        Serial.println("[DEBUG] Available stack space: " + String(uxTaskGetStackHighWaterMark(NULL)) + " words");
        Serial.println("[MEMORY] Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("[CRITICAL] Message too large for stack - using heap allocation");
        
        // Example 1: Simple ASCII test with heap allocation
        {
            // Allocate Message on heap to avoid stack overflow
            Message* textMsg = new Message();
            if (!textMsg) {
                Serial.println("[ERROR] Failed to allocate Message on heap");
                return;
            }
            
            Serial.println("[DEBUG] Message allocated on heap at: 0x" + String((uint32_t)textMsg, HEX));
            
            // Validate protocol constants first
            Serial.println("[DEBUG] SOF_MARKER: 0x" + String(::SOF_MARKER, HEX));
            Serial.println("[DEBUG] MessageType::TEXT_BATCH: " + String(static_cast<int>(MessageType::TEXT_BATCH)));
            Serial.println("[DEBUG] TAG_TEXT_BATCH: " + String(static_cast<int>(TAG_TEXT_BATCH)));
            
            textMsg->hdr.marker = ::SOF_MARKER;
            textMsg->hdr.type = MessageType::TEXT_BATCH;
            textMsg->hdr.id = 100;
            
            textMsg->payload.tag = TAG_TEXT_BATCH;
            TextBatch& tb = textMsg->payload.u.textBatch;
            tb.itemCount = 1; // Start with just one item
            tb.rotation = static_cast<uint8_t>(Rotation::ROT_0);
            
            // Simple ASCII test
            tb.items[0].x = 50; tb.items[0].y = 150;
            tb.items[0].font = static_cast<uint8_t>(FontID::TF);
            tb.items[0].color = 0xFF; tb.items[0].len = 4;
            strcpy(tb.items[0].text, "TEST");
            Serial.println("[DEBUG] Item 0: " + String(tb.items[0].text) + " len=" + String(tb.items[0].len));
            
            // Validate message structure before encoding
            Serial.println("[DEBUG] Validating message structure:");
            Serial.println("  Marker: 0x" + String(textMsg->hdr.marker, HEX));
            Serial.println("  Type: " + String(static_cast<int>(textMsg->hdr.type)));
            Serial.println("  ID: " + String(textMsg->hdr.id));
            Serial.println("  Payload tag: " + String(static_cast<int>(textMsg->payload.tag)));
            Serial.println("  Item count: " + String(tb.itemCount));
            
            uint8_t buffer[512];
            Serial.println("[DEBUG] About to encode simple message...");
            Serial.println("[MEMORY] Free heap before encode: " + String(ESP.getFreeHeap()));
            
            yield(); // Watchdog reset
            
            size_t encodedSize = 0;
            try {
                encodedSize = encode(buffer, sizeof(buffer), *textMsg);
                Serial.println("[ENCODE] Simple text: " + String(encodedSize) + " bytes");
                Serial.println("[MEMORY] Free heap after encode: " + String(ESP.getFreeHeap()));
            } catch (...) {
                Serial.println("[ERROR] Exception caught during encoding!");
                delete textMsg;
                return;
            }
            
            yield(); // Watchdog reset
            
            if (encodedSize > 0) {
                printBuffer(buffer, encodedSize, "Simple encoded message");
                
                Serial.println("[DEBUG] Encoding successful, now decoding...");
                
                // Also allocate decode message on heap
                Message* decodedMsg = new Message();
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message on heap");
                    delete textMsg;
                    return;
                }
                
                ErrorCode result = ErrorCode::SUCCESS;
                try {
                    result = decode(buffer, encodedSize, *decodedMsg);
                    Serial.println("[DEBUG] Decode result: " + String(static_cast<int>(result)));
                } catch (...) {
                    Serial.println("[ERROR] Exception caught during decoding!");
                    delete textMsg;
                    delete decodedMsg;
                    return;
                }
                
                if (result == ErrorCode::SUCCESS) {
                    Serial.println("[DEBUG] Decode successful, creating display group...");
                    TextGroup* group = new TextGroup(2000, DICE_BLACK, DICE_WHITE);
                    if (!group) {
                        Serial.println("[ERROR] Failed to allocate TextGroup");
                        delete textMsg;
                        delete decodedMsg;
                        return;
                    }
                    
                    TextBatch& decoded_tb = decodedMsg->payload.u.textBatch;
                    Serial.println("[DEBUG] Decoded item count: " + String(decoded_tb.itemCount));
                    
                    for (int i = 0; i < decoded_tb.itemCount; i++) {
                        Serial.println("[DEBUG] Adding decoded item " + String(i) + ": " + String(decoded_tb.items[i].text));
                        Text* textObj = new Text(
                            decoded_tb.items[i].text, 0,
                            static_cast<FontID>(decoded_tb.items[i].font),
                            decoded_tb.items[i].x, decoded_tb.items[i].y
                        );
                        if (!textObj) {
                            Serial.println("[ERROR] Failed to allocate Text object for item " + String(i));
                            delete group;
                            delete textMsg;
                            delete decodedMsg;
                            return;
                        }
                        group->add_member(textObj);
                    }
                    
                    Serial.println("[DEBUG] About to enqueue and display...");
                    screen->enqueue(group);
                    screen->update();
                    Serial.println("[DISPLAY] Simple text displayed");
                    
                } else {
                    Serial.println("[ERROR] Decode failed with error code: " + String(static_cast<int>(result)));
                }
                
                // Clean up heap allocations
                delete decodedMsg;
            } else {
                Serial.println("[ERROR] Encoding failed, returned 0 bytes");
            }
            
            // Clean up heap allocation
            delete textMsg;
            Serial.println("[DEBUG] Heap Message cleaned up");
        }
        
        delay(3000); // Wait longer between tests
        
        // Example 2: Multi-language text with heap allocation
        {
            Serial.println("[DEBUG] Starting multi-language test with heap allocation...");
            
            Message* textMsg = new Message(); // Heap allocation
            if (!textMsg) {
                Serial.println("[ERROR] Failed to allocate second Message on heap");
                return;
            }
            
            textMsg->hdr.marker = ::SOF_MARKER;
            textMsg->hdr.type = MessageType::TEXT_BATCH;
            textMsg->hdr.id = 101;
            
            textMsg->payload.tag = TAG_TEXT_BATCH;
            TextBatch& tb = textMsg->payload.u.textBatch;
            tb.itemCount = 2;
            tb.rotation = static_cast<uint8_t>(Rotation::ROT_0);
            
            // Large font
            tb.items[0].x = 100; tb.items[0].y = 180;
            tb.items[0].font = static_cast<uint8_t>(FontID::TF);
            tb.items[0].color = 0xF8; tb.items[0].len = 5;
            strcpy(tb.items[0].text, "FONTS");
            
            // Small font
            tb.items[1].x = 100; tb.items[1].y = 220;
            tb.items[1].font = static_cast<uint8_t>(FontID::TF); // Use TF instead of CHINESE for ASCII
            tb.items[1].color = 0x07; tb.items[1].len = 4;
            strcpy(tb.items[1].text, "TEST");
            
            uint8_t buffer[512];
            size_t encodedSize = encode(buffer, sizeof(buffer), *textMsg);
            Serial.println("[DEBUG] ASCII text encoded: " + String(encodedSize) + " bytes");
            
            if (encodedSize > 0) {
                Message* decodedMsg = new Message(); // Heap allocation
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message for multi-language test");
                    delete textMsg;
                    return;
                }
                
                ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                if (result == ErrorCode::SUCCESS) {
                    TextGroup* group = new TextGroup(2000, DICE_BLACK, DICE_WHITE);
                    if (group) {
                        TextBatch& decoded_tb = decodedMsg->payload.u.textBatch;
                        for (int i = 0; i < decoded_tb.itemCount; i++) {
                            group->add_member(new Text(
                                decoded_tb.items[i].text, 0,
                                static_cast<FontID>(decoded_tb.items[i].font),
                                decoded_tb.items[i].x, decoded_tb.items[i].y
                            ));
                        }
                        screen->enqueue(group);
                        screen->update();
                        Serial.println("[DISPLAY] Font variety displayed");
                    }
                } else {
                    Serial.println("[ERROR] ASCII decode failed with error code: " + String(static_cast<int>(result)));
                }
                
                delete decodedMsg;
            } else {
                Serial.println("[ERROR] ASCII encoding failed");
            }
            
            delete textMsg;
            Serial.println("[DEBUG] Multi-language test completed, heap cleaned up");
        }
    }
    
    /**
     * Test image protocol: encode -> decode -> display (2 examples)
     */
    void test_image_protocol() {
        Serial.println("--- Testing Image Protocol ---");
        
        // Wait for previous content to fully expire
        delay(500);
        
        // Example 1: Image protocol encoding/decoding
        {
            Message* imgMsg = new Message(); // Use heap allocation
            if (!imgMsg) {
                Serial.println("[ERROR] Failed to allocate Image Message on heap");
                return;
            }
            
            imgMsg->hdr.marker = ::SOF_MARKER;
            imgMsg->hdr.type = MessageType::IMAGE_TRANSFER_START;
            imgMsg->hdr.id = 200;
            
            imgMsg->payload.tag = TAG_IMAGE_START;
            ImageStart& is = imgMsg->payload.u.imageStart;
            is.imgId = 50;
            is.fmtRes = 0x11; // JPEG, 240x240
            is.delayMs = 0; // Immediate display (event-driven)
            is.totalSize = 10000;
            is.numChunks = 1;
            is.rotation = static_cast<uint8_t>(Rotation::ROT_90);
            
            uint8_t buffer[256];
            size_t encodedSize = encode(buffer, sizeof(buffer), *imgMsg);
            Serial.println("[ENCODE] Image start: " + String(encodedSize) + " bytes");
            
            if (encodedSize > 0) {
                Message* decodedMsg = new Message(); // Use heap allocation
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message for image test");
                    delete imgMsg;
                    return;
                }
                
                ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                if (result == ErrorCode::SUCCESS) {
                    ImageStart& decoded_is = decodedMsg->payload.u.imageStart;
                    
                    TextGroup* imgGroup = new TextGroup(2000, DICE_BLUE, DICE_WHITE); // Shorter duration
                    imgGroup->add_member(new Text("IMAGE", 0, FontID::TF, 180, 180));
                    imgGroup->add_member(new Text("PROTOCOL", 0, FontID::TF, 160, 220));
                    imgGroup->add_member(new Text("ID: " + String(decoded_is.imgId), 0, FontID::TF, 170, 260));
                    imgGroup->add_member(new Text("ROT: " + String(decoded_is.rotation * 90) + "°", 0, FontID::TF, 160, 300));
                    
                    screen->enqueue(imgGroup);
                    screen->update();
                    Serial.println("[DISPLAY] Image protocol info displayed");
                }
                
                delete decodedMsg;
            }
            
            delete imgMsg;
        }
        
        // Wait for content to expire before next test
        delay(2500);
        
        // Example 2: Show actual image from memory
        Image* img = new Image(128, ImageFormat::JPEG, ImageResolution::SQ480, logo_SIZE, 2000, 1, Rotation::ROT_0);
        img->add_chunk(logo, logo_SIZE);
        screen->enqueue(img);
        screen->update();
        Serial.println("[DISPLAY] Actual image displayed");
    }
    
    /**
     * Test animation protocol: encode -> decode -> display (2 examples)
     * NOTE: Protocol delayMs set to 0 for immediate display; test code uses delay()
     * to control frame timing and simulate master device message intervals.
     */
    void test_animation_protocol() {
        Serial.println("--- Testing Animation Protocol ---");
        
        // Wait for previous content to fully expire
        delay(500);
        
        // Example 1: Protocol-encoded animation sequence (2 frames only for safety)
        const int numFrames = 2;
        for (int frame = 0; frame < numFrames; frame++) {
            // Use heap allocation for animation message
            Message* animMsg = new Message();
            if (!animMsg) {
                Serial.println("[ERROR] Failed to allocate animation Message on heap");
                return;
            }
            
            animMsg->hdr.marker = ::SOF_MARKER;
            animMsg->hdr.type = MessageType::IMAGE_TRANSFER_START;
            animMsg->hdr.id = 300 + frame;
            
            animMsg->payload.tag = TAG_IMAGE_START;
            ImageStart& is = animMsg->payload.u.imageStart;
            is.imgId = 60 + frame;
            is.fmtRes = 0x11;
            is.delayMs = 0; // Immediate display (event-driven)
            is.totalSize = 8000 + frame * 100;
            is.numChunks = 1;
            is.rotation = static_cast<uint8_t>(Rotation::ROT_0);
            
            Serial.println("[DEBUG] Testing animation frame " + String(frame) + " with heap allocation");
            
            uint8_t buffer[256];
            size_t encodedSize = encode(buffer, sizeof(buffer), *animMsg);
            
            if (encodedSize > 0) {
                Message* decodedMsg = new Message(); // Use heap allocation for decode too
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message for animation test");
                    delete animMsg;
                    return;
                }
                
                ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                if (result == ErrorCode::SUCCESS) {
                    // Display actual revolving frame
                    MediaContainer* actualFrame = get_demo_revolving_frame(frame * 6); // Larger gap between frames
                    if (actualFrame) {
                        while (actualFrame->get_status() != MediaStatus::READY && 
                               actualFrame->get_status() != MediaStatus::EXPIRED) {
                            delay(10);
                        }
                        if (actualFrame->get_status() == MediaStatus::READY) {
                            screen->enqueue(actualFrame);
                            screen->update();
                            Serial.println("[DISPLAY] Animation frame " + String(frame));
                        }
                    }
                } else {
                    Serial.println("[ERROR] Animation decode failed with error: " + String(static_cast<int>(result)));
                }
                
                delete decodedMsg;
            } else {
                Serial.println("[ERROR] Animation encoding failed");
            }
            
            delete animMsg;
            
            // Wait for frame to display, then simulate master device interval (not protocol delay)
            delay(2000); // Frame display duration controlled by test code, not protocol
        }
        
        // Example 2: Show completion message
        delay(500); // Extra safety gap
        TextGroup* complete = new TextGroup(1500, DICE_GREEN, DICE_BLACK);
        complete->add_member(new Text("ANIMATION", 0, FontID::TF, 150, 220));
        complete->add_member(new Text("COMPLETE", 0, FontID::TF, 160, 260));
        screen->enqueue(complete);
        screen->update();
        Serial.println("[DISPLAY] Animation protocol complete");
    }
    
    /**
     * Test rotation protocol: encode -> decode -> display (2 examples)
     */
    void test_rotation_protocol() {
        Serial.println("--- Testing Rotation Protocol ---");
        
        // Wait for previous content to fully expire
        delay(500);
        
        // Example 1: Test 0° and 180° rotations only (2 examples)
        int rotations[] = {0, 2}; // 0° and 180°
        for (int i = 0; i < 2; i++) {
            int rot = rotations[i];
            
            // Use heap allocation to avoid stack overflow
            Message* rotMsg = new Message();
            if (!rotMsg) {
                Serial.println("[ERROR] Failed to allocate rotation Message on heap");
                return;
            }
            
            rotMsg->hdr.marker = ::SOF_MARKER;
            rotMsg->hdr.type = MessageType::TEXT_BATCH;
            rotMsg->hdr.id = 400 + rot;
            
            rotMsg->payload.tag = TAG_TEXT_BATCH;
            TextBatch& tb = rotMsg->payload.u.textBatch;
            tb.itemCount = 2; // Add two text items to show coordinate transformation
            tb.rotation = static_cast<uint8_t>(rot);
            
            // Text 1: Corner position to clearly show transformation
            String rotText = String(rot * 90) + "°";
            tb.items[0].x = 50;  // Near left edge in 0° rotation
            tb.items[0].y = 50;  // Near top edge in 0° rotation
            tb.items[0].font = static_cast<uint8_t>(FontID::TF);
            tb.items[0].color = 0xE0; tb.items[0].len = rotText.length();
            strcpy(tb.items[0].text, rotText.c_str());
            
            // Text 2: Position indicator to show coordinate mapping
            String posText = "(" + String(tb.items[0].x) + "," + String(tb.items[0].y) + ")";
            tb.items[1].x = 100; tb.items[1].y = 100;
            tb.items[1].font = static_cast<uint8_t>(FontID::TF);
            tb.items[1].color = 0x1F; tb.items[1].len = posText.length();
            strcpy(tb.items[1].text, posText.c_str());
            
            Serial.println("[DEBUG] Testing rotation " + String(rot * 90) + "° with heap allocation");
            
            uint8_t buffer[512];
            size_t encodedSize = encode(buffer, sizeof(buffer), *rotMsg);
            
            if (encodedSize > 0) {
                Message* decodedMsg = new Message(); // Use heap allocation for decode too
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message for rotation test");
                    delete rotMsg;
                    return;
                }
                
                ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                if (result == ErrorCode::SUCCESS) {
                    TextBatch& decoded_tb = decodedMsg->payload.u.textBatch;
                    Rotation decoded_rotation = static_cast<Rotation>(decoded_tb.rotation);
                    
                    TextGroup* rotGroup = new TextGroup(1500, DICE_BLACK, DICE_WHITE, decoded_rotation);
                    if (rotGroup) {
                        rotGroup->add_member(new Text(
                            decoded_tb.items[0].text, 0,
                            static_cast<FontID>(decoded_tb.items[0].font),
                            decoded_tb.items[0].x, decoded_tb.items[0].y
                        ));
                        
                        screen->enqueue(rotGroup);
                        screen->update();
                        Serial.println("[DISPLAY] Rotation " + String(rot * 90) + "°");
                    }
                } else {
                    Serial.println("[ERROR] Rotation decode failed with error: " + String(static_cast<int>(result)));
                }
                
                delete decodedMsg;
            } else {
                Serial.println("[ERROR] Rotation encoding failed");
            }
            
            delete rotMsg;
            
            // Wait for content to expire before next rotation
            delay(2000);
        }
        
        // Example 2: Visual rotation test
        MediaContainer* rotatedContent = get_demo_textgroup_rotated(Rotation::ROT_90);
        if (rotatedContent) {
            screen->enqueue(rotatedContent);
            screen->update();
            Serial.println("[DISPLAY] Visual rotation test");
        }
    }
    
    /**
     * Test color protocol: encode -> decode -> display (2 examples)
     */
    void test_color_protocol() {
        Serial.println("--- Testing Color Protocol ---");
        
        // Wait for previous content to fully expire
        delay(500);
        
        // Example 1: Color background test
        MediaContainer* colorDemo = get_demo_colors();
        if (colorDemo) {
            screen->enqueue(colorDemo);
            screen->update();
            Serial.println("[DISPLAY] Color background test");
        }
        
        // Wait for background test to expire
        delay(2500);
        
        // Example 2: Text with different colors encoded via protocol
        {
            // Use heap allocation for color message
            Message* colorMsg = new Message();
            if (!colorMsg) {
                Serial.println("[ERROR] Failed to allocate color Message on heap");
                return;
            }
            
            colorMsg->hdr.marker = ::SOF_MARKER;
            colorMsg->hdr.type = MessageType::TEXT_BATCH;
            colorMsg->hdr.id = 500;
            
            colorMsg->payload.tag = TAG_TEXT_BATCH;
            TextBatch& tb = colorMsg->payload.u.textBatch;
            tb.itemCount = 3;
            tb.rotation = static_cast<uint8_t>(Rotation::ROT_0);
            
            // Red text
            tb.items[0].x = 50; tb.items[0].y = 180;
            tb.items[0].font = static_cast<uint8_t>(FontID::TF);
            tb.items[0].color = 0xF8; tb.items[0].len = 3; // Red
            strcpy(tb.items[0].text, "RED");
            
            // Green text
            tb.items[1].x = 150; tb.items[1].y = 180;
            tb.items[1].font = static_cast<uint8_t>(FontID::TF);
            tb.items[1].color = 0x07; tb.items[1].len = 5; // Green
            strcpy(tb.items[1].text, "GREEN");
            
            // Blue text
            tb.items[2].x = 280; tb.items[2].y = 180;
            tb.items[2].font = static_cast<uint8_t>(FontID::TF);
            tb.items[2].color = 0x1F; tb.items[2].len = 4; // Blue
            strcpy(tb.items[2].text, "BLUE");
            
            Serial.println("[DEBUG] Testing color protocol with heap allocation");
            
            uint8_t buffer[512];
            size_t encodedSize = encode(buffer, sizeof(buffer), *colorMsg);
            
            if (encodedSize > 0) {
                Message* decodedMsg = new Message(); // Use heap allocation for decode too
                if (!decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate decode Message for color test");
                    delete colorMsg;
                    return;
                }
                
                ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                if (result == ErrorCode::SUCCESS) {
                    TextGroup* colorGroup = new TextGroup(2000, DICE_BLACK, DICE_WHITE); // Shorter duration
                    if (colorGroup) {
                        TextBatch& decoded_tb = decodedMsg->payload.u.textBatch;
                        for (int i = 0; i < decoded_tb.itemCount; i++) {
                            colorGroup->add_member(new Text(
                                decoded_tb.items[i].text, 0,
                                static_cast<FontID>(decoded_tb.items[i].font),
                                decoded_tb.items[i].x, decoded_tb.items[i].y
                            ));
                        }
                        screen->enqueue(colorGroup);
                        screen->update();
                        Serial.println("[DISPLAY] Color text protocol");
                    }
                } else {
                    Serial.println("[ERROR] Color decode failed with error: " + String(static_cast<int>(result)));
                }
                
                delete decodedMsg;
            } else {
                Serial.println("[ERROR] Color encoding failed");
            }
            
            delete colorMsg;
        }
    }
    
    /**
     * Test all protocol message types: encode -> decode (comprehensive)
     */
    void test_all_message_types() {
        Serial.println("--- Testing All Message Types ---");
        
        // Wait for previous content to fully expire  
        delay(500);
        
        bool allPassed = true;
        
        // Memory check before starting
        Serial.println("[MEMORY] Free PSRAM before message tests: " + String(ESP.getFreePsram()));
        
        // 1. TEXT_BATCH
        {
            Serial.println("Testing TEXT_BATCH...");
            bool passed = false;
            
            try {
                // Use heap allocation to avoid stack overflow
                Message* testMsg = new Message();
                Message* decodedMsg = new Message();
                
                if (!testMsg || !decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate Messages for TEXT_BATCH test");
                    delete testMsg;
                    delete decodedMsg;
                    return;
                }
                
                testMsg->hdr.marker = ::SOF_MARKER;
                testMsg->hdr.type = MessageType::TEXT_BATCH;
                testMsg->hdr.id = 1;
                
                testMsg->payload.tag = TAG_TEXT_BATCH;
                TextBatch& tb = testMsg->payload.u.textBatch;
                tb.itemCount = 1;
                tb.rotation = static_cast<uint8_t>(Rotation::ROT_0);
                
                tb.items[0].x = 100;
                tb.items[0].y = 200;
                tb.items[0].font = static_cast<uint8_t>(FontID::TF);
                tb.items[0].color = 0xFF;
                tb.items[0].len = 4;
                strcpy(tb.items[0].text, "TEST");
                
                uint8_t buffer[512];
                size_t encodedSize = encode(buffer, sizeof(buffer), *testMsg);
                
                if (encodedSize > 0) {
                    ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                    passed = (result == ErrorCode::SUCCESS);
                    if (passed) {
                        passed &= (decodedMsg->hdr.type == MessageType::TEXT_BATCH);
                        passed &= (strcmp(decodedMsg->payload.u.textBatch.items[0].text, "TEST") == 0);
                    }
                }
                
                delete testMsg;
                delete decodedMsg;
            } catch (...) {
                passed = false;
                Serial.println("Exception caught in TEXT_BATCH test");
            }
            
            Serial.println("TEXT_BATCH: " + String(passed ? "PASS" : "FAIL"));
            allPassed &= passed;
        }
        
        // 2. IMAGE_TRANSFER_START
        {
            Serial.println("Testing IMAGE_TRANSFER_START...");
            bool passed = false;
            
            try {
                Message* testMsg = new Message();
                Message* decodedMsg = new Message();
                
                if (!testMsg || !decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate Messages for IMAGE test");
                    delete testMsg;
                    delete decodedMsg;
                    return;
                }
                
                testMsg->hdr.marker = ::SOF_MARKER;
                testMsg->hdr.type = MessageType::IMAGE_TRANSFER_START;
                testMsg->hdr.id = 2;
                
                testMsg->payload.tag = TAG_IMAGE_START;
                ImageStart& is = testMsg->payload.u.imageStart;
                is.imgId = 42;
                is.fmtRes = 0x11;
                is.delayMs = 0; // Immediate display (event-driven)
                is.totalSize = 5000;
                is.numChunks = 1;
                is.rotation = static_cast<uint8_t>(Rotation::ROT_90);
                
                uint8_t buffer[256];
                size_t encodedSize = encode(buffer, sizeof(buffer), *testMsg);
                
                if (encodedSize > 0) {
                    ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                    passed = (result == ErrorCode::SUCCESS);
                    if (passed) {
                        passed &= (decodedMsg->hdr.type == MessageType::IMAGE_TRANSFER_START);
                        passed &= (decodedMsg->payload.u.imageStart.imgId == 42);
                        passed &= (decodedMsg->payload.u.imageStart.rotation == static_cast<uint8_t>(Rotation::ROT_90));
                    }
                }
                
                delete testMsg;
                delete decodedMsg;
            } catch (...) {
                passed = false;
                Serial.println("Exception caught in IMAGE_TRANSFER_START test");
            }
            
            Serial.println("IMAGE_TRANSFER_START: " + String(passed ? "PASS" : "FAIL"));
            allPassed &= passed;
        }
        
        // 3. PING_RESPONSE
        {
            Serial.println("Testing PING_RESPONSE...");
            bool passed = false;
            
            try {
                Message* testMsg = new Message();
                Message* decodedMsg = new Message();
                
                if (!testMsg || !decodedMsg) {
                    Serial.println("[ERROR] Failed to allocate Messages for PING test");
                    delete testMsg;
                    delete decodedMsg;
                    return;
                }
                
                testMsg->hdr.marker = ::SOF_MARKER;
                testMsg->hdr.type = MessageType::PING_RESPONSE;
                testMsg->hdr.id = 42;
                
                testMsg->payload.tag = TAG_PING_RESPONSE;
                PingResponse& pr = testMsg->payload.u.pingResponse;
                pr.status = 0;
                pr.len = 4;
                strcpy(pr.text, "PING");
                
                uint8_t buffer[256];
                size_t encodedSize = encode(buffer, sizeof(buffer), *testMsg);
                
                if (encodedSize > 0) {
                    ErrorCode result = decode(buffer, encodedSize, *decodedMsg);
                    passed = (result == ErrorCode::SUCCESS);
                    if (passed) {
                        passed &= (decodedMsg->hdr.type == MessageType::PING_RESPONSE);
                        passed &= (strcmp(decodedMsg->payload.u.pingResponse.text, "PING") == 0);
                    }
                }
                
                delete testMsg;
                delete decodedMsg;
            } catch (...) {
                passed = false;
                Serial.println("Exception caught in PING_RESPONSE test");
            }
            
            Serial.println("PING_RESPONSE: " + String(passed ? "PASS" : "FAIL"));
            allPassed &= passed;
        }
        
        // 4. Error handling test (should fail safely)
        {
            Serial.println("Testing ERROR_HANDLING...");
            bool passed = true; // This test should fail, so we expect that
            
            try {
                Message* testMsg = new Message();
                if (!testMsg) {
                    Serial.println("[ERROR] Failed to allocate Message for error test");
                    return;
                }
                
                testMsg->hdr.marker = 0xFF; // Invalid marker
                testMsg->hdr.type = MessageType::TEXT_BATCH;
                testMsg->hdr.id = 999;
                
                uint8_t buffer[256];
                size_t size = encode(buffer, sizeof(buffer), *testMsg);
                
                if (size > 0) {
                    Message* decoded = new Message();
                    if (decoded) {
                        ErrorCode result = decode(buffer, size, *decoded);
                        passed = (result != ErrorCode::SUCCESS); // Should fail with invalid marker
                        delete decoded;
                    }
                }
                
                delete testMsg;
            } catch (...) {
                // Exception is expected for invalid data
                passed = true;
                Serial.println("Expected exception caught in ERROR_HANDLING test");
            }
            
            Serial.println("ERROR_HANDLING: " + String(passed ? "PASS" : "FAIL"));
            allPassed &= passed;
        }
        
        // Memory check after testing
        Serial.println("[MEMORY] Free PSRAM after message tests: " + String(ESP.getFreePsram()));
        
        // Display overall result with shorter duration
        TextGroup* resultGroup = new TextGroup(2000, allPassed ? DICE_GREEN : DICE_RED, DICE_BLACK); // Shorter duration
        resultGroup->add_member(new Text("ALL MESSAGE", 0, FontID::TF, 140, 180));
        resultGroup->add_member(new Text("TYPES", 0, FontID::TF, 200, 220));
        resultGroup->add_member(new Text(allPassed ? "PASS" : "FAIL", 0, FontID::TF, 200, 260));
        screen->enqueue(resultGroup);
        screen->update();
        
        Serial.println("ALL MESSAGE TYPES: " + String(allPassed ? "PASS" : "FAIL"));
    }

public:
    
    /**
     * Test SPI protocol message handling
     */
    void test_spi_protocol() {
        Serial.println("=== Testing SPI Protocol ===");
        
        if (!spi) {
            Serial.println("SPI driver not available, skipping SPI tests");
            return;
        }
        
        // Create a text batch message with heap allocation
        Message* msg = new Message();
        if (!msg) {
            Serial.println("[ERROR] Failed to allocate SPI Message on heap");
            return;
        }
        
        msg->hdr.marker = ::SOF_MARKER;  // Use global SOF_MARKER from constants.h
        msg->hdr.type = MessageType::TEXT_BATCH;
        msg->hdr.id = 1;
        
        msg->payload.tag = TAG_TEXT_BATCH;
        TextBatch& tb = msg->payload.u.textBatch;
        tb.itemCount = 1;
        tb.rotation = static_cast<uint8_t>(Rotation::ROT_0);
        
        tb.items[0].x = 240;
        tb.items[0].y = 240;
        tb.items[0].font = static_cast<uint8_t>(FontID::TF);
        tb.items[0].color = 0xFF;
        tb.items[0].len = 8;
        strcpy(tb.items[0].text, "SPI TEST");
        
        // Encode message
        uint8_t spi_buffer[512];
        size_t size = encode(spi_buffer, sizeof(spi_buffer), *msg);
        
        if (size > 0) {
            Serial.println("SPI message ready: " + String(size) + " bytes");
            // In a real scenario, this would be processed by the SPI handler
            // For now, we just decode it back to verify
            Message* decoded = new Message();
            if (decoded) {
                ErrorCode result = decode(spi_buffer, size, *decoded);
                Serial.println("SPI decode result: " + String(static_cast<uint8_t>(result)));
                delete decoded;
            }
        }
        
        delete msg;
        
        Serial.println("SPI protocol tests completed");
    }

    /**
     * Test error handling and edge cases
     */
    void test_error_handling() {
        Serial.println("=== Testing Error Handling ===");
        
        // Test invalid frame index
        MediaContainer* invalid_frame = get_demo_revolving_frame(255);
        if (invalid_frame) {
            screen->enqueue(invalid_frame);
            screen->update();
            delay(1000);
        }
        
        // Test memory stress
        Serial.println("Testing memory allocation stress...");
        for (int i = 0; i < 5; i++) {
            MediaContainer* stress_test = get_demo_startup_logo();
            if (stress_test) {
                delete stress_test; // Clean up immediately
            }
        }
        
        Serial.println("Error handling tests completed");
    }
    
    /**
     * Run all tests in sequence
     */
    void run_all_tests() {
        Serial.println("====================================");
        Serial.println("DiceMaster Screen Module Test Suite");
        Serial.println("====================================");
        
        // test_text_rendering();
        // delay(1000);
        
        // test_image_display();
        // delay(1000);
        
        // test_revolving_animation();
        // delay(1000);
        
        // test_rotation();
        // delay(1000);
        
        test_protocol();
        delay(1000);
        
        test_spi_protocol();
        delay(1000);
        
        test_error_handling();
        delay(1000);
        
        Serial.println("====================================");
        Serial.println("All tests completed successfully!");
        Serial.println("====================================");
    }
    
    /**
     * Interactive demo mode - cycles through different content
     */
    void demo_mode() {
        static unsigned long last_switch = 0;
        static int demo_state = 0;
        static int rotation_state = 0;
        
        unsigned long now = millis();
        
        // Switch demo content every 5 seconds
        if (now - last_switch > 5000) {
            switch (demo_state) {
                case 0:
                    screen->enqueue(get_demo_textgroup());
                    break;
                case 1:
                    screen->enqueue(get_demo_startup_logo());
                    break;
                case 2:
                    screen->enqueue(demo_revolving_animation(revolving_frame_counter));
                    break;
                case 3:
                    screen->enqueue(get_demo_colors());
                    break;
                case 4:
                    // Test rotation - cycle through all 4 rotations
                    screen->enqueue(get_demo_textgroup_rotated(static_cast<Rotation>(rotation_state)));
                    rotation_state = (rotation_state + 1) % 4;
                    break;
                case 5:
                    // Run protocol tests (don't enqueue, just test)
                    // Now using safe heap allocation for both text and image protocols
                    {
                        bool textResult = test_protocol_encode_decode();
                        bool imageResult = test_image_protocol_encode_decode();
                        bool overallResult = textResult && imageResult;
                        
                        Serial.println("Text Protocol: " + String(textResult ? "PASS" : "FAIL"));
                        Serial.println("Image Protocol: " + String(imageResult ? "PASS" : "FAIL"));
                        Serial.println("Overall Protocol: " + String(overallResult ? "PASS" : "FAIL"));
                        Serial.println("Board Status: " + get_board_status());
                        
                        // Show test result on screen
                        TextGroup* result_group = new TextGroup(3000, overallResult ? DICE_GREEN : DICE_RED, DICE_BLACK);
                        result_group->add_member(new Text("PROTOCOL", 0, FontID::TF, 180, 200));
                        result_group->add_member(new Text(overallResult ? "PASS" : "FAIL", 0, FontID::TF, 200, 250));
                        result_group->add_member(new Text("COMPLETE", 0, FontID::TF, 180, 300));
                        screen->enqueue(result_group);
                    }
                    break;
                default:
                    demo_state = -1; // Reset to 0 on next increment
                    break;
            }
            
            demo_state++;
            last_switch = now;
        }
        
        // Always update the screen
        screen->update();
    }
    
    /**
     * Run comprehensive test suite (focused on protocol testing)
     */
    void run_demo_tests() {
        Serial.println("=== STARTING COMPREHENSIVE PROTOCOL TEST SUITE ===");
        
        // Run the single comprehensive protocol test
        test_protocol();
        
        Serial.println("=== PROTOCOL TEST SUITE COMPLETE ===");
    }
    
private:
    /**
     * Debug helper to print buffer contents
     */
    void printBuffer(const uint8_t* buffer, size_t size, const String& label) {
        Serial.print("[BUFFER] " + label + ": ");
        for (size_t i = 0; i < size && i < 64; i++) { // Limit to first 64 bytes
            if (buffer[i] < 16) Serial.print("0");
            Serial.print(String(buffer[i], HEX));
            Serial.print(" ");
        }
        if (size > 64) Serial.print("...");
        Serial.println();
    }

};

} // namespace dice

#endif // DICE_TESTS_H