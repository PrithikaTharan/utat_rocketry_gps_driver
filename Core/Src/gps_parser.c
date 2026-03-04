/*
 * gps_parser.c
 *
 *  Created on: Mar 3, 2026
 *      Author: prith
 *
 *  AI Generated functions
 */

/* -----------------------
 *  Structs
 * ----------------------- */

typedef struct {
    char sentence[nmeaMaxLen];	 //holds one NMEA sentence
    uint16_t length;
} GpsNmeaAssembler;

static NmeaAssembler nmeaString;
static GpsFix currentFix;

/* -----------------------
 *  Internal Helper Functions
 * ----------------------- */
//
static double convertNmeaToDecimal(double nmeaValue, char hemisphere) {
    int degrees = (int)(nmeaValue / 100.0);
    double minutes = nmeaValue - ((double)degrees * 100.0);
    double decimal = degrees + (minutes / 60.0);

    if (hemisphere == 'S' || hemisphere == 'W')
    {
        decimal = -decimal;
    }

    return decimal;
}

//
static uint8_t hexToNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return 0xFF;
}

// Validate NMEA XOR checksum
static bool nmeaChecksumValid(const char* sentence) {
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
    uint8_t low  = hexToNibble(star[2]);
    if (high == 0xFF || low == 0xFF)
        return false;

    uint8_t expected = (high << 4) | low;
    return (calc == expected);
}

//
static void parseRmc(char* sentence, uint32_t nowMs){
    int field = 0;
    char* token = strtok(sentence, ",");

    char latStr[20] = {0};
    char lonStr[20] = {0};
    char speedStr[20] = {0};
    char latHem = 0;
    char lonHem = 0;
    char status = 'V';

    while (token) {
        if (field == 2) status = token[0];
        if (field == 3) strncpy(latStr, token, sizeof(latStr)-1);
        if (field == 4) latHem = token[0];
        if (field == 5) strncpy(lonStr, token, sizeof(lonStr)-1);
        if (field == 6) lonHem = token[0];
        if (field == 7) strncpy(speedStr, token, sizeof(speedStr)-1);

        token = strtok(NULL, ",");
        field++;
    }

    if (status != 'A') {
        currentFix.valid = false;
        return;
    }

    if (latStr[0] && lonStr[0]) {
        double latNmea = atof(latStr);
        double lonNmea = atof(lonStr);

        currentFix.latDeg = convertNmeaToDecimal(latNmea, latHem);
        currentFix.lonDeg = convertNmeaToDecimal(lonNmea, lonHem);

        float speedKnots = (float)atof(speedStr);
        currentFix.speedMps = speedKnots * 0.514444f;

        currentFix.valid = true;
        currentFix.lastUpdateMs = nowMs;
    }
}

//
static void parseGga(char* sentence, uint32_t nowMs) {
    int field = 0;
    char* token = strtok(sentence, ",");

    char satsStr[8] = {0};
    char altStr[20] = {0};

    while (token) {
        if (field == 7) strncpy(satsStr, token, sizeof(satsStr)-1);
        if (field == 9) strncpy(altStr, token, sizeof(altStr)-1);

        token = strtok(NULL, ",");
        field++;
    }

    if (satsStr[0]) {
        int sats = atoi(satsStr);
        if (sats < 0) sats = 0;
        if (sats > 255) sats = 255;
        currentFix.satellitesUsed = (uint8_t)sats;
    }

    if (altStr[0]) {
        currentFix.altMeters = (float)atof(altStr);
        currentFix.lastUpdateMs = nowMs;
    }
}

//
static void parseSentence(char* sentence, uint32_t nowMs) {
    if (strstr(sentence, "RMC") == sentence + 3)
        parseRmc(sentence, nowMs);
    else if (strstr(sentence, "GGA") == sentence + 3)
        parseGga(sentence, nowMs);
}

/* -----------------------
 *  Core Functions
 * ----------------------- */
// init/reset
void gpsParserInit(GpsNmeaAssembler* a, GpsFix* fix){
    memset(&assembler, 0, sizeof(assembler));
    memset(&currentFix, 0, sizeof(currentFix));
    currentFix.valid = false;
}

// feed raw bytes; internally extracts full NMEA sentences and updates fix
void gpsParserFeed(GpsNmeaAssembler* a, GpsFix* fix, const uint8_t* data, size_t length, uint32_t nowMs){
	if (!data || len == 0)
	        return;

	for (size_t i = 0; i < len; i++) {
		char c = (char)data[i];

		if (c == '$') {
			assembler.inSentence = true;
			assembler.length = 0;
		}

		if (!assembler.inSentence)
			continue;

		if (assembler.length < (NmeaSentenceMaxLen - 1)){
			assembler.sentence[assembler.length++] = c;
			assembler.sentence[assembler.length] = '\0';
		}
		else {
			assembler.inSentence = false;
			assembler.length = 0;
			continue;
		}

		if (c == '\n') {
			assembler.inSentence = false;
			if (nmeaChecksumValid(assembler.sentence))
				parseSentence(assembler.sentence, nowMs);
			assembler.length = 0;
		}
	}
}

// getter function if needed for any reason. Copies currentFix in to outFix
void get_fix(const GpsFix* currentFix, GpsFix* outFix){
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
