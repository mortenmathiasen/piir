#include "hvac_panasonic_plugin.h"
#include <stdint.h>
// #include "pigpio.h"

uint8_t readLeftBinary(const char * str, int *index) {
  uint8_t result = 0;
  while (str[*index]!='0' && str[*index]!='1')
    *index = *index + 1;
  for (int i=8 ; i>=0 ; i--) {
    result = result << 1;
    result += str[i+(*index)]=='1';
  }
  *index = *index + 8;
  return result;
}

void hvac_panasonic_plugin(char *symbolString) {
  // Calculate and insert checksum
  uint8_t checksum = 0b11110100;
  int index = 0;
  for (int i=0; i<26; i++)
    checksum += readLeftBinary(symbolString, &index);
  for (int i=0 ; i<8 ; i++) {
    while (symbolString[index]!='0' && symbolString[index]!='1')
      index++;
    symbolString[index+(7-i)] = (checksum << i) & 0b10000000 ? '1':'0';
  }
}
