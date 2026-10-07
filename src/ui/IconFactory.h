#pragma once

#include <QIcon>
#include <QPixmap>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// IconFactory - the application's icon set, drawn as vectors, not loaded from
// asset files.
//
// Why generated icons:
//   * an .ico/.png that is crisp at 32 px is muddy at 16 px and at 125 %/150 %
//     scaling; a vector drawn per requested size is crisp at every size and on
//     every monitor
//   * the palette is honoured, so the toolbar does not turn into a column of
//     black smudges in a dark theme
//   * nothing to deploy, nothing to lose, nothing to load: the icon set cannot
//     be missing from an installation
//
// Every glyph is drawn inside a 24 x 24 design space and rasterised separately
// for each size the platform asks for. QIcon::pixmap() gets a bitmap that was
// drawn at exactly that pixel size, which is what "crisp" means in practice.
//
// An unknown name never returns a null QIcon: it returns a clearly generic
// glyph, because a null icon silently disables the accelerator and makes a
// toolbar entry look broken.
// ---------------------------------------------------------------------------
namespace occ {

class IconFactory
{
public:
    // Cached icon for `name`. Thread affinity: GUI thread only.
    static QIcon icon(const QString &name);

    // A single pixmap at exactly `sizePx` (square). Used for the About logo and
    // for tree item decorations, which need a real pixmap rather than an icon.
    static QPixmap pixmap(const QString &name, int sizePx);

    // Every name the factory knows. Exposed so the icon set can be audited and
    // so a missing glyph is a test failure rather than a visual surprise.
    static QStringList knownNames();

    // Drops the cache. Called when the palette changes so the next request
    // redraws in the new text colour.
    static void clearCache();
};

} // namespace occ
