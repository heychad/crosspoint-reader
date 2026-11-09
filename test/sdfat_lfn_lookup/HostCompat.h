class __FlashStringHelper;
inline unsigned long millis() {
#ifdef HOST_MILLIS_ADVANCES
  static unsigned long ticks = 0;
  return ++ticks;
#else
  return 0;
#endif
}
