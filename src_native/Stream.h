#ifndef STREAM_H
#define STREAM_H

#include "Print.h"

class Stream : public Print {
  public:
  void begin(int baud) {};
  // void print(const char c) {
  //   write(c);
  // };
  void print(char c) {
    write(c);
  };
  void print(const int i) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "%d", i);
    print(buffer);
  };
  void print(const unsigned int u) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "%u", u);
    print(buffer);
  };
  // void print(const char *s) {
  //   for (; *s; ++s) {
  //     write(*s);
  //   }
  // };
  void print(char const *s) {
    for (; *s; ++s) {
      write(*s);
    }
  };
  void println(const unsigned int u) {
    print(u);
    print("\r\n");
  }
  void println(const char *s) {
    print(s);
    print("\r\n");
  }
  virtual void flush() {};
  int read() { return -1; };
};

class MockSerial : public Stream {
  public:
    virtual size_t write(uint8_t c) {
      printf("%c", c);
      return 1;
    }
    virtual size_t write(const uint8_t *buffer, size_t size) {
      for (size_t i = 0; i<size; ++i) {
        write(buffer[i]);
      }
      return size;
    }
    uint8_t dtr() { return 1; }
    size_t available() { return 0; }
};

typedef MockSerial HardwareSerial;

extern MockSerial Serial;
extern MockSerial Serial1;

#endif
