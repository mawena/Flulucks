#!/usr/bin/env bash
# Vérification SANS root des 5 images LUKS : `cryptsetup reencrypt --decrypt` hors ligne sur une
# copie, puis comparaison de la charge utile avec ext4-plain.img. Valide les conventions d'IV de
# luks_encrypt.py avec l'implémentation de cryptsetup.
# Le verrou de rechiffrement vit dans /run/cryptsetup (réservé à root) : on le redirige dans un
# espace de noms utilisateur/montage privé (`unshare -rm`), sans aucun privilège réel.
# Usage : verify_with_cryptsetup.sh <dossier-fixtures>
set -euo pipefail
FX="$(cd "${1:?usage: verify_with_cryptsetup.sh <dossier-fixtures>}" && pwd)"
W="$(mktemp -d "$FX/.vcs.XXXXXX")"
trap 'rm -rf "$W"' EXIT
cd "$W"   # cryptsetup reencrypt écrit LUKS-*.{log,new,org} dans le répertoire courant
MIB=$((1024 * 1024))
fail=0

# check <image> <décalage en Mio de la partition, 0 si aucune> <mot de passe>
check() {
  local img="$1" skip="$2" pass="$3" c="$W/c.img"
  rm -f "$c" "$c.hdr"
  if [ "$skip" -gt 0 ]; then
    dd if="$FX/$img" of="$c" bs=1M skip="$skip" status=none
    # GPT : on retire le Mio final de la table de secours
    [ "${img#gpt}" != "$img" ] && truncate -s $(( $(stat -c %s "$c") - MIB )) "$c"
  else
    cp "$FX/$img" "$c"
  fi
  # LUKS1 : le déchiffrement hors ligne passe par device-mapper (root). On convertit donc d'abord
  # la copie en LUKS2 (opération d'en-tête seule, le chiffrement des données reste identique).
  local v1=0
  if cryptsetup luksDump "$c" | grep -q '^Version:[[:space:]]*1'; then v1=1; fi
  if unshare -rm bash -c '
      mount -t tmpfs none /run && mkdir -p /run/cryptsetup &&
      { [ "$2" = 0 ] || cryptsetup convert --type luks2 -q "$1"; } &&
      cryptsetup reencrypt --decrypt --header "$1.hdr" --force-offline-reencrypt \
        --key-file "$0" --batch-mode "$1"' \
      "$FX/$pass" "$c" "$v1" >"$W/log" 2>&1 \
     && cmp -s -n $((64 * MIB)) "$c" "$FX/ext4-plain.img"; then
    echo "CRYPTSETUP OK : $img"
  else
    echo "CRYPTSETUP ÉCHEC : $img"; sed 's/^/    /' "$W/log" | tail -5; fail=1
  fi
}

check whole-luks2-4k.img      0 pass.txt
check whole-luks2-2slots.img  0 pass2.txt
check whole-luks1-xts.img     0 pass.txt
check gpt-luks2.img           1 pass.txt
check mbr-luks1-essiv.img     1 pass.txt
exit $fail
