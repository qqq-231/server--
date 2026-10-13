#!/usr/bin/env bash

# EDRStore multi-program sequential experiment runner.
# Each dataset gets a clean build/storage state, then upload + download runs
# under one ServerMain/KeyManager pair. Services are stopped with SIGINT,
# matching an interactive Ctrl+C.

set -euo pipefail
shopt -s nullglob

LOG_ROOT="/home/user2/edrstore日志"
PLAIN_COMMAND_DIR="/home/user2/运行命令/命令"
MLE_COMMAND_DIR="/home/user2/运行命令-m1/命令"
ELC_COMMAND_DIR="/home/user2/运行命令-m2/命令"
SERVER_INIT_DELAY=5
COMMAND_INTERVAL=0.3
SERVICE_STOP_TIMEOUT=300
CLIENT_COMMAND_TIMEOUT_SECONDS="${CLIENT_COMMAND_TIMEOUT_SECONDS:-0}"
SERVER_COMPLETION_TIMEOUT_SECONDS="${SERVER_COMPLETION_TIMEOUT_SECONDS:-$CLIENT_COMMAND_TIMEOUT_SECONDS}"
RESOURCE_SAMPLE_INTERVAL_SECONDS="${RESOURCE_SAMPLE_INTERVAL_SECONDS:-1}"
RESOURCE_LOG_INTERVAL_SECONDS="${RESOURCE_LOG_INTERVAL_SECONDS:-10}"

PROGRAM_ORDER=( 
Plain-finesse-g1
Plain-finesse-g2
Plain-finesse-g3
Plain-finesse-g4
Plain-finesse-g5
Plain-finesse-g6
MLE-finesse-g1
MLE-finesse-g2
MLE-finesse-g3
MLE-finesse-g4
MLE-finesse-g5
MLE-finesse-g6
ELC-finesse-g1
ELC-finesse-g2
ELC-finesse-g3
ELC-finesse-g4
ELC-finesse-g5
ELC-finesse-g6
)

  

declare -A PROJECT_DIRS=(


    [MLE-finesse]="/home/user2/代码/原始/MLE/edrstore-main/Baseline"


    [原始-finesse-g1]="/home/user2/代码/测试/原始---wd/edrstore-main copy/EDRStore"
    [原始-finesse-g2]="/home/user2/代码/测试/原始---wd/edrstore-main copy 2/EDRStore"
    [原始-finesse-g3]="/home/user2/代码/测试/原始---wd/edrstore-main copy 3/EDRStore"
    [原始-finesse-g4]="/home/user2/代码/测试/原始---wd/edrstore-main/EDRStore"
    [原始-finesse-g5]="/home/user2/代码/测试/原始---wd/edrstore-main copy 4/EDRStore"
    [原始-finesse-g6]="/home/user2/代码/测试/原始---wd/edrstore-main copy 5/EDRStore"

    [Plain-finesse-g1]="/home/user2/server--/原始/MLE/edrstore-main copy/Baseline"
    [Plain-finesse-g2]="/home/user2/server--/原始/MLE/edrstore-main copy 2/Baseline"
    [Plain-finesse-g3]="/home/user2/server--/原始/MLE/edrstore-main copy 3/Baseline"
    [Plain-finesse-g4]="/home/user2/server--/原始/MLE/edrstore-main/Baseline"
    [Plain-finesse-g5]="/home/user2/server--/原始/MLE/edrstore-main copy 4/Baseline"
    [Plain-finesse-g6]="/home/user2/server--/原始/MLE/edrstore-main copy 5/Baseline"

    [MLE-finesse-g1]="/home/user2/server--/原始/MLE/edrstore-main copy/Baseline"
    [MLE-finesse-g2]="/home/user2/server--/原始/MLE/edrstore-main copy 2/Baseline"
    [MLE-finesse-g3]="/home/user2/server--/原始/MLE/edrstore-main copy 3/Baseline"
    [MLE-finesse-g4]="/home/user2/server--/原始/MLE/edrstore-main/Baseline"
    [MLE-finesse-g5]="/home/user2/server--/原始/MLE/edrstore-main copy 4/Baseline"
    [MLE-finesse-g6]="/home/user2/server--/原始/MLE/edrstore-main copy 5/Baseline"

    [ELC-finesse-g1]="/home/user2/server--/原始/MLE/edrstore-main copy/Baseline"
    [ELC-finesse-g2]="/home/user2/server--/原始/MLE/edrstore-main copy 2/Baseline"
    [ELC-finesse-g3]="/home/user2/server--/原始/MLE/edrstore-main copy 3/Baseline"
    [ELC-finesse-g4]="/home/user2/server--/原始/MLE/edrstore-main/Baseline"
    [ELC-finesse-g5]="/home/user2/server--/原始/MLE/edrstore-main copy 4/Baseline"
    [ELC-finesse-g6]="/home/user2/server--/原始/MLE/edrstore-main copy 5/Baseline"



    [安全-odess_start-g1]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main copy/EDRStore"
    [安全-odess_start-g2]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main copy 2/EDRStore"
    [安全-odess_start-g3]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main copy 3/EDRStore"
    [安全-odess_start-g4]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main/EDRStore"
    [安全-odess_start-g5]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main copy 4/EDRStore"
    [安全-odess_start-g6]="/home/user2/代码/测试/XTS基础上/终版/edrstore-main copy 5/EDRStore"








    [xts_1n_stat_安全-odess_start-2]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1/EDRStore"
    [xts_1n_stat_安全-odess_start-4]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1 copy/EDRStore"
    [xts_1n_stat_安全-odess_start-6]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1 copy 2/EDRStore"
    [xts_1n_stat_安全-odess_start-8]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1 copy 3/EDRStore"
    [xts_1n_stat_安全-odess_start-10]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1 copy 4/EDRStore"
    [xts_1n_stat_安全-odess_start-12]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-1 copy 5/EDRStore"
    [xts_1n_stat_安全-odess_start-14]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-6/EDRStore"
    [xts_1n_stat_安全-odess_start-16]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-2/EDRStore"
    [xts_1n_stat_安全-odess_start-18]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-3/EDRStore"
    [xts_1n_stat_安全-odess_start-20]="/home/user2/代码/测试/XTS基础上/1：N--安全/edrstore-main-odess_start-4/EDRStore"

)

declare -A PROGRAM_COMMAND_DIRS=(
    [Plain-finesse-g1]="$PLAIN_COMMAND_DIR"
    [Plain-finesse-g2]="$PLAIN_COMMAND_DIR"
    [Plain-finesse-g3]="$PLAIN_COMMAND_DIR"
    [Plain-finesse-g4]="$PLAIN_COMMAND_DIR"
    [Plain-finesse-g5]="$PLAIN_COMMAND_DIR"
    [Plain-finesse-g6]="$PLAIN_COMMAND_DIR"
    [MLE-finesse-g1]="$MLE_COMMAND_DIR"
    [MLE-finesse-g2]="$MLE_COMMAND_DIR"
    [MLE-finesse-g3]="$MLE_COMMAND_DIR"
    [MLE-finesse-g4]="$MLE_COMMAND_DIR"
    [MLE-finesse-g5]="$MLE_COMMAND_DIR"
    [MLE-finesse-g6]="$MLE_COMMAND_DIR"
    [ELC-finesse-g1]="$ELC_COMMAND_DIR"
    [ELC-finesse-g2]="$ELC_COMMAND_DIR"
    [ELC-finesse-g3]="$ELC_COMMAND_DIR"
    [ELC-finesse-g4]="$ELC_COMMAND_DIR"
    [ELC-finesse-g5]="$ELC_COMMAND_DIR"
    [ELC-finesse-g6]="$ELC_COMMAND_DIR"
)

declare -A PROGRAM_EXPECTED_MODES=(
    [Plain-finesse-g1]=0 [Plain-finesse-g2]=0 [Plain-finesse-g3]=0
    [Plain-finesse-g4]=0 [Plain-finesse-g5]=0 [Plain-finesse-g6]=0
    [MLE-finesse-g1]=1 [MLE-finesse-g2]=1 [MLE-finesse-g3]=1
    [MLE-finesse-g4]=1 [MLE-finesse-g5]=1 [MLE-finesse-g6]=1
    [ELC-finesse-g1]=2 [ELC-finesse-g2]=2 [ELC-finesse-g3]=2
    [ELC-finesse-g4]=2 [ELC-finesse-g5]=2 [ELC-finesse-g6]=2
)


declare -A UP_COMMANDS=()
declare -A DOWN_COMMANDS=()

CURRENT_SERVER_PID=""
CURRENT_KEY_PID=""
CURRENT_SERVER_TERMINAL=""
CURRENT_KEY_TERMINAL=""
CURRENT_CLIENT_TERMINAL=""
CURRENT_BIN_DIR=""
CURRENT_LOG_DIR=""
CURRENT_RUN_ACTIVE=0
CURRENT_SERVER_REAPED=0
CURRENT_KEY_REAPED=0
CURRENT_RESOURCE_MONITOR_PID=""
CURRENT_CGROUP_DIR=""
CURRENT_RUN_START_TIME=""

timestamp() {
    date '+%F %T'
}

process_is_running() {
    local process_id="$1"
    local process_state

    [[ -n "$process_id" ]] || return 1
    kill -0 "$process_id" 2>/dev/null || return 1
    process_state=$(ps -o stat= -p "$process_id" 2>/dev/null || true)
    [[ -n "$process_state" && "$process_state" != Z* ]]
}

count_fixed_marker() {
    local log_file="$1"
    local marker="$2"
    local marker_count

    if [[ ! -f "$log_file" ]]; then
        printf '0\n'
        return 0
    fi
    marker_count=$(grep -cF -- "$marker" "$log_file" 2>/dev/null || true)
    printf '%s\n' "${marker_count:-0}"
}

wait_for_server_request_completion() {
    local command_idx="$1"
    local total_command_num="$2"
    local terminal_count_before="$3"
    local performance_count_before="$4"
    local wait_start_epoch
    wait_start_epoch=$(date +%s)

    printf '[%s] Client command %s/%s exited successfully; waiting for server request completion.\n' \
        "$(timestamp)" "$command_idx" "$total_command_num" \
        | tee -a "$CURRENT_CLIENT_TERMINAL"

    while true; do
        local terminal_count_now performance_count_now
        terminal_count_now=$(count_fixed_marker "$CURRENT_SERVER_TERMINAL" \
            "total running time of client")
        performance_count_now=$(count_fixed_marker \
            "${CURRENT_BIN_DIR}/breakdown-server-file" \
            "==================================")

        if ((terminal_count_now > terminal_count_before)); then
            printf '[%s] Server completion confirmed by terminal marker for command %s/%s.\n' \
                "$(timestamp)" "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL"
            return 0
        fi
        if ((performance_count_now > performance_count_before)); then
            printf '[%s] Server completion confirmed by Performance Info for command %s/%s.\n' \
                "$(timestamp)" "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL"
            return 0
        fi

        if ! process_is_running "$CURRENT_SERVER_PID"; then
            printf '[%s] ServerMain exited while waiting for completion of command %s/%s.\n' \
                "$(timestamp)" "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL" \
                    "${CURRENT_LOG_DIR}/service-health.log"
            record_service_exit ServerMain "$CURRENT_SERVER_PID" \
                CURRENT_SERVER_REAPED "$CURRENT_SERVER_TERMINAL"
            return 125
        fi
        if ! process_is_running "$CURRENT_KEY_PID"; then
            printf '[%s] KeyManager exited while waiting for server completion of command %s/%s.\n' \
                "$(timestamp)" "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL" \
                    "${CURRENT_LOG_DIR}/service-health.log"
            record_service_exit KeyManager "$CURRENT_KEY_PID" \
                CURRENT_KEY_REAPED "$CURRENT_KEY_TERMINAL"
            return 125
        fi

        if [[ "$SERVER_COMPLETION_TIMEOUT_SECONDS" -gt 0 ]] && \
            (( $(date +%s) - wait_start_epoch >= SERVER_COMPLETION_TIMEOUT_SECONDS )); then
            printf '[%s] Server completion for command %s/%s exceeded timeout of %ss.\n' \
                "$(timestamp)" "$command_idx" "$total_command_num" \
                "$SERVER_COMPLETION_TIMEOUT_SECONDS" \
                | tee -a "$CURRENT_CLIENT_TERMINAL" \
                    "${CURRENT_LOG_DIR}/service-health.log"
            return 124
        fi
        sleep 1
    done
}

describe_wait_status() {
    local wait_status="$1"
    if [[ "$wait_status" -ge 128 ]]; then
        local signal_num=$((wait_status - 128))
        local signal_name
        signal_name=$(kill -l "$signal_num" 2>/dev/null || printf 'UNKNOWN')
        printf 'exit_status=%s signal=%s(%s)' \
            "$wait_status" "$signal_name" "$signal_num"
    else
        printf 'exit_status=%s signal=none' "$wait_status"
    fi
}

record_service_exit() {
    local service_name="$1"
    local service_pid="$2"
    local reaped_var_name="$3"
    local terminal_log="$4"

    [[ -n "$service_pid" ]] || return 0
    if [[ "${!reaped_var_name}" -eq 1 ]]; then
        return 0
    fi

    local wait_status
    set +e
    wait "$service_pid"
    wait_status=$?
    set -e
    printf -v "$reaped_var_name" '%s' 1

    local status_description
    status_description=$(describe_wait_status "$wait_status")
    printf '[%s] %s exited: pid=%s %s\n' \
        "$(timestamp)" "$service_name" "$service_pid" "$status_description" \
        | tee -a "${CURRENT_LOG_DIR}/service-exit.log" >> "$terminal_log"
}

read_proc_kib() {
    local process_id="$1"
    local field_name="$2"
    awk -v field_name="$field_name" \
        '$1 == field_name ":" { print $2; found=1; exit } END { if (!found) print 0 }' \
        "/proc/${process_id}/status" 2>/dev/null || printf '0\n'
}

start_resource_monitor() {
    local resource_log="${CURRENT_LOG_DIR}/service-resource.log"
    local server_pid="$CURRENT_SERVER_PID"
    local key_pid="$CURRENT_KEY_PID"

    (
        local server_peak_rss=0 server_peak_hwm=0
        local key_peak_rss=0 key_peak_hwm=0
        local last_log_epoch=0 summary_written=0

        write_resource_summary() {
            [[ "$summary_written" -eq 0 ]] || return 0
            summary_written=1
            printf '[%s] Resource summary: ServerMain peak_rss=%s KiB peak_hwm=%s KiB; KeyManager peak_rss=%s KiB peak_hwm=%s KiB\n' \
                "$(timestamp)" "$server_peak_rss" "$server_peak_hwm" \
                "$key_peak_rss" "$key_peak_hwm" >> "$resource_log"
        }
        trap 'write_resource_summary; exit 0' INT TERM

        printf 'timestamp,server_rss_kib,server_hwm_kib,key_rss_kib,key_hwm_kib\n' \
            > "$resource_log"
        while process_is_running "$server_pid" || process_is_running "$key_pid"; do
            local server_rss=0 server_hwm=0 key_rss=0 key_hwm=0
            if process_is_running "$server_pid"; then
                server_rss=$(read_proc_kib "$server_pid" VmRSS)
                server_hwm=$(read_proc_kib "$server_pid" VmHWM)
            fi
            if process_is_running "$key_pid"; then
                key_rss=$(read_proc_kib "$key_pid" VmRSS)
                key_hwm=$(read_proc_kib "$key_pid" VmHWM)
            fi
            ((server_rss > server_peak_rss)) && server_peak_rss=$server_rss
            ((server_hwm > server_peak_hwm)) && server_peak_hwm=$server_hwm
            ((key_rss > key_peak_rss)) && key_peak_rss=$key_rss
            ((key_hwm > key_peak_hwm)) && key_peak_hwm=$key_hwm

            local current_epoch
            current_epoch=$(date +%s)
            if ((current_epoch - last_log_epoch >= RESOURCE_LOG_INTERVAL_SECONDS)); then
                printf '%s,%s,%s,%s,%s\n' "$(timestamp)" \
                    "$server_rss" "$server_hwm" "$key_rss" "$key_hwm" \
                    >> "$resource_log"
                last_log_epoch=$current_epoch
            fi
            sleep "$RESOURCE_SAMPLE_INTERVAL_SECONDS"
        done
        write_resource_summary
    ) &
    CURRENT_RESOURCE_MONITOR_PID=$!
}

stop_resource_monitor() {
    [[ -n "$CURRENT_RESOURCE_MONITOR_PID" ]] || return 0
    if process_is_running "$CURRENT_RESOURCE_MONITOR_PID"; then
        kill -TERM "$CURRENT_RESOURCE_MONITOR_PID" 2>/dev/null || true
    fi
    wait "$CURRENT_RESOURCE_MONITOR_PID" 2>/dev/null || true
    CURRENT_RESOURCE_MONITOR_PID=""
}

resolve_cgroup_dir() {
    local process_id="$1"
    local relative_path
    relative_path=$(awk -F: '$1 == "0" { print $3; exit }' \
        "/proc/${process_id}/cgroup" 2>/dev/null || true)
    [[ -n "$relative_path" ]] || return 1
    printf '/sys/fs/cgroup%s\n' "$relative_path"
}

capture_cgroup_memory_state() {
    local label="$1"
    local output_file="${CURRENT_LOG_DIR}/cgroup-memory-${label}.log"
    {
        printf 'captured_at=%s\n' "$(timestamp)"
        printf 'cgroup=%s\n' "${CURRENT_CGROUP_DIR:-unavailable}"
        local metric_file
        for metric_file in memory.current memory.peak memory.max \
            memory.swap.current memory.swap.peak memory.swap.max memory.events \
            memory.events.local; do
            printf '\n[%s]\n' "$metric_file"
            if [[ -n "$CURRENT_CGROUP_DIR" && \
                -r "${CURRENT_CGROUP_DIR}/${metric_file}" ]]; then
                sed -n '1,80p' "${CURRENT_CGROUP_DIR}/${metric_file}"
            else
                printf 'unavailable\n'
            fi
        done
    } > "$output_file"
}

read_snapshot_memory_event() {
    local snapshot_file="$1"
    local event_name="$2"
    awk -v event_name="$event_name" '
        $0 == "[memory.events]" { in_events=1; next }
        /^\[/ { in_events=0 }
        in_events && $1 == event_name { print $2; found=1; exit }
        END { if (!found) print 0 }
    ' "$snapshot_file" 2>/dev/null || printf '0\n'
}

capture_oom_diagnostics() {
    local output_file="${CURRENT_LOG_DIR}/oom-diagnostics.log"
    local before_file="${CURRENT_LOG_DIR}/cgroup-memory-before.log"
    local after_file="${CURRENT_LOG_DIR}/cgroup-memory-after.log"
    capture_cgroup_memory_state after
    {
        printf 'captured_at=%s\n' "$(timestamp)"
        printf 'The cgroup snapshots are cgroup-memory-before.log and cgroup-memory-after.log.\n'
        printf '\nCgroup memory.events changes during this run:\n'
        local event_name before_value after_value event_delta
        for event_name in low high max oom oom_kill oom_group_kill; do
            before_value=$(read_snapshot_memory_event "$before_file" "$event_name")
            after_value=$(read_snapshot_memory_event "$after_file" "$event_name")
            event_delta=$((after_value - before_value))
            printf '%s: before=%s after=%s delta=%s\n' \
                "$event_name" "$before_value" "$after_value" "$event_delta"
        done
        printf '\nKernel OOM messages since run start (%s):\n' "$CURRENT_RUN_START_TIME"
        local kernel_messages=""
        if command -v journalctl >/dev/null 2>&1; then
            kernel_messages=$(journalctl -k --since "$CURRENT_RUN_START_TIME" \
                --no-pager 2>/dev/null \
                | grep -Ei 'out of memory|oom-kill|killed process' || true)
        fi
        if [[ -z "$kernel_messages" ]] && command -v dmesg >/dev/null 2>&1; then
            kernel_messages=$(dmesg --ctime 2>/dev/null \
                | grep -Ei 'out of memory|oom-kill|killed process' || true)
        fi
        if [[ -n "$kernel_messages" ]]; then
            printf '%s\n' "$kernel_messages"
        else
            printf 'No readable matching kernel messages. Check the cgroup memory.events oom/oom_kill counters.\n'
        fi
    } > "$output_file"
}

terminate_client_process_group() {
    local client_pid="$1"
    local client_log="$2"
    local signal_name
    for signal_name in INT TERM KILL; do
        process_is_running "$client_pid" || break
        printf '[%s] Sending SIG%s to ClientMain process group %s.\n' \
            "$(timestamp)" "$signal_name" "$client_pid" \
            | tee -a "$client_log"
        kill "-$signal_name" -- "-$client_pid" 2>/dev/null || \
            kill "-$signal_name" "$client_pid" 2>/dev/null || true
        [[ "$signal_name" == KILL ]] || sleep 2
    done
}

trim_terminal_log() {
    local log_file="$1"
    local temp_file="${log_file}.tail.$$"
    [[ -f "$log_file" ]] || return 0
    tail -n 300 -- "$log_file" > "$temp_file"
    mv -f -- "$temp_file" "$log_file"
}

stop_services_with_ctrl_c() {
    local server_pid="$CURRENT_SERVER_PID"
    local key_pid="$CURRENT_KEY_PID"

    if process_is_running "$server_pid"; then
        printf '\n[%s] user@edrstore:~/bin$ ^C\n' "$(timestamp)" \
            >> "$CURRENT_SERVER_TERMINAL"
        printf '[%s] Sending Ctrl+C (SIGINT) to ServerMain (pid=%s).\n' \
            "$(timestamp)" "$server_pid" >> "$CURRENT_SERVER_TERMINAL"
        kill -INT "$server_pid" 2>/dev/null || true
    fi

    if process_is_running "$key_pid"; then
        printf '\n[%s] user@edrstore:~/bin$ ^C\n' "$(timestamp)" \
            >> "$CURRENT_KEY_TERMINAL"
        printf '[%s] Sending Ctrl+C (SIGINT) to KeyManager (pid=%s).\n' \
            "$(timestamp)" "$key_pid" >> "$CURRENT_KEY_TERMINAL"
        kill -INT "$key_pid" 2>/dev/null || true
    fi

    # Match the proven run_edrstore.sh shutdown pattern: wait normally after
    # SIGINT, with one watchdog that force-stops both services on timeout.
    (
        sleep "$SERVICE_STOP_TIMEOUT"
        if process_is_running "$server_pid"; then
            printf '[%s] ServerMain did not exit after %ss; sending SIGKILL.\n' \
                "$(timestamp)" "$SERVICE_STOP_TIMEOUT" \
                >> "$CURRENT_SERVER_TERMINAL"
            kill -KILL "$server_pid" 2>/dev/null || true
        fi
        if process_is_running "$key_pid"; then
            printf '[%s] KeyManager did not exit after %ss; sending SIGKILL.\n' \
                "$(timestamp)" "$SERVICE_STOP_TIMEOUT" \
                >> "$CURRENT_KEY_TERMINAL"
            kill -KILL "$key_pid" 2>/dev/null || true
        fi
    ) &
    local watchdog_pid=$!

    record_service_exit ServerMain "$server_pid" CURRENT_SERVER_REAPED \
        "$CURRENT_SERVER_TERMINAL"
    record_service_exit KeyManager "$key_pid" CURRENT_KEY_REAPED \
        "$CURRENT_KEY_TERMINAL"
    kill "$watchdog_pid" 2>/dev/null || true
    wait "$watchdog_pid" 2>/dev/null || true
    stop_resource_monitor
    capture_oom_diagnostics
    CURRENT_SERVER_PID=""
    CURRENT_KEY_PID=""
}

move_program_logs() {
    local log_name
    local save_status=0
    for log_name in \
        server-log client-log breakdown-client-file breakdown-server-file; do
        if [[ -f "${CURRENT_BIN_DIR}/${log_name}" ]]; then
            if ! mv -f -- "${CURRENT_BIN_DIR}/${log_name}" \
                "${CURRENT_LOG_DIR}/${log_name}"; then
                save_status=1
            fi
        else
            if ! touch "${CURRENT_LOG_DIR}/${log_name}"; then
                save_status=1
            fi
        fi
    done

    if [[ -f "${CURRENT_BIN_DIR}/config.json" ]]; then
        if ! cp -f -- "${CURRENT_BIN_DIR}/config.json" \
            "${CURRENT_LOG_DIR}/config.json"; then
            save_status=1
        fi
    fi
    return "$save_status"
}

run_post_run_cleanup() {
    local project_dir
    local cleanup_log="${CURRENT_LOG_DIR}/post-run-cleanup.log"
    project_dir="$(dirname -- "$CURRENT_BIN_DIR")"

    printf '[%s] Running post-run cleanup: %s/cleanup.sh\n' \
        "$(timestamp)" "$project_dir"

    if (
        cd "$project_dir"
        bash ./cleanup.sh
    ) >> "$cleanup_log" 2>&1; then
        printf '[%s] Post-run cleanup completed. Details: %s\n' \
            "$(timestamp)" "$cleanup_log"
        return 0
    else
        local cleanup_status=$?
        printf '[%s] Post-run cleanup failed with status %s. Details: %s\n' \
            "$(timestamp)" "$cleanup_status" "$cleanup_log" >&2
        return "$cleanup_status"
    fi
}

finish_current_run() {
    [[ "$CURRENT_RUN_ACTIVE" -eq 1 ]] || return 0
    stop_services_with_ctrl_c
    if ! move_program_logs; then
        CURRENT_RUN_ACTIVE=0
        printf '[%s] Failed to save all run data; post-run cleanup skipped: %s\n' \
            "$(timestamp)" "$CURRENT_LOG_DIR" >&2 || true
        return 1
    fi
    if ! trim_terminal_log "$CURRENT_SERVER_TERMINAL" || \
        ! trim_terminal_log "$CURRENT_KEY_TERMINAL" || \
        ! trim_terminal_log "$CURRENT_CLIENT_TERMINAL"; then
        CURRENT_RUN_ACTIVE=0
        printf '[%s] Failed to finalize terminal logs; post-run cleanup skipped: %s\n' \
            "$(timestamp)" "$CURRENT_LOG_DIR" >&2 || true
        return 1
    fi
    CURRENT_RUN_ACTIVE=0
    printf '[%s] Logs saved: %s\n' "$(timestamp)" "$CURRENT_LOG_DIR"
    run_post_run_cleanup
}

handle_exit() {
    local exit_status=$?
    trap - EXIT INT TERM
    finish_current_run || true
    exit "$exit_status"
}
trap handle_exit EXIT INT TERM

scan_command_directory() {
    local program_name="$1"
    local command_dir="$2"
    [[ -d "$command_dir" ]] || return 0

    local upload_file dataset_name download_file command_key
    for upload_file in "$command_dir"/*_up.sh; do
        [[ -f "$upload_file" ]] || continue
        dataset_name="$(basename "$upload_file" _up.sh)"
        download_file="${command_dir}/${dataset_name}_down.sh"
        [[ -f "$download_file" ]] || continue

        # Only accept raw command files. Wrapper scripts start with a shebang
        # and must not be recursively executed as command templates.
        if ! grep -qE '^[[:space:]]*\./ClientMain[[:space:]]' \
            "$upload_file"; then
            continue
        fi
        command_key="${program_name}|${dataset_name}"
        UP_COMMANDS["$command_key"]="$upload_file"
        DOWN_COMMANDS["$command_key"]="$download_file"
    done
}

prepare_dataset_list() {
    local program_name
    for program_name in "${PROGRAM_ORDER[@]}"; do
        if [[ -z "${PROGRAM_COMMAND_DIRS[$program_name]+x}" ]]; then
            echo "Program has no command-directory mapping: $program_name" >&2
            exit 1
        fi
        scan_command_directory "$program_name" \
            "${PROGRAM_COMMAND_DIRS[$program_name]}"
    done

    if [[ ${#UP_COMMANDS[@]} -eq 0 ]]; then
        echo "No paired raw upload/download command files found." >&2
        exit 1
    fi
}

list_program_datasets() {
    local program_name="$1"
    local command_key

    for command_key in "${!UP_COMMANDS[@]}"; do
        if [[ "$command_key" == "${program_name}|"* ]]; then
            printf '%s\n' "${command_key#*|}"
        fi
    done | sort
}

command_file_uses_only_mode() {
    local command_file="$1"
    local expected_mode="$2"

    awk -v expected_mode="$expected_mode" '
        /^[[:space:]]*\.\/ClientMain[[:space:]]/ {
            command_num++
            mode_found = 0
            for (i = 1; i <= NF; ++i) {
                if ($i == "-m") {
                    mode_found = 1
                    if (i == NF || $(i + 1) != expected_mode) {
                        invalid = 1
                    }
                }
            }
            if (!mode_found) {
                invalid = 1
            }
        }
        END {
            exit(command_num == 0 || invalid)
        }
    ' "$command_file"
}

validate_environment() {
    [[ "$CLIENT_COMMAND_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || {
        echo "CLIENT_COMMAND_TIMEOUT_SECONDS must be a non-negative integer." >&2
        exit 2
    }
    [[ "$SERVER_COMPLETION_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || {
        echo "SERVER_COMPLETION_TIMEOUT_SECONDS must be a non-negative integer." >&2
        exit 2
    }
    [[ "$RESOURCE_SAMPLE_INTERVAL_SECONDS" =~ ^[1-9][0-9]*$ ]] || {
        echo "RESOURCE_SAMPLE_INTERVAL_SECONDS must be a positive integer." >&2
        exit 2
    }
    [[ "$RESOURCE_LOG_INTERVAL_SECONDS" =~ ^[1-9][0-9]*$ ]] || {
        echo "RESOURCE_LOG_INTERVAL_SECONDS must be a positive integer." >&2
        exit 2
    }

    local program_name project_dir command_dir expected_mode
    for program_name in "${PROGRAM_ORDER[@]}"; do
        if [[ -z "${PROJECT_DIRS[$program_name]+x}" ]]; then
            echo "Program has no PROJECT_DIRS mapping: $program_name" >&2
            exit 1
        fi
        project_dir="${PROJECT_DIRS[$program_name]}"
        [[ -d "$project_dir" ]] || {
            echo "Project directory does not exist: $project_dir" >&2
            exit 1
        }
        [[ -f "${project_dir}/cleanup.sh" ]] || {
            echo "Missing cleanup.sh: $project_dir" >&2
            exit 1
        }
        [[ -f "${project_dir}/setup.sh" ]] || {
            echo "Missing setup.sh: $project_dir" >&2
            exit 1
        }
        if [[ -z "${PROGRAM_COMMAND_DIRS[$program_name]+x}" ]]; then
            echo "Program has no command-directory mapping: $program_name" >&2
            exit 1
        fi
        command_dir="${PROGRAM_COMMAND_DIRS[$program_name]}"
        [[ -d "$command_dir" ]] || {
            echo "Command directory does not exist: $command_dir" >&2
            exit 1
        }
        if [[ -z "${PROGRAM_EXPECTED_MODES[$program_name]+x}" ]]; then
            echo "Program has no expected-mode mapping: $program_name" >&2
            exit 1
        fi
    done

    local dataset_name command_key upload_num download_num
    local program_datasets=()
    for program_name in "${PROGRAM_ORDER[@]}"; do
        mapfile -t program_datasets < <(list_program_datasets "$program_name")
        if [[ ${#program_datasets[@]} -eq 0 ]]; then
            printf 'No paired command datasets found for %s in %s\n' \
                "$program_name" "${PROGRAM_COMMAND_DIRS[$program_name]}" >&2
            exit 1
        fi

        expected_mode="${PROGRAM_EXPECTED_MODES[$program_name]}"
        for dataset_name in "${program_datasets[@]}"; do
            command_key="${program_name}|${dataset_name}"
            if [[ -z "${UP_COMMANDS[$command_key]+x}" || \
                  -z "${DOWN_COMMANDS[$command_key]+x}" ]]; then
                printf 'Missing command pair for %s dataset %s\n' \
                    "$program_name" "$dataset_name" >&2
                exit 1
            fi
            upload_num=$(grep -cE '^[[:space:]]*\./ClientMain[[:space:]]' \
                "${UP_COMMANDS[$command_key]}")
            download_num=$(grep -cE '^[[:space:]]*\./ClientMain[[:space:]]' \
                "${DOWN_COMMANDS[$command_key]}")
            if [[ "$upload_num" -ne "$download_num" ]]; then
                printf 'Command count mismatch for %s/%s: upload=%s download=%s\n' \
                    "$program_name" "$dataset_name" "$upload_num" \
                    "$download_num" >&2
                exit 1
            fi
            if ! command_file_uses_only_mode \
                "${UP_COMMANDS[$command_key]}" "$expected_mode" || \
               ! command_file_uses_only_mode \
                "${DOWN_COMMANDS[$command_key]}" "$expected_mode"; then
                printf 'Mode mismatch for %s/%s: expected -m %s\n' \
                    "$program_name" "$dataset_name" "$expected_mode" >&2
                exit 1
            fi
        done
    done
}

clean_and_build() {
    local project_dir="$1"
    printf '[%s] Cleaning and building: %s\n' "$(timestamp)" "$project_dir"
    (
        cd "$project_dir"
        bash cleanup.sh
        mkdir -p bin lib build
        cmake -S . -B build
        cmake --build build -j4
        mkdir -p bin/Containers bin/Recipes bin/Cache
        cp -f config.json bin/config.json
    )
}

launch_service_with_line_buffering() {
    local executable="$1"
    local ldd_output=""
    local asan_runtime=""
    local preload_value="${LD_PRELOAD:-}"

    # stdbuf injects libstdbuf.so through LD_PRELOAD. AddressSanitizer requires
    # libasan.so to be loaded first, so preserve line-buffered logs while
    # explicitly placing the binary's ASan runtime ahead of libstdbuf.so.
    ldd_output=$(ldd "$executable" 2>&1 || true)
    if [[ "$ldd_output" == *"libasan.so"* ]]; then
        asan_runtime=$(awk '
            $1 ~ /^libasan\.so/ && $2 == "=>" && $3 ~ /^\// {
                print $3
                exit
            }
            $1 ~ /^\// && $1 ~ /libasan\.so/ {
                print $1
                exit
            }
        ' <<< "$ldd_output")

        if [[ -n "$asan_runtime" && -r "$asan_runtime" ]]; then
            if [[ ":${preload_value}:" != *":${asan_runtime}:"* ]]; then
                preload_value="${asan_runtime}${preload_value:+:${preload_value}}"
            fi
            printf '[%s] ASan binary detected; preloading %s before stdbuf.\n' \
                "$(timestamp)" "$asan_runtime"
            LD_PRELOAD="$preload_value" exec stdbuf -oL -eL "$executable"
        fi

        # An unresolved ASan dependency cannot be safely combined with
        # stdbuf. Starting directly avoids putting libstdbuf ahead of ASan;
        # the service may use block-buffered stdout in this fallback mode.
        printf '[%s] WARNING: ASan binary detected but libasan.so was not resolved; starting without stdbuf.\n' \
            "$(timestamp)" >&2
        exec "$executable"
    fi

    exec stdbuf -oL -eL "$executable"
}

start_services() {
    local bin_dir="$1"

    printf '[%s] Starting ServerMain.\n' "$(timestamp)"
    (
        cd "$bin_dir"
        launch_service_with_line_buffering ./ServerMain
    ) >> "$CURRENT_SERVER_TERMINAL" 2>&1 &
    CURRENT_SERVER_PID=$!
    CURRENT_SERVER_REAPED=0

    printf '[%s] Starting KeyManager.\n' "$(timestamp)"
    (
        cd "$bin_dir"
        launch_service_with_line_buffering ./KeyManager
    ) >> "$CURRENT_KEY_TERMINAL" 2>&1 &
    CURRENT_KEY_PID=$!
    CURRENT_KEY_REAPED=0

    sleep 1
    if ! process_is_running "$CURRENT_SERVER_PID"; then
        record_service_exit ServerMain "$CURRENT_SERVER_PID" \
            CURRENT_SERVER_REAPED "$CURRENT_SERVER_TERMINAL"
        echo "ServerMain failed to start: $CURRENT_SERVER_TERMINAL" >&2
        return 1
    fi
    if ! process_is_running "$CURRENT_KEY_PID"; then
        record_service_exit KeyManager "$CURRENT_KEY_PID" \
            CURRENT_KEY_REAPED "$CURRENT_KEY_TERMINAL"
        echo "KeyManager failed to start: $CURRENT_KEY_TERMINAL" >&2
        return 1
    fi

    CURRENT_CGROUP_DIR=$(resolve_cgroup_dir "$CURRENT_SERVER_PID" || true)
    capture_cgroup_memory_state before
    start_resource_monitor

    printf '[%s] Waiting %ss for service initialization.\n' \
        "$(timestamp)" "$SERVER_INIT_DELAY"
    sleep "$SERVER_INIT_DELAY"
}

run_command_file() {
    local command_file="$1"
    local phase_name="$2"
    local total_command_num
    total_command_num=$(grep -cEv '^[[:space:]]*(#|$)' \
        "$command_file" || true)

    printf '\n===== %s: %s (%s commands) =====\n' \
        "$phase_name" "$command_file" "$total_command_num" \
        | tee -a "$CURRENT_CLIENT_TERMINAL"

    local command_line command_idx=0 command_status
    while IFS= read -r command_line || [[ -n "$command_line" ]]; do
        command_line="${command_line%$'\r'}"
        command_line="${command_line#"${command_line%%[![:space:]]*}"}"
        command_line="${command_line%"${command_line##*[![:space:]]}"}"
        [[ -z "$command_line" || "$command_line" == \#* ]] && continue
        command_idx=$((command_idx + 1))

        printf '\n[%s] [%s/%s] user@edrstore:~/bin$ %s\n' \
            "$(timestamp)" "$command_idx" "$total_command_num" \
            "$command_line" | tee -a "$CURRENT_CLIENT_TERMINAL"

        if ! process_is_running "$CURRENT_SERVER_PID"; then
            record_service_exit ServerMain "$CURRENT_SERVER_PID" \
                CURRENT_SERVER_REAPED "$CURRENT_SERVER_TERMINAL"
            printf 'ServerMain is not running before command %s/%s.\n' \
                "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL" \
                    "${CURRENT_LOG_DIR}/service-health.log" >&2
            return 125
        fi
        if ! process_is_running "$CURRENT_KEY_PID"; then
            record_service_exit KeyManager "$CURRENT_KEY_PID" \
                CURRENT_KEY_REAPED "$CURRENT_KEY_TERMINAL"
            printf 'KeyManager is not running before command %s/%s.\n' \
                "$command_idx" "$total_command_num" \
                | tee -a "$CURRENT_CLIENT_TERMINAL" \
                    "${CURRENT_LOG_DIR}/service-health.log" >&2
            return 125
        fi

        local is_client_command=0
        local server_terminal_count_before=0
        local server_performance_count_before=0
        if [[ "$command_line" =~ (^|[[:space:]])(\./)?ClientMain[[:space:]] ]]; then
            is_client_command=1
            server_terminal_count_before=$(count_fixed_marker \
                "$CURRENT_SERVER_TERMINAL" "total running time of client")
            server_performance_count_before=$(count_fixed_marker \
                "${CURRENT_BIN_DIR}/breakdown-server-file" \
                "==================================")
        fi

        # Run each command in its own session so the watchdog can stop the
        # complete client process group without touching this runner.
        setsid bash -c 'cd "$1" && exec bash -c "$2" </dev/null' \
            _ "$CURRENT_BIN_DIR" "$command_line" \
            > >(tee -a "$CURRENT_CLIENT_TERMINAL") 2>&1 &
        local client_pid=$!
        local command_start_epoch
        command_start_epoch=$(date +%s)
        local service_failure=0 command_timeout=0

        while process_is_running "$client_pid"; do
            if ! process_is_running "$CURRENT_SERVER_PID"; then
                printf '[%s] ServerMain exited while command %s/%s was running.\n' \
                    "$(timestamp)" "$command_idx" "$total_command_num" \
                    | tee -a "$CURRENT_CLIENT_TERMINAL" \
                        "${CURRENT_LOG_DIR}/service-health.log"
                record_service_exit ServerMain "$CURRENT_SERVER_PID" \
                    CURRENT_SERVER_REAPED "$CURRENT_SERVER_TERMINAL"
                service_failure=1
                terminate_client_process_group "$client_pid" \
                    "$CURRENT_CLIENT_TERMINAL"
                break
            fi
            if ! process_is_running "$CURRENT_KEY_PID"; then
                printf '[%s] KeyManager exited while command %s/%s was running.\n' \
                    "$(timestamp)" "$command_idx" "$total_command_num" \
                    | tee -a "$CURRENT_CLIENT_TERMINAL" \
                        "${CURRENT_LOG_DIR}/service-health.log"
                record_service_exit KeyManager "$CURRENT_KEY_PID" \
                    CURRENT_KEY_REAPED "$CURRENT_KEY_TERMINAL"
                service_failure=1
                terminate_client_process_group "$client_pid" \
                    "$CURRENT_CLIENT_TERMINAL"
                break
            fi

            if [[ "$CLIENT_COMMAND_TIMEOUT_SECONDS" -gt 0 ]] && \
                (( $(date +%s) - command_start_epoch >= CLIENT_COMMAND_TIMEOUT_SECONDS )); then
                printf '[%s] Client command %s/%s exceeded timeout of %ss.\n' \
                    "$(timestamp)" "$command_idx" "$total_command_num" \
                    "$CLIENT_COMMAND_TIMEOUT_SECONDS" \
                    | tee -a "$CURRENT_CLIENT_TERMINAL" \
                        "${CURRENT_LOG_DIR}/service-health.log"
                command_timeout=1
                terminate_client_process_group "$client_pid" \
                    "$CURRENT_CLIENT_TERMINAL"
                break
            fi
            sleep 1
        done

        set +e
        wait "$client_pid"
        command_status=$?
        set -e
        if [[ "$service_failure" -eq 1 ]]; then
            command_status=125
        elif [[ "$command_timeout" -eq 1 ]]; then
            command_status=124
        fi
        if [[ "$command_status" -ne 0 ]]; then
            printf 'Command failed with status %s: %s\n' \
                "$command_status" "$command_line" >&2
            return "$command_status"
        fi

        if [[ "$is_client_command" -eq 1 ]]; then
            local server_wait_status=0
            set +e
            wait_for_server_request_completion "$command_idx" \
                "$total_command_num" "$server_terminal_count_before" \
                "$server_performance_count_before"
            server_wait_status=$?
            set -e
            if [[ "$server_wait_status" -ne 0 ]]; then
                printf 'Server request did not complete after client command %s/%s (status %s): %s\n' \
                    "$command_idx" "$total_command_num" \
                    "$server_wait_status" "$command_line" >&2
                return "$server_wait_status"
            fi
        fi

        if [[ "$command_idx" -lt "$total_command_num" ]]; then
            sleep "$COMMAND_INTERVAL"
        fi
    done < "$command_file"
}

run_one_dataset() {
    local program_name="$1"
    local dataset_name="$2"
    local project_dir="${PROJECT_DIRS[$program_name]}"
    local bin_dir="${project_dir}/bin"
    local command_key="${program_name}|${dataset_name}"
    local upload_command_file="${UP_COMMANDS[$command_key]}"
    local download_command_file="${DOWN_COMMANDS[$command_key]}"
    local run_id
    run_id="$(date '+%Y%m%d_%H%M%S_%N')_up_down_$$"

    CURRENT_LOG_DIR="${LOG_ROOT}/${program_name}/${dataset_name}/${run_id}"
    CURRENT_BIN_DIR="$bin_dir"
    CURRENT_SERVER_TERMINAL="${CURRENT_LOG_DIR}/server-terminal.log"
    CURRENT_KEY_TERMINAL="${CURRENT_LOG_DIR}/key-server-terminal.log"
    CURRENT_CLIENT_TERMINAL="${CURRENT_LOG_DIR}/client-terminal.log"
    CURRENT_SERVER_PID=""
    CURRENT_KEY_PID=""
    CURRENT_SERVER_REAPED=0
    CURRENT_KEY_REAPED=0
    CURRENT_RESOURCE_MONITOR_PID=""
    CURRENT_CGROUP_DIR=""
    CURRENT_RUN_START_TIME=$(timestamp)
    CURRENT_RUN_ACTIVE=1
    mkdir -p "$CURRENT_LOG_DIR"
    touch "$CURRENT_SERVER_TERMINAL" "$CURRENT_KEY_TERMINAL" \
        "$CURRENT_CLIENT_TERMINAL"

    {
        echo "program=$program_name"
        echo "dataset=$dataset_name"
        echo "project=$project_dir"
        echo "command_directory=${PROGRAM_COMMAND_DIRS[$program_name]}"
        echo "expected_mode=${PROGRAM_EXPECTED_MODES[$program_name]}"
        echo "upload_commands=$upload_command_file"
        echo "download_commands=$download_command_file"
        echo "started_at=$CURRENT_RUN_START_TIME"
        echo "client_command_timeout_seconds=$CLIENT_COMMAND_TIMEOUT_SECONDS"
        echo "server_completion_timeout_seconds=$SERVER_COMPLETION_TIMEOUT_SECONDS"
        echo "resource_sample_interval_seconds=$RESOURCE_SAMPLE_INTERVAL_SECONDS"
    } > "${CURRENT_LOG_DIR}/run-info.log"

    printf '\n============================================================\n'
    printf '[%s] Program=%s Dataset=%s\n' \
        "$(timestamp)" "$program_name" "$dataset_name"
    printf '============================================================\n'

    clean_and_build "$project_dir"
    [[ -x "${bin_dir}/ClientMain" ]] || {
        echo "Build did not produce ClientMain: ${bin_dir}/ClientMain" >&2
        return 1
    }
    [[ -x "${bin_dir}/ServerMain" ]] || {
        echo "Build did not produce ServerMain: ${bin_dir}/ServerMain" >&2
        return 1
    }
    [[ -x "${bin_dir}/KeyManager" ]] || {
        echo "Build did not produce KeyManager: ${bin_dir}/KeyManager" >&2
        return 1
    }

    start_services "$bin_dir"
    run_command_file "$upload_command_file" "UPLOAD"
    run_command_file "$download_command_file" "DOWNLOAD"
    finish_current_run
    printf '[%s] Completed Program=%s Dataset=%s\n' \
        "$(timestamp)" "$program_name" "$dataset_name"
    sleep 2
}

usage() {
    cat <<'USAGE'
Usage:
  "/home/user2/server--/run_edrstore_all copy 2.sh"
  "/home/user2/server--/run_edrstore_all copy 2.sh" --list
  "/home/user2/server--/run_edrstore_all copy 2.sh" --program PROGRAM

PROGRAM: use --list to show configured programs and datasets
USAGE
}

main() {
    local selected_program="all"
    case "${1:-}" in
        "")
            ;;
        --list)
            ;;
        --program)
            [[ $# -eq 2 ]] || { usage >&2; return 2; }
            selected_program="$2"
            ;;
        *)
            usage >&2
            return 2
            ;;
    esac

    prepare_dataset_list
    validate_environment

    if [[ "${1:-}" == "--list" ]]; then
        local program_name dataset_name command_key
        local program_datasets=()
        for program_name in "${PROGRAM_ORDER[@]}"; do
            printf 'Program: %s\n' "$program_name"
            printf '  Project: %s\n' "${PROJECT_DIRS[$program_name]}"
            printf '  Command directory: %s\n' \
                "${PROGRAM_COMMAND_DIRS[$program_name]}"
            printf '  Expected mode: -m %s\n' \
                "${PROGRAM_EXPECTED_MODES[$program_name]}"
            mapfile -t program_datasets < <(
                list_program_datasets "$program_name"
            )
            for dataset_name in "${program_datasets[@]}"; do
                command_key="${program_name}|${dataset_name}"
                printf '  %s (%s upload + %s download)\n' \
                    "$dataset_name" \
                    "$(grep -cE '^[[:space:]]*\./ClientMain[[:space:]]' \
                        "${UP_COMMANDS[$command_key]}")" \
                    "$(grep -cE '^[[:space:]]*\./ClientMain[[:space:]]' \
                        "${DOWN_COMMANDS[$command_key]}")"
            done
        done
        return 0
    fi

    local programs_to_run=()
    if [[ "$selected_program" == "all" ]]; then
        programs_to_run=("${PROGRAM_ORDER[@]}")
    elif [[ -n "${PROJECT_DIRS[$selected_program]+x}" ]]; then
        programs_to_run=("$selected_program")
    else
        echo "Unknown program: $selected_program" >&2
        usage >&2
        return 2
    fi

    local program_name dataset_name completed_num=0
    local program_datasets=()
    for program_name in "${programs_to_run[@]}"; do
        mapfile -t program_datasets < <(list_program_datasets "$program_name")
        for dataset_name in "${program_datasets[@]}"; do
            run_one_dataset "$program_name" "$dataset_name"
            completed_num=$((completed_num + 1))
        done
    done

    printf '\nAll runs completed successfully: %s\n' "$completed_num"
}

main "$@"
