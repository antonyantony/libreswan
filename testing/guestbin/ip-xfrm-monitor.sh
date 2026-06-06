#!/bin/sh

set -eu

this_host=$(hostname)
testname=$(basename ${PWD})
host=${this_host}
probe=01
probe_given=0
quiet=0
output_file=

OPTS=$(getopt -o qo: --long host:,testname:,probe:,output-file:,quiet -- "$@")
eval set -- "${OPTS}"

while true ; do
    case "$1" in
        --host )
            host=$2
            shift 2
            ;;
        --testname )
            testname=$2
            shift 2
            ;;
        --probe )
            probe=$2
            probe_given=1
            shift 2
            ;;
        --output-file | -o )
            output_file=$2
            shift 2
            ;;
        --quiet | -q )
            quiet=1
            shift
            ;;
        -- )
            shift
            break
            ;;
        * )
            echo "unrecognized option: $1" 1>&2
            exit 1
            ;;
    esac
done

if [ "$#" -ne 1 ] || { [ "$1" != start ] && [ "$1" != stop ]; } ; then
    echo "usage: $0 [--host HOST] [--testname TESTNAME] [--probe PROBE] [-o|--output-file FILE] [-q|--quiet] { start | stop }" 1>&2
    exit 1
fi
cmd=$1

base="${host}.${testname}.xfrm-monitor-${probe}"
pid_path="/tmp/${base}.pid"
out_file="${base}.txt"
out_path="/tmp/${out_file}"

if [ "${host}" != "${this_host}" ] ; then
    exit 0
fi

# Default copy-out name: short and readable.  Only qualify it with
# the probe when the caller asked for a non-default probe (--probe);
# if that short name is already taken in OUTPUT/ (e.g. a second,
# default-probe monitor on the same host) fall back to the long,
# always-unique name instead of clobbering it.

if [ "${probe_given}" -eq 1 ] ; then
    short_name="${host}.ip-xfrm-monitor-${probe}.txt"
else
    short_name="${host}.ip-xfrm-monitor.txt"
fi

case "${cmd}" in
    start)
        ip -s -d xfrm monitor > ${out_path} 2>&1 &
        echo $! > ${pid_path}
        echo "xfrm-monitor-${probe} started"
        ;;
    stop)
        if test -r ${pid_path} ; then
            pid=$(cat ${pid_path})
            kill -TERM ${pid} 2>/dev/null || true
            wait ${pid} 2>/dev/null || true
            rm -f ${pid_path}
            if [ -n "${output_file}" ] ; then
                dest="OUTPUT/${output_file}"
            elif [ -e "OUTPUT/${short_name}" ] ; then
                dest="OUTPUT/${out_file}"
            else
                dest="OUTPUT/${short_name}"
            fi
            cp ${out_path} ${dest}
            if [ ${quiet} -eq 0 ] ; then
                cat ${out_path}
            fi
        else
            echo "xfrm-monitor-${probe} is not running"
        fi
        ;;
esac
