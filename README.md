slm - music curation that sucks less
====================================

`slm` creates a filesystem-based database of music files.  It
recursively inspects all music files in `MUSIC_DIR` (specified in
`config.h`) and creates entries in `FARM_DIR`, which defaults to
`~/music_farm`.  (A better term than farm/symlink-farm welcome.)  It
indexes on album name and artist name.  A music file that we can read
(currently only ones with ID3 tags, like MP3s) will cause the creation
of two symlinks:

    ${FARM_DIR}/artists/${artist}/${album}/${tracknumber}_${title}.${EXT}
    ${FARM_DIR}/albums/${album}/${tracknumber}_${title}.${EXT}

Where `${artist}`, `${album}`, `${tracknumber}`, and `${title}` come
from the music file's metadata, `${FARM_DIR}` comes from config.h, and
`${EXT}` comes from the original file.

STATUS
------

- ID3 tag reading works correctly for all of my MP3s.
- Symlinks are created as specified above.

TODO
----

- atom tag support - this is what AAC files use.
- back-links to the created symlinks, so if a file moves or is deleted
  we can efficiently update the symlink farm.
- maybe a daemon that watches for new or moved files with
  inotify/kqueue.
- testing under BSDs

license
-------

slm is offered under the MIT license, see LICENSE for details.
