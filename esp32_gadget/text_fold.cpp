#include "text_fold.h"

#include <stdint.h>
#include <string.h>

// Latin-1 and typographic codepoints that have an honest ASCII stand-in.
static char mapCodepoint(uint32_t cp) {
  switch (cp) {
    case 0x00A0: return ' ';   // no-break space
    case 0x00AA: return 'a';
    case 0x00BA: return 'o';
    case 0x00BF: return '?';   // inverted question mark
    case 0x00A1: return '!';   // inverted exclamation mark
    case 0x00DF: return 's';   // sharp s, one letter is close enough
    case 0x00E6: return 'a';   // ae
    case 0x00C6: return 'A';
    case 0x00F8: return 'o';   // slashed o
    case 0x00D8: return 'O';
    case 0x0153: return 'o';   // oe
    case 0x0152: return 'O';
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return '\'';
    case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2026: return '.';
    case 0x20AC: return 'E';   // euro
    case 0x00B7: return '.';
    default: break;
  }

  // Accented Latin letters: keep the base letter, drop the mark.
  if (cp >= 0x00C0 && cp <= 0x00FF) {
    static const char BASE[] =
        "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    // 0xC0..0xFF, with 0xD7 (multiplication) and 0xF7 (division) left out.
    if (cp == 0x00D7) return 'x';
    if (cp == 0x00F7) return '/';
    char c = BASE[cp - 0x00C0];
    if (c >= 'a' && c <= 'z') { /* keep case as the table encodes it */ }
    return c;
  }
  if (cp >= 0x0100 && cp <= 0x017F) {
    // Latin Extended-A: map only the ones that show up in names.
    switch (cp) {
      case 0x0100: case 0x0102: case 0x0104: return 'A';
      case 0x0101: case 0x0103: case 0x0105: return 'a';
      case 0x0106: case 0x0108: case 0x010A: case 0x010C: return 'C';
      case 0x0107: case 0x0109: case 0x010B: case 0x010D: return 'c';
      case 0x010E: case 0x0110: return 'D';
      case 0x010F: case 0x0111: return 'd';
      case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A: return 'E';
      case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B: return 'e';
      case 0x011C: case 0x011E: case 0x0120: case 0x0122: return 'G';
      case 0x011D: case 0x011F: case 0x0121: case 0x0123: return 'g';
      case 0x0124: case 0x0126: return 'H';
      case 0x0125: case 0x0127: return 'h';
      case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: return 'I';
      case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: return 'i';
      case 0x0134: return 'J';
      case 0x0135: return 'j';
      case 0x0136: return 'K';
      case 0x0137: return 'k';
      case 0x0139: case 0x013B: case 0x013D: case 0x013F: return 'L';
      case 0x013A: case 0x013C: case 0x013E: case 0x0140: case 0x0142: return 'l';
      case 0x0143: case 0x0145: case 0x0147: return 'N';
      case 0x0144: case 0x0146: case 0x0148: case 0x0149: return 'n';
      case 0x014A: return 'N';
      case 0x014B: return 'n';
      case 0x014C: case 0x014E: case 0x0150: return 'O';
      case 0x014D: case 0x014F: case 0x0151: return 'o';
      case 0x0154: case 0x0156: case 0x0158: return 'R';
      case 0x0155: case 0x0157: case 0x0159: return 'r';
      case 0x015A: case 0x015C: case 0x015E: case 0x0160: return 'S';
      case 0x015B: case 0x015D: case 0x015F: case 0x0161: return 's';
      case 0x0162: case 0x0164: case 0x0166: return 'T';
      case 0x0163: case 0x0165: case 0x0167: return 't';
      case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172:
        return 'U';
      case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173:
        return 'u';
      case 0x0174: return 'W';
      case 0x0175: return 'w';
      case 0x0176: case 0x0178: return 'Y';
      case 0x0177: return 'y';
      case 0x0179: case 0x017B: case 0x017D: return 'Z';
      case 0x017A: case 0x017C: case 0x017E: return 'z';
      default: break;
    }
  }
  return 0;  // no honest stand-in
}

size_t textFoldAscii(const char *src, char *dst, size_t cap) {
  if (!dst || cap == 0) return 0;
  size_t out = 0;
  if (!src) {
    dst[0] = '\0';
    return 0;
  }
  const unsigned char *p = (const unsigned char *)src;
  while (*p) {
    uint32_t cp = *p;
    int extra = 0;
    if (cp >= 0xF0) {
      extra = 3;
      cp &= 0x07;
    } else if (cp >= 0xE0) {
      extra = 2;
      cp &= 0x0F;
    } else if (cp >= 0xC0) {
      extra = 1;
      cp &= 0x1F;
    } else if (cp >= 0x80) {
      // A continuation byte with no lead byte: the text is not valid UTF-8.
      p++;
      if (out + 1 < cap) dst[out++] = '?';
      continue;
    }
    p++;
    for (int i = 0; i < extra; i++) {
      if ((*p & 0xC0) != 0x80) {
        // Truncated sequence: stop folding rather than emit half a letter.
        extra = -1;
        break;
      }
      cp = (cp << 6) | (*p & 0x3F);
      p++;
    }
    if (extra < 0) {
      if (out + 1 < cap) dst[out++] = '?';
      break;
    }

    // German sharp s expands to two letters, which is the correct
    // transliteration; a single 's' would read as a typo.
    if (cp == 0x00DF) {
      if (out + 2 < cap) {
        dst[out++] = 's';
        dst[out++] = 's';
      }
      continue;
    }

    char mapped = (cp < 0x80) ? (char)cp : mapCodepoint(cp);
    if (mapped == 0) {
      // Emoji, CJK, Cyrillic: no ASCII stand-in. A space reads better than a
      // wall of question marks in a song title.
      mapped = ' ';
    }
    if (out + 1 < cap) dst[out++] = mapped;
  }
  dst[out] = '\0';
  return out;
}
