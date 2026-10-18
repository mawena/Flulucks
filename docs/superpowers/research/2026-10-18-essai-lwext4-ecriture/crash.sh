#!/bin/bash
# crash.sh SCEN N [extra opts]
S=/tmp/claude-1000/-home-charles-gamligo-Work-Mawena-Mobile-Flutter-FluLuks/3c78dec6-4daf-47ab-bc7f-ca3ea2c8f2e7/scratchpad/spike-lwext4
export LANG=C LC_ALL=C
SC=$1; N=$2; shift 2; TAG=${TAGP}crash_${SC}_$N$(echo "$@" | tr -d ' ')
O=$S/out/$TAG; rm -rf $O; mkdir -p $O
I=$S/img/$TAG.img; cp --sparse=always $S/img/A.img $I
timeout 300 ${HBIN:-$S/harness} $I - $SC fail=$N noumount "$@" > /dev/null 2> $O/harness.log; echo "rc=$?" >> $O/harness.log
dumpe2fs -h $I > $O/dumpe2fs_crashed.txt 2>&1
e2fsck -fn $I > $O/fsck_raw.txt 2>&1; echo "fsck_rc=$?" >> $O/fsck_raw.txt
# (a) kernel-side tools: e2fsck -fy then -fn
cp --sparse=always $I $I.a
e2fsck -fy $I.a > $O/fsck_fy.txt 2>&1; echo "fsck_rc=$?" >> $O/fsck_fy.txt
e2fsck -fn $I.a > $O/fsck_fy_then_fn.txt 2>&1; echo "fsck_rc=$?" >> $O/fsck_fy_then_fn.txt
# (b) lwext4 recover
cp --sparse=always $I $I.b
timeout 300 ${HBIN:-$S/harness} $I.b - mountonly nojournal > /dev/null 2> $O/lw_recover.log; echo "rc=$?" >> $O/lw_recover.log
e2fsck -fn $I.b > $O/fsck_lw_then_fn.txt 2>&1; echo "fsck_rc=$?" >> $O/fsck_lw_then_fn.txt
dumpe2fs -h $I.b > $O/dumpe2fs_lw.txt 2>&1
[ -n "$KEEP" ] || rm -f $I $I.a $I.b
summ() { local f=$1; echo "$(tail -1 $f) $(grep -vcE '^(Pass|e2fsck|SPIKE:|fsck_rc|$)' $f)lines"; }
echo "== $TAG harness:[$(grep -c ERR $O/harness.log) errs, $(grep CRASH $O/harness.log | sed 's/.*writes_ok/writes_ok/')] raw:[$(summ $O/fsck_raw.txt)] fy:[$(summ $O/fsck_fy.txt)] fy->fn:[$(summ $O/fsck_fy_then_fn.txt)] lwrec:[$(grep RECOVER $O/lw_recover.log)] lw->fn:[$(summ $O/fsck_lw_then_fn.txt)]"
