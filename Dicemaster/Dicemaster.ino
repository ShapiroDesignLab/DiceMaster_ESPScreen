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
// Available modes: TESTING, DEMO, PRODUCTION, SPI_DEBUG
// Change to SystemMode::PRODUCTION for normal operation
SystemMode current_mode = SystemMode::SPI_DEBUG;  // Set current operating mode
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
	}
	
	Serial.println("=== DiceMaster System Ready ===");
	Serial.println("Current mode: " + String(current_mode == SystemMode::DEMO ? "DEMO" : 
	                                         current_mode == SystemMode::TESTING ? "TESTING" : 
	                                         current_mode == SystemMode::SPI_DEBUG ? "SPI_DEBUG" : "PRODUCTION"));
	Serial.println("System initialized successfully.");
	delay(1000); // Show logo for 2 seconds
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
			
		case SystemMode::SPI_DEBUG:
			// ===============================
			// SPI DEBUG MODE - Display raw hex data
			// ===============================
			run_spi_debug_mode();
			break;
	}
	
	// Check buttons for manual interactions
	// handle_button_presses();
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
	spid->queueTransaction();
	screen->update();
	
	std::vector<MediaContainer*> new_content = spid->poll();
	for (size_t i = 0; i < new_content.size(); ++i) {
		screen->enqueue(new_content[i]);
	}
	
	delay(1);
}

void run_production_mode() {
	// Pure SPI operation mode - no tests or demos

	// Serial.println("Working loop!");

	spid->queueTransaction();
	screen->update();
	
	std::vector<MediaContainer*> new_content = spid->poll();
	// if (new_content.size() > 0)
	// Serial.println(new_content.size());
	for (size_t i = 0; i < new_content.size(); ++i) {
		screen->enqueue(new_content[i]);
	}
	delay(5);
}

void run_spi_debug_mode() {
	// SPI Debug mode - display raw hex bytes received
	static bool debug_initialized = false;
	static unsigned long last_status_update = 0;
	static int no_data_counter = 0;
	
	if (!debug_initialized) {
		Serial.println("Initializing SPI debug mode...");
		Serial.println("Will display received SPI bytes as hex on screen");
		
		// Display initial status on screen
		auto* status_group = new TextGroup(0, DICE_BLACK, DICE_GREEN);
		Text* status_text = new Text("SPI DEBUG MODE\nWaiting for data...", 5000, FontID::TF, 50, 200);
		status_group->add_member(status_text);
		screen->enqueue(status_group);
		
		debug_initialized = true;
		last_status_update = millis();
	}
	
	// Queue SPI transaction
	spid->queueTransaction();
	
	// Poll for debug hex data
	MediaContainer* hex_data = spid->pollDebugHex();
	if (hex_data != nullptr) {
		screen->enqueue(hex_data);
		no_data_counter = 0;  // Reset counter when we get data
	} else {
		no_data_counter++;
		
		// Show periodic status updates when no data is received
		if (millis() - last_status_update > 5000) {  // Every 5 seconds
			auto* status_group = new TextGroup(0, DICE_BLACK, DICE_WHITE);
			String status_msg = "SPI DEBUG MODE\nWaiting for data...\nNo data cycles: " + String(no_data_counter);
			Text* status_text = new Text(status_msg, 2000, FontID::TF, 50, 200);
			status_group->add_member(status_text);
			screen->enqueue(status_group);
			last_status_update = millis();
		}
	}
	
	// Update screen
	screen->update();
	
	delay(10);  // Small delay to prevent overwhelming the display
}
