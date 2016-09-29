
#ifndef TIME_H_
#define TIME_H_

#include "../psychosonda.h"

void datetime_from_epoch(uint32_t epoch, uint8_t * psec, uint8_t * pmin, uint8_t * phour, uint8_t * pday, uint8_t * pwday, uint8_t * pmonth, uint16_t * pyear);
void time_str(char * buff, uint32_t epoch);

uint32_t time_get_actual();
void time_init();


#endif
