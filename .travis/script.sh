#!/bin/bash
set -euxo pipefail
case $TRAVIS_OS_NAME in
  osx)
    export PATH="/usr/local/opt/qt/bin:$PATH"
    export PATH="/usr/local/opt/ccache/libexec:$PATH"
    EXECUTOR=
    ;;
  linux)
    EXECUTOR="docker exec builder "
   ;;
  *) exit 1 ;;
esac

if [ "$VERA" = "true" ]; then $EXECUTOR ./runVera++.sh ; fi
$EXECUTOR bash -c "export CCACHE_DIR=$HOME/.ccache/$TRAVIS_OS_NAME-$CONFIG \
&& export CCACHE_CPP2=yes \
&& export CCACHE_SLOPPINESS=time_macros \
&& which g++ \
&& g++ --version \
&& which qmake \
&& qmake -query \
&& ccache -M 0 \
&& pyenv versions \
&& pkg-config --list-all \
&& { which python3 && python3 -V || true ; } \
&& { which python && python -V || true ; } \
&&  qmake -r CONFIG+=$CONFIG -Wall $QMAKE_EXTRA \
&&  make -k -j2 \
&& pushd tests && qmake -r CONFIG+=$CONFIG $QMAKE_EXTRA && make -j2 && popd \
&& cd bin/x86-$CONFIG && ls "

$EXECUTOR sh -c "cd bin/x86-$CONFIG &&  env DISPLAY=:0 LSAN_OPTIONS='suppressions=asan.supp fast_unwind_on_malloc=0' ./trikScriptRunnerTests$SUFFIX"
$EXECUTOR sh -c "cd bin/x86-$CONFIG &&  env DISPLAY=:0 LSAN_OPTIONS='suppressions=asan.supp fast_unwind_on_malloc=0' ./trikCommunicatorTests$SUFFIX"
$EXECUTOR sh -c "cd bin/x86-$CONFIG &&  env DISPLAY=:0 LSAN_OPTIONS='suppressions=asan.supp fast_unwind_on_malloc=0' ./trikKernelTests$SUFFIX"
