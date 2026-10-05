#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t  sec;
    uint8_t  min;
    uint8_t  hour;
    uint8_t  day;
    uint8_t  month;
    uint16_t year;
} rtc_time_t;

/*
 * Lê a data e hora atual do RTC (CMOS) do hardware.
 * Converte BCD -> Binário e ajusta para o formato 24h/Ano 20xx automaticamente.
 */
void rtc_read_time(rtc_time_t* t);

#endif /* RTC_H */
