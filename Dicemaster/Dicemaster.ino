#include "screen.h"
#include "spi.h"
#include "examples.h"  // New comprehensive examples
#include "tests.h"     // New test suite
#include "jpg.hs/umlogo_sq240.h"
using namespace dice;

Screen* screen;
SPIDriver* spid;
TestSuite* test_suite;

// System mode configuration
SystemMode current_mode = SystemMode::DEMO;  // Set current operating mode
int revolving_counter = 0;  // Counter for revolving animation demo

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
	
	Serial.println("=== DiceMaster System Ready ===");
	Serial.println("Current mode: " + String(current_mode == SystemMode::DEMO ? "DEMO" : 
	                                         current_mode == SystemMode::TESTING ? "TESTING" : "PRODUCTION"));
	Serial.println("System initialized successfully.");
}

void loop() {
	switch (current_mode) {
		case SystemMode::DEMO:
			// ===============================
			// DEMO MODE - Showcase examples
			// ===============================
			run_demo_mode();
			break;
			
		case SystemMode::TESTING:
			// ===============================
			// TESTING MODE - Run test suite
			// ===============================
			run_testing_mode();
			break;
			
		case SystemMode::PRODUCTION:
			// ===============================
			// PRODUCTION MODE - Normal SPI operation
			// ===============================
			run_production_mode();
			break;
	}
	
	// Check buttons for manual interactions
	handle_button_presses();
}

// ===============================
// MODE FUNCTIONS
// ===============================

void run_demo_mode() {
	static bool demo_initialized = false;
	
	if (!demo_initialized) {
		Serial.println("Initializing demo mode...");
		demo_initialized = true;
	}
	
	// Delegate to demo management in examples.h
	run_demo_sequence(screen, revolving_counter);
	
	// Update screen
	screen->update();
	delay(50);
}

void run_testing_mode() {
	static bool tests_completed = false;
	
	if (!tests_completed) {
		Serial.println("\n=== RUNNING COMPREHENSIVE TEST SUITE ===");
		test_suite->run_all_tests();
		tests_completed = true;
		Serial.println("=== TESTING COMPLETE ===");
		Serial.println("All tests finished. System ready for SPI operation.\n");
	}
	
	// After tests complete, run normal SPI operation
	spid->queue_cmd_msgs();
	screen->update();
	
	std::vector<MediaContainer*> new_content = spid->process_msgs();
	for (size_t i = 0; i < new_content.size(); ++i) {
		screen->enqueue(new_content[i]);
	}
	
	delay(1);
}

void run_production_mode() {
	// Pure SPI operation mode - no tests or demos
	spid->queue_cmd_msgs();
	screen->update();
	
	std::vector<MediaContainer*> new_content = spid->process_msgs();
	for (size_t i = 0; i < new_content.size(); ++i) {
		screen->enqueue(new_content[i]);
	}
	
	delay(1);
}

void handle_button_presses() {
	static unsigned long last_button_time = 0;
	static bool mode_switching_enabled = true;
	
	// Both buttons pressed - cycle through modes
	if (screen->down_button_pressed() && screen->up_button_pressed()) {
		if (millis() - last_button_time > 1000 && mode_switching_enabled) { // 1 second debounce
			switch (current_mode) {
				case SystemMode::DEMO:
					current_mode = SystemMode::TESTING;
					Serial.println("=== SWITCHED TO TESTING MODE ===");
					break;
				case SystemMode::TESTING:
					current_mode = SystemMode::PRODUCTION;
					Serial.println("=== SWITCHED TO PRODUCTION MODE ===");
					break;
				case SystemMode::PRODUCTION:
					current_mode = SystemMode::DEMO;
					Serial.println("=== SWITCHED TO DEMO MODE ===");
					break;
			}
			last_button_time = millis();
		}
	}
	// Single button press - trigger test suite in any mode
	else if (screen->down_button_pressed() && !screen->up_button_pressed()) {
		if (millis() - last_button_time > 1000) {
			Serial.println("Down button pressed - running demo tests!");
			test_suite->run_demo_tests();
			last_button_time = millis();
		}
	}
}
