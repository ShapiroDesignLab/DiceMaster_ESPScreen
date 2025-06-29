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
bool demo_mode_enabled = false;  // Set to false for normal SPI operation - focusing on protocol testing
int revolving_counter = 0;       // Counter for revolving animation demo

void setup(void){	
	// Init serial
	Serial.begin(115200);
	Serial.println("Begin DiceMaster Screen Module");

	// Check PSRAM Init Status
	if (psramInit()) Serial.println("\nPSRAM correctly initialized");
	else Serial.println("PSRAM not available");

	spid = new SPIDriver();
	screen = new Screen();
	test_suite = new TestSuite(screen, spid);  // Initialize test suite
	
	// Show startup logo during initialization
	Serial.println("Displaying startup logo...");
	MediaContainer* startup_logo = get_demo_startup_logo();
	if (startup_logo) {
		// Wait for logo decode
		while (startup_logo->get_status() != MediaStatus::READY && 
			startup_logo->get_status() != MediaStatus::EXPIRED) {
		delay(10);
		}
		screen->enqueue(startup_logo);
		screen->update();
		delay(2000); // Show logo for 2 seconds
	}
	
	Serial.println("=== DiceMaster Protocol Testing Mode ===");
	Serial.println("Demo mode: " + String(demo_mode_enabled ? "ENABLED" : "DISABLED"));
	Serial.println("Running comprehensive protocol testing suite...");
}

void loop() {
	if (demo_mode_enabled) {
		// ===============================
		// DEMO MODE - Showcase examples (DISABLED)
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
		
		// Change demo every 6 seconds for protocol testing
		if (now - last_demo_change > 6000) {
			switch (demo_phase) {
				case 0:
				Serial.println("Demo: Multi-language text");
				screen->enqueue(get_demo_textgroup());
				break;
				
				case 1:
				Serial.println("Demo: Protocol test");
				// Show protocol test results
				{
					bool test_result = test_protocol_encode_decode();
					String status = get_board_status();
					Serial.println("Protocol test: " + String(test_result ? "PASS" : "FAIL"));
					Serial.println("Board status: " + status);
					
					TextGroup* protocol_group = new TextGroup(3000, DICE_CYAN, DICE_BLACK);
					protocol_group->add_member(new Text("PROTOCOL", 0, FontID::TF, 180, 180));
					protocol_group->add_member(new Text("TEST", 0, FontID::TF, 200, 230));
					protocol_group->add_member(new Text(test_result ? "PASS" : "FAIL", 0, FontID::TF, 200, 280));
					screen->enqueue(protocol_group);
				}
				break;
				
				case 2:
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
		// PROTOCOL TESTING MODE
		// ===============================
		
		static bool protocol_tests_run = false;
		static unsigned long last_protocol_test = 0;
		unsigned long now = millis();
		
		// Run comprehensive protocol testing suite at startup
		if (!protocol_tests_run) {
			Serial.println("\n=== RUNNING COMPREHENSIVE PROTOCOL TESTING SUITE ===");
			
			// Run the full test suite which includes protocol tests
			test_suite->run_all_tests();
			
			// // Run additional focused protocol tests
			// Serial.println("\n=== FOCUSED PROTOCOL ENCODE/DECODE TESTS ===");
			// for (int test_iteration = 0; test_iteration < 5; test_iteration++) {
			// 	Serial.println("Protocol test iteration " + String(test_iteration + 1) + "/5:");
			// 	bool test_result = test_protocol_encode_decode();
			// 	String status = get_board_status();
			// 	Serial.println("  Result: " + String(test_result ? "PASS" : "FAIL"));
			// 	Serial.println("  Board status: " + status);
				
			// 	// Display test result on screen
			// 	TextGroup* protocol_group = new TextGroup(2000, DICE_CYAN, DICE_BLACK);
			// 	protocol_group->add_member(new Text("PROTOCOL TEST", 0, FontID::TF, 150, 180));
			// 	protocol_group->add_member(new Text("ITERATION " + String(test_iteration + 1), 0, FontID::TF, 170, 220));
			// 	protocol_group->add_member(new Text(test_result ? "PASS" : "FAIL", 0, FontID::TF, 200, 260));
			// 	screen->enqueue(protocol_group);
			// 	screen->update();
				
			// 	delay(2500); // Show each test result for 2.5 seconds
			// }
			
			// protocol_tests_run = true;
			// last_protocol_test = now;
			
			Serial.println("\n=== PROTOCOL TESTING COMPLETE ===");
			Serial.println("Entering normal SPI operation mode...\n");
		}

		// Normal SPI operation - Queue Message Receipt
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
