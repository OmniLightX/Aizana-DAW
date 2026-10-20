# LAME 3.100

Unmodified upstream source archive, retrieved 2026-10-20:
https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz

SHA-256: `ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e`

`cmake/Lame.cmake` verifies and extracts this archive and builds only the
static encoder library, with the application's macOS deployment target and
architectures. No frontend, decoder, or external runtime executable is used.
The archive contains complete source, build scripts, copyright notices and
COPYING (GNU Library GPL version 2). Library source headers permit version 2
or later. Original license texts are also in `licenses/LAME/` and are copied
into the app by the existing license packaging rule.

Aizana uses [LAME](https://lame.sourceforge.io/) for MP3 export. Distribution
of a statically linked binary requires corresponding library source and the
application source/object material and build scripts needed to modify LAME
and relink the application (LGPL section 6). Include this archive, Aizana's
matching source and build instructions in the corresponding-source package;
a notice or upstream link alone does not fulfill this requirement. The
existing combined-work licensing requirements still apply. No changes to
upstream LAME source are made.
