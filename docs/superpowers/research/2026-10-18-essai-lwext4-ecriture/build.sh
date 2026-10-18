L=/home/charles-gamligo/Work/Mawena/Mobile/Flutter/FluLuks/build/native/third_party/lwext4
D="-DCONFIG_USE_DEFAULT_CFG=1 -DCONFIG_EXT4_BLOCKDEVS_COUNT=8 -DCONFIG_EXT4_MOUNTPOINTS_COUNT=8 -DCONFIG_DEBUG_ASSERT=0"
cd /tmp/claude-1000/-home-charles-gamligo-Work-Mawena-Mobile-Flutter-FluLuks/3c78dec6-4daf-47ab-bc7f-ca3ea2c8f2e7/scratchpad/spike-lwext4
gcc -O2 -g -Wall -Wno-unused-result $D -DCONFIG_DEBUG_PRINTF=0 -I$L/include harness.c liblw.a -o harness
gcc -O0 -g $D -DCONFIG_DEBUG_PRINTF=1 -I$L/include harness.c liblwdbg.a -o harness_dbg
