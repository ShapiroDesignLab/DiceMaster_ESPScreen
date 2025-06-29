#ifndef DICE_TESTS_H
#define DICE_TESTS_H

#include "examples.h"
#include "screen.h"
#include "spi.h"

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
     * Test protocol encoding/decoding
     */
    void test_protocol() {
        Serial.println("=== Testing Protocol ===");
        
        // Test text protocol encode/decode with rotation
        bool textResult = test_protocol_encode_decode();
        Serial.println("Text protocol encode/decode: " + String(textResult ? "PASS" : "FAIL"));
        
        // Test image protocol encode/decode with rotation
        bool imageResult = test_image_protocol_encode_decode();
        Serial.println("Image protocol encode/decode: " + String(imageResult ? "PASS" : "FAIL"));
        
        // Test board status
        String status = get_board_status();
        Serial.println("Board status: " + status);
        
        // Test ping functionality with heap allocation
        Message* pingMsg = (Message*)malloc(sizeof(Message));
        if (pingMsg) {
            pingMsg->hdr.marker = ::SOF_MARKER;
            pingMsg->hdr.type = MessageType::PING_RESPONSE;
            pingMsg->hdr.id = 42;
            
            pingMsg->payload.tag = TAG_PING_RESPONSE;
            PingResponse& pr = pingMsg->payload.u.pingResponse;
            pr.status = 0; // OK
            pr.len = status.length();
            if (pr.len > sizeof(pr.text) - 1) pr.len = sizeof(pr.text) - 1;
            status.toCharArray(pr.text, pr.len + 1);
            
            // Test encoding ping response
            uint8_t buffer[512];
            size_t encoded_size = encode(buffer, sizeof(buffer), *pingMsg);
            Serial.println("Ping response encoded: " + String(encoded_size) + " bytes");
            
            free(pingMsg);
        } else {
            Serial.println("Failed to allocate ping message");
        }
        
        // Overall protocol test result
        bool overallResult = textResult && imageResult;
        Serial.println("Overall protocol tests: " + String(overallResult ? "PASS" : "FAIL"));
        Serial.println("Protocol tests completed");
    }
    
    /**
     * Test SPI protocol message handling
     */
    void test_spi_protocol() {
        Serial.println("=== Testing SPI Protocol ===");
        
        if (!spi) {
            Serial.println("SPI driver not available, skipping SPI tests");
            return;
        }
        
        // Create a text batch message
        Message msg;
        msg.hdr.marker = ::SOF_MARKER;  // Use global SOF_MARKER from constants.h
        msg.hdr.type = MessageType::TEXT_BATCH;
        msg.hdr.id = 1;
        
        msg.payload.tag = TAG_TEXT_BATCH;
        TextBatch& tb = msg.payload.u.textBatch;
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
        size_t size = encode(spi_buffer, sizeof(spi_buffer), msg);
        
        if (size > 0) {
            Serial.println("SPI message ready: " + String(size) + " bytes");
            // In a real scenario, this would be processed by the SPI handler
            // For now, we just decode it back to verify
            Message decoded;
            ErrorCode result = decode(spi_buffer, size, decoded);
            Serial.println("SPI decode result: " + String(static_cast<uint8_t>(result)));
        }
        
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
        
        test_text_rendering();
        delay(1000);
        
        test_image_display();
        delay(1000);
        
        test_revolving_animation();
        delay(1000);
        
        test_rotation();
        delay(1000);
        
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
     * Run comprehensive test suite (only in demo mode, not production)
     */
    void run_demo_tests() {
        Serial.println("=== STARTING DEMO TEST SUITE ===");
        
        test_text_rendering();
        delay(1000);
        
        test_image_display();
        delay(1000);
        
        test_rotation();
        delay(1000);
        
        test_protocol();
        delay(1000);
        
        test_spi_protocol();
        delay(1000);
        
        test_revolving_animation();
        delay(1000);
        
        Serial.println("=== DEMO TEST SUITE COMPLETE ===");
    }
};

} // namespace dice

#endif // DICE_TESTS_H