#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "usage: $0 macos <key>" >&2
    exit 2
}

[ "$#" -eq 2 ] || usage
platform=$1
key=$2
[ "$platform" = "macos" ] || usage

configure_preset=macos-debug
target=
configure_args=()

case "$key" in
    AllLoops)
        for loop in ScrapbookStandaloneLoop HelloWorldStandaloneLoop TutorialStandaloneLoop MineSweeperStandaloneLoop FloppyBirdStandaloneLoop HelloWorldScenarioLoop MineSweeperScenarioLoop; do
            bash "${BASH_SOURCE[0]}" macos "$loop"
        done
        exit 0
        ;;
    All) ;;
    HelloWorld) target=LokaHelloMacOS ;;
    MineSweeper) target=LokaMineMacOS ;;
    SimpleViewer) target=LokaSimpleViewerMacOS ;;
    CardShowcase) target=LokaCardShowcaseMacOS ;;
    SimpleText) target=LokaSimpleTextMacOS ;;
    ScrapbookUI) target=ScrapbookUIMacOS ;;
    FloppyBird) target=LokaFloppyBirdMacOS ;;
    SmirkBench) target=LokaSmirkBenchMacOS ;;
    LazyList) target=LokaLazyListMacOS ;;
    Tutorial) target=LokaTutorialMacOS ;;
    SmirkyCard)
        target=LokaSmirkyCardMacOS
        configure_args=(-DLOKA_BUILD_SMIRKYCARD=ON)
        ;;
    Tests)
        target=LokaTestsMacOS
        ;;
    HelloWorldScenarioLoop) target=LokaHelloWorldScenarioLoopMacOS ;;
    ScrapbookStandaloneLoop) configure_preset=macos-standalone-release; target=LokaScrapbookStandaloneLoopMacOS ;;
    HelloWorldStandaloneLoop) configure_preset=macos-standalone-release; target=LokaHelloWorldStandaloneLoopMacOS ;;
    TutorialStandaloneLoop) configure_preset=macos-standalone-release; target=LokaTutorialStandaloneLoopMacOS ;;
    MineSweeperStandaloneLoop) configure_preset=macos-standalone-release; target=LokaMineSweeperStandaloneLoopMacOS ;;
    FloppyBirdStandaloneLoop) configure_preset=macos-standalone-release; target=LokaFloppyBirdStandaloneLoopMacOS ;;
    MineSweeperScenarioLoop) target=LokaMineSweeperScenarioLoopMacOS ;;
    *)
        echo "unknown macOS build key: $key" >&2
        exit 2
        ;;
esac

build_preset=$configure_preset
if [ "$key" = "Tests" ]; then
    build_preset=macos-tests
fi
if [ "$configure_preset" = "macos-standalone-release" ]; then
    build_preset=macos-standalone-loop-release
fi
# Bash 3.2 treats an empty array as unset under nounset.
cmake --preset "$configure_preset" ${configure_args[@]+"${configure_args[@]}"}
if [ -n "$target" ]; then
    exec cmake --build --preset "$build_preset" --target "$target"
fi
exec cmake --build --preset "$build_preset"
