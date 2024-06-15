#include "AudioTools.h"                  // Needs https://github.com/pschatzmann/arduino-audio-tools
#include "arduino.h"

class RingBuf : public Stream {

  private:
    int max_size;
	  int current_size = 0;
	  int current_pos = 0;
    uint8_t* buffer = 0;
    portMUX_TYPE buffer_mutex = portMUX_INITIALIZER_UNLOCKED;

  public:
    RingBuf(int buffer_size) {
      max_size = buffer_size;
    }

    void begin() {
      assert(buffer == 0);
      buffer = (uint8_t*)malloc(max_size);
      assert(buffer != 0);
    }

    int available() {      
      if (buffer == 0)
        return 0;
      taskENTER_CRITICAL(&buffer_mutex);
      int return_value = current_size;
      taskEXIT_CRITICAL(&buffer_mutex);
      return return_value;
    }

    int available_to_write() {
      if (buffer == 0)
        return 0;
      taskENTER_CRITICAL(&buffer_mutex);
      int return_value = max_size - current_size;
      taskEXIT_CRITICAL(&buffer_mutex);
      return return_value;
    }

    int read() {
      taskENTER_CRITICAL(&buffer_mutex);
      if (current_size == 0) {
        taskEXIT_CRITICAL(&buffer_mutex);
        return -1;
      }
      uint8_t return_value = buffer[current_pos];
      current_pos = (current_pos + 1) % max_size;
      current_size--;
      taskEXIT_CRITICAL(&buffer_mutex);
      return return_value;
    }

    size_t write(uint8_t value) {
      taskENTER_CRITICAL(&buffer_mutex);
      if (current_size == max_size) {
        taskEXIT_CRITICAL(&buffer_mutex);
        return -1;
      }
      int write_pos = (current_pos + current_size) % max_size;
      buffer[write_pos] = value;
      current_size++;
      taskEXIT_CRITICAL(&buffer_mutex);
      return 1;
    }

    int readBytes(uint8_t* _buffer, int bytes_to_read) {
      taskENTER_CRITICAL(&buffer_mutex);
      if(current_size < bytes_to_read)
        bytes_to_read = current_size;
      int bytes_before_wrap = max_size - current_pos;
      if (bytes_to_read > bytes_before_wrap) {
        memcpy(_buffer, buffer + current_pos, bytes_before_wrap);
        memcpy(_buffer + bytes_before_wrap, buffer, bytes_to_read - bytes_before_wrap);
      } else
        memcpy(_buffer, buffer + current_pos, bytes_to_read);
      current_pos = (current_pos + bytes_to_read) % max_size;
      current_size -= bytes_to_read;
      taskEXIT_CRITICAL(&buffer_mutex);
      return bytes_to_read;
    }

    uint32_t writeBytes(uint8_t* _buffer, int bytes_to_write) {
      taskENTER_CRITICAL(&buffer_mutex);
      if (current_size + bytes_to_write > max_size)
        bytes_to_write = max_size - current_size;
      int write_pos = (current_pos + current_size) % max_size;
      int free_before_wrap = max_size - write_pos;
      if (bytes_to_write > free_before_wrap)
      {
        memcpy(buffer + write_pos, _buffer, free_before_wrap);
        memcpy(buffer, _buffer + free_before_wrap, bytes_to_write - free_before_wrap);
      } else
        memcpy(buffer + write_pos, _buffer, bytes_to_write);
      current_size = (current_size + bytes_to_write) % max_size;
      taskEXIT_CRITICAL(&buffer_mutex);
      return bytes_to_write;
    }

    int peek() {
      if (current_size == 0)
        return -1;
      return buffer[current_pos];
    }

};