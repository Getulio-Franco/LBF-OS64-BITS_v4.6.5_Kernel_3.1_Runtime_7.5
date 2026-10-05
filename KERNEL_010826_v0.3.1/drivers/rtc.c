#include "rtc.h"

#define CMOS_ADDRESS 0x70
#define CMOS_DATA    0x71

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static uint8_t get_update_in_progress_flag(void) {
    outb(CMOS_ADDRESS, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t read_cmos_register(uint8_t reg) {
    outb(CMOS_ADDRESS, reg);
    return inb(CMOS_DATA);
}

void rtc_read_time(rtc_time_t* t) {
    if (!t) return;

    // Aguarda término de atualização do relógio CMOS
    while (get_update_in_progress_flag());

    uint8_t sec   = read_cmos_register(0x00);
    uint8_t min   = read_cmos_register(0x02);
    uint8_t hour  = read_cmos_register(0x04);
    uint8_t day   = read_cmos_register(0x07);
    uint8_t month = read_cmos_register(0x08);
    uint8_t year  = read_cmos_register(0x09);
    uint8_t reg_b = read_cmos_register(0x0B);

    // Dupla leitura para garantir consistência dos dados
    uint8_t last_sec, last_min, last_hour, last_day, last_month, last_year;
    do {
        last_sec   = sec;
        last_min   = min;
        last_hour  = hour;
        last_day   = day;
        last_month = month;
        last_year  = year;

        while (get_update_in_progress_flag());

        sec   = read_cmos_register(0x00);
        min   = read_cmos_register(0x02);
        hour  = read_cmos_register(0x04);
        day   = read_cmos_register(0x07);
        month = read_cmos_register(0x08);
        year  = read_cmos_register(0x09);
    } while (last_sec != sec || last_min != min || last_hour != hour ||
             last_day != day || last_month != month || last_year != year);

    // Converte de BCD para binário (se bit 2 do registrador B estiver zerado)
    if (!(reg_b & 0x04)) {
        sec   = (sec & 0x0F) + ((sec / 16) * 10);
        min   = (min & 0x0F) + ((min / 16) * 10);
        hour  = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
        day   = (day & 0x0F) + ((day / 16) * 10);
        month = (month & 0x0F) + ((month / 16) * 10);
        year  = (year & 0x0F) + ((year / 16) * 10);
    }

    // Converte de formato 12 horas para 24 horas se necessário
    if (!(reg_b & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    t->sec   = sec;
    t->min   = min;
    t->hour  = hour;
    t->day   = day;
    t->month = month;
    t->year  = (uint16_t)year + 2000;
}
