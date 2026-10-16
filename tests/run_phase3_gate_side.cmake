# run_phase3_gate_side.cmake - Phase 3 gate: side-listen + mcp --attach session.
# Module: test (CMake).
# Owns: CI-safe parallel-session smoke (TUI stand-in is mcp --side-listen).
# Depends: antinomia, loop_inferior, POSIX shell + mkfifo. Not a live TUI demo.
if(NOT DEFINED ANTINOMIA OR NOT DEFINED INFERIOR)
    message(FATAL_ERROR "ANTINOMIA and INFERIOR are required")
endif()
if(WIN32)
    message(STATUS "phase3_gate_side skipped on Windows (POSIX fifo harness)")
    return()
endif()

set(SH_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/phase3_gate_side.sh")
set(ATTACH_OUT "${CMAKE_CURRENT_BINARY_DIR}/phase3_gate_side.out")
set(ATTACH_ERR "${CMAKE_CURRENT_BINARY_DIR}/phase3_gate_side.err")
set(LISTEN_LOG "${CMAKE_CURRENT_BINARY_DIR}/phase3_gate_side.listen")
set(PY "${CMAKE_CURRENT_LIST_DIR}/phase3_gate_side_client.py")

file(WRITE "${SH_SCRIPT}" "#!/bin/sh
set -e
ANT=\"$1\"
INF=\"$2\"
OUT=\"$3\"
ERR=\"$4\"
LOG=\"$5\"
PY=\"$6\"
FIFO=\"$LOG.fifo\"
rm -f \"$FIFO\" \"$LOG\" \"$OUT\" \"$ERR\"
mkfifo \"$FIFO\"
\"$ANT\" mcp --side-listen <\"$FIFO\" >/dev/null 2>\"$LOG\" &
LPID=$!
exec 3>\"$FIFO\"
trap 'exec 3>&-; kill $LPID 2>/dev/null || true; rm -f \"$FIFO\"' EXIT
EP=\"\"
i=0
while [ \$i -lt 80 ]; do
  if [ -s \"$LOG\" ]; then
    EP=\$(head -n 1 \"$LOG\" | tr -d '\\r')
    case \"\$EP\" in
      unix:*|tcp:*) break ;;
    esac
  fi
  i=\$((i+1))
  sleep 0.05
done
if [ -z \"\$EP\" ]; then
  echo \"no endpoint from side-listen\" >&2
  cat \"$LOG\" >&2 || true
  exit 1
fi
python3 \"$PY\" \"$ANT\" \"$EP\" \"$INF\" \"$OUT\" \"$ERR\"
printf 'q\\n' >&3
wait \$LPID 2>/dev/null || true
")

execute_process(COMMAND chmod +x "${SH_SCRIPT}")
execute_process(
    COMMAND "${SH_SCRIPT}"
            "${ANTINOMIA}"
            "${INFERIOR}"
            "${ATTACH_OUT}"
            "${ATTACH_ERR}"
            "${LISTEN_LOG}"
            "${PY}"
    RESULT_VARIABLE sh_rc
    OUTPUT_VARIABLE sh_out
    ERROR_VARIABLE sh_err)
if(NOT sh_rc STREQUAL "0")
    message(FATAL_ERROR "phase3_gate_side failed (${sh_rc}): ${sh_err}\n${sh_out}")
endif()
message(STATUS "phase3_gate_side ok")
