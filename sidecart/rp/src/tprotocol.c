#include "tprotocol.h"

uint32_t tprotocol_last_header_found = 0;
uint32_t tprotocol_new_header_found = 0;
TPParseStep tprotocol_nextTPstep = HEADER_DETECTION;
TransmissionProtocol tprotocol_transmission = {0};
