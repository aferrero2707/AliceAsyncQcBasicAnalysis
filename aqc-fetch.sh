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

# check if the configuration contains a combined runlist
NPERIODS=$(jq ".periods | length" "$CONFIG")
if [ x"${NPERIODS}" != "x0" ]; then
    YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
    PERIOD_COMBINED=$(jq ".period" "$CONFIG" | tr -d "\"")
    PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")

    OUTBASEDIR="inputs/${YEAR}/${PERIOD_COMBINED}/${PASS}"
    mkdir -p "${OUTBASEDIR}"

    PERIODS=$(jq ".periods[]" "$CONFIG" | tr -d "\"")

    for PERIOD in $PERIODS; do
        PERIOD_CONFIG="runs-${PERIOD}-${PASS}.json"
        if [ -e "${PERIOD_CONFIG}" ]; then
            ./aqc-fetch.sh "${PERIOD_CONFIG}"
            (cd "${OUTBASEDIR}" && pwd && ln -s ../../$PERIOD/$PASS/??* .)
        fi
    done

    exit
fi

YEAR=$(jq ".year" "$CONFIG" | tr -d "\"")
PERIOD=$(jq ".period" "$CONFIG" | tr -d "\"")
PASS=$(jq ".pass" "$CONFIG" | tr -d "\"")
ENABLE_CHUNKS=$(jq ".enable_chunks" "$CONFIG" | tr -d "\"")
echo "Enable chunks: ${ENABLE_CHUNKS}"

RUNLIST=$(jq ".runs[]" "$CONFIG" | tr -d "\"")
REFRUNLIST=$(jq ".referenceRuns[].number" "$CONFIG" | tr -d "\"")

rm -f "inputs/qclist.txt"

OUTBASEDIR="inputs/${YEAR}/${PERIOD}/${PASS}"
mkdir -p "${OUTBASEDIR}"

#echo "$REFRUNLIST $RUNLIST" | tr " " "\n" | sort | uniq
FULLRUNLIST=$(echo "$REFRUNLIST $RUNLIST" | tr " " "\n" | sort | uniq)
#echo "$FULLRUNLIST"
#exit;

for RUN in $FULLRUNLIST
do

    echo "Fetching run ${RUN}..."

    ROOTFILES=""
          
    BASEDIR="/alice/data/${YEAR}/${PERIOD}/${RUN}/${PASS}"
    
    #echo "BASEDIR: $BASEDIR"

    OUTDIR="${OUTBASEDIR}/${RUN}"

    ROOTFILE=$(alien_find "${BASEDIR}" '*/QC/QC_fullrun.root')
    
    if [ -n "${ROOTFILE}" ]; then

        # clean-up individual chunks if needed
        rm -f ./${OUTDIR}/QC-???.root
        OUTFILE="${OUTDIR}/QC_fullrun.root"
        echo "  \"${ROOTFILE}\" => \"${OUTFILE}\""
        echo "alien.py cp ${ROOTFILE} file://./${OUTFILE}"
        alien.py cp ${ROOTFILE} file://./${OUTFILE}

    else

        echo -n "Merged root file for run $RUN not found, "

        if [ x"${ENABLE_CHUNKS}" = "x1" ]; then
            echo "fetching individual chunks"
            CHUNKS=$(alien.py ls $BASEDIR)
            
            INDEX=0
            while IFS= read -r CHUNK
            do
                
                #echo "$line"
                
                OUTFILE=${OUTDIR}/QC-$(printf "%03d" ${INDEX}).root
                echo "  \"${BASEDIR}/${CHUNK}/QC/QC.root\" => \"${OUTFILE}\""
                alien.py cp ${BASEDIR}/${CHUNK}/QC/QC.root file://./${OUTFILE}
                #echo "${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root" >> "inputs/${ID}/qclist.txt"
                #if [ -e "${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root" ]; then
                #    ROOTFILES="$ROOTFILES ${OUTDIR}/QC-${RUN}-$(printf "%03d" ${INDEX}).root"
                #fi
                
                INDEX=$((INDEX+1))
                
            done < <(printf '%s\n' "$CHUNKS")

        else

            echo "skipping"
            
        fi

    fi
  
        
    
done
