#include "screen.h"
#include "spi.h"
#include "examples.h"  // New comprehensive examples
#include "tests.h"     // New test suite
#include "jpg.hs/umlogo_sq240.h"
#include "esp_heap_caps.h"
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
	
	// Initialize SPI with callback-based processing
	if (!spid->initialize()) {
		Serial.println("Failed to initialize SPI driver!");
		while(1) delay(1000); // Halt on failure
	}
	
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
	static unsigned long last_media_update = 0;
	static unsigned long last_poll_time = 0;
	static unsigned long last_loop_debug = 0;
	const unsigned long MEDIA_UPDATE_INTERVAL = 16; // 60Hz (~16ms) - faster screen updates
	const unsigned long POLL_INTERVAL = 1; // Poll SPI every 1ms for maximum responsiveness
	
	unsigned long now = millis();
	
	// Debug: Show that main loop is running
	if (now - last_loop_debug > 5000) { // Every 5 seconds
		Serial.println("[MAIN] Main loop running, last media update: " + String(now - last_media_update) + "ms ago");
		last_loop_debug = now;
	}
	
	// Poll SPI transactions at high frequency for fastest response (all modes)
	if (now - last_poll_time >= POLL_INTERVAL) {
		spid->poll_transactions();
		last_poll_time = now;
	}
	
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
	if (current_mode != SystemMode::DEMO) {
		if (now - last_media_update >= MEDIA_UPDATE_INTERVAL) {
			// Get decoded media from the decoding handler
			std::vector<MediaContainer*> new_content = spid->get_decoded_media();
			if (new_content.size() > 0) {
				Serial.println("[MAIN] Retrieved " + String(new_content.size()) + " media items");
				
				// Debug: Log each retrieved image
				for (size_t i = 0; i < new_content.size(); ++i) {
					if (new_content[i]->get_media_type() == MediaType::IMAGE) {
						Serial.println("[MAIN] Retrieved Image ID " + String(new_content[i]->get_image_id()) + 
						               " - Status: " + String((int)new_content[i]->get_status()));
					}
					screen->enqueue(new_content[i]);
				}
			}
				
			// Update screen
			if (screen->num_queued() > 0) {
				screen->update();
				// Serial.println("[MAIN] screen updated with remaining items: " + String(screen->num_queued()));
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
	
	// This function doesn't need to do anything - the architecture handles everything
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
        
        // auto* stats_group = new TextGroup(0, DICE_BLACK, DICE_CYAN);
        // String stats_msg = "SPI CALLBACK STATS:\n";
        // stats_msg += "Transactions: " + String(transaction_count) + "\n";
        // stats_msg += "Raw chunks: " + String(decode_stats.raw_chunks_received) + "\n";
        // stats_msg += "Decoded: " + String(decode_stats.messages_decoded) + "\n";
        // stats_msg += "Decode fails: " + String(decode_stats.decode_failures) + "\n";
        // stats_msg += "SOF errors: " + String(decode_stats.sof_marker_errors) + "\n";
        // stats_msg += "Sync recovery: " + String(decode_stats.sync_recovery_attempts) + "\n";
        // stats_msg += "Queue drops: " + String(decode_stats.raw_queue_overflows) + "\n";
        // stats_msg += "Raw Q: " + String(decode_stats.current_raw_queue_depth) + "/" + String(16) + "\n";
        // stats_msg += "Media Q: " + String(decode_stats.current_decoded_queue_depth) + "/" + String(8) + "\n";
        // stats_msg += "SPI Max: " + String(spi_timing.max_poll_time_ms) + "ms\n";
        // stats_msg += "SPI Avg: " + String(spi_timing.avg_poll_time_ms) + "ms\n";
        
        // // Memory information
        // stats_msg += "Heap: " + String(free_heap / 1024) + "KB\n";
        // if (psramFound()) {
        //     stats_msg += "PSRAM: " + String(free_psram / 1024) + "KB";
        // } else {
        //     stats_msg += "PSRAM: N/A";
        // }
        
        // Text* stats_text = new Text(stats_msg, 2800, FontID::TF, 10, 50);
        // stats_group->add_member(stats_text);
        // screen->enqueue(stats_group);
        last_status_update = millis();
        
        // Minimal serial output - only summary stats every 3 seconds
        Serial.println("[DEBUG] T:" + String(transaction_count) + 
                       " D:" + String(decode_stats.messages_decoded) + 
                       " F:" + String(decode_stats.decode_failures) + 
                       " SOF:" + String(decode_stats.sof_marker_errors) +
                       " Q:" + String(decode_stats.current_raw_queue_depth) + "/" + String(decode_stats.current_decoded_queue_depth) +
                       " H:" + String(free_heap / 1024) + "KB" +
                       " P:" + String(free_psram / 1024) + "KB" +
                       " Overflows:" + String(decode_stats.raw_queue_overflows));
    }
}