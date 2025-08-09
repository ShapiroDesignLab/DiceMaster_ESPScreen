#include "screen.h"
#include "spi.h"
#include "examples.h"  // New comprehensive examples
#include "tests.h"     // New test suite
#include "esp_heap_caps.h"
using namespace dice;

Screen* screen;
SPIDriver* spid;
TestSuite* test_suite;

// System mode configuration
// Available modes: TESTING, DEMO, PRODUCTION, SPI_DEBUG
// Change to SystemMode::PRODUCTION for normal operation
SystemMode current_mode = SystemMode::PRODUCTION;  // Set current operating mode
int revolving_counter = 0;  // Counter for revolving animation demo
int messages_received_counter = 0;  // Counter for messages received/displayed

// Helper function for production mode loading dots animation
void show_loading_dots() {
	static unsigned long last_dots_update = 0;
	static int dots_count = 1;
	
	if (millis() - last_dots_update >= 333) { // 5Hz = 200ms interval
		String dots_text = "";
		for (int i = 0; i < dots_count; i++) {
			dots_text += ".";
		}
		
		// Create and display loading dots with detailed logging
		Serial.println("[LOADING] Creating TextGroup and Text objects...");
		auto* loading_group = new TextGroup(0, DICE_BLACK, DICE_WHITE);
		Text* loading_text = new Text("" + dots_text, 0, FontID::TF, 50, 120);
		Serial.println("[LOADING] Created Text: '" + dots_text + "' at position (50, 120)");
		
		loading_group->add_member(loading_text);
		Serial.printf("[LOADING] TextGroup created with %d members, status: %d\n", 
		              (int)loading_group->size(), (int)loading_group->get_status());
		
		bool enqueue_success = screen->enqueue(loading_group);
		Serial.printf("[LOADING] Enqueue result: %s for dots: %s\n", 
		              enqueue_success ? "SUCCESS" : "FAILED", dots_text.c_str());
		
		if (!enqueue_success) {
			Serial.println("[LOADING] ERROR: Failed to enqueue loading dots - deleting objects");
			delete loading_group; // This will also delete the text member
		}
		
		// Cycle dots count: 1 -> 2 -> 3 -> 4 -> 5 -> 6 -> back to 1
		dots_count++;
		if (dots_count > 6) {
			dots_count = 1;
		}
		
		last_dots_update = millis();
	}
}

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
	
	// Initialize screen thread-safe queues
	if (!screen->initialize_queues()) {
		Serial.println("Failed to initialize screen queues!");
		while(1) delay(1000); // Halt on failure
	}
	
	// Now that queues are initialized, display the startup logo
	screen->draw_startup_logo();
	screen->update(); // Display the logo immediately
	
	// Initialize SPI with callback-based processing
	if (!spid->initialize(screen)) {
		Serial.println("Failed to initialize SPI driver!");
		while(1) delay(1000); // Halt on failure
	}
	
	Serial.println("=== DiceMaster System Ready ===");
	Serial.println("Current mode: " + String(current_mode == SystemMode::DEMO ? "DEMO" : 
	                                         current_mode == SystemMode::TESTING ? "TESTING" : 
	                                         current_mode == SystemMode::SPI_DEBUG ? "SPI_DEBUG" : "PRODUCTION"));
	Serial.println("System initialized successfully.");
	delay(2000); // Show logo for 2 seconds
}

void loop() {
	static unsigned long last_media_update = 0;
	static unsigned long last_loop_debug = 0;
	const unsigned long MEDIA_UPDATE_INTERVAL = 16; // 60Hz (~16ms) - faster screen updates
	
	unsigned long now = millis();
	
	// Debug: Show that main loop is running
	if (now - last_loop_debug > 5000) { // Every 5 seconds
		Serial.println("[MAIN] Main loop running, last media update: " + String(now - last_media_update) + "ms ago");
		last_loop_debug = now;
	}
	
	// No polling needed - SPI driver is fully event-driven
	// The SPIDriver uses task notifications and callbacks for all operations
	
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
			// SPI DEBUG MODE - Display statistics
			// ===============================
			run_spi_debug_mode();
			break;
	}
	
	// Update screen and process decoded media at 60Hz for all modes except DEMO
	// (DEMO mode handles its own timing)
	// NOTE: SPI decoded media is now automatically enqueued to screen via decoding handler
	if (current_mode != SystemMode::DEMO) {
		if (now - last_media_update >= MEDIA_UPDATE_INTERVAL) {
			// Update screen to process queued media (both SPI and local media like dots)
			if (screen->num_queued() > 0) {
				screen->update();
				Serial.println("[MAIN] screen updated with remaining items: " + String(screen->num_queued()));
			} else {
				// Only log this occasionally to avoid spam
				static unsigned long last_no_items_log = 0;
				if (now - last_no_items_log > 2000) { // Every 2 seconds
					Serial.println("[MAIN] No items left in screen queue");
					last_no_items_log = now;
				}
			}
			last_media_update = now;
		}
	}
	
	// Very small delay to prevent overwhelming the system but maintain responsiveness
	delayMicroseconds(10);
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

	screen->update();  // Update screen after running demo sequence
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
	
	// After tests complete, the main loop handles media processing at 30Hz
	// No need to do anything here - SPI is running in background via callbacks
}

void run_production_mode() {
	// Pure SPI operation mode - no tests or demos
	// SPI runs automatically via callbacks, decoding happens in background task
	// Main loop processes decoded media at 30Hz
	
	// Show loading dots animation if no messages have been received yet
	// Check SPI statistics to see if we've received any decoded media
	auto spi_stats = spid->get_decode_statistics();
	if (spi_stats.media_enqueued_to_screen == 0) {
		show_loading_dots();
	}
	
	// This function doesn't need to do anything else - the architecture handles everything
	// SPI callbacks capture data -> Decoding task processes -> Main loop displays at 30Hz
}

void run_spi_debug_mode() {
    // SPI Debug mode - display statistics from the decoding handler
    static bool debug_initialized = false;
    static unsigned long last_status_update = 0;
    
    if (!debug_initialized) {
        Serial.println("Initializing SPI debug mode...");
        Serial.println("Will display SPI and decoding statistics on screen");
        
        // Display initial status on screen
        auto* status_group = new TextGroup(0, DICE_BLACK, DICE_GREEN);
        Text* status_text = new Text("SPI DEBUG MODE\nCallback-based SPI\nWaiting for data...", 5000, FontID::TF, 50, 200);
        status_group->add_member(status_text);
        screen->enqueue(status_group);
        
        debug_initialized = true;
        last_status_update = millis();
    }
    
    // Show periodic statistics about the SPI and decoding handler
    if (millis() - last_status_update > 3000) {  // Every 3 seconds
        auto decode_stats = spid->get_decode_statistics();
        auto spi_timing = spid->get_spi_timing_stats();
        size_t transaction_count = spid->get_transaction_count();
        
        // Get memory statistics
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
        size_t free_psram = psramFound() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
        last_status_update = millis();
        
        // Minimal serial output - only summary stats every 3 seconds
        Serial.println("[DEBUG] T:" + String(transaction_count) + 
                       " D:" + String(decode_stats.messages_decoded) + 
                       " F:" + String(decode_stats.decode_failures) + 
                       " Q:" + String(decode_stats.current_raw_queue_depth) + 
                       " S:" + String(decode_stats.media_enqueued_to_screen) +
                       " H:" + String(free_heap / 1024) + "KB" +
                       " P:" + String(free_psram / 1024) + "KB" +
                       " Overflows:" + String(decode_stats.raw_queue_overflows));
    }
}