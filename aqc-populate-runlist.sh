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

CONFIG="$1"


TYPE=$(jq ".type" "$CONFIG" | tr -d "\"")
YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
PERIOD=$(jq ".period" "$CONFIG" | tr -d "\"")
PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")

RUNLIST=""

if [ x"$TYPE" = "xsim" ]; then

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo "--------------------"
    echo "Getting runs from $BASEDIR/${PASS}"
    echo "--------------------"

    FIRST=1
    while IFS= read -r RUN
    do
        if [ -z "${RUNLIST}" ]; then
            RUNLIST="${RUN}"
        else
            RUNLIST="${RUNLIST}, ${RUN}"
        fi
    done < <(alien_ls "${BASEDIR}/${PASS}" | tr -d "/")

else

    BASEDIR="/alice/${TYPE}/${YEAR}/${PERIOD}"
    echo "--------------------"
    echo "Getting runs from $BASEDIR/*/${PASS}"
    echo "alien_ls $BASEDIR"
    echo "--------------------"
    
    while IFS= read -r RUN
    do
        alien_ls "${BASEDIR}/${RUN}/${PASS}" >& /dev/null
        RESULT=$?
        echo "${RUN} -> ${RESULT}"
        if [ x"${RESULT}" = "x0" ]; then
            if [ -z "${RUNLIST}" ]; then
                RUNLIST="${RUN}"
            else
                RUNLIST="${RUNLIST}, ${RUN}"
            fi
        fi
    done < <(alien_ls $BASEDIR | tr -d '/')

fi

echo ""; echo $RUNLIST  

# replace the contents of the .productionRuns key with strings that can be easily replaced with sed
jq -j --argjson array "[\"PRODUCTIONRUNLIST\"]" '.productionRuns = $array' "$CONFIG" > temp.json && cp temp.json "$CONFIG" && rm temp.json
# use sed to inject the single-line runlists into the JSON configuration
cat "$CONFIG" | sed "s|\"PRODUCTIONRUNLIST\"|$RUNLIST|" > temp.json && cp temp.json "$CONFIG" && rm temp.json
