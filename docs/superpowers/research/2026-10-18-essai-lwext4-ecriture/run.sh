#!/bin/bash
# run.sh NAME BASEIMG SCEN [harness opts...]
S=/tmp/claude-1000/-home-charles-gamligo-Work-Mawena-Mobile-Flutter-FluLuks/3c78dec6-4daf-47ab-bc7f-ca3ea2c8f2e7/scratchpad/spike-lwext4
export LANG=C LC_ALL=C
N=$1; B=$2; SC=$3; shift 3
O=$S/out/$N; rm -rf $O; mkdir -p $O
IMG=$S/img/$N.img; cp --sparse=always $B $IMG
BIN=${HBIN:-$S/harness}
$BIN $IMG $O/shadow $SC "$@" > $O/harness.stdout 2> $O/harness.log; echo "harness_rc=$?" >> $O/harness.log
e2fsck -fn $IMG > $O/fsck.txt 2>&1; echo "fsck_rc=$?" >> $O/fsck.txt
dumpe2fs -h $IMG > $O/dumpe2fs.txt 2>&1
# verify content of shadow files
: > $O/verify.txt
if [ -d $O/shadow ]; then
  for f in $O/shadow/*; do
    [ -e "$f" ] || continue
    p="/$(basename "$f" | tr '#' '/')"
    rm -f $O/dump.tmp
    debugfs -R "dump \"$p\" $O/dump.tmp" $IMG >/dev/null 2>&1
    if [ ! -e $O/dump.tmp ]; then echo "MISSING $p" >> $O/verify.txt
    elif cmp -s "$f" $O/dump.tmp; then echo "OK $p" >> $O/verify.txt
    else echo "DIFF $p ($(stat -c %s "$f") vs $(stat -c %s $O/dump.tmp)) $(cmp "$f" $O/dump.tmp 2>&1 | head -1)" >> $O/verify.txt; fi
  done
  rm -f $O/dump.tmp
fi
echo "== $N: $(tail -1 $O/harness.log) $(grep -c ERR $O/harness.log) harness errs; $(tail -1 $O/fsck.txt); verify: $(grep -c ^OK $O/verify.txt) ok, $(grep -vc ^OK $O/verify.txt) bad"
