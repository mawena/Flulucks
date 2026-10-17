#!/bin/sh
# Test de bout en bout de fluluks-cli. Usage : test_cli.sh <chemin fluluks-cli>  (env FL_FIXTURES)
set -eu
CLI=${1:?usage: test_cli.sh <fluluks-cli>}
FX=${FL_FIXTURES:?FL_FIXTURES non défini}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
TAB=$(printf '\t')
fail() { echo "ÉCHEC : $*" >&2; exit 1; }
run() { # run <nom> args... ; stdout -> $TMP/nom.out, stderr -> $TMP/nom.err, code -> $RC
  n=$1; shift
  RC=0; "$CLI" "$@" >"$TMP/$n.out" 2>"$TMP/$n.err" || RC=$?
}

# sans argument : code 2
run noarg
[ "$RC" -eq 2 ] || fail "sans argument : code $RC (attendu 2)"
run badcmd frobnicate x
[ "$RC" -eq 2 ] || fail "commande inconnue : code $RC (attendu 2)"

# parts
run parts parts "$FX/gpt-luks2.img"
[ "$RC" -eq 0 ] || fail "parts : code $RC"
grep -q "^1${TAB}2048${TAB}.*LUKS2.*aes-xts-plain64" "$TMP/parts.out" || fail "parts : ligne LUKS2 absente"
grep -q "LUKS2 aes-xts-plain64" "$TMP/parts.out" || fail "parts : colonne LUKS2 aes-xts-plain64"

# cat
run cat cat "$FX/gpt-luks2.img" 1 /hello.txt --pass-file "$FX/pass.txt"
[ "$RC" -eq 0 ] || fail "cat hello : code $RC"
[ "$(cat "$TMP/cat.out")" = "Bonjour FluLuks" ] || fail "cat hello : contenu inattendu"

# ls
run ls ls "$FX/whole-luks1-xts.img" 0 / --pass-file "$FX/pass.txt"
[ "$RC" -eq 0 ] || fail "ls : code $RC"
grep -q "^f${TAB}16${TAB}hello.txt\$" "$TMP/ls.out" || fail "ls : f 16 hello.txt absent"
grep -q "^d${TAB}.*${TAB}sous-dossier\$" "$TMP/ls.out" || fail "ls : sous-dossier absent"
cut -f3 "$TMP/ls.out" | LC_ALL=C sort -c || fail "ls : non trié"

# cat binaire, comparé à l'original
run bin cat "$FX/whole-luks2-2slots.img" 0 /sous-dossier/données.bin --pass-file "$FX/pass.txt"
[ "$RC" -eq 0 ] || fail "cat données.bin : code $RC"
cmp "$TMP/bin.out" "$FX/tree/sous-dossier/données.bin" || fail "données.bin différent"

# fichier plus gros que 1 Mio
run gros cat "$FX/whole-luks1-xts.img" 0 /gros.bin --pass-file "$FX/pass.txt"
[ "$RC" -eq 0 ] || fail "cat gros.bin : code $RC"
cmp "$TMP/gros.out" "$FX/tree/gros.bin" || fail "gros.bin différent"

# mauvaise phrase de passe
run bad ls "$FX/gpt-luks2.img" 1 / --pass-file "$FX/pass2.txt"
[ "$RC" -eq 1 ] || fail "mauvaise phrase : code $RC (attendu 1)"
[ "$(cat "$TMP/bad.err")" = "fluluks-cli: phrase de passe incorrecte" ] || fail "mauvaise phrase : stderr = $(cat "$TMP/bad.err")"

# partition introuvable
run np ls "$FX/gpt-luks2.img" 9 / --pass-file "$FX/pass.txt"
[ "$RC" -eq 1 ] || fail "partition introuvable : code $RC"
[ "$(cat "$TMP/np.err")" = "fluluks-cli: partition introuvable" ] || fail "partition introuvable : stderr"

# format non pris en charge
run fat parts "$FX/fat32.img"
[ "$RC" -eq 0 ] || fail "parts fat32 : code $RC"
grep -q "non\$" "$TMP/fat.out" || fail "parts fat32 : doit finir par non"
run fat2 ls "$FX/fat32.img" 0 /
[ "$RC" -eq 1 ] || fail "ls fat32 : code $RC"
[ "$(cat "$TMP/fat2.err")" = "fluluks-cli: format non pris en charge" ] || fail "ls fat32 : stderr"

# chemin inexistant, fichier de phrase de passe absent
run enoent cat "$FX/ext4-plain.img" 0 /nexiste/pas
[ "$RC" -eq 1 ] || fail "chemin inexistant : code $RC"
run nopf ls "$FX/gpt-luks2.img" 1 / --pass-file "$TMP/absent"
[ "$RC" -eq 1 ] || fail "pass-file absent : code $RC"

# EPIPE : sortie 1 (SIGPIPE ignoré), pas de plantage ; PIPESTATUS absent de sh -> sous-shell
( r=0; "$CLI" cat "$FX/whole-luks1-xts.img" 0 /gros.bin --pass-file "$FX/pass.txt" 2>"$TMP/pipe.err" >&3 || r=$?; echo $r >"$TMP/pipe.rc" ) 3>&1 | head -c 10 >/dev/null
[ "$(cat "$TMP/pipe.rc")" = 1 ] || fail "EPIPE : code $(cat "$TMP/pipe.rc") (attendu 1)"
! grep -qi "sanitizer\|Segmentation\|runtime error" "$TMP/pipe.err" || fail "EPIPE : plantage"

# fichier de phrase de passe avec \n final : pris tel quel, donc refusé
printf 'motdepasse-FluLuks\n' >"$TMP/pass-nl.txt"
run nl ls "$FX/gpt-luks2.img" 1 / --pass-file "$TMP/pass-nl.txt"
[ "$RC" -eq 1 ] || fail "pass-file avec \\n : code $RC (attendu 1)"
[ "$(cat "$TMP/nl.err")" = "fluluks-cli: phrase de passe incorrecte" ] || fail "pass-file avec \\n : stderr"

# parts LUKS1
run p1 parts "$FX/whole-luks1-xts.img"
grep -q "LUKS1 aes-xts-plain64" "$TMP/p1.out" || fail "parts LUKS1"

# jamais de phrase de passe dans les sorties
for f in "$TMP"/*.out "$TMP"/*.err; do
  case "$f" in "$TMP/bin.out"|"$TMP/gros.out") continue;; esac
  if grep -q "motdepasse-FluLuks" "$f"; then fail "phrase de passe dans $f"; fi
done
echo "test_cli : OK"
