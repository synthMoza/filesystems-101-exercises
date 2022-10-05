#!/bin/bash
#
# Written by synthMoza. All rights reserved. 2022
#
# Send exercises/check results via HTTP requests
#

# Change parameters to fit your needs
repositoryOwner=synthMoza
serverUrl=http://185.106.102.104:9091
outputFile=test_result.json

if [[ $# -ne 1 ]]; then
	echo "No exercise name specified. Finishing the script..."
	exit 1
fi

exerciseName=$1

curl -s -X POST $serverUrl/submit/$repositoryOwner/$exerciseName
curl -s -X GET $serverUrl/results/$repositoryOwner/$exerciseName/last | jq > $outputFile
cat $outputFile

