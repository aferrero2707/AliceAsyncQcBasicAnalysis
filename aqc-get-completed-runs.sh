#! /bin/bash

export INFOLOGGER_MODE=stdout
export SCRIPTDIR=$(readlink -f $(dirname $0))
#echo "SCRIPTDIR: ${SCRIPTDIR}"

if [[ -z $(which jq) ]]; then
       echo "The jq command is missing, exiting."
       exit 1
fi


if [[ -z $(which alien.py) ]]; then
       echo "The alien.py command is missing, exiting."
       exit 1
fi

UPDATE_CONFIG=0
if [ x"$1" = "x-u" ]; then
    UPDATE_CONFIG=1
    shift
fi

CONFIG="$1"

# check if the configuration contains a combined runlist
NPERIODS=$(jq ".periods | length" "$CONFIG")
if [ x"${NPERIODS}" != "x0" ]; then
    YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
    PERIOD_COMBINED=$(jq ".period" "$CONFIG" | tr -d "\"")
    PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")

    OUTBASEDIR="inputs/${YEAR}/${PERIOD_COMBINED}/${PASS}"
    mkdir -p "${OUTBASEDIR}"

    PERIODS=$(jq ".periods[]" "$CONFIG" | tr -d "\"")
    UPDATE_OPT=""
    if [ x"${UPDATE_CONFIG}" = "x1" ]; then
        UPDATE_OPT="-u"
    fi

    FULLRUNLIST=""

    for PERIOD in $PERIODS; do
        PERIOD_CONFIG="runs-${PERIOD}-${PASS}.json"
        if [ -e "${PERIOD_CONFIG}" ]; then
            ./aqc-get-completed-runs.sh ${UPDATE_OPT} "${PERIOD_CONFIG}"
            RUNLIST=$(jq ".runs[]" "${PERIOD_CONFIG}" | tr -d "\"" | head -c -1 | tr -s "\n" ",")
            if [ ! -z "${FULLRUNLIST}" ]; then
                FULLRUNLIST="${FULLRUNLIST},"
            fi
            echo "RUNLIST: $RUNLIST"
            FULLRUNLIST="${FULLRUNLIST}${RUNLIST}"
        fi
    done

    echo "FULL RUNLIST: ${FULLRUNLIST}"

    jq -j --argjson array "[\"RUNLIST\"]" '.runs = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json

    # use sed to inject the single-line runlists into the JSON configuration
    cat "$CONFIG" | sed "s|\"RUNLIST\"|$FULLRUNLIST|" > temp.json && cp temp.json "$CONFIG" && rm temp.json

    exit
fi

TYPE=$(jq ".type" "$CONFIG" | tr -d "\"")
YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
PERIOD=$(jq ".period" "$CONFIG" | tr -d "\"")
PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")

RUNLIST=

read_dom () {
    local IFS="\>"
    read -d \< ENTITY CONTENT
    local ret=$?
    TAG_NAME=${ENTITY%% *}
    ATTRIBUTES=${ENTITY#* }
    return $ret
}

get_ctime () {
    if [[ $TAG_NAME = "file" ]] ; then
        eval local $ATTRIBUTES
        CTIME="$ctime"
        #echo "get_ctime: $ctime"
    fi
}

get_seconds () {
    awk -v start="$1" 'BEGIN{split(start,d,"[- :]"); print mktime(d[1]" "d[2]" "d[3]" "d[4]" "d[5]" 0")}'
}

if [ x"$TYPE" = "xsim" ]; then

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo "--------------------"
    echo "Getting completed runs from $BASEDIR/${PASS}"
    echo "--------------------"

    FIRST=1
    while IFS= read -r RUN
    do

        ROOTFILE=$(alien_ls "${BASEDIR}/${PASS}/${RUN}/QC/vertexQC.root" 2> /dev/null)
        #echo "ROOTFILE: $ROOTFILE"
        if [ -n "${ROOTFILE}" ]; then
            #echo "Run $RUN found"
            if [ ${FIRST} -eq 0 ]; then
                RUNLIST="${RUNLIST}, "
            fi
            RUNLIST="${RUNLIST}${RUN}"
            FIRST=0
        fi
        
    done < <(alien_ls "${BASEDIR}/${PASS}" | tr -d "/")

else

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo ""
    echo "Getting completed runs from $BASEDIR/*/${PASS}"
    #echo "alien_find $BASEDIR \"*/$PASS/*/QC/QC_fullrun.root\""
    echo ""

    PRODSTART=$(cat "$CONFIG" | jq -c '.productionStart' | tr -d '"')
    PRODSTARTSEC=$(get_seconds "$PRODSTART")
    #echo "PRODSTART: $PRODSTART"
    #echo "PRODSTARTSEC: $PRODSTARTSEC"

    MISSINGRUNLIST=
    
    FIRST=1
    while IFS= read -r RUN
    do
        
        FOUND=0
        
        ROOTFILE=$(alien_find -x - "$BASEDIR/$RUN/$PASS" "*/QC/QC_fullrun.root")
        #echo "ROOTFILE: $ROOTFILE"
        if [ ! -z "$ROOTFILE" ]; then
            #echo "Run $RUN found"
            
            CTIMESEC="0"
            while read_dom; do
                #echo "TAG_NAME: $TAG_NAME"
                if [[ $TAG_NAME = "file" ]]; then
                    #echo $ATTRIBUTES
                    get_ctime
                    #ctime=$(get_ctime)
                    #echo "CTIME: $CTIME"
                    CTIMESEC=$(get_seconds "$CTIME")
                    break
                fi
            done < <(echo "$ROOTFILE" | sed 's|/>|>|g')         
            IFS=

            if [ $CTIMESEC -ge $PRODSTARTSEC ]; then
                FOUND=1
            else
                echo "Run $RUN is too old"
            fi
        fi

        if [ x"$FOUND" = "x1" ]; then
            if [ ${FIRST} -eq 0 ]; then
                RUNLIST="${RUNLIST}, "
            fi
            RUNLIST="${RUNLIST}${RUN}"
            FIRST=0
        else
            if [ -n "${MISSINGRUNLIST}" ]; then
                MISSINGRUNLIST="${MISSINGRUNLIST}, "
            fi
            MISSINGRUNLIST="${MISSINGRUNLIST}${RUN}"
        fi
        
    done < <(cat "$CONFIG" | jq -c '.productionRuns' | tr -d '[' | tr -d ']' | sed 's|,|\n|g')

fi

echo "============================="
echo "List of runs missing in alien"
echo "============================="
echo ""; echo $MISSINGRUNLIST; echo ""


echo "============================="
echo "List of runs found in alien"
echo "============================="
echo ""; echo $RUNLIST; echo ""


if [ x"${UPDATE_CONFIG}" = "x1" ]; then
    # save the .productionRuns key as a comma-separated single line
    PRODUCTIONRUNS=$(cat "$CONFIG" | jq -c '.productionRuns' | tr -d '[' | tr -d ']' | sed 's|,|, |g')

    # replace the contents of the .productionRuns and .runs keys with strings that can be easily replaced with sed
    jq -j --argjson array "[\"PRODUCTIONRUNLIST\"]" '.productionRuns = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json
    jq -j --argjson array "[\"RUNLIST\"]" '.runs = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json

    # use sed to inject the single-line runlists into the JSON configuration
    cat "$CONFIG" | sed "s|\"PRODUCTIONRUNLIST\"|$PRODUCTIONRUNS|" | sed "s|\"RUNLIST\"|$RUNLIST|" > temp.json && cp temp.json "$CONFIG" && rm temp.json
    
fi
