/*
 * gps_parser.c
 *
 *  Created on: Mar 3, 2026
 *      Author: prith
 *
 *  AI Generated functions
 */
#include <stdlib.h>
#include <string.h>

/* -----------------------
 *  Structs
 * ----------------------- */

typedef struct {
    char sentence[nmeaMaxLen];  // holds one NMEA sentence
    uint16_t length;
    bool inSentence;
} GpsNmeaAssembler;

static GpsNmeaAssembler assembler;

/* -----------------------
 *  Internal Helper Functions
 * ----------------------- */
// NMEA sends lat/lon in degrees + minutes (ddmm.mmmm). This converts to decimalDegrees
static double DegreeMinutesToDecimalDegrees(double DegreeMinutes, char hemisphere) {
    int degrees = (int)(DegreeMinutes / 100.0);
    double minutes = DegreeMinutes - ((double)degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
        decimal = -decimal;

    return decimal;
}

// turn ASCII hexadecimal character into its integer equivalent (0 to 15)
static uint8_t hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');  // subtract A(base16) add 10(base10)
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');  // incase ascii character is in lowercase
    return 0xFF;                                      // if error, return char larger than a nibble
}

// Validate NMEA checksum
static bool nmeaChecksumValid(const char* sentence) {
    // taken from: https://forum.arduino.cc/t/nmea-checksums-explained/1046083
    if (!sentence || sentence[0] != '$')
        return false;

    const char* star = strchr(sentence, '*');
    if (!star || !star[1] || !star[2])
        return false;

    uint8_t calc = 0;
    for (const char* p = sentence + 1; p < star; p++) {
        calc ^= (uint8_t)(*p);
    }

    uint8_t high = hexToNibble(star[1]);
    uint8_t low = hexToNibble(star[2]);
    if (high == 0xFF || low == 0xFF)
        return false;

    uint8_t expected = (high << 4) | low;
    return (calc == expected);
}

// parse rmc nmea string
static void parseRmc(GpsFix* fix, char* sentence /*rmc string*/, uint32_t nowMs /*timestamp from when message arrived*/) {
    int field = 0;                        // represents each piece of the rmc string
    char* token = strtok(sentence, ",");  // create a pointer to first item in the rmc string

    // local buffers to hold info
    char latStr[20] = {0};
    char lonStr[20] = {0};
    char speedStr[20] = {0};
    char latHem = 0;
    char lonHem = 0;
    char status = 'V';  // must be A to confirm state is valid (V means data not valid

    while (token) {
        if (field == 2) status = token[0];  // if field == 2, at the 3rd part of the rmc string, this represents status
        if (field == 3) strncpy(latStr, token, sizeof(latStr) - 1);
        if (field == 4) latHem = token[0];
        if (field == 5) strncpy(lonStr, token, sizeof(lonStr) - 1);
        if (field == 6) lonHem = token[0];
        if (field == 7) strncpy(speedStr, token, sizeof(speedStr) - 1);

        token = strtok(NULL, ",");  // move to the next token in sentence
        field++;
    }

    if (status != 'A') {  // 'A' indicates a valid fix
        fix->valid = false;
        return;
    }

    if (latStr[0] && lonStr[0]) {       // make sure lat and lon are non empty
        double latNmea = atof(latStr);  // convert string from nmea to float
        double lonNmea = atof(lonStr);

        fix->lat = DegreeMinutesToDecimalDegrees(latNmea, latHem);
        fix->lon = DegreeMinutesToDecimalDegrees(lonNmea, lonHem);

        float speedKnots = (float)atof(speedStr);
        fix->speedMps = speedKnots * 0.514444f;  // cause 1 knot = 0.514444 m/s

        fix->valid = true;
        fix->lastUpdateMs = nowMs;
    }
}

// parse gga nmea string
static void parseGga(GpsFix* fix, char* sentence, uint32_t nowMs) {
    // exact same logic as parseRMC function but now with parameters applying to gga strings instead
    int field = 0;
    char* token = strtok(sentence, ",");

    char satsStr[8] = {0};
    char altStr[20] = {0};

    while (token) {
        if (field == 7) strncpy(satsStr, token, sizeof(satsStr) - 1);
        if (field == 9) strncpy(altStr, token, sizeof(altStr) - 1);

        token = strtok(NULL, ",");
        field++;
    }

    if (satsStr[0]) {
        int sats = atoi(satsStr);
        if (sats < 0) sats = 0;
        if (sats > 255) sats = 255;
        fix->satellitesUsed = (uint8_t)sats;
    }

    if (altStr[0]) {
        fix->alt = (float)atof(altStr);
        if (fix->valid)  // don't update timestamp if never had RMC
            fix->lastUpdateMs = nowMs;
    }
}

// figure out if rmc or gga and parse apporpriatly
static void parseSentence(GpsFix* fix, char* sentence, uint32_t nowMs) {
    if (strstr(sentence, "RMC") == sentence + 3)
        parseRmc(fix, sentence, nowMs);
    else if (strstr(sentence, "GGA") == sentence + 3)
        parseGga(fix, sentence, nowMs);
}

/* -----------------------
 *  Core Functions
 * ----------------------- */
// init/reset
void gpsParserInit(GpsFix* fix) {
    memset(&assembler, 0, sizeof(assembler));
    memset(fix, 0, sizeof(*fix));
    fix->valid = false;
}

// takes raw incoming UART bytes and tries to turn them into complete NMEA sentences, then updates fix
void gpsParserFeed(GpsFix* fix, const uint8_t* data, size_t length, uint32_t nowMs) {
    if (!data || length == 0)
        return;

    for (size_t i = 0; i < length; i++) {
        char c = (char)data[i];

        if (c == '$') {
            assembler.inSentence = true;
            assembler.length = 0;
        }

        if (!assembler.inSentence)
            continue;

        if (assembler.length < (nmeaMaxLen - 1)) {
            assembler.sentence[assembler.length++] = c;
            assembler.sentence[assembler.length] = '\0';
        }
        // TO PREVENT OVERFLOW: Abandon current string, wait for the next '$'
        else {
            assembler.inSentence = false;
            assembler.length = 0;
            continue;
        }

        if (c == '\n') {                                        // \r\n is the escape sequence for nmea strings
            assembler.inSentence = false;                       // end check
            if (nmeaChecksumValid(assembler.sentence))          // do the checksum and only proceed if validated
                parseSentence(fix, assembler.sentence, nowMs);  // update the fix struct
            assembler.length = 0;                               // reset assembler
        }
    }
}

// getter function if needed for any reason. Copies currentFix in to outFix
void getFix(const GpsFix* currentFix, GpsFix* outFix) {
    if (!currentFix || !outFix) return;

    *outFix = *currentFix;
}

/* --------------------------------------------
 *  Old functions from previous implmentations
 * ------------------------------------------- */
/* Commented out cause gpsGetFix(...) will handle all this at once by updating a struct
//Returns current state of GPS
int NEOM9N_getTime(float* time, unsigned char* GPSData, int size);
int NEOM9N_getSpeed(float* speed, unsigned char* GPSData);
int NEOM9N_getPosition(float* latitude, char* latitudeHemisphere,
                       float* longitude, char* longitudeHemisphere,
                       unsigned char* GPSData, int size);
int NEOM9N_getAltitude(float* altitude, unsigned char* GPSData);
*/
