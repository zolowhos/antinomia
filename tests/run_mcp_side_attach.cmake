# run_mcp_side_attach.cmake - side-channel listen + mcp --attach smoke.
# Module: test (CMake).
# Owns: headless ANTINOMIA_MCP attach against mcp --side-listen.
# Depends: antinomia executable and a POSIX shell with mkfifo.
if(NOT DEFINED ANTINOMIA)
    message(FATAL_ERROR "ANTINOMIA is required")
endif()

if(WIN32)
    message(STATUS "mcp side attach smoke skipped on Windows (POSIX fifo harness)")
    return()
endif()

set(INIT_IN "${CMAKE_CURRENT_BINARY_DIR}/mcp_side_init.jsonl")
set(ATTACH_OUT "${CMAKE_CURRENT_BINARY_DIR}/mcp_side_attach.out")
set(ATTACH_ERR "${CMAKE_CURRENT_BINARY_DIR}/mcp_side_attach.err")
set(LISTEN_LOG "${CMAKE_CURRENT_BINARY_DIR}/mcp_side_listen.err")
set(SH_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/mcp_side_attach.sh")
file(WRITE "${INIT_IN}"
     "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n")

file(WRITE "${SH_SCRIPT}" "#!/bin/sh
set -e
ANT=\"$1\"
INIT=\"$2\"
OUT=\"$3\"
ERR=\"$4\"
LOG=\"$5\"
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
\"$ANT\" mcp --attach \"\$EP\" <\"$INIT\" >\"$OUT\" 2>\"$ERR\"
printf 'q\\n' >&3
wait \$LPID 2>/dev/null || true
grep -q '\"protocolVersion\"' \"$OUT\"
grep -q 'antinomia' \"$OUT\"
")

execute_process(COMMAND chmod +x "${SH_SCRIPT}")
execute_process(
    COMMAND "${SH_SCRIPT}"
            "${ANTINOMIA}"
            "${INIT_IN}"
            "${ATTACH_OUT}"
            "${ATTACH_ERR}"
            "${LISTEN_LOG}"
    RESULT_VARIABLE sh_rc
    OUTPUT_VARIABLE sh_out
    ERROR_VARIABLE sh_err)
if(NOT sh_rc STREQUAL "0")
    message(FATAL_ERROR "mcp side attach failed (${sh_rc}): ${sh_err}\n${sh_out}")
endif()
