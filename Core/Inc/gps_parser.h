/*
 * gps_parser.h
 *
 *  Created on: Mar 3, 2026
 *      Author: prith
 *
 *  Purpose:
 *  	- turn uart bytes into full nmea sentences
        - validatae nmea checksum to ensure data hasn't been correupted
        - parse data and put into gpsFix struct(holds lat, lon, speed, alt, satallites). Can add more to this if need be
    Notes:
        - this file is seperated from interrupts/DMA and UART
        - only needs to be changed if switching from NEMA --> UBX or vise versa

 */

#ifndef INC_GPS_PARSER_H_
#define INC_GPS_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define nmeaMaxLen 256  // using 256 bytes as an upper bound

/*-----------------------
 *  Structs
-----------------------*/
typedef struct {
    bool valid;              // true if there's been a valid fix (checksum passes)
    double lat;              // lattitude
    double lon;              // longitude
    float alt;               // altitude
    float speedMps;          // meters per second. (converted from RMC knots)
    uint8_t satellitesUsed;  // from data sent by GGA
    uint32_t lastUpdateMs;   // feature implemented using HAL_GEtTick() --> can measure navigation rate (hz).
                             // Default navigation rate is set to be 1Hz on neom9n
} GpsFix;

/*-----------------------
 * Function Declarations
-----------------------*/
// init/reset
void gpsParserInit(GpsFix* fix);

// feed raw bytes; internally extracts full NMEA sentences and updates fix
void gpsParserFeed(GpsFix* fix, const uint8_t* data, size_t length, uint32_t nowMs);

// getter function if needed for any reason. Copies currentFix in to outFix
void getFix(const GpsFix* currentFix, GpsFix* outFix);

#endif /* INC_GPS_PARSER_H_ */
