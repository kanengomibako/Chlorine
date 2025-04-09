#ifndef _EEPROM_HPP
#define _EEPROM_HPP

#ifdef __cplusplus
extern "C" {
#endif

void loadData();
void saveData();
void eraseData();

#ifdef __cplusplus
}
#endif

#endif  // _EEPROM_HPP
