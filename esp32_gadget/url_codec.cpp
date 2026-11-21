#include "url_codec.h"

// The two implementations this replaces had the same escape table and the same
// capacity arithmetic, and differed only in how they reported a buffer that did
// not fit: Spotify's wrote "" and returned void, the calendar's returned false.
// The boolean form is the superset — a caller that does not care ignores it — so
// that is the contract kept here.

bool urlEncode(const char *in, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!in) return false;

  static const char kHex[] = "0123456789ABCDEF";
  size_t written = 0;
  for (const char *p = in; *p; p++) {
    const unsigned char c = (unsigned char)*p;
    const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '.' ||
                            c == '_' || c == '~';
    const size_t need = unreserved ? 1 : 3;
    if (written + need + 1 > cap) {
      out[0] = '\0';
      return false;
    }
    if (unreserved) {
      out[written++] = (char)c;
    } else {
      out[written++] = '%';
      out[written++] = kHex[c >> 4];
      out[written++] = kHex[c & 0x0F];
    }
  }
  out[written] = '\0';
  return true;
}
