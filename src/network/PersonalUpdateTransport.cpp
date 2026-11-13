#include "PersonalUpdateTransport.h"

// The verified HTTPS implementation is the remaining update-channel integration.
bool personalupdate::transportAvailable() { return false; }
bool personalupdate::fetchRelease(UpdateSink, void*) { return false; }
bool personalupdate::fetchImage(const char*, UpdateSink, void*) { return false; }
