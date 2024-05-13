#ifndef DICEMASTER_SPI_AGENT
#define DICEMASTER_SPI_AGENT

#include <ArduinoSTL.h>

class FileManager{

private:
  uint16_t * img_buffer;
  uint16_t * SPI_buffer;

public:
  FileManager() : img_buffer(nullptr), SPI_buffer(nullptr) {}


  // Work out the protocol stuff
  void recv(){

  }

};

#endif