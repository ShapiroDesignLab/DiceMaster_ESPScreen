#ifndef DICEMASTER_IMAGE
#define DICEMASTER_IMAGE

#define IMG_BMP 0
#define GIF 1
#define VID 2


class Media {
// Class to manage a single Image file
protected:
  // ID info
  uint16_t ID;      // image ID
  String name;
  uint16_t format;   // 0 for in-ram, 1-3 for on-disk images
  bool in_ram;

  // Disk info
  String disk_img;  // file name
  uint16_t size_on_disk;

  // Image in RAM info
  uint16_t size_in_ram;
  const uint16_t * ram_media;

  Media(unsigned int uid, const uint16_t* buffer_ptr, 
      uint16_t len, uint16_t iformat, String alias) 
      : ID(uid), name(alias), format(iformat), 
      size_on_disk(0), ram_media(buffer_ptr), size_in_ram(len)
      {};

  // REQUIRE: enough RAM space
  virtual const uint16_t* load2RAM(){}
  virtual const uint16_t* dump2Disk(){}

public:
  virtual const uint16_t* getFrame(){}
};



#endif