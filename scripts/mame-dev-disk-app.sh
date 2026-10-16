#!/usr/bin/env bash

set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage() {
    echo "usage: $0 [--target|--build|--build-and-prepare] <key>" >&2
    exit 2
}

mode=prepare
if [ "${1-}" = "--target" ] || [ "${1-}" = "--build" ] || [ "${1-}" = "--build-and-prepare" ]; then
    mode=$1
    shift
fi
[ "$#" -eq 1 ] || usage
key=$1

# The VS Code tasks supply the selected preset; CLI callers default to 68K.
case "${LOKA_RETRO68_SELECTED_PRESET-retro68-68k-release}" in
    retro68-68k-*) cpu=68k; suffix=68K ;;
    retro68-ppc-*) cpu=ppc; suffix=PPC ;;
    *) echo "Select a Retro68 68K or PPC configure preset in CMake Tools." >&2; exit 2 ;;
esac
export LOKA_MAME_CPU="$cpu"

target=
preset=retro68-${cpu}-release
bin=
data=()

case "$key" in
    AllStandaloneLoops|AllStandaloneFlows)
        [ "$mode" != "--target" ] || usage
        preset=retro68-${cpu}-standalone-release
        if [ "$key" = "AllStandaloneLoops" ]; then
            target=LokaStandaloneLoop${suffix}All
            disk_option=--all-loops
        else
            target=LokaStandaloneFlow${suffix}All
            disk_option=--all-flows
        fi
        if [ "$mode" = "--build" ] || [ "$mode" = "--build-and-prepare" ]; then
            "$script_dir/retro68-cmake.sh" --preset "$preset"
            "$script_dir/retro68-cmake.sh" --build --preset "$preset" --target "$target"
        fi
        [ "$mode" != "--build" ] || exit 0
        exec "$script_dir/mame-dev-disk.sh" "$disk_option"
        ;;
    AllLoops)
        [ "$mode" = "--build" ] || usage
        for loop in ScrapbookStandaloneLoop HelloWorldStandaloneLoop TutorialStandaloneLoop MineSweeperStandaloneLoop FloppyBirdStandaloneLoop ScrapbookStandaloneFlow HelloWorldStandaloneFlow TutorialStandaloneFlow MineSweeperStandaloneFlow FloppyBirdStandaloneFlow HelloWorldScenarioLoop MineSweeperScenarioLoop; do
            bash "${BASH_SOURCE[0]}" --build "$loop"
        done
        exit 0
        ;;
    ScrapbookStandaloneLoop)
        target=LokaScrapbookStandaloneLoop${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaScrapbookStandaloneLoop${suffix}.bin
        data=(build/retro68/${cpu}/Standalone/Release/tests/toolbox/ASSETS.LRP)
        ;;
    HelloWorldStandaloneLoop)
        target=LokaHelloStandaloneLoop${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaHelloStandaloneLoop${suffix}.bin
        ;;
    TutorialStandaloneLoop)
        target=LokaTutorialStandaloneLoop${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaTutorialStandaloneLoop${suffix}.bin
        ;;
    MineSweeperStandaloneLoop)
        target=LokaMineStandaloneLoop${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaMineStandaloneLoop${suffix}.bin
        ;;
    FloppyBirdStandaloneLoop)
        target=LokaFloppyStandaloneLoop${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaFloppyStandaloneLoop${suffix}.bin
        ;;
    All)
        [ "$mode" != "--target" ] || usage
        if [ "$mode" = "--build" ]; then
            "$script_dir/retro68-cmake.sh" --preset "$preset"
            exec "$script_dir/retro68-cmake.sh" --build --preset "$preset"
        fi
        if [ "$mode" = "--build-and-prepare" ]; then
            "$script_dir/retro68-cmake.sh" --preset "$preset" -DLOKA_BUILD_SMIRKYCARD=ON
            "$script_dir/retro68-cmake.sh" --build --preset "$preset"
        fi
        exec "$script_dir/mame-dev-disk.sh" --all
        ;;
    HelloWorld)
        target=LokaHello${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/HelloWorld/LokaHello${suffix}.bin
        ;;
    SmirkyCard)
        target=LokaSmirkyCard${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/SmirkyCard/LokaSmirkyCard${suffix}.bin
        # Manifest readers in mame-*-disk* and SmirkyCard/CMakeLists.txt stay parallel.
        while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
            case "$script" in ''|\#*) continue ;; esac
            data+=("example/SmirkyCard/$script")
        done < "$workspace_dir/example/SmirkyCard/disk-scripts.txt"
        # Same grammar as the adjacent script manifest; pictures are data forks.
        while IFS=$' \t\r' read -r script || [ -n "$script" ]; do
            case "$script" in ''|\#*) continue ;; esac
            data+=("example/SmirkyCard/assets/$script.PICT")
        done < "$workspace_dir/example/SmirkyCard/disk-pictures.txt"
        ;;
    MineSweeper)
        target=LokaMine${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/MineSweeper/LokaMine${suffix}.bin
        ;;
    SimpleViewer)
        target=LokaSimpleViewer${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/SimpleViewer/LokaSimpleViewer${suffix}.bin
        ;;
    SimpleText)
        target=LokaSimpleText${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/SimpleText/LokaSimpleText${suffix}.bin
        ;;
    FloppyBird)
        target=LokaFloppyBird${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/FloppyBird/LokaFloppyBird${suffix}.bin
        ;;
    SmirkBench)
        target=LokaSmirkBench${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/SmirkBench/LokaSmirkBench${suffix}.bin
        ;;
    LazyList)
        target=LokaLazyList${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/LazyList/LokaLazyList${suffix}.bin
        ;;
    Tutorial)
        target=LokaTutorial${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/Tutorial/LokaTutorial${suffix}.bin
        ;;
    ScrapbookUI)
        target=ScrapbookUI${suffix}_APPL
        bin=build/retro68/${cpu}/Release/example/ScrapbookUI/ScrapbookUI${suffix}.bin
        data=(build/retro68/${cpu}/Release/example/ScrapbookUI/ASSETS.LRP)
        ;;
    ScrapbookStandaloneFlow)
        target=LokaScrapbookStandaloneFlow${suffix}_APPL
        bin=build/presentation/toolbox-${cpu}-release/LokaScrapbookStandaloneFlow${suffix}.bin
        data=(build/presentation/toolbox-${cpu}-release/ASSETS.LRP)
        ;;
    HelloWorldStandaloneFlow)
        target=LokaHelloStandaloneFlow${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaHelloStandaloneFlow${suffix}.bin
        ;;
    TutorialStandaloneFlow)
        target=LokaTutorialStandaloneFlow${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaTutorialStandaloneFlow${suffix}.bin
        ;;
    MineSweeperStandaloneFlow)
        target=LokaMineStandaloneFlow${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaMineStandaloneFlow${suffix}.bin
        ;;
    FloppyBirdStandaloneFlow)
        target=LokaFloppyStandaloneFlow${suffix}_APPL
        preset=retro68-${cpu}-standalone-release
        bin=build/retro68/${cpu}/Standalone/Release/tests/toolbox/LokaFloppyStandaloneFlow${suffix}.bin
        ;;
    HelloWorldScenarioLoop)
        target=LokaHelloScenarioLoop${suffix}_APPL
        bin=build/retro68/${cpu}/Release/tests/toolbox/LokaHelloScenarioLoop${suffix}.bin
        ;;
    MineSweeperScenarioLoop)
        target=LokaMineScenarioLoop${suffix}_APPL
        bin=build/retro68/${cpu}/Release/tests/toolbox/LokaMineScenarioLoop${suffix}.bin
        ;;
    *)
        echo "unknown SCSI app key: $key" >&2
        exit 2
        ;;
esac

build_app() {
    if [ "$key" = "ScrapbookStandaloneFlow" ]; then
        "$script_dir/toolbox-standalone-flow.sh" Stage "$cpu"
        return
    fi
    if [ "$key" = "SmirkyCard" ]; then
        "$script_dir/retro68-cmake.sh" --preset "$preset" -DLOKA_BUILD_SMIRKYCARD=ON
    else
        "$script_dir/retro68-cmake.sh" --preset "$preset"
    fi
    "$script_dir/retro68-cmake.sh" --build --preset "$preset" --target "$target"
}

if [ "$mode" = "--target" ]; then
    printf '%s\n' "$target"
elif [ "$mode" = "--build" ]; then
    build_app
else
    if [ "$mode" = "--build-and-prepare" ]; then
        build_app
    fi
    arguments=("$workspace_dir/$bin")
    for item in ${data[@]+"${data[@]}"}; do
        arguments+=("$workspace_dir/$item")
    done
    exec "$script_dir/mame-dev-disk.sh" "${arguments[@]}"
fi
