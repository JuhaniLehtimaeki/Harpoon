#pragma once

#include <QString>

namespace Harpoon {

// Release notes as written on forges (Markdown, sometimes with HTML) turned
// into the small HTML subset a QML Text shows with Text.StyledText:
// headings and **bold** in bold, list items with bullets, links (only
// http/https) as <a href>, everything else escaped. Not a full Markdown
// parser: release notes need to be readable, not perfect.
QString releaseNotesToStyledText(const QString &markdown);

} // namespace Harpoon
