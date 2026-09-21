#pragma once

#include <cstddef>
#include <string>

// Pulls the release notes out of the metadata stream.
//
// Not through ReleaseJsonParser: its token buffer is 512 bytes and a string longer than that is
// discarded whole, so release notes never survive it. This watches the bytes for the release's
// own "body" field and keeps the first NOTES_MAX characters, which is all the dialog can show.
class ReleaseNotesScanner {
 public:
  // Sized to what the dialog can show: ten lines of roughly fifty characters. Held as one
  // std::string, so this is the entire memory cost however long the release notes are.
  static constexpr size_t NOTES_MAX = 500;

  void feed(const char* data, const size_t len) {
    for (size_t i = 0; i < len && !finished; i++) {
      const char c = data[i];
      if (!capturing) {
        // Match the key, allowing for the whitespace a pretty-printed response might carry.
        if (c == KEY[matched] || (matched == KEY_COLON_INDEX && (c == ' ' || c == '\t'))) {
          if (c == KEY[matched]) matched++;
          if (matched == sizeof(KEY) - 1) {
            capturing = true;
            matched = 0;
          }
        } else {
          matched = (c == KEY[0]) ? 1 : 0;
        }
        continue;
      }
      if (escaped) {
        escaped = false;
        switch (c) {
          case 'n':
            append('\n');
            break;
          case 'r':
            break;  // CRLF in notes: the \n already ended the line
          case 't':
            append(' ');
            break;
          case 'u':
            unicodeLeft = 4;  // \uXXXX: drop the escape rather than half-decode it
            break;
          default:
            append(c);  // \" and \\ arrive as themselves
        }
        continue;
      }
      if (unicodeLeft > 0) {
        unicodeLeft--;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        finished = true;  // end of the body string
        continue;
      }
      append(c);
    }
  }

  bool done() const { return finished || notes.size() >= NOTES_MAX; }
  const std::string& text() const { return notes; }

 private:
  void append(const char c) {
    if (notes.size() < NOTES_MAX) {
      notes.push_back(c);
    } else {
      finished = true;
    }
  }

  static constexpr char KEY[] = "\"body\":\"";
  static constexpr size_t KEY_COLON_INDEX = 7;  // the character after "body":

  std::string notes;
  size_t matched = 0;
  size_t unicodeLeft = 0;
  bool capturing = false;
  bool escaped = false;
  bool finished = false;
};
