Kodas Sprint 8 - KVal Native C Backend
======================================

Applies on top of commit before f07776b.

Contents:
  seed/src/codegen/krt.h           - KVal tagged-union runtime header (NEW)
  seed/src/codegen/krt.c           - KRT runtime implementation (NEW)
  seed/src/codegen/codegen_krt.c   - Native C code generator (NEW)
  seed/src/codegen/codegen_c.h     - minor interface tweak (MODIFIED)
  seed/src/fs.h / seed/src/fs.c    - small fs helper for build pipeline (MODIFIED)
  seed/src/main.c                  - wires cmd_build into KRT backend (MODIFIED)
  kodas-sprint8.patch              - single git-format-patch for the whole commit
  README.txt                       - this file

To apply via patch file:
  cd <your-kodas-repo>
  git am kodas-sprint8.patch
  make          # from seed/

To apply by copying files over (if you don't use the patch):
  cp -r seed/ <your-kodas-repo>/
  cd seed && make
