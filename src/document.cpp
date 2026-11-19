#include "document.h"
#include "persistence.h"
#include <sstream>

// Writing and reading the user's own content. Each feature adds its own lines.
void writeDocument(std::ostream& o, const Document& doc) {
    (void)o; (void)doc;
}

bool readDocumentLine(const std::string& key, std::istringstream& in, const std::string& line, Document& doc) {
    (void)key; (void)in; (void)line; (void)doc;
    return false;
}
