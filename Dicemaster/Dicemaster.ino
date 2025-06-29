#include "screen.h"
#include "spi.h"
#include "examples.h"  // New comprehensive examples
#include "tests.h"     // New test suite
#include "jpg.hs/umlogo_sq240.h"
using namespace dice;

Screen* screen;
SPIDriver* spid;
TestSuite* test_suite;  // Test suite for comprehensive demos

// Demo mode configuration
bool demo_mode_enabled = true;  // Set to false for normal SPI operation
int revolving_counter = 0;      // Counter for revolving animation demo

void setup(void)
{
  // Init serial
  Serial.begin(115200);
  Serial.println("Begin DiceMaster Screen Module");

  // Check PSRAM Init Status
  if (psramInit()) Serial.println("\nPSRAM correctly initialized");
  else Serial.println("PSRAM not available");

  spid = new SPIDriver();
  screen = new Screen();
  test_suite = new TestSuite(screen, spid);  // Initialize test suite
  
  Serial.println("=== DiceMaster Examples and Tests Available ===");
  Serial.println("Demo mode: " + String(demo_mode_enabled ? "ENABLED" : "DISABLED"));
  Serial.println("To run full test suite, set demo_mode_enabled = false in code");
}

void loop()
{
  if (demo_mode_enabled) {
    // ===============================
    // DEMO MODE - Showcase examples
    // ===============================
    
    static unsigned long last_demo_change = 0;
    static int demo_phase = 0;
    static bool test_suite_run = false;
    unsigned long now = millis();
    
    // Run comprehensive test suite once at startup
    if (!test_suite_run) {
      test_suite->run_demo_tests();
      test_suite_run = true;
      last_demo_change = now; // Reset timer after tests
    }
    
    // Change demo every 4 seconds
    if (now - last_demo_change > 4000) {
      switch (demo_phase) {
        case 0:
          Serial.println("Demo: Multi-language text");
          screen->enqueue(get_demo_textgroup());
          break;
          
        case 1:
          Serial.println("Demo: Font capabilities");
          screen->enqueue(get_demo_fonts());
          break;
          
        case 2:
          Serial.println("Demo: Rotation test - 0°");
          screen->enqueue(get_demo_textgroup_rotated(Rotation::ROT_0));
          break;
          
        case 3:
          Serial.println("Demo: Rotation test - 90°");
          screen->enqueue(get_demo_textgroup_rotated(Rotation::ROT_90));
          break;
          
        case 4:
          Serial.println("Demo: Rotation test - 180°");
          screen->enqueue(get_demo_textgroup_rotated(Rotation::ROT_180));
          break;
          
        case 5:
          Serial.println("Demo: Rotation test - 270°");
          screen->enqueue(get_demo_textgroup_rotated(Rotation::ROT_270));
          break;
          
        case 6:
          Serial.println("Demo: Image rotation - 0°");
          {
            MediaContainer* rotated_img = get_demo_image_rotated(Rotation::ROT_0);
            if (rotated_img) {
              while (rotated_img->get_status() != MediaStatus::READY && 
                     rotated_img->get_status() != MediaStatus::EXPIRED) {
                delay(10);
              }
              screen->enqueue(rotated_img);
            }
          }
          break;
          
        case 7:
          Serial.println("Demo: Image rotation - 90°");
          {
            MediaContainer* rotated_img = get_demo_image_rotated(Rotation::ROT_90);
            if (rotated_img) {
              while (rotated_img->get_status() != MediaStatus::READY && 
                     rotated_img->get_status() != MediaStatus::EXPIRED) {
                delay(10);
              }
              screen->enqueue(rotated_img);
            }
          }
          break;
          
        case 8:
          Serial.println("Demo: Startup logo");
          screen->enqueue(get_demo_startup_logo());
          break;
          
        case 9:
          Serial.println("Demo: Revolving logo animation");
          // Show several frames in sequence
          for (int i = 0; i < 4; i++) {
            MediaContainer* frame = get_demo_revolving_frame(revolving_counter % revolving_umlogo_12_count);
            if (frame) {
              // Wait for decode
              while (frame->get_status() != MediaStatus::READY && 
                     frame->get_status() != MediaStatus::EXPIRED) {
                delay(1);
              }
              screen->enqueue(frame);
              screen->update();
              delay(200); // Fast animation
              revolving_counter++;
            }
          }
          break;
          
        case 10:
          Serial.println("Demo: Protocol test");
          // Show protocol test results
          {
            bool test_result = test_protocol_encode_decode();
            String status = get_board_status();
            Serial.println("Protocol test: " + String(test_result ? "PASS" : "FAIL"));
            Serial.println("Board status: " + status);
            
            TextGroup* protocol_group = new TextGroup(2000, DICE_CYAN, DICE_BLACK);
            protocol_group->add_member(new Text("PROTOCOL", 0, FontID::TF, 180, 180));
            protocol_group->add_member(new Text("TEST", 0, FontID::TF, 200, 230));
            protocol_group->add_member(new Text(test_result ? "PASS" : "FAIL", 0, FontID::TF, 200, 280));
            screen->enqueue(protocol_group);
          }
          break;
          
        case 11:
          Serial.println("Demo: Color test cycle");
          // Show multiple color backgrounds in sequence
          for (int color_cycle = 0; color_cycle < 6; color_cycle++) {
            MediaContainer* color_demo = get_demo_colors();
            if (color_demo) {
              screen->enqueue(color_demo);
              screen->update();
              delay(1200); // Show each color for 1.2 seconds
            }
          }
          break;
          
        default:
          demo_phase = -1; // Reset
          Serial.println("Demo cycle complete, restarting...");
          break;
      }
      
      demo_phase++;
      last_demo_change = now;
    }
    
    // Update screen
    screen->update();
    delay(50);
    
  } else {
    // ===============================
    // NORMAL SPI OPERATION MODE
    // ===============================
    
    // Queue Message Receipt
    spid->queue_cmd_msgs();

    // Show existing content
    screen->update();

    // Process received messages
    std::vector<MediaContainer*> new_content = spid->process_msgs();
    for (size_t i = 0; i < new_content.size(); ++i) {
      screen->enqueue(new_content[i]);
    }

    delay(1);
  }
  
  // Check buttons for manual test trigger (optional)
  if (screen->down_button_pressed() && screen->up_button_pressed()) {
    Serial.println("Both buttons pressed - running full test suite!");
    test_suite->run_demo_tests();
    delay(1000); // Debounce
  }
}
