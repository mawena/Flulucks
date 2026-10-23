# Archives 7z et RAR de référence

Fichiers `.uu` copiés tels quels de `libarchive/test/` de libarchive 3.8.10 (même archive que
`native/third_party/libarchive`, licence BSD à deux clauses : `native/third_party/libarchive/COPYING`).
Ils ont été faits par 7-Zip et WinRAR, qu'aucun outil libre ne remplace ici. `tools/fixtures/make_archive_fixtures.sh`
les décode dans le dossier des archives de test. Contenus attendus : ceux des tests de libarchive
(`test_read_format_7zip.c`, `test_read_format_rar.c`, `test_read_format_rar5.c`, `test_read_format_*_encryption_data.c`).

| Fichier | Contenu |
|---|---|
| `test_read_format_7zip_lzma2.7z` | `file1`, 2 844 octets, LZMA2 |
| `test_read_format_7zip_bcj_lzma2.7z` | `x86exe`, 27 328 octets, BCJ + LZMA2 |
| `test_read_format_7zip_encryption.7z` | `bar.txt`, 4 octets, chiffré (AES) |
| `test_read_format_rar.rar` | RAR4 : `test.txt` (« test text document\r\n »), lien `testlink` → `test.txt`, `testdir/test.txt`, `testdir` |
| `test_read_format_rar_encryption_data.rar` | RAR4 : `foo.txt`, 16 octets, chiffré |
| `test_read_format_rar5_multiple_files.rar` | RAR5 : `test1.bin` à `test4.bin`, 4 096 octets chacun (valeurs de `verify_data`) |
